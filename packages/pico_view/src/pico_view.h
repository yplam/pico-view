// FFI surface for the `pico-view` Rust cdylib.
//
// Everything except frame delivery travels as protobuf messages; the schemas
// live in the pico-view repo under proto/ (pv_ffi.proto imports pv_wire.proto)
// and the generated Dart types ship in lib/src/gen/.

#include <stdint.h>

// Initialize the Dart DL API and store the SendPort native handle for the
// engine push channel. Call once at startup with the pointer from
// `NativeApi.initializeApiDLData`. Returns 0 on success; -1 when the Dart DL
// API could not be initialized.
//
// Everything the engine sends back arrives on this SendPort as Uint8List
// payloads, each one an encoded `picoview.ffi.PvEvent`: touch, link, and OTA
// events, plus the `PvResponse` answering each pv_request. Link events are
// posted on transitions only.
int32_t pv_init(void *api_data, int64_t send_port);

// Accept one control-plane request: decode `req_len` bytes at `req` as an
// encoded `picoview.ffi.PvRequest` and start executing it.
//
// Returns 0 when the request was accepted, -1 when it could not be decoded.
// Neither return value says anything about the *outcome* — this call does not
// wait for the device. The answer is posted later to the pv_init SendPort as a
// PvEvent carrying a PvResponse whose `id` echoes `PvRequest.id`; the caller
// matches the two. Set `PvRequest.id` to 0 to run a request without an answer.
//
// Every accepted request with a nonzero id is answered exactly once, so a
// caller waiting on an id is never left hanging.
int32_t pv_request(const uint8_t *req, uintptr_t req_len);

// Push one RGBA8888 frame (len == width*height*4) to the panel. Fire-and-forget.
// The hot path: deliberately raw (no protobuf, no correlation id).
// Returns 0 if enqueued; -1 no device open; -2 enqueue failed.
int32_t pv_lcd_flush(const uint8_t *rgba_ptr, uintptr_t len, uint32_t width,
                     uint32_t height);

// Stop the worker and close the device.
// Blocks until the device is fully torn down. Returns 0.
int32_t pv_close(void);
