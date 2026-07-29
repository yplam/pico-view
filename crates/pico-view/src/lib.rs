//! pico-view: drive an LCD + capacitive touch from captured Flutter frames,
//! with a thin Dart FFI surface.
//!
//! The whole engine is intentionally small: Dart pushes captured frames through
//! `pv_lcd_flush`, a single worker thread diffs each frame and streams the
//! changed regions to a [`transport::Transport`], and everything else travels through
//! one generic `pv_request` call carrying protobuf messages.
//! Engine events flow back over a Dart `SendPort` as encoded `PvEvent` messages.
//!
//! ## FFI contract
//! - `pv_init(api_data, send_port)` — once, at startup.
//! - `pv_request(req, req_len, &resp, &resp_len)` — one encoded
//!   `picoview.ffi.PvRequest` in, one encoded `PvResponse` out.
//! - `pv_free(ptr, len)` — release a `pv_request` response buffer.
//! - `pv_lcd_flush(rgba_ptr, len, w, h)` — push one RGBA8888 frame.
//! - `pv_close()` — tear down.

mod auth;
mod config;
mod esp32p4;
mod frame;
mod lcd;
mod panels;
mod post;
mod proto;
mod touch;
mod transport;
mod worker;

use post::DART_PORT;
use prost::Message;
use proto::ffi::{pv_request, pv_response, Ack, Error, ErrorCode, PvRequest, PvResponse};
use std::sync::atomic::Ordering;
use std::sync::{Once, OnceLock};
use worker::{Cmd, CmdError, Engine, ReqError};

pub use auth::open_for_check;
pub use config::PicoViewConfig;
pub use panels::PanelSpec;
pub use proto::{ffi, wire};

pub fn resolve_panel(model: &str) -> Option<PanelSpec> {
    panels::resolve(model)
}

/// The process-wide engine handle.
static ENGINE: OnceLock<Option<Engine>> = OnceLock::new();

fn engine() -> Option<&'static Engine> {
    ENGINE.get_or_init(Engine::spawn).as_ref()
}

/// Guards one-time logger installation.
static LOG_INIT: Once = Once::new();

/// Install a stderr logger so the crate's `log::*` calls are visible.
fn init_logging() {
    LOG_INIT.call_once(|| {
        let _ = env_logger::Builder::from_env(
            env_logger::Env::default().default_filter_or("info"),
        )
        .try_init();
    });
}


fn ack() -> PvResponse {
    PvResponse { resp: Some(pv_response::Resp::Ack(Ack {})) }
}

fn err(code: ErrorCode, message: impl Into<String>) -> PvResponse {
    PvResponse {
        resp: Some(pv_response::Resp::Error(Error { code: code as i32, message: message.into() })),
    }
}

/// The engine, or the response to send when it could not be started.
fn engine_or_err() -> Result<&'static Engine, PvResponse> {
    engine().ok_or_else(|| err(ErrorCode::Internal, "engine worker failed to start"))
}

/// Map an answer the supervisor gave us onto its FFI error code.
fn cmd_err(e: CmdError) -> PvResponse {
    match e {
        CmdError::NotOpen => err(ErrorCode::NotOpen, "no device open"),
        CmdError::AlreadyOpen => err(ErrorCode::AlreadyOpen, "a device is already open"),
        CmdError::Device(m) => err(ErrorCode::Device, m),
    }
}

/// Map a request/reply round-trip that produced no answer at all.
fn req_err(e: ReqError, what: &str) -> PvResponse {
    match e {
        ReqError::WorkerGone => err(ErrorCode::Enqueue, "engine worker is gone"),
        ReqError::Timeout => err(ErrorCode::Timeout, format!("{what}: {e}; try again")),
        ReqError::Panicked => err(ErrorCode::Internal, format!("{what}: {e}")),
    }
}

/// Execute one decoded control-plane request. Every arm produces a `PvResponse`.
fn handle_request(req: Option<pv_request::Req>) -> PvResponse {
    use pv_request::Req;
    match req {
        // Dart package is newer than this native library and sent a variant we don't know.
        None => err(
            ErrorCode::Unsupported,
            "unknown request variant (Dart package newer than native library?)",
        ),
        Some(Req::OpenDevice(open)) => open_device(open),
        Some(Req::CloseDevice(_)) => {
            close_device();
            ack()
        }
        Some(Req::OtaStart(ota)) => {
            if ota.image.is_empty() {
                return err(ErrorCode::BadRequest, "firmware image is empty");
            }
            enqueue(Cmd::Ota(ota.image))
        }
        Some(Req::SetParam(p)) => match p.param {
            Some(wire::set_param::Param::Brightness(b)) => {
                enqueue(Cmd::SetBrightness(b.min(255) as u8))
            }
            None => err(
                ErrorCode::Unsupported,
                "unknown SetParam variant (Dart package newer than native library?)",
            ),
        },
        Some(Req::Haptics(h)) => enqueue(Cmd::Haptics(h)),
        Some(Req::GetDeviceInfo(_)) => get_device_info(),
    }
}

/// Open the ESP32-P4 device and start a session.
fn open_device(open: ffi::OpenDevice) -> PvResponse {
    let mut cfg = PicoViewConfig { index: open.index, ..PicoViewConfig::default() };
    if !open.serial.is_empty() {
        cfg.serial = Some(open.serial);
    }
    if !open.model.is_empty() {
        cfg.model = open.model;
    }
    let panel = match panels::resolve(&cfg.model) {
        Some(p) => p,
        None => {
            return err(
                ErrorCode::BadRequest,
                format!("unknown model '{}'; known models: {}", cfg.model, panels::known_models()),
            );
        }
    };

    let engine = match engine_or_err() {
        Ok(e) => e,
        Err(resp) => return resp,
    };
    match engine.request(|reply| Cmd::Open { cfg, panel, reply }, worker::OPEN_TIMEOUT) {
        Ok(Ok(())) => ack(),
        Ok(Err(e)) => {
            log::error!("open_device: {e}");
            cmd_err(e)
        }
        Err(e) => req_err(e, "open_device"),
    }
}

/// Query the open device for its `DeviceInfo`
fn get_device_info() -> PvResponse {
    let engine = match engine_or_err() {
        Ok(e) => e,
        Err(resp) => return resp,
    };
    match engine.request(Cmd::GetDeviceInfo, worker::DEVICE_INFO_TIMEOUT) {
        Ok(Ok(info)) => PvResponse { resp: Some(pv_response::Resp::DeviceInfo(info)) },
        Ok(Err(e)) => cmd_err(e),
        Err(e) => req_err(e, "get_device_info"),
    }
}

/// Queue a fire-and-forget command on the worker.
fn enqueue(cmd: Cmd) -> PvResponse {
    let engine = match engine_or_err() {
        Ok(e) => e,
        Err(resp) => return resp,
    };
    if !engine.is_open() {
        return err(ErrorCode::NotOpen, "no device open");
    }
    match engine.send(cmd) {
        Ok(()) => ack(),
        Err(e) => err(ErrorCode::Enqueue, e.to_string()),
    }
}

/// End the session and close the device.
fn close_device() {
    let Some(engine) = ENGINE.get().and_then(Option::as_ref) else {
        return;
    };
    engine.cancel();
    if let Err(e) = engine.request(Cmd::Close, worker::CLOSE_TIMEOUT) {
        log::warn!("close_device: {e}");
    }
}

// --- FFI ---------------------------------------------------------------------

/// Initialize the Dart DL API and store the SendPort for engine events .
/// Returns `0` on success, `-1` when the Dart DL API could not be initialized.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pv_init(api_data: *mut core::ffi::c_void, send_port: i64) -> i32 {
    init_logging();
    if engine().is_none() {
        log::error!("pv_init: engine worker failed to start");
    }
    if unsafe { dart_sys::Dart_InitializeApiDL(api_data) } != 0 {
        // Version mismatch between the bundled headers and the running Dart SDK.
        log::error!("pv_init: Dart_InitializeApiDL failed (DL API version mismatch)");
        return -1;
    }
    DART_PORT.store(send_port, Ordering::Relaxed);
    0
}

/// Handle one control-plane request: decode `req_len` bytes at `req_ptr` as a
/// `picoview.ffi.PvRequest`, execute it, and return the encoded `PvResponse`
/// through `resp_out`/`resp_len_out` (the caller owns the buffer and MUST
/// release it with `pv_free`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pv_request(
    req_ptr: *const u8,
    req_len: usize,
    resp_out: *mut *mut u8,
    resp_len_out: *mut usize,
) -> i32 {
    if req_ptr.is_null() || resp_out.is_null() || resp_len_out.is_null() {
        return -1;
    }
    let bytes = unsafe { std::slice::from_raw_parts(req_ptr, req_len) };
    let req = match PvRequest::decode(bytes) {
        Ok(r) => r,
        Err(e) => {
            log::error!("pv_request: undecodable request ({} bytes): {e}", req_len);
            return -1;
        }
    };

    let buf = handle_request(req.req).encode_to_vec().into_boxed_slice();
    let len = buf.len();
    unsafe {
        *resp_out = Box::into_raw(buf) as *mut u8;
        *resp_len_out = len;
    }
    0
}

/// Free a response buffer returned by `pv_request`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pv_free(ptr: *mut u8, len: usize) {
    if !ptr.is_null() {
        drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(ptr, len)) });
    }
}

/// Push one RGBA8888 frame (`len == w*h*4`) to the panel. Fire-and-forget.
/// Returns `0` if enqueued; `-1` no device open; `-2` enqueue failed.
///
/// The hot path, called once per repaint from Flutter's raster thread: an atomic
/// load, one copy into a recycled buffer, and at most one channel send.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pv_lcd_flush(
    rgba_ptr: *const u8,
    len: usize,
    width: u32,
    height: u32,
) -> i32 {
    if rgba_ptr.is_null() {
        return -1;
    }
    let Some(engine) = engine() else {
        return -1;
    };
    if !engine.is_open() {
        return -1;
    }
    let rgba = unsafe { std::slice::from_raw_parts(rgba_ptr, len) };
    if engine.push_frame(rgba, width, height) {
        0
    } else {
        -2
    }
}

/// Stop the worker and close the device.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pv_close() -> i32 {
    close_device();
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    fn error_of(resp: PvResponse) -> Error {
        match resp.resp {
            Some(pv_response::Resp::Error(e)) => e,
            other => panic!("expected error response, got {other:?}"),
        }
    }

    #[test]
    fn empty_request_is_unsupported() {
        let e = error_of(handle_request(None));
        assert_eq!(e.code, ErrorCode::Unsupported as i32);
    }

    #[test]
    fn unknown_model_is_bad_request() {
        let e = error_of(handle_request(Some(pv_request::Req::OpenDevice(ffi::OpenDevice {
            index: 0,
            model: "no-such-panel".into(),
            serial: String::new(),
        }))));
        assert_eq!(e.code, ErrorCode::BadRequest as i32);
        assert!(e.message.contains("no-such-panel"), "{}", e.message);
    }

    #[test]
    fn device_commands_without_device_are_not_open() {
        let e = error_of(handle_request(Some(pv_request::Req::OtaStart(ffi::OtaStart {
            image: vec![0u8; 4],
        }))));
        assert_eq!(e.code, ErrorCode::NotOpen as i32);
    }

    #[test]
    fn empty_ota_image_is_bad_request() {
        let e = error_of(handle_request(Some(pv_request::Req::OtaStart(ffi::OtaStart {
            image: vec![],
        }))));
        assert_eq!(e.code, ErrorCode::BadRequest as i32);
    }

    #[test]
    fn get_device_info_without_device_is_not_open() {
        let e = error_of(handle_request(Some(pv_request::Req::GetDeviceInfo(
            wire::GetDeviceInfo {},
        ))));
        assert_eq!(e.code, ErrorCode::NotOpen as i32);
    }
}
