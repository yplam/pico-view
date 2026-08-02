// See panel_task.h for the design. The API functions in the top half only touch
// queues/semaphores/atomics -- every esp_lcd call happens in panel_task() at the
// bottom, so a wedge inside the SPI driver can only ever pin this task, never a
// caller.
#include "panel_task.h"

#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "pv_panel_task";

// Pool depth: while one blit's pixels drain over SPI, the next reassembles off
// USB into the other buffer. Two is enough to fully overlap receive and transmit.
#define NUM_BLIT_BUFS 2
// Initial payload capacity (pre-CONFIG traffic is tiny); the first configure
// grows the pool to a full frame of the panel geometry.
#define BLIT_CAP_INITIAL 4096

// Request queue depth. Concurrent producers hold at most: NUM_BLIT_BUFS blits
// (rx_task), one sync RPC (rx_task or the face render task each), one clear,
// plus best-effort progress updates -- 8 leaves headroom without letting a
// stalled task hide behind a deep backlog.
#define REQ_QUEUE_DEPTH 8

// Bounded-wait budgets. Stall detectors, not pacing: generous multiples of the
// healthy worst cases (a full-frame blit drains in ~13ms at 40MHz QSPI, a
// configure runs ~600ms of controller power-on delays), yet each still fits the
// 5s Task-WDT budget of an rx_task that fed the WDT just before waiting -- at
// most one long wait runs per fault, since the latch fails later ops fast.
#define ACQUIRE_TIMEOUT_MS 2000
#define SEND_TIMEOUT_MS    1000
#define RPC_TIMEOUT_MS     3000
#define FACE_TIMEOUT_MS    2000

typedef enum {
    OP_BLIT,             // async; recycles the pool buffer when drained
    OP_FACE_BLIT,        // sync RPC (face render task)
    OP_CONFIGURE,        // sync RPC (rx_task); grows the pool
    OP_FENCE,            // sync RPC (rx_task); pure barrier
    OP_CLEAR,            // async
    OP_PROGRESS_BEGIN,   // async, best-effort
    OP_PROGRESS_UPDATE,  // async, best-effort
} panel_op_t;

typedef struct {
    panel_op_t op;
    union {
        struct { uint8_t *buf; uint32_t pix_len; uint16_t x, y, w, h; bool present; } blit;
        struct { const void *pixels; uint16_t x, y, w, h; } face;
        struct { pv_config_t cfg; } configure;
        struct { bool is_recovery; } progress_begin;
        struct { uint8_t pct; } progress_update;
    } u;
} panel_req_t;

static TaskHandle_t  s_task;
static QueueHandle_t s_req;    // panel_req_t x REQ_QUEUE_DEPTH
static QueueHandle_t s_free;   // uint8_t* pool buffers ready for reassembly

// Payload capacity of every pool buffer. Changes only while rx_task is parked
// inside the configure RPC, so rx_task's reads between RPCs need no locking.
// (The buffers themselves are tracked solely by s_free -- every one is either in
// that queue or owned by whoever dequeued it.)
static size_t s_cap;

// Latched when a bounded wait on this task (free buffer, queue send, RPC reply)
// times out: the task is stuck inside esp_lcd and will never answer. Folded
// into pv_panel_task_faulted() alongside panel.c's completion-timeout fault.
static atomic_bool s_unresponsive;

// True until the next PRESENT blit lights the backlight. Written from rx_task
// and the TinyUSB task (usb_event_cb), consumed in panel_task -- atomic.
static atomic_bool s_backlight_gate;

// Sync-RPC plumbing: one {done, result} pair per calling task -- rx_task
// (configure/fence) and the emote render task (face blits). Each caller issues
// one RPC at a time, so a pair is never shared; the semaphore hand-off orders
// the result write before the caller's read.
static SemaphoreHandle_t s_rx_done;
static esp_err_t         s_rx_result;
static SemaphoreHandle_t s_face_done;
static esp_err_t         s_face_result;

// fps + heap telemetry, counted at PRESENT blits.
static int64_t  s_fps_t0;
static uint32_t s_frames;

static bool s_wdt_added;

static void latch_unresponsive(const char *what) {
    if (!atomic_exchange(&s_unresponsive, true)) {
        ESP_LOGE(TAG, "panel task unresponsive (%s); fault latched, rx_task will recover",
                 what);
    }
}

bool pv_panel_task_faulted(void) {
    return atomic_load(&s_unresponsive) || pv_panel_faulted();
}

void pv_panel_task_gate_backlight(void) { atomic_store(&s_backlight_gate, true); }
bool pv_panel_task_backlight_gated(void) { return atomic_load(&s_backlight_gate); }

size_t pv_panel_task_buf_cap(void) { return s_cap; }

uint8_t *pv_panel_task_acquire_buf(void) {
    if (pv_panel_task_faulted()) return NULL;  // fail fast; rx_task is headed for reboot
    uint8_t *buf = NULL;
    if (xQueueReceive(s_free, &buf, pdMS_TO_TICKS(ACQUIRE_TIMEOUT_MS)) != pdTRUE) {
        latch_unresponsive("no recycled blit buffer");
        return NULL;
    }
    return buf;
}

void pv_panel_task_release_buf(uint8_t *buf) {
    if (!buf) return;
    // Depth == pool size, so this can only fail on an ownership bug.
    if (xQueueSend(s_free, &buf, 0) != pdTRUE) {
        ESP_LOGE(TAG, "free-buffer queue full on release (ownership bug)");
    }
}

bool pv_panel_task_submit_blit(uint8_t *buf, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h, bool present,
                               uint32_t pix_len) {
    panel_req_t r = { .op = OP_BLIT };
    r.u.blit.buf = buf;
    r.u.blit.x = x; r.u.blit.y = y; r.u.blit.w = w; r.u.blit.h = h;
    r.u.blit.present = present;
    r.u.blit.pix_len = pix_len;
    if (!pv_panel_task_faulted() &&
        xQueueSend(s_req, &r, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) == pdTRUE) {
        return true;
    }
    if (!pv_panel_task_faulted()) latch_unresponsive("blit queue full");
    pv_panel_task_release_buf(buf);  // ownership contract: never hand it back
    return false;
}

static esp_err_t sync_rpc(const panel_req_t *r, SemaphoreHandle_t done,
                          const esp_err_t *result, uint32_t timeout_ms,
                          const char *what) {
    if (pv_panel_task_faulted()) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_req, r, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) != pdTRUE) {
        latch_unresponsive(what);
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        // A late reply leaves a stale token on `done` -- harmless: the latch
        // fails every future RPC fast, so nothing waits here again before the
        // recovery reboot.
        latch_unresponsive(what);
        return ESP_ERR_TIMEOUT;
    }
    return *result;
}

esp_err_t pv_panel_task_configure(const pv_config_t *cfg) {
    panel_req_t r = { .op = OP_CONFIGURE };
    r.u.configure.cfg = *cfg;
    return sync_rpc(&r, s_rx_done, &s_rx_result, RPC_TIMEOUT_MS, "configure");
}

esp_err_t pv_panel_task_fence(void) {
    panel_req_t r = { .op = OP_FENCE };
    return sync_rpc(&r, s_rx_done, &s_rx_result, RPC_TIMEOUT_MS, "fence");
}

esp_err_t pv_panel_task_face_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                  const void *pixels) {
    panel_req_t r = { .op = OP_FACE_BLIT };
    r.u.face.pixels = pixels;
    r.u.face.x = x; r.u.face.y = y; r.u.face.w = w; r.u.face.h = h;
    return sync_rpc(&r, s_face_done, &s_face_result, FACE_TIMEOUT_MS, "face blit");
}

// drop_on_full: progress draws are decorative -- losing one under load is fine,
// but a clear that can't queue means the task is stuck, which must latch.
static void async_send(const panel_req_t *r, bool drop_on_full, const char *what) {
    if (pv_panel_task_faulted()) return;
    TickType_t wait = drop_on_full ? 0 : pdMS_TO_TICKS(SEND_TIMEOUT_MS);
    if (xQueueSend(s_req, r, wait) != pdTRUE && !drop_on_full) {
        latch_unresponsive(what);
    }
}

void pv_panel_task_clear(void) {
    panel_req_t r = { .op = OP_CLEAR };
    async_send(&r, false, "clear");
}

void pv_panel_task_progress_begin(bool is_recovery) {
    panel_req_t r = { .op = OP_PROGRESS_BEGIN };
    r.u.progress_begin.is_recovery = is_recovery;
    async_send(&r, true, "progress");
}

void pv_panel_task_progress_update(uint8_t pct) {
    panel_req_t r = { .op = OP_PROGRESS_UPDATE };
    r.u.progress_update.pct = pct;
    async_send(&r, true, "progress");
}

// ---------------------------------------------------------------------------
// A frame's last rect (PRESENT) was queued: light the backlight on the first
// one after (re)connect, and keep a rolling fps figure.
// ---------------------------------------------------------------------------
static void on_frame_presented(void) {
    if (atomic_exchange(&s_backlight_gate, false)) {
        pv_panel_set_backlight(true);
    }
    s_frames++;
    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - s_fps_t0;
    if (elapsed >= 2000000) {
        // Integer fps*10 to avoid pulling float formatting into the log path.
        uint32_t fps10 = (uint32_t)((uint64_t)s_frames * 10000000ULL / (uint64_t)elapsed);
        // Heap figures ride along so a slow leak -- the classic "runs for hours
        // then wedges" cause -- shows up in the monitor well before it bites.
        ESP_LOGI(TAG, "%u.%u fps | q=%u psram=%u/min%u int=%u/min%u",  // min = lifetime low-water
                 (unsigned)(fps10 / 10), (unsigned)(fps10 % 10),
                 (unsigned)uxQueueMessagesWaiting(s_req),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        s_frames = 0;
        s_fps_t0 = now;
    }
}

// Grow every pool buffer to hold a full-frame BLIT for the new geometry. Runs
// in panel_task with rx_task parked in the configure RPC, so the whole pool is
// back in the free queue (the request queue is FIFO, so every earlier blit has
// been processed). The new set is allocated before the old is freed, so a
// failure leaves a working pool.
static esp_err_t pool_grow(size_t needed) {
    if (needed <= s_cap) return ESP_OK;
    uint8_t *old[NUM_BLIT_BUFS];
    for (int i = 0; i < NUM_BLIT_BUFS; i++) {
        if (xQueueReceive(s_free, &old[i], 0) != pdTRUE) {
            // A buffer is unaccounted for: refuse to grow rather than corrupt
            // the pool. The host sees the CONFIG fail and closes.
            for (int j = 0; j < i; j++) pv_panel_task_release_buf(old[j]);
            ESP_LOGE(TAG, "pool grow: buffer(s) missing from the free queue");
            return ESP_ERR_INVALID_STATE;
        }
    }
    uint8_t *grown[NUM_BLIT_BUFS] = {0};
    bool ok = true;
    for (int i = 0; i < NUM_BLIT_BUFS && ok; i++) {
        grown[i] = heap_caps_aligned_alloc(PV_DMA_ALIGN,
                                           PV_DMA_SIZE(needed + PV_BLIT_DST_OFFSET),
                                           MALLOC_CAP_SPIRAM);
        if (!grown[i]) ok = false;
    }
    // Free the set we are not keeping and put the other back in the pool.
    for (int i = 0; i < NUM_BLIT_BUFS; i++) {
        heap_caps_free(ok ? old[i] : grown[i]);  // NULL-safe
        pv_panel_task_release_buf(ok ? grown[i] : old[i]);
    }
    if (!ok) return ESP_ERR_NO_MEM;
    s_cap = needed;
    return ESP_OK;
}

static void panel_task(void *arg) {
    (void)arg;
    // Subscribed to the Task WDT: a wedge inside any esp_lcd call below stops
    // the per-iteration feed and panics with a backtrace naming the stuck call.
    // Every op is far under the 5s budget (configure ~600ms is the longest), and
    // the idle receive timeout keeps the feed running when quiet.
    if (esp_task_wdt_add(NULL) == ESP_OK) s_wdt_added = true;
    ESP_LOGI(TAG, "panel task up (pool %dx%u+%u bytes)%s", NUM_BLIT_BUFS,
             (unsigned)s_cap, (unsigned)PV_BLIT_DST_OFFSET,
             s_wdt_added ? "; subscribed to task WDT" : "");
    for (;;) {
        if (s_wdt_added) esp_task_wdt_reset();
        panel_req_t r;
        if (xQueueReceive(s_req, &r, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
        switch (r.op) {
            case OP_BLIT: {
                const uint8_t *pix = r.u.blit.buf + PV_BLIT_DST_OFFSET + sizeof(pv_blit_t);
                esp_err_t err = pv_panel_blit(r.u.blit.x, r.u.blit.y,
                                              r.u.blit.w, r.u.blit.h,
                                              pix, r.u.blit.pix_len);
                if (err == ESP_OK) {
                    if (r.u.blit.present) on_frame_presented();
                    // Reap before recycling the buffer. A timeout latches the
                    // panel fault (inside pv_panel_wait_one); rx_task reboots.
                    pv_panel_wait_one();
                }
                pv_panel_task_release_buf(r.u.blit.buf);
                break;
            }
            case OP_FACE_BLIT: {
                esp_err_t err = pv_panel_blit(r.u.face.x, r.u.face.y,
                                              r.u.face.w, r.u.face.h,
                                              r.u.face.pixels,
                                              (size_t)r.u.face.w * r.u.face.h * 2);
                if (err == ESP_OK && !pv_panel_wait_one()) err = ESP_ERR_TIMEOUT;
                s_face_result = err;
                xSemaphoreGive(s_face_done);
                break;
            }
            case OP_CONFIGURE: {
                esp_err_t err = pv_panel_configure(&r.u.configure.cfg);
                if (err == ESP_OK) {
                    err = pool_grow(sizeof(pv_blit_t) +
                                    (size_t)r.u.configure.cfg.width *
                                            r.u.configure.cfg.height * 2);
                }
                s_rx_result = err;
                xSemaphoreGive(s_rx_done);
                break;
            }
            case OP_FENCE:
                // Pure barrier: reaching here means every prior request -- and
                // its pixel DMA, reaped per-blit above -- has finished.
                s_rx_result = ESP_OK;
                xSemaphoreGive(s_rx_done);
                break;
            case OP_CLEAR:
                pv_panel_clear();
                break;
            case OP_PROGRESS_BEGIN:
                pv_panel_progress_begin(r.u.progress_begin.is_recovery);
                break;
            case OP_PROGRESS_UPDATE:
                pv_panel_progress_update(r.u.progress_update.pct);
                break;
        }
    }
}

esp_err_t pv_panel_task_start(void) {
    s_req  = xQueueCreate(REQ_QUEUE_DEPTH, sizeof(panel_req_t));
    s_free = xQueueCreate(NUM_BLIT_BUFS, sizeof(uint8_t *));
    s_rx_done   = xSemaphoreCreateBinary();
    s_face_done = xSemaphoreCreateBinary();
    if (!s_req || !s_free || !s_rx_done || !s_face_done) return ESP_ERR_NO_MEM;
    s_cap = BLIT_CAP_INITIAL;
    for (int i = 0; i < NUM_BLIT_BUFS; i++) {
        uint8_t *buf = heap_caps_aligned_alloc(PV_DMA_ALIGN,
                                               PV_DMA_SIZE(s_cap + PV_BLIT_DST_OFFSET),
                                               MALLOC_CAP_SPIRAM);
        if (!buf) return ESP_ERR_NO_MEM;
        pv_panel_task_release_buf(buf);
    }
    atomic_init(&s_unresponsive, false);
    atomic_init(&s_backlight_gate, true);
    s_fps_t0 = esp_timer_get_time();
    // Priority between the face render task (4) and rx_task (6): blits drain
    // promptly but never starve the USB link. (Mostly moot -- the dual-core P4
    // runs both, and this task spends its life blocked on the DMA semaphore.)
    if (xTaskCreate(panel_task, "pv_panel", 4096, NULL, 5, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
