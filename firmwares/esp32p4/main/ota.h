// Firmware OTA update + recovery for the pico-view ESP32-P4 backend.
//
// The product board exposes only the USB-HS OTG port (no USB-Serial-JTAG), so
// updates are streamed over the existing vendor-bulk link into the passive OTA
// slot and committed with the esp_ota APIs. This is OPEN firmware: there is no
// image signing and no Secure Boot, so integrity rests on the whole-image
// SHA-256 the host announces and pv_ota_finish verifies. App rollback + a
// boot-loop detector + a factory recovery app are the anti-brick safety nets.
// See ../README.md for the full design.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

// Call once at the very start of app_main, before USB/panel bring-up. Logs the
// running partition and runs the boot-loop detector: an RTC-retained counter is
// bumped every boot and only cleared by pv_ota_note_handshake(); if it crosses a
// threshold (the app keeps rebooting without ever reaching a host handshake),
// the boot partition is switched to the factory recovery app and the device
// reboots. Protects against a "valid but wedged" app on a board with no button.
void pv_ota_boot_check(void);

// Call when a host HELLO handshake completes. Cancels the pending-verify
// rollback for a freshly-flashed image (esp_ota_mark_app_valid) and resets the
// boot-loop counter -- proof the running app is healthy.
void pv_ota_note_handshake(void);

// True if this is the trimmed factory recovery BUILD (-DPV_RECOVERY=ON), which
// skips touch/render. Deliberately not "runs from the factory partition": a
// plain `idf.py flash` targets factory with a full app, which must behave fully.
bool pv_ota_is_recovery(void);

// OTA session, driven from the rx dispatcher with fields decoded from the CTRL
// OtaBegin/OtaData messages. Each returns a pv_ota_err (0 == ok).
//   begin:  validate the request, pick the passive slot, erase it. `sha256` is
//           the expected whole-image hash; `version` (NUL-terminated) is log-only.
//   write:  append one chunk; *pct_out gets 0..100.
//   finish: check size+SHA-256, run esp_ota_end (structural validation only --
//           no signature check) and set boot. On success the caller should
//           report DONE and reboot; on error the session is torn down and the
//           current app keeps running.
//   abort:  discard the in-progress image and free resources.
int16_t pv_ota_begin(uint32_t image_size, const uint8_t sha256[32], const char *version);
int16_t pv_ota_write(uint32_t seq, const uint8_t *data, uint32_t len, uint8_t *pct_out);
int16_t pv_ota_finish(void);
void    pv_ota_abort(void);
