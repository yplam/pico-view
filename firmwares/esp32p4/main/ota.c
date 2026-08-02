// Firmware OTA update + recovery. See ota.h for the contract and the project OTA
// plan for the design. The session functions are driven serially from the rx
// dispatcher (main.c), so no internal locking is needed; the boot-loop counter
// lives in RTC-retained memory so it survives panic/watchdog resets.
#include "ota.h"

#include <inttypes.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "psa/crypto.h"

static const char *TAG = "pv_ota";

// --- Boot-loop detector (anti-brick for a board with no recovery button) -----
// "PVBT" as a magic guarding the RTC counter against power-on garbage.
#define PV_BOOT_MAGIC     0x50564254u
// Consecutive boots without a host handshake before we fall back to recovery.
#define PV_MAX_BOOT_TRIES 5

static RTC_NOINIT_ATTR uint32_t s_boot_magic;
static RTC_NOINIT_ATTR uint32_t s_boot_tries;

// --- OTA session state (rx-dispatcher thread only) ---------------------------
static bool                    s_active;
static esp_ota_handle_t        s_handle;
static const esp_partition_t  *s_part;
static uint32_t                s_size;      // announced image size
static uint32_t                s_written;   // bytes written so far
static uint32_t                s_next_seq;  // expected next chunk seq
static uint8_t                 s_expect_sha[32];
static psa_hash_operation_t    s_sha;

static const esp_partition_t *factory_partition(void) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
}

// Recovery is a *build-time* property (the trimmed image flashed to `factory`),
// not "whichever image happens to run from the factory partition" -- otherwise a
// plain `idf.py flash` (which targets factory) would run the full app in recovery
// mode. The factory PARTITION is only the boot-redirect target below.
bool pv_ota_is_recovery(void) {
#ifdef PV_RECOVERY
    return true;
#else
    return false;
#endif
}

void pv_ota_boot_check(void) {
    const esp_partition_t *run = esp_ota_get_running_partition();
    bool from_factory = run && run->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY;
    if (run) {
        ESP_LOGI(TAG, "running from '%s' @0x%06" PRIx32 "%s%s", run->label,
                 (uint32_t)run->address, from_factory ? " [factory]" : "",
                 pv_ota_is_recovery() ? " (recovery build)" : "");
    }

    if (from_factory) {
        // The factory slot is the boot-loop safety net itself -- an image running
        // from it has nowhere safer to fall back to, so it never self-ejects.
        // Keep the counter clean for the next app-slot boot.
        s_boot_magic = PV_BOOT_MAGIC;
        s_boot_tries = 0;
        return;
    }

    if (s_boot_magic != PV_BOOT_MAGIC) {  // cold boot: RTC RAM uninitialised
        s_boot_magic = PV_BOOT_MAGIC;
        s_boot_tries = 0;
    }
    s_boot_tries++;
    if (s_boot_tries > PV_MAX_BOOT_TRIES) {
        ESP_LOGE(TAG, "boot-loop: %" PRIu32 " boots with no handshake; entering recovery",
                 s_boot_tries);
        s_boot_tries = 0;
        const esp_partition_t *fac = factory_partition();
        if (fac && esp_ota_set_boot_partition(fac) == ESP_OK) {
            esp_restart();  // no return
        }
        ESP_LOGE(TAG, "no factory partition to fall back to; retrying app");
    }
}

void pv_ota_note_handshake(void) {
    s_boot_magic = PV_BOOT_MAGIC;
    s_boot_tries = 0;

    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "app confirmed healthy: marked valid, rollback cancelled");
        }
    }
}

// Tear down an in-progress session, discarding the partial image.
static void ota_reset(void) {
    if (!s_active) return;
    esp_ota_abort(s_handle);
    psa_hash_abort(&s_sha);
    s_active = false;
}

int16_t pv_ota_begin(uint32_t image_size, const uint8_t sha256[32], const char *version) {
    if (s_active) return PV_OTA_ERR_BUSY;
    if (image_size == 0) return PV_OTA_ERR_SIZE;

    // A new OTA cannot start while our own image is still pending verification
    // (esp_ota_begin would reject it anyway). The host retries after handshake.
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        return PV_OTA_ERR_ROLLBACK_PENDING;
    }

    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) return PV_OTA_ERR_NO_PARTITION;

    esp_err_t e = esp_ota_begin(s_part, image_size, &s_handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin(%s, %" PRIu32 ") failed: %s",
                 s_part->label, image_size, esp_err_to_name(e));
        return e == ESP_ERR_OTA_ROLLBACK_INVALID_STATE ? PV_OTA_ERR_ROLLBACK_PENDING
                                                       : PV_OTA_ERR_BEGIN;
    }

    s_size = image_size;
    s_written = 0;
    s_next_seq = 0;
    memcpy(s_expect_sha, sha256, sizeof(s_expect_sha));
    s_sha = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&s_sha, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        esp_ota_abort(s_handle);
        return PV_OTA_ERR_BEGIN;
    }
    s_active = true;
    ESP_LOGI(TAG, "OTA begin -> %s, %" PRIu32 " bytes, version '%s'",
             s_part->label, s_size, version ? version : "");
    return PV_OTA_ERR_NONE;
}

int16_t pv_ota_write(uint32_t seq, const uint8_t *data, uint32_t len, uint8_t *pct_out) {
    if (!s_active) return PV_OTA_ERR_STATE;
    uint32_t n = len;

    if (seq != s_next_seq) {
        ESP_LOGW(TAG, "OTA seq gap: got %" PRIu32 ", expected %" PRIu32, seq, s_next_seq);
        ota_reset();
        return PV_OTA_ERR_SEQ;
    }
    if (s_written + n > s_size) {
        ota_reset();
        return PV_OTA_ERR_OVERFLOW;
    }
    esp_err_t e = esp_ota_write(s_handle, data, n);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(e));
        ota_reset();
        return PV_OTA_ERR_WRITE;
    }
    psa_hash_update(&s_sha, data, n);
    s_written += n;
    s_next_seq++;
    if (pct_out) *pct_out = (uint8_t)((uint64_t)s_written * 100 / s_size);
    return PV_OTA_ERR_NONE;
}

int16_t pv_ota_finish(void) {
    if (!s_active) return PV_OTA_ERR_STATE;
    if (s_written != s_size) {
        ESP_LOGW(TAG, "OTA size mismatch: got %" PRIu32 ", expected %" PRIu32,
                 s_written, s_size);
        ota_reset();
        return PV_OTA_ERR_SIZE;
    }

    uint8_t got[32];
    size_t got_len = 0;
    psa_hash_finish(&s_sha, got, sizeof(got), &got_len);
    if (got_len != sizeof(got) || memcmp(got, s_expect_sha, sizeof(got)) != 0) {
        ESP_LOGE(TAG, "OTA SHA-256 mismatch");
        esp_ota_abort(s_handle);
        s_active = false;
        return PV_OTA_ERR_HASH;
    }

    // esp_ota_end structurally validates the image and consumes the handle
    // regardless of result. Authenticity is intentionally NOT checked -- this is
    // open firmware with no image signing, so a fork can OTA freely; the
    // whole-image SHA-256 above is the integrity gate.
    esp_err_t e = esp_ota_end(s_handle);
    s_active = false;
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end (verify) failed: %s", esp_err_to_name(e));
        return PV_OTA_ERR_VERIFY;
    }
    if ((e = esp_ota_set_boot_partition(s_part)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(e));
        return PV_OTA_ERR_SET_BOOT;
    }
    ESP_LOGI(TAG, "OTA complete -> boot '%s' on next reset", s_part->label);
    return PV_OTA_ERR_NONE;
}

void pv_ota_abort(void) {
    if (s_active) {
        ESP_LOGW(TAG, "OTA aborted by host at %" PRIu32 "/%" PRIu32 " bytes",
                 s_written, s_size);
        ota_reset();
    }
}

