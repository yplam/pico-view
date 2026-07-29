//! Generated protobuf types for the two schema packages (see ../../proto):
//!
//! - [`ffi`] — the Dart <-> Rust control plane (`PvRequest` / `PvResponse` /
//!   `PvEvent`), carried through `pv_request` and the `pv_init` SendPort.
//! - [`wire`] — host <-> device messages, carried as one encoded
//!   `HostToDevice`/`DeviceToHost` per CTRL frame on the USB link (wire v2, see
//!   [`crate::esp32p4`]). Device-originated payloads (`Touch`, `OtaStatus`) are
//!   forwarded into `ffi` events unchanged.

// The included sources are committed under `src/proto/` and regenerated with
// `cargo xtask gen-proto` — see that task. Ordinary builds need no `protoc`.

pub mod wire {
    include!("proto/picoview.wire.rs");
}

pub mod ffi {
    include!("proto/picoview.ffi.rs");
}
