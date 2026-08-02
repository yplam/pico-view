# Deployment guide — pico-view ESP32-P4 firmware

End-to-end runbook: **build → flash → update over OTA.**

This is **open hardware**. The firmware ships with no image signing and no
Secure Boot, so anyone can build and flash their own — that is the intended
workflow, not an escape hatch. There is no per-device provisioning step and no
vendor lock-down: any board running this firmware is driven by any host.

Cross-links, not repeated here:

- [`README.md`](README.md) — architecture, wire protocol, partition/OTA design.

---

## 0. Prerequisites

```sh
. ~/esp/esp-idf/export.sh          # IDF 5.5.x venv on PATH (idf.py, esptool, espefuse)
cd firmware/esp32p4-pico-view
idf.py set-target esp32p4          # first time only (downloads esp_tinyusb etc.)
```

Flash/console use the board's **USB-Serial-JTAG** port; the high-speed
**USB-OTG** port is the vendor-bulk data link to the host and the field OTA path.

---

## 1. Build & flash (everyone)

No keys, nothing to generate:

```sh
idf.py menuconfig          # set GPIOs to match your board wiring (first time)
idf.py build               # plain, reflashable image -- no signing
idf.py -p $PORT flash monitor
```

A plain `idf.py flash` writes the app to `factory` and leaves `otadata` empty, so
the board boots from `factory`. That is fine — recovery behaviour is gated on the
`PV_RECOVERY` build flag, not on which partition runs, so a normal build behaves
fully even from `factory`.

**Forking:** change anything you like — no contract to keep, nothing to
re-provision.

One firmware source serves every panel — geometry/rotation/touch all come from
the host `CONFIG` message, so **one app binary drives any supported panel**.

Image roles:

| Image | Build | Flashed to | Role |
|---|---|---|---|
| **App** | `idf.py build` | `factory` (dev) / `ota_0` (shipped) | panel + touch + USB + OTA |
| **Recovery** | `idf.py -DPV_RECOVERY=ON build` | `factory` | minimal USB + OTA fallback (anti-brick) |
| **Bootloader + partition table** | any build | `0x2000` / `0x8000` | flashed once at the bench; **never** OTA'd |

Flash layout ([`partitions.csv`](partitions.csv), 8 MB): `factory` (1 MB
recovery) + `ota_0`/`ota_1` (2 MB each) + `otadata` + `assets` (face pack).

---

## 2. Host engine & field OTA

Field units expose only the USB-OTG port. Updates stream an app image over the
vendor-bulk link into the passive slot; the device verifies the whole-image
SHA-256 before switching slots and rebooting. A freshly-flashed app boots
pending-verify and only commits after a successful host handshake, so a bad image
rolls back. The image is the same `esp32p4-pico-view.bin` you built in §1.

The OTA transport lives in the host engine
([`crates/pico-view/src/esp32p4.rs`](../../crates/pico-view/src/esp32p4.rs),
`OTA_BEGIN`/`OTA_DATA`/`OTA_END`), driven from the app; progress comes back as
`OtaStatus` events.

The NanoDash app exposes this over USB-OTG from **Settings → Advanced → Firmware
update**: pick the `esp32p4-pico-view.bin` from §1, confirm, and it streams the
image with a live progress bar (the tile is disabled until a panel is
connected). The image is unchanged — the picker is just a front end for the same
`OtaStart` request.

Build the host engine:

```sh
cd ../.. && ./build.sh          # builds crates/pico-view and stages the library for the Flutter package
```

If a unit ever wedges, invalid `otadata` — or five consecutive boots with no
host handshake — boots the `factory` image, from which you can reflash an app
slot over OTA; no recovery button, no bench access required.

---

## Quick reference

**Build & flash (open, no keys)**
```sh
idf.py build
idf.py -p $PORT flash monitor
```
