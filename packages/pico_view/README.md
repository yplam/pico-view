# pico_view

Mirror a Flutter widget subtree to an external **LCD + capacitive touch panel**, and feed physical 
touch events from the panel back into that same subtree. The panel is driven by **ESP32-P4 firmware 
over a driverless USB link**; the display/touch engine is Rust and Flutter talks to it through FFI.

## Usage

```dart
import 'package:pico_view/pico_view.dart';

final controller = PicoViewController()..init();
controller.open(const PicoViewConfig()); // default model: st77916-round-360

// Anywhere in your tree:
PicoView(
  controller: controller,
  child: const MyDashboard(),
);
```

The child is laid out at exactly the panel's logical resolution
(`controller.config.width` × `.height` — e.g. 360×360 for the round panel), so a
captured pixel maps 1:1 to a panel pixel and a panel touch maps 1:1 to a
child-local coordinate. Capture is driven off Flutter's frame pipeline and only
sends when the content actually changed, so a static screen produces no USB
traffic (cap the rate with `PicoView(maxFps: ...)`). If no device is open, frames
are dropped and the widget still renders on-screen.

The controller also exposes broadcast streams — `touches`, `linkStates`,
`otaEvents` — plus device ops (`setBrightness`, `playHaptic`, `stopHaptic`,
`otaStart`) and `getDeviceInfo()`, which round-trips to the device for its
serial, firmware version, panel geometry and capabilities.

See [`example/`](example/) for a runnable app that mirrors a clock to the panel
and switches face on a physical tap.

## Native engine

The `pico-view` engine is Rust; its source lives in this repo under
[`crates/pico-view`](https://github.com/yplam/pico-view/tree/main/crates/pico-view).
The built libraries are **not committed** — the Dart build hook
(`hook/build.dart`) downloads the release pinned in `native/engine.lock`,
verifies its SHA-256, and links it as a native code asset, so building the app
needs no Rust toolchain. Builds are published for **Linux x64/arm64**, **macOS
x64/arm64**, and **Windows x64**; other targets are unsupported.

Enable native assets in the consuming app:

```sh
flutter config --enable-native-assets
```

To build the engine yourself and link that instead, run `./build.sh` at the repo
root (see [`native/README.md`](native/README.md) for both override mechanisms).

The USB link is **driverless** on all three platforms (a vendor-class interface
with WinUSB/MS-OS-2.0 descriptors): Windows auto-binds WinUSB, macOS claims it
via libusb natively, and Linux needs only a one-line udev rule for non-root
access. No CH347 or other kernel driver is involved.

## FFI surface & bindings

The C ABI is frozen at five functions in `src/pico_view.h`; everything except the
raw-frame hot path (`pv_lcd_flush`) travels as **protobuf messages** whose schemas
live at the repo root under `proto/`. Two kinds of generated Dart bindings back
this:

- the C-symbol bindings, regenerated when `src/pico_view.h` changes:

  ```sh
  dart run ffigen --config ffigen.yaml
  ```

- the protobuf message types under `lib/src/gen/`, regenerated with `protoc` from
  the `proto/` schemas when the wire/FFI messages change:

  ```sh
  protoc --dart_out=lib/src/gen -I ../../proto pv_ffi.proto pv_wire.proto
  ```

New engine capabilities arrive as new protobuf message variants, not new C
symbols, so the ABI itself stays fixed.

## License

Apache-2.0. See [LICENSE](LICENSE).
