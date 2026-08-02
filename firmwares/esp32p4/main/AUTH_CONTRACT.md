# Attestation contract — what a firmware fork must keep to stay "genuine"

This is open hardware: fork the firmware, rewire the panel, change anything you
like. **One small piece is what lets the official host software (nano-dash)
recognize your board as a genuine vendor-provisioned unit.** Keep it and your
fork stays attestable; drop it and your board still works exactly the same — the
host just labels it *unverified* (see below). Nothing here is required to make
the display or touch work, and nothing on the link is refused on the result.

## Why a fork can attest at all

Device identity lives in **hardware, not firmware**:

- a per-device **ECDSA P-256** private key burned into a read-protected eFuse
  key block (purpose `ECDSA_KEY_P256`) and used only by the on-chip **ECDSA
  peripheral** — the private scalar never appears in clear, even with full
  flash/JTAG access, and it **survives a full flash erase**;
- a vendor-CA-signed **device certificate** in the `devid` flash partition.

Both were written by the vendor at provisioning; the eFuse key survives
reflashing *and* a flash erase (an erase only loses the cert, which is
re-issuable from the same key). *Any* firmware running on the chip can ask the
ECDSA peripheral to sign with that key and can read the cert from `devid`. So
attestation proves the **board** is genuine; it says nothing about which
firmware is running (that's the deliberate trade for allowing forks — there is
no Secure Boot).

The CA that signs the cert is also ECDSA P-256.

## The four things to preserve

1. **The `devid` data partition** in your `partitions.csv` (subtype `0x40` at
   its current offset). It holds the CA-signed cert; wiping it or dropping it
   from the table makes the unit unprovisioned (but the eFuse key survives, so
   re-writing a cert re-provisions it).
2. **The eFuse key** — untouched. You never write it; provisioning did. Locate
   it by *purpose* (`ESP_EFUSE_KEY_PURPOSE_ECDSA_KEY_P256`), not a fixed block
   index.
3. **The signing behavior** in `auth.c` — sign challenges with the eFuse ECDSA
   key via PSA (`psa_sign_hash` over the on-chip ECDSA peripheral).
4. **The wire exchange** — answer `AuthChallenge` with an `AuthResponse` and
   advertise the auth capability in `HelloAck` when provisioned.

The easiest path: keep `auth.c`, `auth.h`, `i2c_bus`/panel aside, and the
attestation handling in `main.c` as-is. Everything below is only needed if you
reimplement the signing yourself.

## The exact bytes the host verifies

Constants from `protocol.h`:

```
PV_CERT_CONTEXT = "PVUS-DEVCERT-V2"   (15 bytes, no NUL)
PV_AUTH_CONTEXT = "PVUS-ATTEST-V2"    (14 bytes, no NUL)

cert signature  (CA = ECDSA P-256, made at provisioning):
    ECDSA_P256( SHA-256( PV_CERT_CONTEXT || cert_bytes[0..93] ) )
challenge sig   (device = ECDSA P-256 via the on-chip peripheral, per open):
    ECDSA_P256( SHA-256( PV_AUTH_CONTEXT || nonce(32) || device_pubkey(65) ) )
```

Both signatures are a 64-byte `r‖s` pair (32+32, big-endian).

Device certificate (`pv_device_cert_t`, packed, 157 bytes, written to `devid`):

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `cert_version` | 2 |
| 1 | 3 | `reserved` | |
| 4 | 16 | `device_id` | ASCII, NUL-padded (e.g. `PV4-000123`) |
| 20 | 4 | `issued_at` | unix seconds (informational) |
| 24 | 4 | `expires_at` | unix seconds; 0 = never |
| 28 | 65 | `device_pubkey` | SEC1 uncompressed P-256 point (`0x04 ‖ X ‖ Y`) |
| 93 | 64 | `ca_signature` | ECDSA P-256 over bytes `[0, 93)` (see `PV_CERT_CONTEXT`) |

The `devid` partition stores this behind an 8-byte container header
(`magic "PVID"` LE + `fmt_version = 2` + 3 reserved), then the cert (157 B) —
and nothing else, since the identity key lives in eFuse, not flash; see
`auth.c`.

The host verifier is `crates/pico-view/src/auth.rs` — read it alongside this;
the two are the same contract from opposite ends.

## If the host does NOT recognize your board

**Nothing happens.** The board connects, the panel lights up, touch works — the
host just reports `verified = false` and no device id on its link event, which
the UI may show as a label. A bare board you provisioned yourself, a unit whose
`devid` is blank, a fork that dropped `auth.c` entirely, and a board that
presents a certificate that fails to verify all land in the same place: driven
normally, labelled unverified.

Attestation answers "is this one of ours?" for the vendor's own benefit
(support, RMA, telling a real unit from a lookalike). It is deliberately **not**
a license check, and the host has no code path that refuses a device over it.

Want your *own* attestation ecosystem (your own CA, your own host build)? Run
`provisioning/gen_ca.sh` to make your own CA, provision boards against it, and
paste your CA public key into `auth.rs` (`CA_PUBKEY_SEC1`) in your host fork.
Only the CA *public* key ships in software — your fork then drives the boards you
provisioned against your CA.
