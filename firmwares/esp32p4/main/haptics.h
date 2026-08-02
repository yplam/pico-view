// DRV2605L haptic driver (LRA), on the shared expansion I2C bus.
//
// Plays effects from the DRV2605L's on-chip ROM waveform library via the
// waveform sequencer + internal GO trigger. Mirrors the wire Haptics message:
// the host sends an effect id, the device fires it. Best-effort, no ack.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Bring up the DRV2605L on the shared bus (address/actuator from Kconfig). Call
// once the panel CONFIG has arrived, alongside pv_touch_configure and under the
// same lock (the device add must not race a touch poll). Idempotent; a no-op
// returning ESP_OK when haptics is disabled at build time.
esp_err_t pv_haptics_configure(void);

// True when this build includes the DRV2605L driver (advertised in HELLO caps).
//
// BUILD-time only, NOT "the chip answered": HELLO is served before any CONFIG,
// so pv_haptics_configure() has not run yet and the device cannot know whether a
// DRV2605L is really on the bus. A board with the driver built in but no chip
// fitted therefore advertises the capability and then silently no-ops every
// Haptics message. Hosts must treat this cap as "may work", not "will work".
bool pv_haptics_available(void);

// Fire one ROM waveform-library effect (id 1..123). `library` 1..7 selects the
// ROM library; 0 keeps the configured default. Best-effort: logs and returns on
// an I2C error, and is a no-op until pv_haptics_configure has succeeded.
void pv_haptics_play(uint8_t effect, uint8_t library);

// Stop a running effect (clears the DRV2605L GO bit).
void pv_haptics_stop(void);
