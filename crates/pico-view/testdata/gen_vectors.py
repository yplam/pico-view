#!/usr/bin/env python3
"""Regenerate the attestation test vectors used by src/auth.rs unit tests.

Creates a TEST-ONLY CA + device key (never used for real units) and emits, in
this directory:
  test_ca_pub.sec1   65-byte uncompressed CA public key
  other_ca_pub.sec1  a different CA, for wrong-CA negative tests
  nonce.bin          the 32-byte challenge the response answers
  cert.bin           a valid 157-byte pv_device_cert_t (v2, ECDSA P-256)
  sig.bin            the 64-byte ECDSA challenge signature (r||s) for cert.bin

Layouts must match main/protocol.h; the signing rules match main/auth.c and
provisioning/make_cert.py. Run with the ESP-IDF venv python (has cryptography).
"""
import hashlib
import struct
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

CERT_CONTEXT = b"PVUS-DEVCERT-V2"
AUTH_CONTEXT = b"PVUS-ATTEST-V2"
HERE = Path(__file__).resolve().parent


def ecdsa_rs(key, msg: bytes) -> bytes:
    """ECDSA P-256/SHA-256 signature as raw 64-byte r||s (big-endian)."""
    r, s = decode_dss_signature(key.sign(msg, ec.ECDSA(hashes.SHA256())))
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def sec1(key) -> bytes:
    return key.public_key().public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint
    )


def main() -> None:
    ca = ec.generate_private_key(ec.SECP256R1())
    other_ca = ec.generate_private_key(ec.SECP256R1())

    # Deterministic nonce so the vector files stay self-consistent.
    nonce = hashlib.sha256(b"pico-view attest test nonce").digest()

    # P-256 device key; cert signed by the (ECDSA) test CA.
    dev = ec.generate_private_key(ec.SECP256R1())
    pubkey = sec1(dev)  # 65-byte SEC1 uncompressed point
    assert len(pubkey) == 65 and pubkey[0] == 0x04

    # TBS: version(1) reserved(3) device_id(16) issued_at(4) expires_at(4) pubkey(65).
    tbs = struct.pack("<Bxxx16sII", 2, b"PV4-TEST0001", 20260101, 0) + pubkey
    assert len(tbs) == 93, len(tbs)
    cert = tbs + ecdsa_rs(ca, CERT_CONTEXT + tbs)
    assert len(cert) == 157, len(cert)

    # Challenge signature: device signs sha256(AUTH_CONTEXT || nonce || pubkey).
    # p256/cryptography hash the message themselves, so pass the preimage.
    sig = ecdsa_rs(dev, AUTH_CONTEXT + nonce + pubkey)
    assert len(sig) == 64, len(sig)

    (HERE / "test_ca_pub.sec1").write_bytes(sec1(ca))
    (HERE / "other_ca_pub.sec1").write_bytes(sec1(other_ca))
    (HERE / "nonce.bin").write_bytes(nonce)
    (HERE / "cert.bin").write_bytes(cert)
    (HERE / "sig.bin").write_bytes(sig)
    print("vectors written to", HERE)


if __name__ == "__main__":
    main()
