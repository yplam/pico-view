# `pico-view` engine binaries

Nothing in this directory is committed except `engine.lock` and this file.

The engine source lives in this repo under `crates/pico-view`, but the built
libraries are not checked in — five desktop targets is ~9 MB of binaries, and git
would keep a full copy of every refresh forever. Instead `hook/build.dart`
downloads the build for the current target from a GitHub Release and verifies it
against the SHA-256 pinned in `engine.lock`.

Releases come from `.github/workflows/release.yml`, triggered by pushing a `v*`
tag. It builds all five targets and publishes them as raw, triple-named assets
plus a `SHA256SUMS`:

```
libpico_view-x86_64-unknown-linux-gnu.so
libpico_view-aarch64-unknown-linux-gnu.so
libpico_view-x86_64-apple-darwin.dylib
libpico_view-aarch64-apple-darwin.dylib
pico_view-x86_64-pc-windows-msvc.dll
```

To move the package onto a new engine build, bump `tag` in `engine.lock` and
paste that release's `SHA256SUMS` into it verbatim.

The downloaded library is bundled as the
`package:pico_view/src/pico_view_bindings_generated.dart` code asset, which is
what the `@ffi.Native` lookups in the generated bindings resolve against. The
engine talks to the panel over a **driverless USB** link (WinUSB on Windows,
libusb on macOS, a udev rule on Linux), so no separate runtime driver ships
alongside it.

## Linking a local engine build

One override bypasses the download, gitignored and not digest-checked:

**`engine.local`** — a file holding the path to a cdylib, relative to this
directory or absolute. `./build.sh` at the repo root builds the engine and
writes it for you.
