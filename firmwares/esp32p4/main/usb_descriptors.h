// USB descriptor set for the pico-view vendor-bulk device.
//
// A single vendor-class (0xFF) interface with one bulk OUT + one bulk IN
// endpoint. MS OS 2.0 descriptors advertise the WINUSB compatible-ID so Windows
// auto-binds WinUSB.sys with no INF/Zadig; macOS and Linux (with a udev rule)
// claim the vendor interface through libusb directly.
#pragma once
#include <stdint.h>
#include "tusb.h"

// Endpoint addresses (bulk).
#define PV_EP_OUT 0x01
#define PV_EP_IN  0x81

// Bulk max packet sizes per USB speed.
#define PV_EP_SIZE_FS 64
#define PV_EP_SIZE_HS 512

// Accessors used by main.c to populate tinyusb_config_t.descriptor.
const tusb_desc_device_t *pv_device_descriptor(void);
const tusb_desc_device_qualifier_t *pv_qualifier_descriptor(void);
const uint8_t *pv_fs_config_descriptor(void);
const uint8_t *pv_hs_config_descriptor(void);
const char **pv_string_descriptors(int *count);

// Fill the USB serial string descriptor from the ESP32-P4 factory eFuse MAC
// (12 uppercase hex digits). MUST be called before the descriptors are handed
// to TinyUSB (tinyusb_driver_install), since the host reads the serial at
// enumeration. Idempotent.
void pv_usb_serial_init(void);

// The USB serial string (string descriptor 3): the eFuse-derived unique id,
// also reported to the host in the DeviceInfo reply so the app can identify a
// specific unit. Valid after pv_usb_serial_init(); the all-zero default before.
const char *pv_serial_string(void);
