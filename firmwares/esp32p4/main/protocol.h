// pico-view USB wire protocol v2 (host <-> ESP32-P4).
//
// One length-prefixed framing in both directions, reassembled across 512-byte
// HS bulk packets. All multi-byte header/payload scalar fields are LITTLE-endian
// EXCEPT BLIT pixel data, which is RGB565 BIG-endian (the order ST7789/ST77916
// latch at RAMWR -- produced verbatim by the host's rgba_rect_to_rgb565_be).
//
// v2 splits the traffic in two:
//   - BLIT frames stay raw (a full 360x360 frame is ~253 KB and needs no schema
//     evolution).
//   - Everything else is ONE protobuf-encoded message per CTRL frame:
//     picoview.wire.HostToDevice on the OUT endpoint, DeviceToHost on the IN
//     endpoint. The schema is ../../../proto/pv_wire.proto (single source of
//     truth, shared with the Rust host via prost and Dart via protoc); the
//     nanopb bindings in gen/pv_wire.pb.h are bounded by pv_wire.options and
//     regenerated with ../gen_proto.sh.
//
// The Rust host (crates/pico-view/src/esp32p4.rs) mirrors the framing; keep the
// two in sync.
#pragma once
#include <stdint.h>

// "PVUS" as a LE u32 ('P'=0x50 is the first byte on the wire).
#define PV_MAGIC 0x53555650u

// 12-byte fixed header preceding every message in either direction.
//   magic:u32  type:u16  flags:u16  payload_len:u32
typedef struct __attribute__((packed)) {
    uint32_t magic;        // PV_MAGIC
    uint16_t type;         // pv_msg_type
    uint16_t flags;        // pv_flags
    uint32_t payload_len;  // bytes of payload following this header
} pv_header_t;

#define PV_HEADER_LEN 12

// Frame types. 1..31 are reserved: they were the v1 per-message ids, and CTRL
// starting at 32 makes a v2 host talking to v1 firmware (or vice versa) fail
// cleanly at the HELLO handshake instead of mis-parsing.
// Type 33 is retired (it was a reserved raw-PCM audio frame) and must not be
// reused.
enum pv_msg_type {
    PV_MSG_BLIT = 3,   // raw: pv_blit_t + w*h*2 bytes RGB565-BE (host -> device)
    PV_MSG_CTRL = 32,  // one encoded picoview.wire.HostToDevice / DeviceToHost
};

// Header flags.
enum pv_flags {
    PV_FLAG_PRESENT = 1 << 0,  // BLIT: last rect of the current frame
};

// Protocol version, bumped on any incompatible wire change. v2 = protobuf CTRL.
#define PV_PROTO_VERSION 2

// --- BLIT (raw, deliberately not protobuf) -----------------------------------

// One windowed update. Followed immediately by w*h*2 bytes of RGB565-BE pixels.
typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} pv_blit_t;

// --- Internal panel/touch configuration --------------------------------------
// Decoded from the CTRL Config message in main.c; panel.c / touch.c consume
// this struct so their driver code stays independent of the wire encoding.

enum pv_panel_model {
    PV_PANEL_ST77916 = 0,
    PV_PANEL_ST7789  = 1,
};

typedef struct __attribute__((packed)) {
    uint16_t model;       // pv_panel_model
    uint16_t width;       // visible width in the wired orientation
    uint16_t height;      // visible height
    uint16_t x_offset;    // column inset into controller RAM
    uint16_t y_offset;    // row inset into controller RAM
    uint16_t rotation;    // 0 / 90 / 180 / 270 -> MADCTL scan order (degrees)
    uint8_t  invert;      // 1 => emit display-inversion-on during init
    uint8_t  touch_addr;  // 7-bit CST816 I2C address; 0 => touch disabled
    uint8_t  touch_flags; // pv_touch_flags (axis swap/flip, applied on-device)
    uint8_t  reserved;
} pv_config_t;

enum pv_touch_flags {
    PV_TOUCH_SWAP_XY = 1 << 0,
    PV_TOUCH_FLIP_X  = 1 << 1,
    PV_TOUCH_FLIP_Y  = 1 << 2,
};

// One touch sample as produced by touch.c. Phase: 0=down, 1=move, 2=up (main.c
// maps this to the wire TouchPhase enum, which is offset by one for the proto3
// UNSPECIFIED zero value). On up the coordinates are not meaningful.
enum pv_touch_phase {
    PV_TOUCH_DOWN = 0,
    PV_TOUCH_MOVE = 1,
    PV_TOUCH_UP   = 2,
};

typedef struct __attribute__((packed)) {
    uint8_t  phase;  // pv_touch_phase
    uint8_t  pad;
    uint16_t x;
    uint16_t y;
} pv_touch_t;

// --- Device attestation (genuine-hardware check) ------------------------------
// Lets the host recognize a genuine vendor unit. It is a LABEL, not a gate: the
// host drives the panel identically whatever comes back, so an unprovisioned or
// self-built board is a first-class citizen that simply reports unverified. See
// AUTH_CONTRACT.md for what a fork must keep to stay recognizable.
//
// The host sends a fresh random nonce (AuthChallenge) and the device answers
// (AuthResponse) with its factory-provisioned certificate plus an ECDSA
// P-256/SHA-256 signature over the challenge. The identity key is burned into a
// read-protected eFuse block (purpose ECDSA_KEY_P256) and only ever used by the
// on-chip ECDSA peripheral, so nothing secret lives in flash and the key
// survives a full flash erase (the cert in `devid` is re-issuable from it). The
// host verifies the cert against the vendor CA public key, then the challenge
// signature against the cert's device public key. Both keys are P-256.
//
// Domain separation: every signature covers a context literal, so one produced
// here can never be replayed as another kind of statement.
//   cert:      CA signs     SHA-256(PV_CERT_CONTEXT || cert bytes [0, 93))
//   challenge: device signs SHA-256(PV_AUTH_CONTEXT || nonce || device_pubkey)
// Binding the device public key into the challenge digest ties the response to
// the identity being claimed; the nonce defeats record-and-replay. Signatures
// are 64-byte r||s pairs (32+32, big-endian).

#define PV_CERT_CONTEXT "PVUS-DEVCERT-V2"  // 15 bytes, no NUL
#define PV_AUTH_CONTEXT "PVUS-ATTEST-V2"   // 14 bytes, no NUL

#define PV_AUTH_NONCE_LEN 32
#define PV_DEVICE_ID_LEN  16
#define PV_CA_SIG_LEN     64  // vendor CA (ECDSA P-256) signature: r||s, 32+32 big-endian

#define PV_EC_PUBKEY_LEN  65  // SEC1 uncompressed P-256 point: 0x04 || X[32] || Y[32]
#define PV_EC_SIG_LEN     64  // ECDSA P-256 signature: r||s, 32+32 big-endian

// Compact device certificate, written to the `devid` partition at provisioning
// (see provisioning/make_cert.py) and carried OPAQUELY in
// AuthResponse.certificate -- its layout is versioned independently of the
// protobuf schema. `cert_version` is the evolution hook: any change bumps it and
// appends, never reorders.
typedef struct __attribute__((packed)) {
    uint8_t  cert_version;                       // 2
    uint8_t  reserved[3];
    char     device_id[PV_DEVICE_ID_LEN];        // ASCII, NUL-padded, e.g. "PV4-000123"
    uint32_t issued_at;                          // unix seconds (informational)
    uint32_t expires_at;                         // unix seconds; 0 = never
    uint8_t  device_pubkey[PV_EC_PUBKEY_LEN];    // SEC1 uncompressed P-256 point
    uint8_t  ca_signature[PV_CA_SIG_LEN];        // ECDSA P-256 over the 93 bytes above
} pv_device_cert_t;  // 157 bytes

// Local attestation result (auth.c -> main.c; main.c maps it to the wire
// AuthStatus enum, which is offset by one for the proto3 UNSPECIFIED value).
enum pv_auth_status {
    PV_AUTH_OK            = 0,  // cert + signature valid and present
    PV_AUTH_UNPROVISIONED = 1,  // no eFuse identity key / no valid cert on this unit
    PV_AUTH_ERR_INTERNAL  = 2,  // signing failed (should not happen in the field)
    PV_AUTH_ERR_BAD_REQ   = 3,  // malformed challenge
};

typedef struct {
    uint8_t  status;                            // pv_auth_status; buffers zeroed unless OK
    uint8_t  reserved[3];
    uint8_t  cert[sizeof(pv_device_cert_t)];    // opaque cert (157 bytes)
    uint8_t  signature[PV_EC_SIG_LEN];          // over the challenge (see PV_AUTH_CONTEXT)
} pv_auth_response_t;

// --- OTA firmware update ------------------------------------------------------
// The host streams an app image into the passive OTA slot: one OtaBegin, N
// OtaData chunks (each carrying a strictly increasing seq), then OtaEnd. The
// firmware verifies the whole-image SHA-256 before switching the boot partition
// and rebooting. There is no image signing (open firmware -- see ota.h). Chunks
// are bounded to 8192 image bytes by pv_wire.options (OtaData.data max_size).

// Failure codes carried in OtaStatus.err (0 == none).
enum pv_ota_err {
    PV_OTA_ERR_NONE             = 0,
    PV_OTA_ERR_BUSY             = 1,  // an OTA session is already active
    PV_OTA_ERR_STATE            = 2,  // BEGIN/DATA/END arrived out of order
    PV_OTA_ERR_NO_PARTITION     = 3,  // no passive OTA slot found
    PV_OTA_ERR_BEGIN            = 4,  // esp_ota_begin failed
    PV_OTA_ERR_WRITE            = 5,  // esp_ota_write failed
    PV_OTA_ERR_SEQ              = 6,  // out-of-order / duplicate chunk
    PV_OTA_ERR_OVERFLOW         = 7,  // more bytes than image_size announced
    PV_OTA_ERR_SIZE             = 8,  // received != image_size at END
    PV_OTA_ERR_HASH             = 9,  // SHA-256 mismatch
    PV_OTA_ERR_VERIFY           = 10, // esp_ota_end structural image check failed
    PV_OTA_ERR_SET_BOOT         = 11, // esp_ota_set_boot_partition failed
    PV_OTA_ERR_ROLLBACK_PENDING = 12, // current app not yet marked valid; retry after handshake
};
