//! Touch event emission.
//!
//! The ESP32-P4 firmware owns the capacitive touch controller and reports
//! down/move/up records over USB; the backend ([`crate::esp32p4`]) decodes them
//! and forwards each through [`emit`], which pushes a `PvEvent` touch message
//! to Dart (see [`crate::post`]).

use crate::proto::wire::TouchPhase;

/// Push one touch event to the Dart event stream. The firmware-sourced touch
/// records from the ESP32-P4 backend are forwarded through this so the
/// Dart-side touch stream has a single, stable format.
pub(crate) fn emit(phase: TouchPhase, x: u16, y: u16) {
    crate::post::post_touch(phase, x as u32, y as u32);
}
