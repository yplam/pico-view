#!/usr/bin/env python3
"""Provision one pico-view unit: burn the eFuse ECDSA identity key, flash the cert.

One factory pass, auto-licensed (no activation step). Burns a free key block
(default BLOCK_KEY1) with purpose ECDSA_KEY_P256 -- a read-protected per-device
P-256 private key used only by the on-chip ECDSA peripheral -- then flashes a
vendor-CA-signed certificate to the `devid` partition. Nothing secret ever
touches flash, and the eFuse key survives a full flash erase (only the cert is
lost, and it can be reissued from the same key -- see the registry log below).
Irreversible for the eFuse block it burns; refuses to touch a board whose target
key block is already in use. See README.md.

Usage:
  provision_device.py --port /dev/ttyACM0 --device-id PV4-000123 \
      --ca ~/secure/pico-view-keys/vendor_ca.pem \
      --log ~/secure/pico-view-keys/provisioned.log \
      [--key-block N] [--expires-at UNIX] [--dry-run]

Requires the ESP-IDF venv on PATH (espefuse/esptool) and `cryptography`.
Afterwards verify over USB:  cargo run --example attest_check  -> "AUTH OK".
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

DEVID_OFFSET = 0x13000  # `devid` partition (partitions.csv)


def run(cmd: list[str], dry: bool, capture: bool = False) -> str:
    print("+", " ".join(cmd), file=sys.stderr)
    if dry and not capture:
        return ""
    res = subprocess.run(cmd, check=True, text=True, capture_output=capture)
    return res.stdout if capture else ""


def efuse_summary(port: str) -> dict:
    out = run(["espefuse.py", "--chip", "esp32p4", "--port", port, "summary",
               "--format", "json"], dry=False, capture=True)
    # espefuse prints a banner before the JSON; take the outermost object.
    m = re.search(r"\{.*\}", out, re.S)
    if not m:
        raise SystemExit("could not parse espefuse summary output")
    return json.loads(m.group(0))


def key_block_in_use(summary: dict, block_index: int) -> bool:
    purpose = summary.get(f"KEY_PURPOSE_{block_index}", {}).get("value")
    return purpose not in (None, "USER", 0, "0")


def read_mac(port: str) -> str:
    out = run(["esptool.py", "--chip", "esp32p4", "--port", port, "read_mac"],
              dry=False, capture=True)
    m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", out)
    return m.group(1).lower() if m else "unknown"


def provision(args, here: Path, summary: dict, issued_at: int) -> str:
    block = args.key_block          # e.g. 1 -> BLOCK_KEY1
    if key_block_in_use(summary, block):
        raise SystemExit(f"BLOCK_KEY{block} already has a purpose set -- "
                         "pick a free block with --key-block or refuse")

    with tempfile.TemporaryDirectory(dir="/dev/shm") as tmp:
        key_pem = Path(tmp) / "device_ec.pem"
        devid_bin = Path(tmp) / "devid.bin"

        # Per-device P-256 identity key. Written to tmpfs only (never persistent
        # disk) and shredded with the tempdir at the end of this pass.
        priv = ec.generate_private_key(ec.SECP256R1())
        key_pem.write_bytes(priv.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption()))

        # burn_key read-protects the block for the ECDSA_KEY_P256 purpose.
        # espefuse loads the PEM itself, extracts the raw scalar, and reverses
        # it to little-endian (the "Reverse" attr on ECDSA_KEY_P256) -- so hand
        # it the PEM, not a raw/pre-reversed scalar.
        run(["espefuse.py", "--chip", "esp32p4", "--port", args.port,
             "--do-not-confirm", "burn_key", f"BLOCK_KEY{block}", str(key_pem),
             "ECDSA_KEY_P256"], dry=args.dry_run)
        run([sys.executable, str(here / "make_cert.py"),
             "--device-id", args.device_id, "--device-key", str(key_pem),
             "--ca", args.ca, "--out", str(devid_bin),
             "--issued-at", str(issued_at), "--expires-at", str(args.expires_at)],
            dry=False)
        run(["esptool.py", "--chip", "esp32p4", "--port", args.port,
             "write_flash", hex(DEVID_OFFSET), str(devid_bin)], dry=args.dry_run)

        return priv.public_key().public_bytes(
            serialization.Encoding.X962,
            serialization.PublicFormat.UncompressedPoint).hex()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True)
    ap.add_argument("--device-id", required=True)
    ap.add_argument("--ca", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--key-block", type=int, default=1,
                    help="key block index for the ECDSA identity key (default 1)")
    ap.add_argument("--expires-at", type=int, default=0,
                    help="cert unix expiry; 0 = never (default, auto-license)")
    ap.add_argument("--dry-run", action="store_true",
                    help="print commands without burning/flashing")
    args = ap.parse_args()

    here = Path(__file__).resolve().parent
    summary = efuse_summary(args.port)
    mac = read_mac(args.port) if not args.dry_run else "dry-run"
    issued_at = int(time.time())

    # tmpfs so device private keys never touch persistent storage.
    pubkey_hex = provision(args, here, summary, issued_at)

    if not args.dry_run:
        # Append-only registry: (device_id, mac, pubkey, issued_at). The mac is a
        # label read from the chip -- it is NOT in the signed cert; the pubkey is
        # the anchor and is what a future restore server reissues a cert against.
        stamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
        record = {"device_id": args.device_id, "mac": mac,
                  "pubkey": pubkey_hex, "issued_at": issued_at,
                  "provisioned_at": stamp, "port": args.port}
        with open(args.log, "a") as f:
            f.write(json.dumps(record) + "\n")
        print(f"provisioned '{args.device_id}' (mac {mac}); logged to {args.log}")
        print("verify now: cargo run --example attest_check (in crates/pico-view)")


if __name__ == "__main__":
    main()
