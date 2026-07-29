//! Open-time configuration, built from the `OpenDevice` request Dart sends
//! through `pv_request`.

use crate::panels::DEFAULT_MODEL;

/// Open-time configuration.
#[derive(Debug, Clone)]
pub struct PicoViewConfig {
    /// Index of the ESP32-P4 device to open. Ignored when [`serial`] is set.
    pub index: u32,

    /// Select the device by its USB serial string. `None` selects by index.
    pub serial: Option<String>,

    /// Panel model name; resolved to a [`crate::panels::PanelSpec`] preset.
    pub model: String,
}

impl Default for PicoViewConfig {
    fn default() -> Self {
        PicoViewConfig { index: 0, serial: None, model: DEFAULT_MODEL.to_string() }
    }
}
