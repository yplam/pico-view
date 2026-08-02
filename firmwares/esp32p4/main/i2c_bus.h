// Shared expansion I2C master bus.
//
// The CST816 touch controller (touch.c) and the DRV2605L haptic driver
// (haptics.c) sit on the same physical I2C bus. Both take their bus handle from
// here so the master bus is created exactly once, from the PV_TOUCH_I2C_* wiring
// (SDA/SCL/port). Device-level speed is set per device when it is added.
#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"

// Return the shared master bus, creating it on first call. Thread-unsafe with
// respect to concurrent first calls, so bring both consumers up from the same
// task (both configure at CONFIG time on rx_task). Returns ESP_ERR_NOT_SUPPORTED
// when neither touch nor haptics is built in (no wiring to create the bus from).
esp_err_t pv_i2c_bus_get(i2c_master_bus_handle_t *out);
