# pico-view ESP32-P4 display backend firmware

Firmware for an **ESP32-P4-Function-EV-Board** that turns the P4 into a USB-HS
display bridge for [pico-view](../../README.md).
The host streams dirty RGB565 rectangles over a driverless **vendor-bulk** USB
endpoint; the firmware writes them straight into an SPI panel's GRAM and returns
capacitive-touch events on the bulk IN endpoint.

The panel is driven over **QSPI** (4 data lines, no D/C line) at 40 MHz: `esp_lcd`
quad mode puts the 8-bit command on a single data line and the RGB565 pixel
payload on all four, with each command framed by a 1-byte ST77916 opcode (`0x02`
register write, `0x32` RAMWR color write).

## Build & flash

```sh
. ~/esp/esp-idf/export.sh
cd firmwares/esp32p4
idf.py set-target esp32p4      # first time only (downloads esp_tinyusb)
idf.py menuconfig              # set the GPIO pins to match your board
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # flash over the USB-Serial-JTAG port
```

Flash/console use the board's **USB-Serial-JTAG** port; the high-speed **USB-OTG**
port is the vendor-bulk data link to the host.

## Wire protocol v2 (host ⇄ device)

Framing is defined in [`main/protocol.h`](main/protocol.h) and the message
schema in [`../../proto/pv_wire.proto`](../../proto/pv_wire.proto) — the Rust
host (prost) and this firmware (nanopb, bounded by `pv_wire.options`) share the
schema; regenerate the C bindings in `main/gen/` with
[`gen_proto.sh`](gen_proto.sh). Every frame in both directions is a 12-byte
little-endian header + payload, reassembled across 512-byte HS bulk packets:

```
magic "PVUS" : u32   type : u16   flags : u16   payload_len : u32
```

| Type | Dir | Payload |
| --- | --- | --- |
| `BLIT` (3) | H→D | raw: `x,y,w,h:u16` + `w*h*2` bytes RGB565-**big-endian**; `flags` bit0 = PRESENT (last rect of frame) |
| `CTRL` (32) | both | ONE protobuf-encoded `picoview.wire.HostToDevice` (OUT) / `DeviceToHost` (IN) |

Types 1–31 are reserved (the v1 per-message ids), so a v2 host talking to v1
firmware — or vice versa — fails cleanly at the HELLO handshake instead of
mis-parsing. Type 33 is retired (a reserved raw-PCM audio frame) and is not
reused. `BLIT` stays raw on purpose: a full 360×360 frame is ~253 KB and
needs no schema evolution.

The CTRL `Config` message drives the panel/touch init, so **one firmware image
serves every panel** — geometry, rotation, invert and touch axis swap/flip all
come from the host preset (`panels.rs`); the device answers with `ConfigAck`.
Touch samples come back as `Touch` messages. Pixel bytes are RGB565 big-endian,
streamed verbatim to RAMWR (the order ST7789/ST77916 latch), so there is no
per-pixel conversion on the device.

## Driverless USB

The device is a single vendor-class (0xFF) interface with one bulk OUT (0x01) +
one bulk IN (0x81) endpoint, advertising **MS OS 2.0 descriptors** with the
`WINUSB` compatible-ID (see [`main/usb_descriptors.c`](main/usb_descriptors.c)):

- **Windows** auto-binds `WinUSB.sys` on first plug — no INF, no Zadig.
- **macOS** claims the vendor interface through libusb natively.
- **Linux** needs a one-line udev rule for non-root access — install
  [`99-pico-view.rules`](99-pico-view.rules) (no kernel module required).

VID/PID is `0x303A:0x839A` (Espressif shared VID + the PID allocated to
pico-view via [espressif/usb-pids](https://github.com/espressif/usb-pids); update
the udev rule's `idProduct` if you ever change it).

## Firmware update (OTA) + recovery

The product board exposes only the USB-HS OTG port (no USB-Serial-JTAG), so field
updates are streamed over the **same vendor-bulk link** into the passive app slot
and committed with the `esp_ota` APIs — no external tool, no re-enumeration during
transfer. See [`main/ota.c`](main/ota.c).

**Flash layout** ([`partitions.csv`](partitions.csv)): `factory` (1 MB recovery
app) + `ota_0`/`ota_1` (2 MB each) + `otadata` + a ~2.9 MB `assets` partition for
the idle-face pack, in 8 MB of flash. Only the app slots are updated;
the bootloader and partition table are **never** OTA'd (the P4 ROM has no
recovery-bootloader fallback in IDF 5.5.x, so an interrupted bootloader write
would brick the unit).

**Protocol** (CTRL messages, [`../../proto/pv_wire.proto`](../../proto/pv_wire.proto)):
`OtaBegin` (size + SHA-256 + version) → N × `OtaData` (`seq` + ≤ 8192 bytes) →
`OtaEnd`. The device verifies the whole-image SHA-256 (`esp_ota_end`) before
switching the boot slot and rebooting. Progress/results come back as
`OtaStatus`. Every v2 firmware accepts updates (no capability bit); the
`HelloAck` carries the running app version.

**Anti-brick, no recovery button** — three nets:
1. *App rollback* — a freshly-flashed app boots in pending-verify and only marks
   itself valid after a successful host handshake; if it crashes first, the
   bootloader reverts to the previous slot. (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`)
2. *Boot-loop detector* — an RTC-retained counter (survives panic/WDT resets)
   redirects boot to `factory` after too many resets with no handshake, covering
   a "valid but later wedged" app.
3. *Factory recovery app* — invalid `otadata`, or more than five consecutive
   boots with no host handshake, boots the `factory` image, which only does USB +
   OTA. Reflash an app slot from there.

**No image signing, no Secure Boot — this is open hardware.** The default build
needs no keys: `idf.py build` produces a plain, reflashable image, and OTA
integrity rests on the whole-image SHA-256 the host announces and the device
checks. Firmware *authenticity* is deliberately not enforced, so you can build
and flash your own fork freely. We intentionally do **not** ship Secure Boot or
flash-encryption config: burning Secure Boot is a one-way door that would
permanently tie a unit to a single signing key and end its ability to run forks
— the opposite of the goal here. Anti-rollback is likewise off — it would make the never-updated `factory` recovery image unbootable after
the first secure-version bump.

**Recovery build** — the trimmed factory image (skips touch/render):

```sh
idf.py -DPV_RECOVERY=ON build   # -> flash to the `factory` partition
```

**Manufacturing** — flash bootloader + partition table + the recovery build to
`factory` + the app to `ota_0`, then write `otadata` to select `ota_0` so a
shipped unit boots the application (not recovery). *Dev note:* a plain
`idf.py flash` writes
the app to `factory` and leaves `otadata` empty, so it boots from `factory`;
that's fine for development — recovery behaviour is gated on the `PV_RECOVERY`
build flag, not on which partition runs, so a normal build behaves fully even
when run from `factory`.

## Bring-up checklist

1. **Enumeration** — `lsusb -v` shows the vendor interface + BOS/MS-OS-2.0; on
   Windows, Device Manager binds WinUSB with no prompt. *Gate: prove driverless
   before relying on it.*
2. **Display** — after a `CONFIG` + full-frame `BLIT`, the ST77916 shows the
   image; partial-rect BLITs land in the right window. Watch for SPI-DMA-from-
   PSRAM issues (the BLIT buffer lives in PSRAM); if pixels are corrupt, route
   the blit buffer through an internal DMA bounce.
3. **Touch** — CST816 events arrive as `TOUCH` records with correct coordinates.
   With `PV_PIN_TOUCH_INT` wired, the touch task wakes on the INT line (falling
   edge) and only polls at 15 ms while a finger is down; set the INT pin to `-1`
   to fall back to pure polling.
4. **Throughput** — the firmware streams BLITs through a 2-deep PSRAM buffer pool
   so the next frame is received off USB while the previous one drains over SPI;
   the device logs a rolling fps figure. Benchmark full-frame fps and raise
   `PV_LCD_PCLK_HZ` once stable.

