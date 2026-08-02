// USB descriptors + MS OS 2.0 (WCID) handling for the pico-view vendor device.
//
// Closely modeled on TinyUSB's examples/device/webusb_serial, reduced to a
// single vendor interface (no CDC, no WebUSB landing page). esp_tinyusb pulls
// the device / configuration / string descriptors from tinyusb_config_t; the
// BOS descriptor and the MS OS 2.0 control request are application callbacks we
// implement here.
#include <stdio.h>

#include "usb_descriptors.h"
#include "protocol.h"
#include "esp_mac.h"
#include "esp_log.h"

#define PV_USB_VID 0x303A  // Espressif shared VID
#define PV_USB_PID 0x839A  // PID allocated to pico-view via espressif/usb-pids
#define PV_USB_BCD 0x0210  // USB 2.1 so the host fetches the BOS descriptor

// ---------------------------------------------------------------------------
// Device descriptor
// ---------------------------------------------------------------------------
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = PV_USB_BCD,
    // Defer class to the interface so the vendor interface gets WCID-bound.
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = PV_USB_VID,
    .idProduct          = PV_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

// Device qualifier: required for a HS-capable device; mirrors the device
// descriptor's key fields for the "other speed".
static const tusb_desc_device_qualifier_t desc_qualifier = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = PV_USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00,
};

// ---------------------------------------------------------------------------
// Configuration descriptor (one vendor interface, 2 bulk endpoints)
// ---------------------------------------------------------------------------
enum { ITF_NUM_VENDOR = 0, ITF_NUM_TOTAL };

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

#define PV_CONFIG_DESC(ep_size)                                                       \
    /* config: number, interface count, string index, total length, attr, power */   \
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),          \
    /* vendor interface: number, string index, EP out, EP in, EP size */             \
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 4, PV_EP_OUT, PV_EP_IN, (ep_size))

static const uint8_t desc_fs_config[] = { PV_CONFIG_DESC(PV_EP_SIZE_FS) };
static const uint8_t desc_hs_config[] = { PV_CONFIG_DESC(PV_EP_SIZE_HS) };

// ---------------------------------------------------------------------------
// String descriptors
// ---------------------------------------------------------------------------
enum { PV_STR_SERIAL = 3 };  // index of the serial string below

// USB serial number: 12 uppercase hex digits of the ESP32-P4's factory eFuse
// MAC (a per-chip unique id), so the host can select a specific unit by serial
// at enumeration. Filled by pv_usb_serial_init() before the descriptors reach
// TinyUSB; static storage keeps the pointer valid for the device's lifetime.
// The all-zero default only shows if the eFuse read ever fails.
static char s_serial[13] = "000000000000";

static const char *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },  // 0: supported language (English 0x0409)
    "pico-view",                   // 1: manufacturer
    "pico-view ESP32-P4 display",  // 2: product
    s_serial,                      // 3: serial (eFuse MAC hex; see below)
    "pico-view vendor",            // 4: vendor interface
};

// ---------------------------------------------------------------------------
// BOS + MS OS 2.0 (WinUSB / WCID)
// ---------------------------------------------------------------------------
// Vendor request code the host issues to fetch the MS OS 2.0 descriptor set.
#define PV_MS_OS_20_VENDOR_CODE 0x01

// Length of the MS OS 2.0 descriptor set: set header (10) + configuration
// subset (8) + function subset (8) + compatible-ID (20) + registry property
// (132) = 178 (0xB2).
#define PV_MS_OS_20_DESC_LEN 0xB2

#define PV_BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t desc_bos[] = {
    // total length, number of device capabilities
    TUD_BOS_DESCRIPTOR(PV_BOS_TOTAL_LEN, 1),
    // Microsoft OS 2.0 descriptor set: total length, vendor request code
    TUD_BOS_MS_OS_20_DESCRIPTOR(PV_MS_OS_20_DESC_LEN, PV_MS_OS_20_VENDOR_CODE),
};

static const uint8_t desc_ms_os_20[] = {
    // Set header: wLength, wDescriptorType, dwWindowsVersion, wTotalLength
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(PV_MS_OS_20_DESC_LEN),

    // Configuration subset header: wLength, wDescriptorType, bConfigurationValue,
    // bReserved, wTotalLength (of this subset)
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0, U16_TO_U8S_LE(PV_MS_OS_20_DESC_LEN - 0x0A),

    // Function subset header: wLength, wDescriptorType, bFirstInterface,
    // bReserved, wSubsetLength
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_NUM_VENDOR, 0, U16_TO_U8S_LE(PV_MS_OS_20_DESC_LEN - 0x0A - 0x08),

    // Compatible ID feature: wLength, wDescriptorType, compatibleID[8], subCompatibleID[8]
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),  // sic: tinyusb enum spelling
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // Registry property feature: wLength, wDescriptorType, wPropertyDataType,
    // wPropertyNameLength, propertyName, wPropertyDataLength, propertyData
    U16_TO_U8S_LE(PV_MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),  // REG_MULTI_SZ, name length
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
    'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
    'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050),  // wPropertyDataLength (80 bytes)
    // {975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}\0\0  as UTF-16LE
    '{', 0x00, '9', 0x00, '7', 0x00, '5', 0x00, 'F', 0x00, '4', 0x00,
    '4', 0x00, 'D', 0x00, '9', 0x00, '-', 0x00, '0', 0x00, 'D', 0x00,
    '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00,
    'D', 0x00, '-', 0x00, '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00,
    '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
    '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00,
    'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};

_Static_assert(sizeof(desc_ms_os_20) == PV_MS_OS_20_DESC_LEN,
               "MS OS 2.0 descriptor length mismatch");

// ---------------------------------------------------------------------------
// Accessors for main.c
// ---------------------------------------------------------------------------
const tusb_desc_device_t *pv_device_descriptor(void) { return &desc_device; }
const tusb_desc_device_qualifier_t *pv_qualifier_descriptor(void) { return &desc_qualifier; }
const uint8_t *pv_fs_config_descriptor(void) { return desc_fs_config; }
const uint8_t *pv_hs_config_descriptor(void) { return desc_hs_config; }

const char **pv_string_descriptors(int *count) {
    *count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]);
    return (const char **)string_desc_arr;
}

const char *pv_serial_string(void) { return s_serial; }

void pv_usb_serial_init(void) {
    uint8_t mac[6] = {0};
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        // Leave the all-zero default; the unit still enumerates, just without a
        // unique serial. Logged so a bad eFuse read is diagnosable.
        ESP_LOGW("pv_usb", "eFuse MAC read failed (%s); serial stays %s",
                 esp_err_to_name(err), s_serial);
        return;
    }
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---------------------------------------------------------------------------
// TinyUSB application callbacks
// ---------------------------------------------------------------------------
// BOS descriptor (esp_tinyusb has no config-struct slot for it).
const uint8_t *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

// Vendor control transfers: respond to the MS OS 2.0 descriptor-set request so
// Windows binds WinUSB. Everything else is unsupported (stall).
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;  // nothing to do for data/ack stages
    }

    switch (request->bmRequestType_bit.type) {
        case TUSB_REQ_TYPE_VENDOR:
            if (request->bRequest == PV_MS_OS_20_VENDOR_CODE &&
                request->wIndex == 7) {
                // wIndex 7 == MS_OS_20_DESCRIPTOR_INDEX
                uint16_t total = tu_le16toh(*(uint16_t *)(desc_ms_os_20 + 8));
                return tud_control_xfer(rhport, request, (void *)desc_ms_os_20, total);
            }
            return false;
        default:
            return false;
    }
}
