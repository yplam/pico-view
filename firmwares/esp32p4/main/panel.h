// QSPI panel backend: ST77916 360x360 over QSPI (4 data lines, no D/C line).
//
// Mirrors the host engine's driver.rs + lcd.rs: the panel holds the image in its
// own GRAM, so there is no framebuffer here -- each BLIT just windows
// (CASET/RASET) and streams RGB565-BE pixels (RAMWR) straight into GRAM. The
// power-on init sequence is ported verbatim from driver.rs. The host CONFIG model
// field is honoured only for ST77916; other models log a warning and are driven
// with the ST77916 sequence.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "protocol.h"

// Alignment for any buffer handed to the SPI color DMA out of PSRAM. On the
// ESP32-P4 both L1 and L2 cache lines are 64 bytes; a cache-line-aligned start
// keeps the driver's writeback sync on the fast path instead of the unaligned
// edge cases (a prime suspect for lost color completions).
#define PV_DMA_ALIGN 64

// Round a DMA buffer's SIZE up to whole cache lines. heap_caps_aligned_alloc
// aligns the START; this pads the TAIL, because a pixel run is w*h*2 bytes and
// almost never a multiple of 64 (a 360-wide, 22-row progress band is 15840 =
// 247.5 lines). Without the padding the final line the driver writes back is
// shared with whatever the allocator placed next -- the same unaligned-PSRAM-DMA
// hazard as a misaligned start, just at the other end of the buffer.
#define PV_DMA_SIZE(n) (((size_t)(n) + (PV_DMA_ALIGN - 1)) & ~((size_t)PV_DMA_ALIGN - 1))

// (Re)initialise the SPI bus + panel for the geometry/model in `cfg` and run the
// controller power-on sequence (reset, init regs, clear GRAM, display on). Safe
// to call again on a new CONFIG; tears down any previous panel IO first. The
// backlight is left OFF -- the caller turns it on once real content is presented
// (see pv_panel_set_backlight).
esp_err_t pv_panel_configure(const pv_config_t *cfg);

// Queue one windowed update into GRAM. `pixels` is RGB565 big-endian, row-major,
// w*h*2 bytes. The pixel DMA is started ASYNCHRONOUSLY and this returns as soon
// as it is queued, so the next BLIT can be reassembled off USB while this one
// drains over SPI. The caller MUST NOT reuse the pixel buffer until a matching
// pv_panel_wait_one() has reaped this transfer's completion. No-op error if the
// panel is not configured yet.
esp_err_t pv_panel_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t *pixels, size_t len);

// Wait for one queued color transfer to finish draining over SPI, freeing the
// caller to recycle the pixel buffer that fed it. Exactly one wait per blit.
// Bounded: it gives up after PV_PANEL_WAIT_TIMEOUT_MS and returns false, so a
// lost SPI/DMA completion can never wedge the caller. A false return means the
// pipeline stalled: the fault is latched (pv_panel_faulted) and further blits
// are refused until recovery.
bool pv_panel_wait_one(void);

// True once any pv_panel_wait_one() has timed out: the QSPI pipeline is stuck on
// a phantom in-flight transfer. While latched, pv_panel_blit/clear/progress all
// refuse work (ESP_ERR_INVALID_STATE) -- re-entering esp_lcd would block forever
// behind the stuck transaction (tx_param and io_del drain the color queue with
// portMAX_DELAY). Only a reboot really recovers (rx_task polls this and
// restarts); the flag is cleared by a successful pv_panel_configure after it.
bool pv_panel_faulted(void);

// Gate the panel backlight on/off (no-op if no backlight GPIO is wired, or
// before the first pv_panel_configure has set the LEDC channel up). Used to
// light the panel on the first presented frame and dark it on USB
// suspend/unmount. "On" restores the current brightness level.
void pv_panel_set_backlight(bool on);

// Set the backlight brightness, 0 (dark) .. 255 (full), via LEDC PWM duty. The
// level is remembered and re-applied whenever the backlight is gated on; setting
// it while the panel is lit takes effect immediately. No-op if no backlight is
// wired. Driven by the host's SetParam(brightness) CTRL message.
void pv_panel_set_brightness(uint8_t brightness);

// Current backlight level (0..255), as last set by pv_panel_set_brightness. The
// idle face saves this to dim the panel while it plays and restore the host's
// level afterward. Returns 255 when no backlight is wired.
uint8_t pv_panel_get_brightness(void);

// Wipe the panel GRAM to black (synchronous). No-op before the first
// pv_panel_configure. Used to leave the panel clean after the USB host stays
// disconnected -- pair with pv_panel_set_backlight(false).
void pv_panel_clear(void);

// Current panel geometry (0 before the first pv_panel_configure).
uint16_t pv_panel_width(void);
uint16_t pv_panel_height(void);

// OTA "updating" screen. progress_begin() paints the background and an empty
// progress-bar frame; progress_update(pct) fills the bar to pct (0..100). Both
// are synchronous and no-ops before the panel is configured. Used while the
// normal render pipeline is paused during a firmware update. `is_recovery` tints
// the screen so the operator can tell a recovery-mode update apart.
void pv_panel_progress_begin(bool is_recovery);
void pv_panel_progress_update(uint8_t pct);
