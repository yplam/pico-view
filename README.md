# pico-view

Mirror a Flutter widget subtree to an external **LCD + capacitive touch panel**,
and feed physical touches from the panel back into that same subtree.

```dart
final controller = PicoViewController()..init();
controller.open(const PicoViewConfig()); // default model: st77916-round-360

PicoView(
  controller: controller,
  child: const MyDashboard(),
);
```

The child is laid out at exactly the panel's resolution, so a captured pixel maps
1:1 to a panel pixel and a panel touch maps 1:1 to a child-local coordinate. If
no device is attached, frames are dropped and the widget still renders on-screen.

## Using it in an app

```sh
flutter pub add pico_view
flutter config --enable-native-assets   # once, per machine
```

The package's build hook downloads the prebuilt engine for your target from this
repo's releases and verifies it against a pinned SHA-256. Prebuilt targets are
**Linux x64/arm64**, **macOS x64/arm64** and **Windows x64**.

See [`packages/pico_view/README.md`](packages/pico_view/README.md) for the API,
and [`packages/pico_view/example/`](packages/pico_view/example/) for a runnable
app that mirrors a clock to the panel and switches face on a physical tap.

### Linux: panel permissions

libusb has to claim the vendor interface without root, which needs one udev rule
(a permissions file, not a driver):

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="839a", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-pico-view.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Then replug the panel. Windows auto-binds WinUSB from the device's MS-OS-2.0
descriptors and macOS claims the interface natively, so neither needs setup.

## Development

### Engine

```sh
./build.sh                                  # host target
./build.sh --target aarch64-apple-darwin    # or any supported triple
```

This builds the engine from `crates/` and writes
`packages/pico_view/native/engine.local`, an override the package's build hook
reads before the pinned release.

### Firmware

```sh
. ~/esp/esp-idf/export.sh
cd firmwares/esp32p4
idf.py set-target esp32p4      # first time only
idf.py menuconfig              # set GPIO pins to match your board
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

See `firmwares/esp32p4/README.md` for the wire protocol and pin map.

### Regenerating bindings

When `packages/pico_view/src/pico_view.h` changes, the header, the Rust exports
and the Dart bindings have to move together:

```sh
cd packages/pico_view && dart run ffigen --config ffigen.yaml
```

When the `proto/` schemas change, regenerate all three sides — Dart:

```sh
cd packages/pico_view
protoc --dart_out=lib/src/gen -I ../../proto pv_ffi.proto pv_wire.proto
```

The Rust side generates from `proto/` at build time; the firmware's nanopb
bindings are regenerated with `firmwares/esp32p4/gen_proto.sh`.

## Releasing

Two independent tag tracks, because the engine binaries and the Dart package
version on their own schedules:

| Tag | Workflow | Effect |
| --- | --- | --- |
| `engine-v*` | [`release.yml`](.github/workflows/release.yml) | Builds all five desktop targets, publishes them as a GitHub Release, and writes the tag + checksums back into `packages/pico_view/native/engine.lock` on the default branch. |
| `v*` | [`publish.yml`](.github/workflows/publish.yml) | Publishes `packages/pico_view` to pub.dev. Refuses to run if the tag and `pubspec.yaml` version disagree. |

Cut an `engine-v*` release first when the Rust engine changed, so the `v*`
package release ships pinned to it.

## License

Apache-2.0. See [LICENSE](LICENSE).
