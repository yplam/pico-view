# Changelog

## 0.3.0

First published release.

### Display

- `PicoView` widget mirrors its child subtree to an external SPI LCD driven by
  ESP32-P4 firmware over a driverless USB vendor-bulk link. The child is laid
  out at exactly the panel's resolution, so a captured pixel maps 1:1 to a panel
  pixel.
- Capture is driven off Flutter's frame pipeline rather than a fixed-rate timer,
  and frames are only sent when the content actually changed — a static screen
  produces no USB traffic. Cap the rate with `PicoView(maxFps: ...)`.
- A trailing capture guarantees the final resting frame of an animation reaches
  the panel even when it settles inside a throttle window.
- The mirror keeps running while the app window is hidden or minimized, by
  driving the frame pipeline manually once the engine stops requesting frames.
- `PicoViewController.flushRgba` pushes a pre-rendered RGBA8888 frame directly,
  for producers that bypass the widget capture path (set `suspendCapture` while
  they own the panel).

### Touch

- Physical capacitive touches are injected back into the mirrored subtree as
  synthetic pointer events, so ordinary `GestureDetector`s and Material widgets
  respond to the panel.
- Per-event deltas and monotonic timestamps are carried through, so drag and
  velocity-based recognizers (sliders, flings) behave as they do on-screen.
- Raw events are also exposed on `PicoViewController.touches` in panel pixel
  coordinates.

### Device

- `open` / `dispose` lifecycle, `setBrightness`, `playHaptic` / `stopHaptic`
  (DRV2605L ROM waveforms), and `otaStart` for streaming a signed firmware
  image, with progress on `otaEvents`.
- `getDeviceInfo()` round-trips to the device for its id, serial, firmware
  version, panel geometry and capabilities.
- `linkStates` reports connect/disconnect transitions; the engine reconnects on
  its own.
- Devices carrying a vendor-provisioned identity can be attested — see
  `deviceVerified` and `attestedDeviceId`.

### Packaging

- The Rust engine ships as a prebuilt library for Linux x64/arm64, macOS
  x64/arm64 and Windows x64. `hook/build.dart` downloads the release pinned in
  `native/engine.lock`, verifies its SHA-256 and links it as a native code
  asset, so consuming apps need no Rust toolchain.
- The C ABI is frozen at five symbols; everything except the raw-frame hot path
  travels as protobuf messages, so new engine capabilities arrive as new message
  variants rather than new symbols.
- A no-op web stub keeps code using `package:pico_view` compiling for web.
