//! Rust → Dart push channel.
//!
//! Engine events (touch, link transitions, OTA progress) *and* the answers to
//! `pv_request` calls are encoded as one `picoview.ffi.PvEvent` protobuf
//! message each and posted to the stored `SendPort` as a `kTypedData`/`Uint8`
//! `Dart_CObject` via `Dart_PostCObject_DL` (arriving in Dart as a
//! `Uint8List`). Dart copies the bytes synchronously during the post, so no
//! finalizer / owned buffer is needed on our side.

use crate::proto::ffi::{pv_event, LinkEvent, LinkState, PvEvent, PvResponse};
use crate::proto::wire::{OtaState, OtaStatus, Touch, TouchPhase};
use crate::transport::DeviceIdentity;
use dart_sys::{
    Dart_CObject, Dart_CObject_Type_Dart_CObject_kTypedData, Dart_PostCObject_DL,
    Dart_TypedData_Type_Dart_TypedData_kUint8,
};
use prost::Message;
use std::sync::atomic::{AtomicI64, Ordering};

/// SendPort native handle, stored by `pv_init`. `-1` until initialized.
pub static DART_PORT: AtomicI64 = AtomicI64::new(-1);

/// Encode one event and post it to the Dart SendPort. No-op before `pv_init`.
pub fn post_event(event: pv_event::Event) {
    let port = DART_PORT.load(Ordering::Relaxed);
    if port == -1 {
        return;
    }
    let buf = PvEvent { event: Some(event) }.encode_to_vec();

    let mut obj = Dart_CObject {
        type_: Dart_CObject_Type_Dart_CObject_kTypedData,
        value: unsafe {
            let mut v: dart_sys::_Dart_CObject__bindgen_ty_1 = std::mem::zeroed();
            v.as_typed_data.type_ = Dart_TypedData_Type_Dart_TypedData_kUint8;
            v.as_typed_data.length = buf.len() as isize;
            v.as_typed_data.values = buf.as_ptr();
            v
        },
    };

    // Dart_PostCObject copies typed data before returning, so `buf` can drop.
    unsafe {
        if let Some(post_fn) = Dart_PostCObject_DL {
            post_fn(port, &mut obj);
        }
    }
}

/// Post the answer to one `pv_request`, tagged with the id that request
/// carried so the caller can match it up. `id == 0` means the caller asked for
/// no answer, so nothing is posted.
pub fn post_response(id: u32, resp: PvResponse) {
    if id == 0 {
        return;
    }
    post_event(pv_event::Event::Response(PvResponse { id, ..resp }));
}

/// Post one touch event (in LCD pixel coordinates).
pub fn post_touch(phase: TouchPhase, x: u32, y: u32) {
    post_event(pv_event::Event::Touch(Touch { phase: phase as i32, x, y }));
}

/// Post a firmware-update progress/result event. `err` is a
/// [`crate::esp32p4`] `pv_ota_err` code (0 on success).
pub fn post_ota_status(state: OtaState, pct: u32, err: i32) {
    post_event(pv_event::Event::Ota(OtaStatus { state: state as i32, pct, err }));
}

/// Post a device link-state transition. The worker de-duplicates so Dart only
/// sees transitions; `detail` carries the reason for DISCONNECTED. The identity
/// fields are only meaningful on CONNECTED (see [`post_link_connected`]); other
/// states leave them empty.
pub fn post_link(state: LinkState, detail: &str) {
    post_event(pv_event::Event::Link(LinkEvent {
        state: state as i32,
        detail: detail.to_string(),
        verified: false,
        device_id: String::new(),
        fw_version: String::new(),
    }));
}

/// Post a CONNECTED transition carrying what the handshake learned about the
/// device: its `fw_version` (empty when the firmware doesn't report one) and the
/// attestation result. `verified` is advisory — an unverified device is
/// connected and driven just the same (see [`crate::auth`]).
pub fn post_link_connected(identity: &DeviceIdentity) {
    post_event(pv_event::Event::Link(LinkEvent {
        state: LinkState::Connected as i32,
        detail: String::new(),
        verified: identity.verified,
        device_id: identity.device_id.clone().unwrap_or_default(),
        fw_version: identity.fw_version.clone().unwrap_or_default(),
    }));
}
