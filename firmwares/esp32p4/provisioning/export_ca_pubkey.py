#!/usr/bin/env python3
"""Print the vendor-CA public key as the Rust constant embedded in the engine.

Usage: export_ca_pubkey.py <vendor_ca_pub.pem>   (public or private PEM both work)
"""
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def load_pubkey(path: str) -> ec.EllipticCurvePublicKey:
    data = open(path, "rb").read()
    try:
        key = serialization.load_pem_public_key(data)
    except ValueError:
        key = serialization.load_pem_private_key(data, password=None).public_key()
    if not isinstance(key, ec.EllipticCurvePublicKey) or key.curve.name != "secp256r1":
        raise SystemExit("expected a P-256 (secp256r1) key")
    return key


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sec1 = load_pubkey(sys.argv[1]).public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint
    )
    assert len(sec1) == 65 and sec1[0] == 0x04
    rows = [", ".join(f"0x{b:02x}" for b in sec1[i : i + 11]) for i in range(0, 65, 11)]
    body = "\n".join(f"    {r}," for r in rows)
    print("// Vendor-CA public key (SEC1 uncompressed). Regenerate with")
    print("// firmwares/esp32p4/provisioning/export_ca_pubkey.py.")
    print(f"pub const CA_PUBKEY_SEC1: [u8; 65] = [\n{body}\n];")


if __name__ == "__main__":
    main()
