// CST816 capacitive touch poller (CST816S/CST816D share the register map).
//
// Mirrors the host engine's touch.rs: reads 6 bytes from register 0x01, derives
// down/move/up from the finger count, drops out-of-range bus-noise samples, and
// applies axis swap/flip on-device. Produces pv_touch_t records for the bulk IN
// endpoint.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "protocol.h"

// Bring up the I2C master bus + CST816 from the panel CONFIG (address, axis
// flags, geometry). touch_addr == 0 disables touch (returns ESP_OK, no bus).
// Safe to call again on a new CONFIG.
esp_err_t pv_touch_configure(const pv_config_t *cfg);

// Poll once. Returns true and fills `out` when there is an event to send to the
// host; false when nothing changed (or touch is disabled).
bool pv_touch_poll(pv_touch_t *out);

// True while a finger is currently down (between a reported down and its up). The
// touch task uses this to poll fast for move/release tracking and otherwise sleep
// until the INT line (or a slow fallback tick) wakes it.
bool pv_touch_active(void);
