//! pico-view: drive an LCD + capacitive touch from captured Flutter frames,
//! with a thin Dart FFI surface.
//!
//! The whole engine is intentionally small: Dart pushes captured frames through
//! `pv_lcd_flush`, a single worker thread diffs each frame and streams the
//! changed regions to a [`transport::Transport`], and everything else travels through
//! one generic `pv_request` call carrying protobuf messages.
//! Engine events *and* request answers flow back over a Dart `SendPort` as
//! encoded `PvEvent` messages.
//!
//! ## FFI contract
//! - `pv_init(api_data, send_port)` — once, at startup.
//! - `pv_request(req, req_len)` — accept one encoded `picoview.ffi.PvRequest`.
//! - `pv_lcd_flush(rgba_ptr, len, w, h)` — push one RGBA8888 frame.
//! - `pv_close()` — tear down (blocking).

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
use std::time::Duration;
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


/// Upper bound on a caller-supplied `timeout_ms`, so a bad value can't pin a
/// scratch thread (and a Dart `Future`) for the life of the process.
const REQUEST_TIMEOUT_MAX: Duration = Duration::from_secs(10);

/// Resolve a request's `timeout_ms` against the default for its variant.
/// `0` means "use the default"; anything else is clamped to
/// [`REQUEST_TIMEOUT_MAX`].
fn deadline(timeout_ms: u32, default: Duration) -> Duration {
    if timeout_ms == 0 {
        default
    } else {
        Duration::from_millis(timeout_ms as u64).min(REQUEST_TIMEOUT_MAX)
    }
}

/// Responses are built without an id; [`post::post_response`] stamps in the one
/// the request carried.
fn ack() -> PvResponse {
    PvResponse { id: 0, resp: Some(pv_response::Resp::Ack(Ack {})) }
}

fn err(code: ErrorCode, message: impl Into<String>) -> PvResponse {
    PvResponse {
        id: 0,
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

/// Whether a request has to round-trip to the device — and so must not run on
/// the caller's thread. The rest are validation plus a channel send.
fn is_blocking(req: &Option<pv_request::Req>) -> bool {
    use pv_request::Req;
    matches!(
        req,
        Some(Req::OpenDevice(_)) | Some(Req::CloseDevice(_)) | Some(Req::GetDeviceInfo(_))
    )
}

/// Route one decoded request to wherever it can safely run, and post its answer
/// when it lands. See the module docs for why this is split.
fn dispatch(request: PvRequest) {
    let PvRequest { id, timeout_ms, req } = request;

    if !is_blocking(&req) {
        post::post_response(id, handle_request(req, timeout_ms));
        return;
    }

    // A scratch thread per round-trip. These are rare and user-initiated (open,
    // close, "what device is this?"), so a thread each is cheaper.
    let spawned = std::thread::Builder::new()
        .name("pv-request".into())
        .spawn(move || post::post_response(id, handle_request(req, timeout_ms)));

    if let Err(e) = spawned {
        log::error!("pv_request: could not spawn a thread for the request: {e}");
        post::post_response(id, err(ErrorCode::Internal, format!("thread spawn failed: {e}")));
    }
}

/// Execute one decoded control-plane request. Every arm produces a `PvResponse`.
///
/// `timeout_ms` is the caller's deadline for the arms that round-trip; `0`
/// leaves each one on its own default (see [`deadline`]).
fn handle_request(req: Option<pv_request::Req>, timeout_ms: u32) -> PvResponse {
    use pv_request::Req;
    match req {
        // Dart package is newer than this native library and sent a variant we don't know.
        None => err(
            ErrorCode::Unsupported,
            "unknown request variant (Dart package newer than native library?)",
        ),
        Some(Req::OpenDevice(open)) => open_device(open, deadline(timeout_ms, worker::OPEN_TIMEOUT)),
        Some(Req::CloseDevice(_)) => {
            close_device(deadline(timeout_ms, worker::CLOSE_TIMEOUT));
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
        Some(Req::GetDeviceInfo(_)) => {
            get_device_info(deadline(timeout_ms, worker::DEVICE_INFO_TIMEOUT))
        }
    }
}

/// Open the ESP32-P4 device and start a session.
fn open_device(open: ffi::OpenDevice, timeout: Duration) -> PvResponse {
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
    match engine.request(|reply| Cmd::Open { cfg, panel, reply }, timeout) {
        Ok(Ok(())) => ack(),
        Ok(Err(e)) => {
            log::error!("open_device: {e}");
            cmd_err(e)
        }
        Err(e) => req_err(e, "open_device"),
    }
}

/// Query the open device for its `DeviceInfo`
fn get_device_info(timeout: Duration) -> PvResponse {
    let engine = match engine_or_err() {
        Ok(e) => e,
        Err(resp) => return resp,
    };
    match engine.request(Cmd::GetDeviceInfo, timeout) {
        Ok(Ok(info)) => PvResponse { id: 0, resp: Some(pv_response::Resp::DeviceInfo(info)) },
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
fn close_device(timeout: Duration) {
    let Some(engine) = ENGINE.get().and_then(Option::as_ref) else {
        return;
    };
    engine.cancel();
    if let Err(e) = engine.request(Cmd::Close, timeout) {
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

/// Accept one control-plane request: decode `req_len` bytes at `req_ptr` as a
/// `picoview.ffi.PvRequest` and start executing it.
///
/// Returns as soon as the request is accepted — it does **not** wait for the
/// device. The `PvResponse` is posted to the `pv_init` SendPort once the
/// request completes, carrying the same `id` the request did.
///
/// Returns `0` when the request was accepted (an answer is coming, provided
/// `id` was nonzero), `-1` when it could not be decoded (no answer is coming).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pv_request(req_ptr: *const u8, req_len: usize) -> i32 {
    if req_ptr.is_null() {
        return -1;
    }
    let bytes = unsafe { std::slice::from_raw_parts(req_ptr, req_len) };
    match PvRequest::decode(bytes) {
        Ok(req) => {
            dispatch(req);
            0
        }
        Err(e) => {
            log::error!("pv_request: undecodable request ({} bytes): {e}", req_len);
            -1
        }
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

/// Stop the worker and close the device, blocking until it is torn down.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pv_close() -> i32 {
    close_device(worker::CLOSE_TIMEOUT);
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
        let e = error_of(handle_request(None, 0));
        assert_eq!(e.code, ErrorCode::Unsupported as i32);
    }

    #[test]
    fn unknown_model_is_bad_request() {
        let e = error_of(handle_request(
            Some(pv_request::Req::OpenDevice(ffi::OpenDevice {
                index: 0,
                model: "no-such-panel".into(),
                serial: String::new(),
            })),
            0,
        ));
        assert_eq!(e.code, ErrorCode::BadRequest as i32);
        assert!(e.message.contains("no-such-panel"), "{}", e.message);
    }

    #[test]
    fn device_commands_without_device_are_not_open() {
        let e = error_of(handle_request(
            Some(pv_request::Req::OtaStart(ffi::OtaStart { image: vec![0u8; 4] })),
            0,
        ));
        assert_eq!(e.code, ErrorCode::NotOpen as i32);
    }

    #[test]
    fn empty_ota_image_is_bad_request() {
        let e = error_of(handle_request(
            Some(pv_request::Req::OtaStart(ffi::OtaStart { image: vec![] })),
            0,
        ));
        assert_eq!(e.code, ErrorCode::BadRequest as i32);
    }

    #[test]
    fn get_device_info_without_device_is_not_open() {
        let e = error_of(handle_request(
            Some(pv_request::Req::GetDeviceInfo(wire::GetDeviceInfo {})),
            0,
        ));
        assert_eq!(e.code, ErrorCode::NotOpen as i32);
    }

    // --- request routing -------------------------------------------------------

    #[test]
    fn only_device_round_trips_leave_the_callers_thread() {
        // The split that keeps `pv_request` off the UI thread: exactly the
        // three variants that wait on the device get a thread of their own.
        assert!(is_blocking(&Some(pv_request::Req::OpenDevice(ffi::OpenDevice::default()))));
        assert!(is_blocking(&Some(pv_request::Req::CloseDevice(ffi::CloseDevice {}))));
        assert!(is_blocking(&Some(pv_request::Req::GetDeviceInfo(wire::GetDeviceInfo {}))));

        assert!(!is_blocking(&Some(pv_request::Req::OtaStart(ffi::OtaStart::default()))));
        assert!(!is_blocking(&Some(pv_request::Req::SetParam(wire::SetParam::default()))));
        assert!(!is_blocking(&Some(pv_request::Req::Haptics(wire::Haptics::default()))));
        // An unknown variant is a rejection, and rejections answer inline.
        assert!(!is_blocking(&None));
    }

    #[test]
    fn zero_timeout_keeps_the_per_variant_default() {
        assert_eq!(deadline(0, worker::OPEN_TIMEOUT), worker::OPEN_TIMEOUT);
        assert_eq!(deadline(0, worker::DEVICE_INFO_TIMEOUT), worker::DEVICE_INFO_TIMEOUT);
    }

    #[test]
    fn caller_timeout_overrides_the_default_but_is_capped() {
        assert_eq!(deadline(250, worker::OPEN_TIMEOUT), Duration::from_millis(250));
        // A caller can shorten *or* lengthen the default...
        assert_eq!(deadline(30_000, worker::DEVICE_INFO_TIMEOUT), Duration::from_secs(30));
        // ...but never past the cap, so a bad value can't pin a thread forever.
        assert_eq!(deadline(u32::MAX, worker::OPEN_TIMEOUT), REQUEST_TIMEOUT_MAX);
    }

    #[test]
    fn a_response_carries_the_id_of_the_request_it_answers() {
        // What the whole correlation scheme rests on: `post_response` stamps
        // the request's id onto an answer that was built without one.
        let answered = PvResponse { id: 7, ..ack() };
        assert_eq!(answered.id, 7);
        assert!(matches!(answered.resp, Some(pv_response::Resp::Ack(_))));
    }
}
