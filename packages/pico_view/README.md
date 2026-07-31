# pico_view

Mirror a Flutter widget subtree to an external **LCD + capacitive touch panel**, and feed physical 
touch events from the panel back into that same subtree. The panel is driven by **ESP32-P4 firmware 
over a driverless USB link**; the display/touch engine is Rust and Flutter talks to it through FFI.

## Usage

```dart
import 'package:pico_view/pico_view.dart';

final controller = PicoViewController()..init();
await controller.open(const PicoViewConfig()); // default model: st77916-round-360

// Anywhere in your tree:
PicoView(
  controller: controller,
  child: const MyDashboard(),
);
```

Every device call is asynchronous. `open` waits for the panel to come up (up to
~10s when nothing is attached) and the others round-trip or queue through the
engine, but the waiting happens on the engine's own thread — awaiting them never
stalls your isolate. Frame delivery is the exception: `flushRgba` stays
synchronous because `PicoView` calls it once per repaint.

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

See [`example/`](https://github.com/yplam/pico-view/tree/master/packages/pico_view/example)
for a runnable app that mirrors a clock to the panel and switches face on a
physical tap.

## Native engine

The `pico-view` engine is Rust; its source lives in this repo under
[`crates/pico-view`](https://github.com/yplam/pico-view/tree/master/crates/pico-view).
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
root (see [`native/README.md`](https://github.com/yplam/pico-view/blob/master/packages/pico_view/native/README.md)
for both override mechanisms).

## Connecting the panel

The USB link is **driverless** on all three platforms (a vendor-class interface
with WinUSB/MS-OS-2.0 descriptors), so no CH347 or other kernel driver is
involved. Windows auto-binds WinUSB from the device's MS-OS-2.0 descriptors and
macOS claims the interface natively — neither needs any setup.

**Linux** needs one udev rule so libusb can claim the vendor interface without
root. It is a permissions file, not a driver:

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="839a", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-pico-view.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Then replug the panel. `MODE="0666"` rather than `0660` (which would leave the
node `root:root` and lock the device down harder than udev's `0664` default) or
`TAG+="uaccess"` (which needs an active local seat, so it breaks over SSH).
Adjust `idProduct` if your firmware changes `PV_USB_PID`.

Without the rule, `PicoViewController.open` fails with a device error even
though the panel is plugged in and enumerated.

## FFI surface & bindings

The C ABI is frozen at four functions in `src/pico_view.h`; everything except the
raw-frame hot path (`pv_lcd_flush`) travels as **protobuf messages** whose schemas
live at the repo root under `proto/`.

`pv_request` hands a request over and returns — it never waits on the device.
Each request carries an `id`, and the engine posts the answering `PvResponse` to
the `pv_init` SendPort alongside the touch/link/OTA events; `PicoViewController`
keeps a `Completer` per outstanding id and completes the matching `Future`. That
is what keeps a 10-second `open` off the calling isolate.

Two kinds of generated Dart bindings back this:

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

Apache-2.0. See [LICENSE](https://github.com/yplam/pico-view/blob/master/packages/pico_view/LICENSE).
