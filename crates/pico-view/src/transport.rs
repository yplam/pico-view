//! The bus-agnostic seam between the worker's frame pipeline and a concrete
//! display backend.
//!
//! [`crate::worker`] does all the panel-independent work — dirty-rectangle
//! diffing and packing each changed region to big-endian RGB565 — and then hands
//! the packed regions to a [`Transport`]. One backend implements it:
//!
//! - [`crate::esp32p4::Esp32P4Transport`] — stream the regions as vendor-bulk
//!   `BLIT` messages to ESP32-P4 firmware that owns the panel; touch arrives
//!   asynchronously on a reader thread, so `poll` is a no-op.

use crate::lcd::Rect;
use crate::panels::PanelShape;

/// Why opening the device failed: enumeration, claim, IO, handshake, or panel
/// init. Surfaced to Dart as `ERROR_CODE_DEVICE`.
#[derive(Debug)]
pub struct OpenError(pub String);

/// What the device reported about itself during the handshake, carried out to
/// Dart on the CONNECTED link event.
#[derive(Debug, Clone, Default)]
pub struct DeviceIdentity {
    /// The device's firmware version (ESP-IDF app version) from the HELLO_ACK.
    /// `None` for firmware that doesn't report one (pre-v2) or backends with no
    /// handshake.
    pub fw_version: Option<String>,
    /// Whether the device proved it is genuine vendor hardware (see
    /// [`crate::auth`]). Advisory: `false` just means "not a provisioned unit",
    /// and the panel is driven the same either way.
    pub verified: bool,
    /// The attested device id from the certificate. `Some` only when
    /// `verified`.
    pub device_id: Option<String>,
}

impl std::fmt::Display for OpenError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

/// Panel geometry the worker needs to diff and pack frames, independent of how
/// the backend drives the glass. (The P4 backend forwards geometry to the device
/// in a `CONFIG` message.)
pub struct PanelGeom {
    pub width: u32,
    pub height: u32,
    /// Visible outline. Carried so the worker can report it on `DeviceInfo`;
    /// diffing and streaming always cover the full rectangle.
    pub shape: PanelShape,
}

/// A concrete display backend: it accepts already-diffed, already-packed regions
/// and emits them to its bus, and (optionally) sources touch events.
pub trait Transport: Send {
    /// Push the changed regions of one frame. Each entry is a panel-local
    /// [`Rect`] paired with its `rect.w * rect.h * 2` bytes of big-endian RGB565
    /// (as produced by [`crate::lcd::rgba_rect_to_rgb565_be`]). Ownership is
    /// transferred so a backend can move the pixel buffers straight onto the bus
    /// without copying. The regions form one logical frame; the backend may
    /// treat the last entry as the frame boundary (the P4 sets PRESENT on it).
    fn flush(&mut self, rects: Vec<(Rect, Vec<u8>)>) -> Result<(), String>;

    /// Periodic worker tick (every `POLL_INTERVAL`). Backends that source touch
    /// asynchronously (like the P4) leave this as the default no-op; a polling
    /// backend would read its touch controller here.
    ///
    /// Returns `Err` when the underlying bus transfer failed. The worker uses a
    /// sustained run of these errors (together with [`Transport::flush`]
    /// failures) to detect that the device has been unplugged and to begin
    /// trying to reopen it; a single transient error is harmless.
    fn poll(&mut self) -> Result<(), String> {
        Ok(())
    }

    /// Stream a signed firmware image to the device and commit it (backends that
    /// support over-the-wire updates). This blocks the worker for the duration of
    /// the transfer and posts `"type":"ota"` progress events to Dart as it runs.
    /// On success the device reboots into the new image, so the caller should
    /// treat the link as lost and reconnect. The default is "unsupported".
    ///
    /// `cancel` is set when the host is shutting down (`pv_close`): the backend
    /// must notice it promptly (sub-second), tell the device to discard the
    /// partial image if it can, and return `Err` — otherwise closing the app
    /// mid-update would block on the full transfer/timeout.
    fn ota(&mut self, _image: &[u8], _cancel: &std::sync::atomic::AtomicBool) -> Result<(), String> {
        Err("firmware update is not supported by this backend".to_string())
    }

    /// Query the device for its self-reported [`wire::DeviceInfo`](crate::proto::wire::DeviceInfo)
    /// — serial, firmware version, capabilities, panel geometry. Unlike the
    /// fire-and-forget commands this round-trips to the device and blocks the
    /// worker briefly (sub-second) for the reply, so the FFI `get_device_info`
    /// request can answer synchronously. Default: unsupported.
    fn get_device_info(&mut self) -> Result<crate::proto::wire::DeviceInfo, String> {
        Err("device info is not supported by this backend".to_string())
    }

    /// Set the panel backlight brightness, 0 (dark) – 255 (full). Fire-and-forget:
    /// the device acknowledges with `ParamAck`, but the worker does not block on
    /// it (a UI slider may send these rapidly). Default: unsupported.
    fn set_brightness(&mut self, _brightness: u8) -> Result<(), String> {
        Err("runtime parameters are not supported by this backend".to_string())
    }

    /// Drive the device's haptic actuator (DRV2605L). Fire-and-forget like
    /// [`set_brightness`]: the device does not ack, and UI gestures may trigger
    /// these rapidly, so the worker does not block. Default: unsupported.
    fn haptics(&mut self, _cmd: crate::proto::wire::Haptics) -> Result<(), String> {
        Err("haptics is not supported by this backend".to_string())
    }

    /// Send a liveness heartbeat so the device can tell "host still here, screen
    /// just static" from "host gone". The worker calls this on a slow cadence
    /// (~1s) while the link is up; a device that self-blanks on host silence
    /// (like the P4) uses it to hold the panel lit through idle stretches. Default
    /// no-op: backends whose device has no idle-blank behaviour need nothing here.
    fn keepalive(&mut self) -> Result<(), String> {
        Ok(())
    }
}
