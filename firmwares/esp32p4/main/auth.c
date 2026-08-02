// Device attestation. See auth.h for the contract and protocol.h for the
// signed-message layouts and the eFuse-key design. All state is initialised once
// in pv_auth_init() (before the rx task exists) and read-only afterwards.
#include "auth.h"

#include <string.h>

#include "esp_efuse.h"
#include "esp_efuse_chip.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include "psa_crypto_driver_esp_ecdsa.h"

static const char *TAG = "pv_auth";

// `devid` partition: 8-byte container header + the identity payload (see
// partitions.csv). fmt_version 2 = a v2 pv_device_cert_t and nothing after it
// (the identity key lives in eFuse, not in flash).
#define PV_DEVID_SUBTYPE 0x40
#define PV_DEVID_MAGIC   0x44495650u  // "PVID" LE
#define PV_DEVID_FMT     2
#define PV_DEVID_HDR_LEN 8

// Active identity, cached at init. s_provisioned == false => unprovisioned.
static bool             s_provisioned;
static psa_key_id_t     s_key_id;  // PSA opaque handle to the eFuse ECDSA key
static pv_device_cert_t s_cert;

// sha256(PV_AUTH_CONTEXT || nonce || device_pubkey) -- the context literal
// domain-separates attestation signatures (see protocol.h).
static void challenge_digest(const uint8_t *nonce, uint8_t out[32]) {
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    psa_hash_update(&op, (const uint8_t *)PV_AUTH_CONTEXT,
                    sizeof(PV_AUTH_CONTEXT) - 1);
    psa_hash_update(&op, nonce, PV_AUTH_NONCE_LEN);
    psa_hash_update(&op, s_cert.device_pubkey, PV_EC_PUBKEY_LEN);
    size_t out_len = 0;
    psa_hash_finish(&op, out, 32, &out_len);
}

// Import the read-protected eFuse ECDSA key as a PSA opaque key (per boot). The
// key reference (curve + eFuse block) is public; the scalar never leaves eFuse.
static bool load_efuse_key(void) {
    esp_efuse_block_t blk;
    if (!esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_ECDSA_KEY_P256, &blk)) {
        ESP_LOGW(TAG, "no ECDSA_KEY_P256 key in eFuse; unprovisioned");
        return false;
    }
    esp_ecdsa_opaque_key_t k = {
        .curve = ESP_ECDSA_CURVE_SECP256R1,
        .efuse_block = (uint8_t)blk,
    };
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&a, 256);
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&a, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_lifetime(&a, PSA_KEY_LIFETIME_ESP_ECDSA_VOLATILE);
    psa_status_t st = psa_import_key(&a, (const uint8_t *)&k, sizeof(k), &s_key_id);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "eFuse ECDSA key import failed (psa %d)", (int)st);
        return false;
    }
    return true;
}

// Confirm the eFuse key actually pairs with the cert we will present. A devid
// copied from another chip fails here: its pubkey won't match this eFuse key
// (the ECDSA peripheral derives the public point from the eFuse scalar).
static bool key_matches_cert(void) {
    uint8_t pub[PV_EC_PUBKEY_LEN];
    size_t n = 0;
    if (psa_export_public_key(s_key_id, pub, sizeof(pub), &n) != PSA_SUCCESS ||
        n != PV_EC_PUBKEY_LEN) {
        ESP_LOGE(TAG, "eFuse ECDSA public key export failed");
        return false;
    }
    return memcmp(pub, s_cert.device_pubkey, PV_EC_PUBKEY_LEN) == 0;
}

// Sign a 32-byte challenge digest with the eFuse ECDSA key. Produces the raw
// 64-byte r||s pair (big-endian) the host's p256 verifier expects.
static bool sign_challenge(const uint8_t hash[32], uint8_t out[PV_EC_SIG_LEN]) {
    size_t sig_len = 0;
    psa_status_t st = psa_sign_hash(s_key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                    hash, 32, out, PV_EC_SIG_LEN, &sig_len);
    if (st != PSA_SUCCESS || sig_len != PV_EC_SIG_LEN) {
        ESP_LOGE(TAG, "ECDSA sign failed (psa %d, len %u)",
                 (int)st, (unsigned)sig_len);
        return false;
    }
    return true;
}

// Load the cert from `devid` and the eFuse identity key, then cross-check they
// pair. `part` positioned at the devid partition; the header has already been
// read and validated.
static bool load_identity(const esp_partition_t *part) {
    if (esp_partition_read(part, PV_DEVID_HDR_LEN, &s_cert, sizeof(s_cert)) != ESP_OK) {
        ESP_LOGE(TAG, "devid cert read failed");
        return false;
    }
    if (s_cert.cert_version != PV_DEVID_FMT) {
        ESP_LOGE(TAG, "devid cert unsupported (cert v%u)", s_cert.cert_version);
        return false;
    }
    if (!load_efuse_key()) {
        return false;
    }
    if (!key_matches_cert()) {
        ESP_LOGE(TAG, "eFuse key does not match cert; unprovisioned");
        return false;
    }
    ESP_LOGI(TAG, "provisioned as '%.*s'", PV_DEVICE_ID_LEN, s_cert.device_id);
    return true;
}

void pv_auth_init(void) {
    s_provisioned = false;

    if (psa_crypto_init() != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed; attestation disabled");
        return;
    }

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, PV_DEVID_SUBTYPE, "devid");
    if (!part) {
        ESP_LOGE(TAG, "devid partition missing from the table");
        return;
    }

    struct __attribute__((packed)) {
        uint32_t magic;
        uint8_t  fmt_version;
        uint8_t  reserved[3];
    } hdr;
    if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK) {
        ESP_LOGE(TAG, "devid header read failed");
        return;
    }
    if (hdr.magic != PV_DEVID_MAGIC) {
        ESP_LOGW(TAG, "devid partition blank; running unprovisioned");
        return;
    }
    if (hdr.fmt_version != PV_DEVID_FMT) {
        ESP_LOGE(TAG, "devid unknown fmt_version %u", hdr.fmt_version);
        return;
    }

    s_provisioned = load_identity(part);
}

bool pv_auth_provisioned(void) { return s_provisioned; }

void pv_auth_handle_challenge(const uint8_t *payload, uint32_t len,
                              pv_auth_response_t *out) {
    memset(out, 0, sizeof(*out));
    if (len < PV_AUTH_NONCE_LEN) {
        out->status = PV_AUTH_ERR_BAD_REQ;
        return;
    }
    if (!s_provisioned) {
        out->status = PV_AUTH_UNPROVISIONED;
        return;
    }

    uint8_t hash[32];
    challenge_digest(payload, hash);
    if (!sign_challenge(hash, out->signature)) {
        memset(out, 0, sizeof(*out));
        out->status = PV_AUTH_ERR_INTERNAL;
        return;
    }
    memcpy(out->cert, &s_cert, sizeof(s_cert));
    out->status = PV_AUTH_OK;
}
