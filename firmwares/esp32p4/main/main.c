// pico-view ESP32-P4 firmware entry point.
//
// Brings up the USB-HS vendor-bulk endpoint, then:
//   - rx_task drains bulk OUT (woken by tud_vendor_rx_cb), reassembles PVUS
//     frames, and dispatches them: raw BLITs into buffers from the panel task's
//     2-deep PSRAM pool, CTRL frames into one HostToDevice each (nanopb).
//   - panel_task (panel_task.c) is the single owner of the esp_lcd/QSPI path,
//     reached only through bounded queue ops, so a wedged SPI pipeline can
//     never pin the USB link.
//   - touch_task polls the CST816 (woken by its INT line, or a slow fallback
//     tick) and pushes CTRL DeviceToHost{touch} frames on bulk IN.
// See protocol.h for the framing and ../../proto/pv_wire.proto for the message
// schema; panel.c / touch.c mirror the host engine's driver.rs / touch.rs.
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ldo_regulator.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"

#include "esp_app_desc.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include "gen/pv_wire.pb.h"
#include "protocol.h"
#include "usb_descriptors.h"
#include "panel.h"
#include "panel_task.h"
#include "touch.h"
#include "haptics.h"
#include "ota.h"
#include "auth.h"
#include "face.h"

static const char *TAG = "pv_main";

// Touch INT is event-driven only when both touch and a valid INT GPIO are set.
#if CONFIG_PV_TOUCH_ENABLE && (CONFIG_PV_PIN_TOUCH_INT >= 0)
#define PV_TOUCH_INT_ENABLED 1
#else
#define PV_TOUCH_INT_ENABLED 0
#endif

#define RX_CHUNK 512

// Show the idle screen once the host has been silent this long -- no BLITs and
// no keepalive. One timeout covers every way the host can go away: a pulled
// cable, a cleanly-quit app, or a crashed/killed host. A live host sends frames
// or a >=1s Keepalive, so it never trips while connected, and a brief
// unplug/replug reconnects within the window without blinking the screen.
#define PV_IDLE_BLANK_MS 5000

// Abandon a half-received message after this long without COMPLETING one (see
// parser_tick). Longer than the host's 2s per-transfer write timeout, so
// a legitimately slow frame is never torn up; shorter than PV_IDLE_BLANK_MS, so
// the parser is always clean by the time the idle screen comes up.
#define PV_PARSER_STALE_MS 3000

// Serializes bulk IN writes (touch_task + rx_task HELLO_ACK).
static SemaphoreHandle_t s_tx_mtx;
// Guards CST816 (re)configuration vs. polling across tasks.
static SemaphoreHandle_t s_touch_mtx;
// Set once a CONFIG has initialised the panel; cleared if a later one fails.
// Read from touch_task on the other core, so it must be atomic (the P4 is
// dual-core).
static atomic_bool s_ready;
// Set while a firmware update streams: the idle screen, touch reporting and
// incoming BLITs all stand down so flash writes own the link. Read from
// touch_task on the other core, so it must be atomic.
static atomic_bool s_ota_active;

static TaskHandle_t s_rx_task;
static TaskHandle_t s_touch_task;

// rx_task is watched by the Task WDT: it is the one task that can wedge on a
// stalled panel/SPI teardown. The OTA path runs multi-second synchronous flash
// erase (pv_ota_begin) and image verify (pv_ota_finish) in this same task, which
// would trip the 5s WDT, so it detaches rx_task for the duration of a session.
// Guarded so the add/delete stay balanced (the IDF API errors on a double add or
// a delete-when-absent). Only ever called from rx_task, so esp_task_wdt_*(NULL)
// always refers to rx_task.
static bool s_rx_wdt_added;
static void rx_wdt_resume(void) {
    if (!s_rx_wdt_added && esp_task_wdt_add(NULL) == ESP_OK) s_rx_wdt_added = true;
}
static void rx_wdt_pause(void) {
    if (s_rx_wdt_added && esp_task_wdt_delete(NULL) == ESP_OK) s_rx_wdt_added = false;
}

// One encoded HostToDevice reassembles here (rx_task only). The bound is the
// nanopb worst case over every oneof variant -- OtaData's 8 KB chunk dominates.
// (BLIT payloads go to the panel_task.c buffer pool instead; see parser_feed.)
static uint8_t  s_ctrl_buf[picoview_wire_HostToDevice_size];
// Its decoded form (also rx_task only; static because the OtaData variant makes
// the union ~8 KB, too big for the task stack).
static picoview_wire_HostToDevice s_h2d;

// ---------------------------------------------------------------------------
// Bulk IN: frame + send one PVUS message.
// ---------------------------------------------------------------------------
static void send_msg(uint16_t type, uint16_t flags, const void *payload, uint32_t plen) {
    if (!tud_mounted()) return;
    pv_header_t h = {
        .magic = PV_MAGIC, .type = type, .flags = flags, .payload_len = plen,
    };
    xSemaphoreTake(s_tx_mtx, portMAX_DELAY);
    // All-or-nothing: a partial tud_vendor_write (TX FIFO full because the host
    // stalled reading) would tear the message and desync the host's framer.
    // Every IN message is safely droppable -- touch is a stream, OTA status
    // repeats, and a handshake reply is re-requested by the host -- so drop the
    // whole message when the FIFO can't take all of it.
    if (tud_vendor_write_available() >= sizeof(h) + plen) {
        tud_vendor_write(&h, sizeof(h));
        if (plen) tud_vendor_write(payload, plen);
        tud_vendor_flush();
    } else {
        ESP_LOGW(TAG, "IN FIFO full; dropping msg type %u (%u bytes)",
                 type, (unsigned)plen);
    }
    xSemaphoreGive(s_tx_mtx);
}

// Encode one DeviceToHost and send it as a CTRL frame. Called from rx_task and
// touch_task; the local encode buffer keeps it re-entrant (send_msg serialises
// the actual bulk IN write).
static void send_ctrl(const picoview_wire_DeviceToHost *msg) {
    uint8_t buf[picoview_wire_DeviceToHost_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    if (!pb_encode(&os, picoview_wire_DeviceToHost_fields, msg)) {
        // Statically impossible (every field is bounded); log loudly if not.
        ESP_LOGE(TAG, "DeviceToHost encode failed: %s", PB_GET_ERROR(&os));
        return;
    }
    send_msg(PV_MSG_CTRL, 0, buf, os.bytes_written);
}

// ---------------------------------------------------------------------------
// Idle screen: the host has gone quiet past PV_IDLE_BLANK_MS (unplugged, quit,
// or crashed). Hand the panel to the emote engine for a looping facial
// animation (face.c); with no asset pack, or if the engine fails to come up,
// fall back to darking the panel and wiping GRAM. Either way *idled latches so
// this runs once per idle stretch. rx_task only. No drain is needed: the panel
// task is serial and FIFO, so the face's first stripe (or the clear) lands
// after any host blit still queued.
// ---------------------------------------------------------------------------
static void enter_idle_panel(bool *idled) {
    if (*idled) return;
    // Mid-update: the panel belongs to the OTA progress screen, and flash writes
    // must not contend with the emote engine's asset mmap and render task. A
    // stalled OTA is the host's problem to retry, not the idle screen's.
    if (atomic_load(&s_ota_active)) return;
    // Pipeline already stuck: rx_task reboots on its next iteration, so don't
    // bring up the emote engine on a dead panel.
    if (pv_panel_task_faulted()) return;
    if (pv_face_available() && pv_face_start() == ESP_OK) {
        ESP_LOGI(TAG, "host idle %ums; showing idle face", (unsigned)PV_IDLE_BLANK_MS);
        *idled = true;
        return;
    }
    ESP_LOGI(TAG, "host idle %ums; blanking panel", (unsigned)PV_IDLE_BLANK_MS);
    pv_panel_set_backlight(false);
    pv_panel_task_gate_backlight();
    pv_panel_task_clear();
    *idled = true;
}

// ---------------------------------------------------------------------------
// Host is talking again: reclaim the panel from the idle screen before rx_task
// drives any CONFIG/BLIT. Stopping the emote engine joins its render task, so no
// flush callback can be touching the panel once this returns. When the idle
// screen was a blank there is nothing to stop -- it stays dark until the
// backlight gate relights it. rx_task only.
// ---------------------------------------------------------------------------
static void leave_idle_panel(bool *idled) {
    if (!*idled) return;
    pv_face_stop();
    *idled = false;
}

// ---------------------------------------------------------------------------
// Dispatch a fully-reassembled CTRL message.
// ---------------------------------------------------------------------------
static void send_config_ack(picoview_wire_Status status, const char *detail) {
    picoview_wire_DeviceToHost m = picoview_wire_DeviceToHost_init_zero;
    m.which_msg = picoview_wire_DeviceToHost_config_ack_tag;
    m.msg.config_ack.status = status;
    if (detail) {
        strlcpy(m.msg.config_ack.detail, detail, sizeof(m.msg.config_ack.detail));
    }
    send_ctrl(&m);
    if (status != picoview_wire_Status_STATUS_OK) {
        ESP_LOGE(TAG, "CONFIG rejected: %s", detail ? detail : "");
    }
}

static void send_param_ack(picoview_wire_Status status, const char *detail) {
    picoview_wire_DeviceToHost m = picoview_wire_DeviceToHost_init_zero;
    m.which_msg = picoview_wire_DeviceToHost_param_ack_tag;
    m.msg.param_ack.status = status;
    if (detail) {
        strlcpy(m.msg.param_ack.detail, detail, sizeof(m.msg.param_ack.detail));
    }
    send_ctrl(&m);
}

// Runtime parameter set (currently: backlight brightness). The host may fire
// these rapidly (e.g. a UI slider); the ParamAck is best-effort feedback.
static void handle_set_param(const picoview_wire_SetParam *p) {
    switch (p->which_param) {
        case picoview_wire_SetParam_brightness_tag: {
            uint32_t b = p->param.brightness;
            if (b > 255) b = 255;  // wire field is u32; clamp to the 8-bit range
            pv_panel_set_brightness((uint8_t)b);
            ESP_LOGI(TAG, "SET_PARAM brightness=%u", (unsigned)b);
            send_param_ack(picoview_wire_Status_STATUS_OK, NULL);
            break;
        }
        default:
            ESP_LOGW(TAG, "SET_PARAM unknown param %u", (unsigned)p->which_param);
            send_param_ack(picoview_wire_Status_STATUS_UNSUPPORTED, "unknown param");
            break;
    }
}

// Fire (or stop) a haptic effect on the DRV2605L. Fire-and-forget, like
// SetParam: the host may trigger these rapidly from UI gestures and does not
// wait for a reply.
static void handle_haptics(const picoview_wire_Haptics *h) {
    switch (h->which_cmd) {
        case picoview_wire_Haptics_play_tag:
            pv_haptics_play((uint8_t)h->cmd.play.effect, (uint8_t)h->cmd.play.library);
            break;
        case picoview_wire_Haptics_stop_tag:
            pv_haptics_stop();
            break;
        default:
            ESP_LOGW(TAG, "unhandled Haptics cmd %u", (unsigned)h->which_cmd);
            break;
    }
}

static void handle_hello(const picoview_wire_Hello *h) {
    picoview_wire_DeviceToHost m = picoview_wire_DeviceToHost_init_zero;
    m.which_msg = picoview_wire_DeviceToHost_hello_ack_tag;
    m.msg.hello_ack.proto_version = PV_PROTO_VERSION;
    m.msg.hello_ack.has_caps = true;
    m.msg.hello_ack.caps.set_param = true;  // backlight brightness (SetParam)
    m.msg.hello_ack.caps.haptics = pv_haptics_available();  // DRV2605L (Haptics)
    // Only provisioned units claim `auth`, so the host skips the challenge
    // entirely on a self-built board instead of waiting for an answer that
    // isn't coming. Either way the host drives the panel (AuthChallenge).
    m.msg.hello_ack.caps.auth = pv_auth_provisioned();
    const esp_app_desc_t *d = esp_app_get_description();
    if (d) {
        strlcpy(m.msg.hello_ack.fw_version, d->version, sizeof(m.msg.hello_ack.fw_version));
    }
    send_ctrl(&m);
    ESP_LOGI(TAG, "HELLO (host proto %u) -> HELLO_ACK (proto %u, auth %d)",
             (unsigned)h->proto_version, PV_PROTO_VERSION, (int)m.msg.hello_ack.caps.auth);
    // A completed handshake proves the running image is healthy: confirm it
    // (cancel pending-verify rollback) and reset the boot-loop counter.
    pv_ota_note_handshake();
}

// Answer a host GetDeviceInfo with what this unit knows about itself. The panel
// SHAPE is deliberately left UNSPECIFIED: the wire Config never carried it (it is
// a host-side dirty-clipping property), so the host overlays it from the panel
// preset it opened with. Width/height are 0 until the first CONFIG initialises
// the panel; the host only queries after a completed open, so they are valid.
static void handle_get_device_info(void) {
    picoview_wire_DeviceToHost m = picoview_wire_DeviceToHost_init_zero;
    m.which_msg = picoview_wire_DeviceToHost_device_info_tag;
    picoview_wire_DeviceInfo *di = &m.msg.device_info;
    // Both the stable device id and the USB serial are the eFuse-derived unique
    // id (12 hex of the factory MAC); there is no separate provisioned id yet.
    strlcpy(di->device_id, pv_serial_string(), sizeof(di->device_id));
    strlcpy(di->serial, pv_serial_string(), sizeof(di->serial));
    const esp_app_desc_t *d = esp_app_get_description();
    if (d) {
        strlcpy(di->fw_version, d->version, sizeof(di->fw_version));
    }
    di->proto_version = PV_PROTO_VERSION;
    di->has_panel = true;
    di->panel.width = pv_panel_width();
    di->panel.height = pv_panel_height();
    di->panel.shape = picoview_wire_PanelShape_PANEL_SHAPE_UNSPECIFIED;
    di->has_caps = true;
    di->caps.set_param = true;
    di->caps.haptics = pv_haptics_available();
    send_ctrl(&m);
    ESP_LOGI(TAG, "GET_DEVICE_INFO -> DeviceInfo (fw '%s', %ux%u)",
             di->fw_version, di->panel.width, di->panel.height);
}

static void handle_config(const picoview_wire_Config *c) {
    pv_config_t cfg = {0};
    switch (c->model) {
        case picoview_wire_PanelModel_PANEL_MODEL_ST77916: cfg.model = PV_PANEL_ST77916; break;
        case picoview_wire_PanelModel_PANEL_MODEL_ST7789:  cfg.model = PV_PANEL_ST7789;  break;
        default:
            send_config_ack(picoview_wire_Status_STATUS_UNSUPPORTED, "unknown panel model");
            return;
    }
    if (c->width == 0 || c->height == 0 || c->width > UINT16_MAX || c->height > UINT16_MAX ||
        c->x_offset > UINT16_MAX || c->y_offset > UINT16_MAX) {
        send_config_ack(picoview_wire_Status_STATUS_ERROR, "bad panel geometry");
        return;
    }
    if (c->touch_addr > 0x7F) {
        send_config_ack(picoview_wire_Status_STATUS_ERROR, "bad touch address");
        return;
    }
    cfg.width = (uint16_t)c->width;
    cfg.height = (uint16_t)c->height;
    cfg.x_offset = (uint16_t)c->x_offset;
    cfg.y_offset = (uint16_t)c->y_offset;
    cfg.rotation = (uint16_t)c->rotation_deg;
    cfg.invert = c->invert ? 1 : 0;
    cfg.touch_addr = (uint8_t)c->touch_addr;
    cfg.touch_flags = (c->touch_swap_xy ? PV_TOUCH_SWAP_XY : 0) |
                      (c->touch_flip_x ? PV_TOUCH_FLIP_X : 0) |
                      (c->touch_flip_y ? PV_TOUCH_FLIP_Y : 0);

    // Sync RPC into the panel task: FIFO-ordered behind any queued blits (so
    // nothing is in flight when the panel IO is torn down and rebuilt) and it
    // grows the blit pool to the new geometry before replying. On a stuck
    // pipeline this fails fast (or times out and latches the fault); rx_task
    // then reboots and the host retries after the device re-enumerates.
    esp_err_t err = pv_panel_task_configure(&cfg);
    if (err != ESP_OK) {
        // Stop accepting blits. Not just for a first CONFIG: a failure partway
        // through a RE-config leaves the panel on the new geometry with the pool
        // still sized for the old one, so every later BLIT would be dropped by
        // the parser's capacity check with a warning per rect and no way out.
        atomic_store(&s_ready, false);
        const char *why = (err == ESP_ERR_NO_MEM)  ? "no PSRAM for frame buffers"
                        : (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE)
                              ? "panel stalled"
                              : "panel init failed";
        send_config_ack(picoview_wire_Status_STATUS_ERROR, why);
        return;
    }
    xSemaphoreTake(s_touch_mtx, portMAX_DELAY);
    pv_touch_configure(&cfg);
    // Haptics shares the touch I2C bus; bring the DRV2605L up under the same lock
    // so its device-add can't race a touch poll. A failure is non-fatal — the
    // panel still runs, haptics just stay silent.
    if (pv_haptics_configure() != ESP_OK) {
        ESP_LOGW(TAG, "haptics init failed; continuing without haptics");
    }
    xSemaphoreGive(s_touch_mtx);

    pv_panel_task_gate_backlight();
    atomic_store(&s_ready, true);
    send_config_ack(picoview_wire_Status_STATUS_OK, NULL);
}

// Validate one reassembled BLIT and hand it to the panel task. Consumes `buf`
// on every path: a valid rect transfers ownership via submit (which recycles it
// after the pixels drain, or on failure), a rejected one is released back to
// the pool here.
static void handle_blit(uint8_t *buf, const uint8_t *payload, uint32_t len,
                        uint16_t flags) {
    if (atomic_load(&s_ready) && len >= sizeof(pv_blit_t)) {
        pv_blit_t b;
        memcpy(&b, payload, sizeof(b));
        uint32_t pix_len = len - sizeof(pv_blit_t);
        if ((uint32_t)b.w * b.h * 2 != pix_len) {
            ESP_LOGW(TAG, "BLIT size mismatch: %ux%u vs %u bytes",
                     b.w, b.h, (unsigned)pix_len);
        } else if (b.x + b.w > pv_panel_width() || b.y + b.h > pv_panel_height()) {
            ESP_LOGW(TAG, "BLIT out of bounds: (%u,%u %ux%u)", b.x, b.y, b.w, b.h);
        } else {
            pv_panel_task_submit_blit(buf, b.x, b.y, b.w, b.h,
                                      (flags & PV_FLAG_PRESENT) != 0, pix_len);
            return;
        }
    }
    pv_panel_task_release_buf(buf);
}

// --- OTA firmware update (control-path messages; see ota.c) -----------------
// Last percentage pushed to the panel/host this session, to throttle updates.
static uint8_t s_ota_last_pct;

static void send_ota_status(picoview_wire_OtaState state, uint8_t pct, int16_t err) {
    picoview_wire_DeviceToHost m = picoview_wire_DeviceToHost_init_zero;
    m.which_msg = picoview_wire_DeviceToHost_ota_status_tag;
    m.msg.ota_status.state = state;
    m.msg.ota_status.pct = pct;
    m.msg.ota_status.err = err;
    send_ctrl(&m);
}

static void handle_ota_begin(const picoview_wire_OtaBegin *b) {
    // Quiesce the render pipeline before flash writes start: the fence returns
    // once every queued blit (and its pixel DMA) has fully drained, so nothing
    // races the OTA. On a stuck pipeline it fails fast: refuse the session
    // rather than start one whose progress-screen draws would go to a dead
    // panel while rx_task is about to reboot anyway. The host retries after
    // the device re-enumerates.
    if (pv_panel_task_fence() != ESP_OK) {
        send_ota_status(picoview_wire_OtaState_OTA_STATE_FAILED, 0, PV_OTA_ERR_BEGIN);
        return;
    }
    // Detach rx_task from the WDT before the flash erase inside pv_ota_begin (and,
    // later, the image verify inside pv_ota_finish): both are multi-second blocking
    // ops. Re-attached below on any path that leaves the OTA session.
    rx_wdt_pause();
    int16_t err = pv_ota_begin(b->image_size, b->sha256, b->version);
    if (err != PV_OTA_ERR_NONE) {
        rx_wdt_resume();
        send_ota_status(picoview_wire_OtaState_OTA_STATE_FAILED, 0, err);
        return;
    }
    atomic_store(&s_ota_active, true);
    s_ota_last_pct = 0xFF;
    pv_panel_set_backlight(true);  // the update screen must be visible
    pv_panel_task_progress_begin(pv_ota_is_recovery());
    pv_panel_task_progress_update(0);
    send_ota_status(picoview_wire_OtaState_OTA_STATE_RECEIVING, 0, PV_OTA_ERR_NONE);
}

static void handle_ota_data(const picoview_wire_OtaData *d) {
    if (!atomic_load(&s_ota_active)) {
        send_ota_status(picoview_wire_OtaState_OTA_STATE_FAILED, 0, PV_OTA_ERR_STATE);
        return;
    }
    uint8_t pct = 0;
    int16_t err = pv_ota_write(d->seq, d->data.bytes, d->data.size, &pct);
    if (err != PV_OTA_ERR_NONE) {
        atomic_store(&s_ota_active, false);
        rx_wdt_resume();
        send_ota_status(picoview_wire_OtaState_OTA_STATE_FAILED,
                        s_ota_last_pct == 0xFF ? 0 : s_ota_last_pct, err);
        return;
    }
    if (pct != s_ota_last_pct) {
        s_ota_last_pct = pct;
        pv_panel_task_progress_update(pct);
        send_ota_status(picoview_wire_OtaState_OTA_STATE_RECEIVING, pct, PV_OTA_ERR_NONE);
    }
}

static void handle_ota_end(void) {
    if (!atomic_load(&s_ota_active)) {
        send_ota_status(picoview_wire_OtaState_OTA_STATE_FAILED, 0, PV_OTA_ERR_STATE);
        return;
    }
    send_ota_status(picoview_wire_OtaState_OTA_STATE_VERIFYING, 100, PV_OTA_ERR_NONE);
    pv_panel_task_progress_update(100);
    int16_t err = pv_ota_finish();
    atomic_store(&s_ota_active, false);
    if (err != PV_OTA_ERR_NONE) {
        rx_wdt_resume();  // stayed paused across the verify; session is over
        send_ota_status(picoview_wire_OtaState_OTA_STATE_FAILED, 100, err);
        return;
    }
    send_ota_status(picoview_wire_OtaState_OTA_STATE_DONE, 100, PV_OTA_ERR_NONE);
    ESP_LOGI(TAG, "OTA committed; rebooting into the new app");
    vTaskDelay(pdMS_TO_TICKS(100));  // let DONE + logs reach the host
    esp_restart();                   // no return
}

static void handle_ota_abort(void) {
    pv_ota_abort();
    atomic_store(&s_ota_active, false);
    rx_wdt_resume();
    send_ota_status(picoview_wire_OtaState_OTA_STATE_UNSPECIFIED, 0, PV_OTA_ERR_NONE);
}

static void handle_auth_challenge(const picoview_wire_AuthChallenge *c) {
    // The ECDSA signature is sub-millisecond and nothing else is in flight
    // between HELLO and CONFIG, so answering inline here is fine.
    pv_auth_response_t resp;
    pv_auth_handle_challenge(c->nonce, PV_AUTH_NONCE_LEN, &resp);

    picoview_wire_DeviceToHost m = picoview_wire_DeviceToHost_init_zero;
    m.which_msg = picoview_wire_DeviceToHost_auth_response_tag;
    switch (resp.status) {  // local pv_auth_status -> wire AuthStatus
        case PV_AUTH_OK:
            m.msg.auth_response.status = picoview_wire_AuthStatus_AUTH_STATUS_OK;
            break;
        case PV_AUTH_UNPROVISIONED:
            m.msg.auth_response.status = picoview_wire_AuthStatus_AUTH_STATUS_UNPROVISIONED;
            break;
        case PV_AUTH_ERR_BAD_REQ:
            m.msg.auth_response.status = picoview_wire_AuthStatus_AUTH_STATUS_MALFORMED_CHALLENGE;
            break;
        default:
            m.msg.auth_response.status = picoview_wire_AuthStatus_AUTH_STATUS_SIGNING_ERROR;
            break;
    }
    if (resp.status == PV_AUTH_OK) {
        // The cert travels as opaque bytes; its layout is its own versioned
        // format (cert[0] = cert_version), parsed at fixed offsets by the host.
        m.msg.auth_response.certificate.size = sizeof(resp.cert);
        memcpy(m.msg.auth_response.certificate.bytes, resp.cert, sizeof(resp.cert));
        m.msg.auth_response.signature.size = sizeof(resp.signature);
        memcpy(m.msg.auth_response.signature.bytes, resp.signature, sizeof(resp.signature));
    }
    send_ctrl(&m);
    ESP_LOGI(TAG, "AUTH_CHALLENGE -> response (status %u)", resp.status);
}

static void dispatch_ctrl(const uint8_t *payload, uint32_t len) {
    pb_istream_t is = pb_istream_from_buffer(payload, len);
    if (!pb_decode(&is, picoview_wire_HostToDevice_fields, &s_h2d)) {
        ESP_LOGW(TAG, "CTRL decode failed (%u bytes): %s", (unsigned)len, PB_GET_ERROR(&is));
        return;
    }
    switch (s_h2d.which_msg) {
        case picoview_wire_HostToDevice_hello_tag:
            handle_hello(&s_h2d.msg.hello);
            break;
        case picoview_wire_HostToDevice_config_tag:
            handle_config(&s_h2d.msg.config);
            break;
        case picoview_wire_HostToDevice_ota_begin_tag:
            handle_ota_begin(&s_h2d.msg.ota_begin);
            break;
        case picoview_wire_HostToDevice_ota_data_tag:
            handle_ota_data(&s_h2d.msg.ota_data);
            break;
        case picoview_wire_HostToDevice_ota_end_tag:
            handle_ota_end();
            break;
        case picoview_wire_HostToDevice_ota_abort_tag:
            handle_ota_abort();
            break;
        case picoview_wire_HostToDevice_auth_challenge_tag:
            handle_auth_challenge(&s_h2d.msg.auth_challenge);
            break;
        case picoview_wire_HostToDevice_set_param_tag:
            handle_set_param(&s_h2d.msg.set_param);
            break;
        case picoview_wire_HostToDevice_haptics_tag:
            handle_haptics(&s_h2d.msg.haptics);
            break;
        case picoview_wire_HostToDevice_keepalive_tag:
            // Liveness only: receiving any OUT bytes already reset the idle
            // watchdog in rx_task, so there is nothing to do here.
            break;
        case picoview_wire_HostToDevice_get_device_info_tag:
            handle_get_device_info();
            break;
        default:
            // which_msg == 0 means the variant was newer than the schema this
            // firmware was built with (nanopb skips unknown fields). The host
            // gates these on capabilities we don't advertise.
            ESP_LOGW(TAG, "unhandled CTRL variant %u", (unsigned)s_h2d.which_msg);
            break;
    }
}

// ---------------------------------------------------------------------------
// Bulk OUT reassembly: a byte-fed state machine spanning 512-byte packets. Each
// message's payload is routed to either the small control buffer or a pool blit
// buffer, chosen when the header is decoded.
// ---------------------------------------------------------------------------
typedef struct {
    enum { ST_HEADER, ST_PAYLOAD } state;
    uint8_t  hdr[PV_HEADER_LEN];
    size_t   hdr_got;
    pv_header_t cur;
    uint8_t *dst;       // where payload bytes accumulate (payload_len is
                        // validated against its capacity before ST_PAYLOAD)
    bool     dst_blit;  // dst is a pool buffer -> dispatch as BLIT
    uint8_t *dst_buf;   // acquired pool buffer backing dst (owned until dispatch)
    size_t   pay_got;
    uint32_t skip_left; // payload too big for any buffer: discard this many bytes
    // When the parser last held no partial message, stamped from INSIDE
    // parser_feed (see parser_tick for why it cannot be sampled per rx_task
    // iteration instead).
    int64_t  last_clean_us;
} parser_t;

static void parser_reset(parser_t *p) {
    p->state = ST_HEADER;
    p->hdr_got = 0;
    p->pay_got = 0;
    p->skip_left = 0;
    p->last_clean_us = esp_timer_get_time();
    if (p->dst_buf) {
        // A BLIT was mid-reassembly (e.g. unplug mid-frame): hand its pool
        // buffer back so the accounting stays exact.
        pv_panel_task_release_buf(p->dst_buf);
        p->dst_buf = NULL;
    }
}

// True while the parser is holding the tail of an incomplete message: a partial
// header, a payload still filling, or a payload being skipped.
static bool parser_partial(const parser_t *p) {
    return p->state != ST_HEADER || p->hdr_got != 0 || p->skip_left != 0 ||
           p->dst_buf != NULL;
}

// Abandon a message that has been half-received for PV_PARSER_STALE_MS. Called
// once per rx_task iteration.
//
// The case that matters is a host that closed the INTERFACE without the device
// un-enumerating (the app quit, crashed, or hot-restarted): `tud_mounted()`
// stays true, so the unmount path in rx_task never runs, and the parser is left
// owing the rest of a BLIT -- up to ~253 KB for a full frame. Every byte of the
// next session's HELLO would then be swallowed as pixel data, the host's two
// HELLO attempts would time out, and only its reopen-then-USB-reset escalation
// (~5s later) would break the deadlock. The reset also hands back the pool
// buffer the dead message was reassembling into, which the idle face needs
// before it can grow the pool.
//
// Two things this is careful about, both of which rule out simpler triggers:
//   - NOT "no bytes received": a stuck parser consumes everything the host
//     sends, including the HELLOs of the session trying to reconnect, so a
//     byte-triggered re-arm would push the deadline out forever.
//   - NOT "parser was partial at the last N ticks": under a sustained stream the
//     RX FIFO routinely empties mid-payload, so every iteration can legitimately
//     sample a partial parser even though messages are completing at 60fps.
//     Hence last_clean_us, stamped from inside parser_feed as each message
//     completes -- that is the real liveness signal.
//
// Deliberately does NOT flush the RX FIFO: whatever is queued may already
// contain the next session's frames, and parser_feed resyncs on the magic.
static void parser_tick(parser_t *p) {
    if (!parser_partial(p)) {
        p->last_clean_us = esp_timer_get_time();
        return;
    }
    if (esp_timer_get_time() - p->last_clean_us < (int64_t)PV_PARSER_STALE_MS * 1000) {
        return;
    }
    ESP_LOGW(TAG, "half-received frame stalled for %ums; resetting parser",
             (unsigned)PV_PARSER_STALE_MS);
    parser_reset(p);  // re-stamps last_clean_us
}

static void parser_feed(parser_t *p, const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        // A stalled panel was detected mid-chunk: stop feeding it and let rx_task
        // recover (reboot). The rest of this chunk is dropped, which is harmless
        // -- the device re-enumerates and the host restreams from scratch.
        if (pv_panel_task_faulted()) return;
        if (p->skip_left) {
            size_t n = len - i;
            if (n > p->skip_left) n = p->skip_left;
            i += n;
            p->skip_left -= n;
            if (p->skip_left == 0) p->state = ST_HEADER;
            continue;
        }

        if (p->state == ST_HEADER) {
            p->hdr[p->hdr_got++] = data[i++];
            if (p->hdr_got < PV_HEADER_LEN) continue;

            memcpy(&p->cur, p->hdr, sizeof(p->cur));
            if (p->cur.magic != PV_MAGIC) {
                // Desync: drop the oldest byte and keep scanning for the magic.
                memmove(p->hdr, p->hdr + 1, PV_HEADER_LEN - 1);
                p->hdr_got = PV_HEADER_LEN - 1;
                continue;
            }
            p->hdr_got = 0;

            uint32_t plen = p->cur.payload_len;
            if (p->cur.type == PV_MSG_BLIT) {
                if (plen == 0) continue;
                size_t cap = pv_panel_task_buf_cap();
                if (atomic_load(&s_ota_active) || plen > cap) {
                    // No rendering during an update; also guards oversized rects.
                    if (plen > cap) {
                        ESP_LOGW(TAG, "BLIT payload %u > buf %u; dropping",
                                 (unsigned)plen, (unsigned)cap);
                    }
                    p->skip_left = plen;
                    continue;
                }
                // Acquire a recycled pool buffer -- the bounded wait here is the
                // natural pacing of USB ingest to SPI drain speed. NULL means
                // the panel task stopped recycling (fault latched): bail and let
                // rx_task recover.
                p->dst_buf = pv_panel_task_acquire_buf();
                if (!p->dst_buf) return;
                p->dst = p->dst_buf + PV_BLIT_DST_OFFSET;
                p->dst_blit = true;
            } else if (p->cur.type == PV_MSG_CTRL) {
                if (plen == 0) {
                    // Legal but meaningless (decodes to no variant); ignore.
                    continue;
                }
                if (plen > sizeof(s_ctrl_buf)) {
                    ESP_LOGW(TAG, "CTRL payload %u > %u; dropping",
                             (unsigned)plen, (unsigned)sizeof(s_ctrl_buf));
                    p->skip_left = plen;
                    continue;
                }
                p->dst = s_ctrl_buf;
                p->dst_blit = false;
            } else {
                // Unknown frame type (e.g. a v1 host, or a future type): skip
                // the payload; the sender's handshake will fail cleanly.
                ESP_LOGW(TAG, "unknown frame type %u (%u bytes); skipping",
                         p->cur.type, (unsigned)plen);
                p->skip_left = plen;
                continue;
            }
            p->state = ST_PAYLOAD;
            p->pay_got = 0;
            continue;
        }

        // ST_PAYLOAD
        size_t want = p->cur.payload_len - p->pay_got;
        size_t avail = len - i;
        size_t n = want < avail ? want : avail;
        memcpy(p->dst + p->pay_got, data + i, n);
        p->pay_got += n;
        i += n;
        if (p->pay_got == p->cur.payload_len) {
            if (p->dst_blit) {
                handle_blit(p->dst_buf, p->dst, p->cur.payload_len, p->cur.flags);
                p->dst_buf = NULL;  // consumed on every path (submitted or released)
            } else {
                dispatch_ctrl(p->dst, p->cur.payload_len);
            }
            p->state = ST_HEADER;
            // The parser is clean again: this is the liveness signal parser_tick
            // measures staleness against.
            p->last_clean_us = esp_timer_get_time();
        }
    }
}

// Recover from a panel stall -- a lost color completion, or a panel task that
// stopped answering bounded waits -- by rebooting. rx_task only.
//
// Rebooting rather than re-initing in place: a lost completion leaves the QSPI
// peripheral stuck on a phantom in-flight color transaction, and tearing the bus
// down goes through esp_lcd_panel_io_del(), which drains in-flight color
// transactions with portMAX_DELAY -- so it would block rx_task forever on that
// same stuck transfer. A restart is the only way back to a known SPI state; the
// device re-enumerates in ~1s and the host's reconnect re-CONFIGs from scratch.
// The Task WDT (rx_task is subscribed) is the backstop if this is never reached.
static void recover_panel(void) {
    ESP_LOGE(TAG, "panel/SPI stalled (lost color completion); rebooting to recover");
    vTaskDelay(pdMS_TO_TICKS(50));  // let the log drain to the monitor first
    esp_restart();                  // no return
}

// Bulk OUT data has arrived (called from the TinyUSB task): wake rx_task.
void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize) {
    (void)itf; (void)buffer; (void)bufsize;
    if (s_rx_task) xTaskNotifyGive(s_rx_task);
}

static void rx_task(void *arg) {
    (void)arg;
    // Watch rx_task with the Task WDT: it is the one task that can wedge on the
    // panel/SPI path. A missed reset panics with a backtrace and reboots in
    // CONFIG_ESP_TASK_WDT_TIMEOUT_S instead of hanging the USB link indefinitely.
    rx_wdt_resume();
    ESP_LOGI(TAG, "rx_task up; subscribed to task WDT (%ds)", CONFIG_ESP_TASK_WDT_TIMEOUT_S);

    parser_t p = {0};
    uint8_t chunk[RX_CHUNK];
    bool was_mounted = false;
    bool idled = false;   // idle screen (face or blank) is up; don't repeat per stretch
    // Deadline after which an idle host gets the idle screen. Reset on connect and
    // on any received bytes (a frame or a keepalive), so it only fires when the
    // host has genuinely gone silent.
    int64_t idle_deadline = esp_timer_get_time() + (int64_t)PV_IDLE_BLANK_MS * 1000;
    parser_reset(&p);  // arms the parser's staleness clock (see parser_tick)
    for (;;) {
        // Feed the WDT once per iteration. The loop turns over at least every
        // 50ms in every steady state (the notify-take timeouts below), so only a
        // genuine block trips it. Skipped while paused for an OTA session.
        if (s_rx_wdt_added) esp_task_wdt_reset();
        // Handle a panel stall before touching the pipeline again.
        if (pv_panel_task_faulted()) recover_panel();
        // Ticked on every iteration, mounted or not: a stuck parser swallows
        // everything the host sends, so it must be cleared without waiting for
        // the link to drop.
        parser_tick(&p);
        if (!tud_mounted()) {
            if (was_mounted) {
                // Reconnect must start clean: reset the parser (returning any
                // half-filled pool buffer) and discard whatever is left in the
                // RX FIFO. Queued blits finish in the panel task on their own.
                parser_reset(&p);
                tud_vendor_read_flush();
                // A host that vanished mid-OTA left the session (and the WDT)
                // paused; abort it so the link and the WDT return to a clean state.
                if (atomic_load(&s_ota_active)) {
                    pv_ota_abort();
                    atomic_store(&s_ota_active, false);
                }
                rx_wdt_resume();  // no-op unless an OTA had paused it
                was_mounted = false;
                // Hold the last frame; start the idle countdown from the unplug.
                // Any idle screen already up (idled) keeps showing across the unplug.
                idle_deadline = esp_timer_get_time() + (int64_t)PV_IDLE_BLANK_MS * 1000;
                ESP_LOGI(TAG, "USB unmounted; RX flushed, parser reset");
            }
            if (esp_timer_get_time() >= idle_deadline) enter_idle_panel(&idled);
            pv_face_tick();  // wander the idle face's expression (no-op if none up)
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }
        if (!was_mounted) {
            was_mounted = true;
            // Keep any idle screen (idled) up until real host bytes arrive below.
            idle_deadline = esp_timer_get_time() + (int64_t)PV_IDLE_BLANK_MS * 1000;
            ESP_LOGI(TAG, "USB mounted; awaiting CONFIG/frames");
        }

        bool any = false;
        uint32_t drained = 0;
        while (tud_vendor_available()) {
            uint32_t got = tud_vendor_read(chunk, sizeof(chunk));
            if (got) {
                // Host is talking again: reclaim the panel from the idle face before
                // any CONFIG/BLIT in these bytes is dispatched by parser_feed.
                if (idled) leave_idle_panel(&idled);
                parser_feed(&p, chunk, got);
                any = true;
                // USB-HS ingests faster than QSPI drains, so a sustained stream
                // can hold this loop for seconds. Feed the WDT every 32 chunks
                // (16KB) so throughput is never mistaken for a wedge -- a real
                // block inside parser_feed still stops the feeding and trips it.
                if ((++drained & 0x1F) == 0 && s_rx_wdt_added) esp_task_wdt_reset();
            }
        }
        if (any) {
            // Host is alive (a frame, keepalive, or any CTRL): push the deadline
            // out. If the idle screen had blanked while still mounted, the next
            // presented frame relights the panel through the backlight gate.
            idle_deadline = esp_timer_get_time() + (int64_t)PV_IDLE_BLANK_MS * 1000;
        } else {
            // Nothing to drain: show the idle screen if the host has gone quiet, then
            // sleep until rx_cb wakes us (slow fallback tick so idle is noticed).
            if (esp_timer_get_time() >= idle_deadline) enter_idle_panel(&idled);
            pv_face_tick();  // wander the idle face's expression (no-op if none up)
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        }
    }
}

// ---------------------------------------------------------------------------
// Touch poll -> bulk IN. Woken by the CST816 INT line (when wired) for low
// latency; falls back to a 15ms tick while a finger is down to track move/up,
// and a slow tick otherwise.
// ---------------------------------------------------------------------------
#if PV_TOUCH_INT_ENABLED
static void IRAM_ATTR touch_isr(void *arg) {
    (void)arg;
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_touch_task, &hp);
    portYIELD_FROM_ISR(hp);
}
#endif

static void touch_task(void *arg) {
    (void)arg;
    for (;;) {
        TickType_t wait;
        if (!atomic_load(&s_ready) || !tud_mounted() || atomic_load(&s_ota_active)) {
            wait = pdMS_TO_TICKS(100);  // idle / updating: just re-check link state
        } else if (pv_touch_active()) {
            wait = pdMS_TO_TICKS(15);   // finger down: track move/release
        } else {
#if PV_TOUCH_INT_ENABLED
            wait = portMAX_DELAY;       // sleep until the INT line fires
#else
            wait = pdMS_TO_TICKS(15);   // no INT wired: plain poll
#endif
        }
        ulTaskNotifyTake(pdTRUE, wait);

        if (!atomic_load(&s_ready) || !tud_mounted() || atomic_load(&s_ota_active)) continue;
        pv_touch_t evt;
        bool have;
        xSemaphoreTake(s_touch_mtx, portMAX_DELAY);
        have = pv_touch_poll(&evt);
        xSemaphoreGive(s_touch_mtx);
        if (have) {
            picoview_wire_DeviceToHost m = picoview_wire_DeviceToHost_init_zero;
            m.which_msg = picoview_wire_DeviceToHost_touch_tag;
            // pv_touch_phase is 0-based; the wire enum is offset by one for the
            // proto3 UNSPECIFIED zero value.
            m.msg.touch.phase = (picoview_wire_TouchPhase)(evt.phase + 1);
            m.msg.touch.x = evt.x;
            m.msg.touch.y = evt.y;
            send_ctrl(&m);
        }
    }
}

// ---------------------------------------------------------------------------
// USB device events (esp_tinyusb routes tud_*_cb through this): keep the
// backlight in step with the link. Suspend/resume require
// CONFIG_TINYUSB_SUSPEND_CALLBACK / _RESUME_CALLBACK (set in sdkconfig.defaults).
// ---------------------------------------------------------------------------
static void usb_event_cb(tinyusb_event_t *event, void *arg) {
    (void)arg;
    switch (event->id) {
        case TINYUSB_EVENT_DETACHED:
            // Don't dark the panel immediately: a brief replug or a host reboot
            // shouldn't blink the screen. rx_task holds the last frame lit until
            // the idle window elapses, then shows the idle screen.
            ESP_LOGI(TAG, "USB detached (bus reset / unplug)");
            pv_panel_task_gate_backlight();  // relight on the next frame
            if (s_rx_task) xTaskNotifyGive(s_rx_task);  // cleanup + start idle countdown
            break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
        case TINYUSB_EVENT_SUSPENDED:
            ESP_LOGI(TAG, "USB suspended; backlight off");
            pv_panel_set_backlight(false);
            break;
#endif
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
        case TINYUSB_EVENT_RESUMED:
            // GRAM still holds the last frame; if we'd already lit it, relight.
            ESP_LOGI(TAG, "USB resumed");
            if (atomic_load(&s_ready) && !pv_panel_task_backlight_gated()) {
                pv_panel_set_backlight(true);
            }
            break;
#endif
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Panel power rail: bring up the ESP32-P4 internal LDO that supplies the panel
// VCC (LDO_VO4 -> 3V3 on the reference board) before anything touches the panel.
// The channel handle is acquired once and never released -- releasing it would
// cut panel power. (The LDO API has no current setting; the ~200mA the panel
// draws is a hardware capability of the internal LDO, not a software knob.)
// ---------------------------------------------------------------------------
#if CONFIG_PV_LCD_LDO_CHAN > 0
static esp_ldo_channel_handle_t s_lcd_ldo;

static void lcd_power_on(void) {
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = CONFIG_PV_LCD_LDO_CHAN,
        .voltage_mv = CONFIG_PV_LCD_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &s_lcd_ldo));
    ESP_LOGI(TAG, "panel power: LDO_VO%d @ %dmV",
             CONFIG_PV_LCD_LDO_CHAN, CONFIG_PV_LCD_LDO_VOLTAGE_MV);
}
#else
static void lcd_power_on(void) {}
#endif

static void touch_int_install(void) {
#if PV_TOUCH_INT_ENABLED
    gpio_config_t intc = {
        .pin_bit_mask = 1ULL << CONFIG_PV_PIN_TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // CST816 INT is active-low
    };
    ESP_ERROR_CHECK(gpio_config(&intc));
    esp_err_t isr = gpio_install_isr_service(0);
    if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) {  // tolerate already-installed
        ESP_ERROR_CHECK(isr);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_PV_PIN_TOUCH_INT, touch_isr, NULL));
    ESP_LOGI(TAG, "touch INT on GPIO%d (event-driven)", CONFIG_PV_PIN_TOUCH_INT);
#endif
}

void app_main(void) {
    // Runs the boot-loop detector first: a wedged app that keeps resetting without
    // reaching a host handshake is redirected to the factory recovery app here
    // (may reboot and not return). Also logs the running partition.
    pv_ota_boot_check();
    bool recovery = pv_ota_is_recovery();
    // Load the device identity (eFuse key + cert) before the rx task exists, so
    // the HELLO_ACK can report whether this unit can attest. Both the app and
    // the recovery image do it -- an unprovisioned board just reports false and
    // is driven all the same.
    pv_auth_init();

    lcd_power_on();  // power the panel rail before any panel/touch bring-up

    s_tx_mtx = xSemaphoreCreateMutex();
    s_touch_mtx = xSemaphoreCreateMutex();
    atomic_init(&s_ready, false);
    atomic_init(&s_ota_active, false);

    // The panel task must exist before any host traffic (rx_task submits to it)
    // and before the idle face can come up.
    ESP_ERROR_CHECK(pv_panel_task_start());

    // Derive the USB serial from the eFuse MAC before the descriptors are read.
    pv_usb_serial_init();
    int str_count = 0;
    const char **strings = pv_string_descriptors(&str_count);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();  // HS port on the P4
    cfg.descriptor.device = pv_device_descriptor();
    cfg.descriptor.qualifier = pv_qualifier_descriptor();
    cfg.descriptor.string = strings;
    cfg.descriptor.string_count = str_count;
    cfg.descriptor.full_speed_config = pv_fs_config_descriptor();
    cfg.descriptor.high_speed_config = pv_hs_config_descriptor();
    cfg.event_cb = usb_event_cb;  // backlight follows attach/detach/suspend/resume
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));
    ESP_LOGI(TAG, "USB vendor device installed (VID 0x303A)");

    xTaskCreate(rx_task, "pv_rx", 4096, NULL, 6, &s_rx_task);
    // The factory recovery app only receives firmware over USB -- it never drives
    // touch, so skip the touch task and its INT there.
    if (!recovery) {
        // 4 KB: the task now stack-allocates a DeviceToHost + its encode buffer
        // (~700 bytes) per touch event on top of the TinyUSB write path.
        xTaskCreate(touch_task, "pv_touch", 4096, NULL, 4, &s_touch_task);
        touch_int_install();  // after s_touch_task exists (the ISR notifies it)
    }

    ESP_LOGI(TAG, "pico-view firmware ready; awaiting %s",
             recovery ? "recovery OTA" : "CONFIG");
}
