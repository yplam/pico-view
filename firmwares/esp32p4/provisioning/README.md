# Device provisioning

Everything that turns a blank ESP32-P4 board into a **genuine, attestable
unit** — a per-device identity key + certificate so official software can
recognize it. This is the *only* eFuse burn (one key block); it does not lock
the firmware down (open hardware — forks are expected), and it does not unlock
anything either: a board that skips all of this works exactly the same, it is
just reported unverified. Read this before touching a soldered board: burning a
key block is irreversible for that block.

## What "genuine" means here

Each unit carries a **device certificate** in the `devid` flash partition
(device id + device P-256 public key, signed by the offline **vendor CA**) plus a
per-device secret protected in eFuse:

A per-device **ECDSA P-256** private key burned into a read-protected eFuse key
block (purpose `ECDSA_KEY_P256`) and used only by the on-chip **ECDSA
peripheral** (157-byte cert, `pv_device_cert_t`). Nothing secret ever touches
flash, and the eFuse key **survives a full flash erase** — an erase only loses
the cert, which is re-issuable from the same key. The CA that signs the cert is
also ECDSA P-256.

The host engine (`crates/pico-view`) embeds only the CA *public* key. On every
open it sends a fresh nonce; the ECDSA peripheral signs it; the host verifies
cert → CA, then signature → cert's device public key. No secret ever ships in
software.

## Key ceremony & custody

Generate once, **offline**, and store outside any repo (encrypted volume plus
an offline backup, e.g. `~/secure/pico-view-keys/`):

| File | Role | Used by |
|---|---|---|
| `vendor_ca.pem` | Signs device certificates | `make_cert.py` at provisioning |
| `vendor_ca_pub.pem` | CA public key | embedded in the Rust engine |
| `provisioned.log` | Append-only record of every unit | support / RMA / revocation |

- `./gen_ca.sh <keydir>` creates the CA pair and prints the Rust constant for
  `crates/pico-view/src/auth.rs` (via `export_ca_pubkey.py`).
- Losing `vendor_ca.pem` means no new units can be provisioned; shipped units
  keep working. Guard it (encrypted volume + offline backup).
- One compromised device key ≠ compromised fleet: keys are per-device, and the
  host can blocklist a leaked `device_id`/pubkey in a future engine release.

There is **no firmware-signing key**: this is open hardware, the firmware is
unsigned and reflashable, and identity is the only thing provisioning writes.

## eFuse key-block budget (6 × 256-bit)

Provisioning burns exactly **one** block (the ECDSA key defaults to KEY1,
override with `--key-block`). Everything else stays free — no Secure Boot, no
flash encryption — so the chip is otherwise untouched and fully reflashable.

| Block | Purpose | Burned when |
|---|---|---|
| KEY1 (BLK5) | `ECDSA_KEY_P256` — device identity key (read-protected) | provisioning |
| KEY0, KEY2–KEY5 | free | — |

## Per-device provisioning

Prereqs: IDF venv on PATH (`. ~/esp/esp-idf/export.sh`), board in download mode
on `$PORT`, firmware already flashed (or flash it afterwards — order doesn't
matter, `pv_auth_init` re-checks every boot).

```
./provision_device.py --port /dev/ttyACM0 --device-id PV4-000001 \
    --ca ~/secure/pico-view-keys/vendor_ca.pem \
    --log ~/secure/pico-view-keys/provisioned.log
```

What it does (idempotence: refuses to run on a board whose target key block is
already burned):

1. Generates the device P-256 key **in tmpfs** (never touches persistent disk).
2. `espefuse burn_key BLOCK_KEY1 <ec_priv_be.bin> ECDSA_KEY_P256`
   (read-protected). espefuse reverses the scalar to little-endian for the ECDSA
   peripheral, so the burned file is the natural big-endian scalar.
3. `make_cert.py` builds the 157-byte cert from the device *public* key and
   writes it to `devid` (`esptool write_flash 0x13000 devid.bin`). No key
   material is stored in flash.
4. Appends the unit to the provisioning log (`device_id`, chip `mac`, `pubkey`,
   `issued_at`), then shreds the temp key.
5. Verify host-side: `cargo run --example attest_check` in `crates/pico-view`
   with the board on USB — it must print `AUTH OK <device-id>`. The firmware also
   self-checks at boot: it exports the eFuse key's public point and compares it
   to the cert's `device_pubkey` (`auth.c` `key_matches_cert`), so a cert copied
   from another chip logs `eFuse key does not match cert` and the unit refuses to
   advertise auth — a mismatched cert cannot silently ship a broken unit.

Notes:
- The registry `mac` is a label read from the chip (`esptool read_mac`); it is
  **not** part of the signed cert. The `pubkey` is the identity anchor and is
  what a future licensing/restore server would reissue a cert against.
- `CONFIG_EFUSE_VIRTUAL` can dry-run the firmware's discovery/error paths, but
  the ECDSA **peripheral** reads real eFuses, so end-to-end attestation needs a
  real burn. A board with only one key block burned stays fully reflashable.
- An unprovisioned board runs fine and is driven normally; it just doesn't
  advertise the auth capability, so the host skips the challenge and reports
  `verified = false` on its link event. A board that presents a cert and fails
  verification ends up in the same place. Attestation is a **label**, not a gate
  — provisioning is about knowing which units are ours (support, RMA, telling a
  real unit from a lookalike), not about deciding what the host will drive.

## No production hardening / lock-down step

This is deliberately absent. Because the product is open hardware, we do **not**
burn Secure Boot, flash encryption, or JTAG/download-disable fuses — doing so
would end the unit's ability to run forked firmware, which is the whole point.
Identity provisioning (one ECDSA key block) is the only eFuse burn. If you
maintain a separate locked SKU, keep that hardening config in a private overlay,
out of this repo.

## What this does and does not protect (honest limits)

Does: proves the **hardware** is a genuine vendor unit to official software;
gives each unit a per-unit cryptographic identity (support / RMA / revocation);
survives firmware forks (identity is in hardware, not firmware).

Does not: prove which firmware is running (there is no Secure Boot — that's the
trade for allowing forks); stop someone patching the distributed engine to skip
the check, writing their own host driver that never asks for attestation, or
relaying challenges to one genuine device they own. Those are inherent to
shipping software that runs on hardware the owner controls.
