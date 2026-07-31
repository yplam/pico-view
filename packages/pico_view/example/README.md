# pico_view_example

Mirrors a clock to a pico-view panel and feeds physical touch back into the same
widget tree — tap the panel and the face switches, because the touch arrives as
an ordinary pointer event in `ClockFace`.

With no panel attached the app still runs: frames are dropped, and the status
line under the clock reports the link state.

```sh
flutter config --enable-native-assets   # once
flutter run -d linux                    # or macos / windows
```

On Linux, non-root access to the panel needs a udev rule — see "Connecting the
panel" in the [package README](../README.md). To run against an engine built
from `crates/` instead of the release
pinned in `../native/engine.lock`, run `./build.sh` at the repo root — it writes
the `../native/engine.local` override the build hook reads first.
