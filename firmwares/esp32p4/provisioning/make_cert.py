#!/usr/bin/env python3
"""Build a pico-view device certificate (`devid` partition image).

Emits a fmt-2 container + pv_device_cert_t (157 B) -- nothing else: the identity
key is an ECDSA P-256 private key burned into eFuse, so no key material lives in
flash. Needs only the device *public* key (P-256) and the vendor CA private key,
which makes build_cert() equally usable by a future licensing/restore server
that reissues a cert for a known device pubkey.

Layout must match pv_device_cert_t / the PVID container in main/protocol.h and
main/auth.c. The CA (ECDSA P-256) signs SHA-256(PV_CERT_CONTEXT || TBS).

Usage:
  make_cert.py --device-id PV4-000123 --device-key device_ec.pem \
               --ca vendor_ca.pem --out devid.bin \
               [--issued-at UNIX] [--expires-at UNIX]
"""
import argparse
import struct
import sys
import time

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

CERT_CONTEXT = b"PVUS-DEVCERT-V2"  # keep in sync with protocol.h
DEVID_MAGIC = 0x44495650  # "PVID" LE
DEVICE_ID_LEN = 16
PUBKEY_LEN = 65  # SEC1 uncompressed P-256 point

CERT_VERSION = 2


def _ca_sign(ca: ec.EllipticCurvePrivateKey, tbs: bytes) -> bytes:
    r, s = decode_dss_signature(ca.sign(CERT_CONTEXT + tbs, ec.ECDSA(hashes.SHA256())))
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def _device_id_bytes(device_id: str) -> bytes:
    did = device_id.encode("ascii")
    if not 1 <= len(did) <= DEVICE_ID_LEN:
        raise SystemExit(f"device id must be 1..{DEVICE_ID_LEN} ASCII chars")
    return did.ljust(DEVICE_ID_LEN, b"\0")


def _sec1(pub: ec.EllipticCurvePublicKey) -> bytes:
    sec1 = pub.public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint
    )
    if len(sec1) != PUBKEY_LEN or sec1[0] != 0x04:
        raise SystemExit("device public key must be an uncompressed P-256 point")
    return sec1


def _container(fmt_version: int, payload: bytes) -> bytes:
    # PVID container header: magic(u32) | fmt_version(u8) | reserved[3].
    return struct.pack("<IBxxx", DEVID_MAGIC, fmt_version) + payload


def build_cert(device_id: str, device_pub: ec.EllipticCurvePublicKey,
               ca: ec.EllipticCurvePrivateKey, issued_at: int,
               expires_at: int = 0) -> bytes:
    # TBS: version(1) reserved(3) device_id(16) issued_at(4) expires_at(4) pubkey(65).
    tbs = struct.pack("<Bxxx16sII", CERT_VERSION, _device_id_bytes(device_id),
                      issued_at, expires_at) + _sec1(device_pub)
    assert len(tbs) == 93, len(tbs)
    cert = tbs + _ca_sign(ca, tbs)
    assert len(cert) == 157, len(cert)
    return _container(CERT_VERSION, cert)  # 8 + 157 = 165 bytes


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--device-id", required=True)
    ap.add_argument("--device-key", required=True,
                    help="device P-256 key PEM (public or private; only the "
                         "public point is used)")
    ap.add_argument("--ca", required=True, help="vendor CA private key PEM")
    ap.add_argument("--out", required=True)
    ap.add_argument("--issued-at", type=int, default=int(time.time()))
    ap.add_argument("--expires-at", type=int, default=0,
                    help="unix expiry; 0 = never (default)")
    args = ap.parse_args()

    ca = serialization.load_pem_private_key(open(args.ca, "rb").read(), password=None)
    if not isinstance(ca, ec.EllipticCurvePrivateKey) or ca.curve.name != "secp256r1":
        raise SystemExit("CA key must be P-256 (secp256r1)")

    data = open(args.device_key, "rb").read()
    try:
        dev_pub = serialization.load_pem_public_key(data)
    except ValueError:
        dev_pub = serialization.load_pem_private_key(data, password=None).public_key()
    if not isinstance(dev_pub, ec.EllipticCurvePublicKey) or dev_pub.curve.name != "secp256r1":
        raise SystemExit("device key must be a P-256 (secp256r1) key")

    blob = build_cert(args.device_id, dev_pub, ca, args.issued_at, args.expires_at)

    with open(args.out, "wb") as f:
        f.write(blob)
    print(f"wrote {args.out} ({len(blob)} bytes) for '{args.device_id}'",
          file=sys.stderr)


if __name__ == "__main__":
    main()
