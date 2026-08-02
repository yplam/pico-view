// Panel task: the single owner of the esp_lcd/QSPI pipeline.
//
// Every esp_lcd entry point can block WITHOUT BOUND inside the SPI driver once
// the QSPI pipeline wedges: tx_param and io_del both drain queued color
// transactions with portMAX_DELAY. Rather than let rx_task -- the USB link --
// take that risk, one dedicated task makes every esp_lcd call and every other
// task talks to it exclusively through bounded queue operations:
//
//   rx_task:      acquire_buf -> reassemble one BLIT into it -> submit_blit;
//                 configure / fence (sync RPC); clear / progress (async)
//   face render:  face_blit (sync RPC per flushed stripe)
//   panel task:   the only caller of panel.c's pv_panel_* -- except the LEDC
//                 backlight/brightness helpers, which touch no SPI state and
//                 stay callable from anywhere.
//
// Ordering: the request queue is FIFO and the task is serial, so "configure runs
// after every previously submitted blit" and "the idle face's first stripe lands
// after the last host blit" hold by construction -- no drain calls, no
// panel-ownership handoff protocol.
//
// Failure: a panel task wedged inside esp_lcd makes the bounded waits here time
// out and LATCH a fault (pv_panel_task_faulted, which also folds in panel.c's
// completion-timeout fault). rx_task polls that and reboots; the panel task's own
// Task-WDT subscription is the backstop, panicking with a backtrace that names
// the stuck driver call.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "panel.h"
#include "protocol.h"

// A BLIT reassembles at this offset into an acquired pool buffer: the 8-byte
// pv_blit_t header fills the tail of the first cache line so the pixel bytes --
// the part handed to the SPI DMA out of PSRAM -- start cache-line aligned.
#define PV_BLIT_DST_OFFSET (PV_DMA_ALIGN - sizeof(pv_blit_t))

// Create the queues + blit buffer pool and start the task. Call once, before
// any other pv_panel_task_* call (in practice: before tinyusb_driver_install).
esp_err_t pv_panel_task_start(void);

// --- Blit buffer pool (producer: rx_task) ------------------------------------

// Take a free pool buffer to reassemble one BLIT into (at PV_BLIT_DST_OFFSET).
// Blocks (bounded) until the panel task recycles one -- in steady streaming
// that is the natural pacing to SPI drain speed. NULL means the wait timed out
// and the fault was latched: stop feeding and let rx_task recover.
uint8_t *pv_panel_task_acquire_buf(void);

// Return an acquired buffer WITHOUT submitting it (parser reset, validation
// failure). Never blocks.
void pv_panel_task_release_buf(uint8_t *buf);

// Payload capacity (pv_blit_t header + pixels) of every pool buffer. Grown by
// configure; only rx_task observes it, between its own sync configure calls.
size_t pv_panel_task_buf_cap(void);

// Queue one reassembled BLIT for the panel. Takes ownership of `buf` on every
// path -- it is recycled to the free pool after its pixels drain, or
// immediately on failure; the caller must not touch it again either way.
// `present` mirrors PV_FLAG_PRESENT (lights the gated backlight + fps count).
// False when the queue send timed out (fault latched).
bool pv_panel_task_submit_blit(uint8_t *buf, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h, bool present,
                               uint32_t pix_len);

// --- Synchronous RPCs ---------------------------------------------------------

// (Re)configure the panel -- FIFO-ordered behind every queued blit -- and grow
// the pool to a full frame of the new geometry. rx_task only.
// ESP_ERR_NO_MEM: pool growth failed; ESP_ERR_TIMEOUT: panel task unresponsive
// (fault latched); other errors: pv_panel_configure's result.
esp_err_t pv_panel_task_configure(const pv_config_t *cfg);

// Barrier: returns ESP_OK once every previously queued request has fully
// finished (used to quiesce pixel DMA before OTA flash writes). rx_task only.
esp_err_t pv_panel_task_fence(void);

// Blit one stripe of the idle face and wait for its pixels to drain, so the
// caller may recycle the stripe buffer on return. Emote render task only.
esp_err_t pv_panel_task_face_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                  const void *pixels);

// --- Async ops (fire-and-forget) ----------------------------------------------

void pv_panel_task_clear(void);                       // wipe GRAM (idle blank)
void pv_panel_task_progress_begin(bool is_recovery);  // OTA screen; drop-on-full
void pv_panel_task_progress_update(uint8_t pct);      // OTA bar;   drop-on-full

// --- Backlight gate -----------------------------------------------------------

// Arm "light the backlight on the next PRESENT blit". Set on configure, USB
// detach and idle blank so the panel never lights a stale or half-drawn frame.
void pv_panel_task_gate_backlight(void);
bool pv_panel_task_backlight_gated(void);

// True once the pipeline is dead: a color completion timed out inside the panel
// task (pv_panel_faulted) OR the panel task itself stopped answering bounded
// waits. rx_task polls this and reboots to recover.
bool pv_panel_task_faulted(void);
