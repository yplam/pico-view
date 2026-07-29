//! Built-in panel presets.
//!
//! The FFI caller can't reasonably know a panel's controller init quirks, glass
//! insets, MADCTL rotation, or touch axis mapping — those are fixed properties of
//! *which physical module* is plugged in. So instead of making Dart pass a dozen
//! fields, it passes one [`model`](crate::config::PicoViewConfig::model) name and
//! we resolve the whole [`PanelSpec`] here (which the ESP32-P4 backend forwards
//! to the firmware as a `CONFIG` message). Add a panel by appending a preset to
//! [`PRESETS`].

/// Physical outline of the panel's active area. A [`PanelShape::Round`] panel
/// still addresses its controller RAM as a full rectangle, but the four corners
/// are not visible glass.
///
/// This is descriptive only: the engine streams and diffs the full rectangle
/// either way. It exists so the app can be told the outline (it is reported on
/// `DeviceInfo.panel.shape`, which the device itself cannot fill in) and lay its
/// content out to clear a round rim.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PanelShape {
    /// Full rectangle is visible.
    Rect,
    /// Inscribed circle (diameter = `min(width, height)`, centered) is visible.
    Round,
}

/// A fully-resolved panel description: everything
/// [`open_session`](crate::worker) needs after the device is opened. One per
/// supported physical module.
#[derive(Debug, Clone, Copy)]
pub struct PanelSpec {
    /// Display-controller profile id, mapped to a firmware panel model id by the
    /// ESP32-P4 backend (see `esp32p4::build_config`).
    pub driver: &'static str,
    /// Visible outline (rectangular vs. round); reported to the app, not used
    /// to clip what is streamed. See [`PanelShape`].
    pub shape: PanelShape,
    /// Visible width in pixels (in the panel's wired orientation).
    pub width: u32,
    /// Visible height in pixels.
    pub height: u32,
    /// Column offset into controller RAM (glass inset).
    pub x_offset: u16,
    /// Row offset into controller RAM (glass inset).
    pub y_offset: u16,
    /// Emit display-inversion-on during init.
    pub invert: bool,
    /// Display rotation in degrees (0/90/180/270); drives MADCTL.
    pub rotation: u32,

    /// Touch-controller id, or `"none"` to disable touch.
    pub touch: &'static str,
    /// 7-bit I2C address of the touch controller.
    pub touch_addr: u8,
    /// Swap touch X/Y axes before reporting.
    pub touch_swap_xy: bool,
    /// Mirror the touch X axis.
    pub touch_flip_x: bool,
    /// Mirror the touch Y axis.
    pub touch_flip_y: bool,
}

/// The default model, used when the caller omits `model`. The 360x360 ST77916
/// round panel with a CST816D touch controller.
pub const DEFAULT_MODEL: &str = "st77916-round-360";

/// Registry of supported panels, keyed by model name. Resolve with [`resolve`].
const PRESETS: &[(&str, PanelSpec)] = &[
    // 360x360 round ST77916 panel + CST816D capacitive touch (the default).
    (
        "st77916-round-360",
        PanelSpec {
            driver: "st77916",
            shape: PanelShape::Round,
            width: 360,
            height: 360,
            x_offset: 0,
            y_offset: 0,
            invert: true,
            rotation: 0,
            touch: "cst816d",
            touch_addr: 0x15,
            touch_swap_xy: false,
            touch_flip_x: false,
            touch_flip_y: false,
        },
    ),
    // Waveshare 1.69" ST7789 panel (240x280, 20px row inset) + CST816S touch.
    (
        "st7789-1.69",
        PanelSpec {
            driver: "st7789",
            shape: PanelShape::Rect,
            width: 240,
            height: 280,
            x_offset: 0,
            y_offset: 20,
            invert: true,
            rotation: 0,
            touch: "cst816s",
            touch_addr: 0x15,
            touch_swap_xy: false,
            touch_flip_x: false,
            touch_flip_y: false,
        },
    ),
];

/// Resolve a model name to its [`PanelSpec`], or `None` if unknown.
pub fn resolve(model: &str) -> Option<PanelSpec> {
    PRESETS
        .iter()
        .find(|(name, _)| *name == model)
        .map(|(_, spec)| *spec)
}

/// Comma-separated list of known model names, for error messages.
pub fn known_models() -> String {
    PRESETS
        .iter()
        .map(|(name, _)| *name)
        .collect::<Vec<_>>()
        .join(", ")
}
