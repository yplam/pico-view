// FFI surface for the `pico-view` Rust cdylib.
//
// Everything except frame delivery travels as protobuf messages; the schemas
// live in the pico-view repo under proto/ (pv_ffi.proto imports pv_wire.proto)
// and the generated Dart types ship in lib/src/gen/.

#include <stdint.h>

// Initialize the Dart DL API and store the SendPort native handle for the
// engine event push channel. Call once at startup with the pointer from
// `NativeApi.initializeApiDLData`. Returns 0 on success; -1 when the Dart DL
// API could not be initialized.
//
// Events arrive on the SendPort as Uint8List payloads, each one encoded
// `picoview.ffi.PvEvent` message. Link events are posted on transitions only.
int32_t pv_init(void *api_data, int64_t send_port);

// Handle one control-plane request: decode `req_len` bytes at `req` as an
// encoded `picoview.ffi.PvRequest`, execute it, and return the encoded
// `PvResponse` via `resp`/`resp_len`. The caller owns the returned buffer and
// MUST release it with pv_free.
//
// Returns 0 whenever a response was produced;
// -1 only when no response could be produced, in which case `resp`/`resp_len` are untouched.
//
// Every request answers synchronously, but "synchronously" means different
// things per variant:
//   - open_device blocks until the device is open and the panel initialized,
//     then answers `ack` (or ERROR_CODE_DEVICE / ERROR_CODE_TIMEOUT). A
//     LinkEvent(CONNECTED) is posted alongside the `ack`.
//   - close_device answers `ack` after teardown completes.
//   - get_device_info round-trips to the device and answers `device_info`.
//   - ota_start answers `ack` once the transfer is *enqueued*; progress and the
//     result arrive later as OtaStatus PvEvents on the pv_init SendPort.
//   - set_param and haptics are fire-and-forget: `ack` means queued, not applied.
int32_t pv_request(const uint8_t *req, uintptr_t req_len, uint8_t **resp,
                   uintptr_t *resp_len);

// Free a response buffer returned by pv_request. Null is a no-op. `len` must
// be the exact length pv_request reported for the pointer.
void pv_free(uint8_t *ptr, uintptr_t len);

// Push one RGBA8888 frame (len == width*height*4) to the panel. Fire-and-forget.
// The hot path: deliberately raw (no protobuf).
// Returns 0 if enqueued; -1 no device open; -2 enqueue failed.
int32_t pv_lcd_flush(const uint8_t *rgba_ptr, uintptr_t len, uint32_t width,
                     uint32_t height);

// Stop the worker and close the device.
// Blocks until the device is fully torn down. Returns 0.
int32_t pv_close(void);
