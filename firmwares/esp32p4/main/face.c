// Idle facial animation over the esp_emote_expression engine. See face.h for the
// panel-ownership contract.
#include "face.h"

#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"         // esp_random() for the idle expression wander
#include "esp_timer.h"          // esp_timer_get_time() for the wander schedule
#include "expression_emote.h"   // emote_* C API (init / assets / api)

#include "panel.h"
#include "panel_task.h"
#include "protocol.h"

static const char *TAG = "pv_face";

// Label of the asset-pack partition (partitions.csv). Holds the mmap emote pack
// built from esp_emote_assets' 360_360 set (see top-level CMakeLists.txt).
#define PV_FACE_PARTITION   "assets"

// Idle expression "wander": the face drifts from a calm resting expression to a
// brief friendly accent (or a quick wink) and back, so the bot reads as alive
// rather than frozen on one grin.
//
// Names come from the pack's emote.json. NOTE: most positive names there alias
// the same Happy.eaf animation, so the accents below are picked to be visually
// DISTINCT looping faces. Keep the set friendly -- never sad/angry/crying on an
// ambient idle screen.
#define PV_FACE_BASE_EMOJI   "idle"     // calm resting face (loops)
#define PV_FACE_BLINK_EMOJI  "neutral"  // one-shot wink (winking.eaf, loop=false)

// Distinct, friendly looping accents to visit between rests.
static const char *const PV_FACE_ACCENTS[] = { "sleepy", "thinking", "winking" };

// Hold durations (ms). The resting face lingers (relaxed pace); accents and the
// wink are brief punctuation. Actual holds are picked uniformly in [MIN, MAX].
#define PV_FACE_BASE_MIN_MS    8000
#define PV_FACE_BASE_MAX_MS    15000
#define PV_FACE_ACCENT_MIN_MS  2500
#define PV_FACE_ACCENT_MAX_MS  4500
#define PV_FACE_BLINK_MS       900
// Percent chance a "leave the resting face" step is a quick wink vs. an accent.
#define PV_FACE_BLINK_PCT      35

// Backlight level while the idle face plays. The face is ambient, so it runs dim
// to save power and avoid a bright screen glowing when the host is away. The
// host's level is saved on start and restored on stop (s_saved_brightness).
#define PV_FACE_IDLE_BRIGHTNESS  16   // ~10% of 255

// Board default panel geometry, used only when the host never sent a CONFIG (a
// device sitting at boot with no app running). Mirrors the host engine's
// "st77916-round-360" spec (crates/pico-view/src/panels.rs).
#define PV_FACE_DEF_WIDTH   360
#define PV_FACE_DEF_HEIGHT  360

// Target animation frame rate and the flush-stripe height (in scanlines). The
// engine renders the frame in horizontal stripes of buf_pixels; 16 lines * 360px
// * 2 bytes = ~11.5 KB per buffer, kept in internal DMA-capable RAM.
#define PV_FACE_FPS         20
#define PV_FACE_STRIPE_LINES 16

// The single emote handle. rx_task-only: created in pv_face_start, destroyed in
// pv_face_stop; the engine's internal render task lives for that span.
static emote_handle_t s_emote;

// Idle-wander state (rx_task only, valid while s_emote != NULL): whether the
// calm resting face is showing, and the deadline for the next change.
static bool    s_on_base;
static int64_t s_next_us;

// Host backlight level saved on pv_face_start and restored on pv_face_stop, so
// dimming the panel for the idle face never clobbers a brightness the host set.
static uint8_t s_saved_brightness;

// Uniform random ms in [lo, hi] using the hardware RNG. hi >= lo assumed.
static uint32_t face_rand_ms(uint32_t lo, uint32_t hi) {
    return lo + (esp_random() % (hi - lo + 1));
}

// Switch the eye animation. A failure just leaves the previous face up until the
// next tick, which is fine for an idle screen. The emote engine takes its own
// gfx lock, so this is safe against the render task.
static void face_set_emoji(const char *name) {
    esp_err_t err = emote_set_anim_emoji(s_emote, name);
    if (err != ESP_OK) ESP_LOGW(TAG, "set emoji '%s' failed: %s", name, esp_err_to_name(err));
}

// ---------------------------------------------------------------------------
// Flush callback: the engine's render task hands us one finished, windowed
// region of the frame. `data` is RGB565 already byte-swapped to big-endian (the
// .swap flag below), exactly what the ST77916 wants; the bounds are half-open.
// The RPC returns once the pixels have drained, so the engine may recycle the
// buffer as soon as emote_notify_flush_finished releases it.
//
// Runs on the emote render task, not rx_task -- safe because the panel task
// serialises it against anything rx_task submits. On a stalled pipeline the RPC
// latches the panel fault and rx_task reboots, so just log here.
// ---------------------------------------------------------------------------
static void face_flush_cb(int x_start, int y_start, int x_end, int y_end,
                          const void *data, emote_handle_t handle) {
    uint16_t x = (uint16_t)x_start;
    uint16_t y = (uint16_t)y_start;
    uint16_t w = (uint16_t)(x_end - x_start);
    uint16_t h = (uint16_t)(y_end - y_start);

    esp_err_t err = pv_panel_task_face_blit(x, y, w, h, data);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "face blit failed: %s", esp_err_to_name(err));
    }
    emote_notify_flush_finished(handle);
}

bool pv_face_available(void) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                    PV_FACE_PARTITION) != NULL;
}

// Configure the panel with the board default if the host never did, so the face
// can show at boot before any app connects. No-op once the panel has geometry.
static esp_err_t ensure_panel_configured(void) {
    if (pv_panel_width() != 0) return ESP_OK;
    pv_config_t cfg = {
        .model    = PV_PANEL_ST77916,
        .width    = PV_FACE_DEF_WIDTH,
        .height   = PV_FACE_DEF_HEIGHT,
        .x_offset = 0,
        .y_offset = 0,
        .rotation = 0,
        .invert   = 1,   // ST77916 round panel runs with display inversion on
        .touch_addr = 0, // no touch on the idle face; host CONFIG sets real touch
        .touch_flags = 0,
    };
    esp_err_t err = pv_panel_task_configure(&cfg);
    if (err != ESP_OK) ESP_LOGE(TAG, "default panel configure failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t pv_face_start(void) {
    if (s_emote) return ESP_OK;  // already showing

    esp_err_t err = ensure_panel_configured();
    if (err != ESP_OK) return err;

    int w = pv_panel_width();
    int h = pv_panel_height();

    emote_config_t cfg = {
        .flags = {
            .swap = true,           // RGB565 little-endian -> big-endian for the panel
            .double_buffer = false, // single stripe buffer; our flush is synchronous
            .buff_dma = true,       // flush buffers feed the SPI DMA directly
            .buff_spiram = false,   // keep them in internal RAM (small: ~11.5 KB)
        },
        .gfx_emote = { .h_res = w, .v_res = h, .fps = PV_FACE_FPS },
        .buffers = { .buf_pixels = (size_t)w * PV_FACE_STRIPE_LINES },
        .task = {
            .task_priority = 4,     // below rx_task (6); renders while rx_task idles
            .task_stack = 6144,
            .task_affinity = -1,    // any core
            .task_stack_in_ext = false,
        },
        .flush_cb = face_flush_cb,
        .update_cb = NULL,
        .user_data = NULL,
    };

    s_emote = emote_init(&cfg);
    if (!s_emote) {
        ESP_LOGE(TAG, "emote_init failed");
        return ESP_FAIL;
    }

    emote_data_t src = {
        .type = EMOTE_SOURCE_PARTITION,
        .source = { .partition_label = PV_FACE_PARTITION },
        .flags = { .mmap_enable = 1 },
    };
    err = emote_mount_and_load_assets(s_emote, &src);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "asset load from '%s' failed: %s", PV_FACE_PARTITION, esp_err_to_name(err));
        goto fail;
    }

    err = emote_set_anim_emoji(s_emote, PV_FACE_BASE_EMOJI);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set emoji '%s' failed: %s", PV_FACE_BASE_EMOJI, esp_err_to_name(err));
        goto fail;
    }
    // Start on the resting face; schedule the first wander after a full base hold.
    s_on_base = true;
    s_next_us = esp_timer_get_time() + (int64_t)face_rand_ms(PV_FACE_BASE_MIN_MS, PV_FACE_BASE_MAX_MS) * 1000;

    emote_notify_all_refresh(s_emote);   // kick the first render
    // Save the host's level (restored in pv_face_stop) and dim BEFORE gating the
    // light on, so the face never flashes at full brightness.
    s_saved_brightness = pv_panel_get_brightness();
    pv_panel_set_brightness(PV_FACE_IDLE_BRIGHTNESS);
    pv_panel_set_backlight(true);        // assets are good: light the face
    ESP_LOGI(TAG, "idle face '%s' up (%dx%d)", PV_FACE_BASE_EMOJI, w, h);
    return ESP_OK;

fail:
    emote_deinit(s_emote);
    s_emote = NULL;
    return err;
}

void pv_face_stop(void) {
    if (!s_emote) return;
    // emote_deinit stops and joins the render task, so no flush callback can be in
    // flight (or start) once it returns -- the panel is free for rx_task again.
    emote_deinit(s_emote);
    s_emote = NULL;
    // Restore the host's backlight level so the reclaimed panel isn't left dim.
    pv_panel_set_brightness(s_saved_brightness);
    ESP_LOGI(TAG, "idle face stopped");
}

void pv_face_tick(void) {
    if (!s_emote) return;                       // no face up: nothing to animate

    int64_t now = esp_timer_get_time();
    if (now < s_next_us) return;                // not time for the next change yet

    if (s_on_base) {
        // Leave the resting face for a brief accent, or a quick one-shot wink.
        // Either way the next tick after the hold returns to the calm base below.
        if ((esp_random() % 100) < PV_FACE_BLINK_PCT) {
            face_set_emoji(PV_FACE_BLINK_EMOJI);
            s_next_us = now + (int64_t)PV_FACE_BLINK_MS * 1000;
        } else {
            const char *accent = PV_FACE_ACCENTS[esp_random() % (sizeof(PV_FACE_ACCENTS) / sizeof(PV_FACE_ACCENTS[0]))];
            face_set_emoji(accent);
            s_next_us = now + (int64_t)face_rand_ms(PV_FACE_ACCENT_MIN_MS, PV_FACE_ACCENT_MAX_MS) * 1000;
        }
        s_on_base = false;
    } else {
        // Back to the calm resting face for a longer, relaxed stretch.
        face_set_emoji(PV_FACE_BASE_EMOJI);
        s_next_us = now + (int64_t)face_rand_ms(PV_FACE_BASE_MIN_MS, PV_FACE_BASE_MAX_MS) * 1000;
        s_on_base = true;
    }
}
