//! Device attestation: recognize whether the connected unit is genuine vendor hardware.
//!
//! **Advisory only.** Nothing on the link is gated on the outcome: a board that
//! fails to attest — self-built, unprovisioned, or answering nonsense — is
//! driven exactly like a genuine one, and the result surfaces as the `verified`
//! flag on the CONNECTED link event and nowhere else.
//!
//! Mirrors the attestation section of `firmwares/esp32p4/main/protocol.h`.
//! After `HelloAck`, [`crate::esp32p4`] sends a fresh 32-byte nonce
//! (`AuthChallenge`) and the device answers (`AuthResponse`) with its
//! factory-provisioned certificate plus a signature over the challenge produced
//! by its eFuse-resident ECDSA identity key. The certificate travels as opaque
//! bytes in the protobuf message: its fixed layout is its own versioned format
//! (parsed at the offsets below), independent of the wire schema. We verify two
//! signatures:
//!
//! 1. the certificate against the vendor-CA public key embedded below, then
//! 2. the challenge signature against the certificate's device public key.
//!
//! The device key and the CA are both **ECDSA P-256** (SHA-256). The device key
//! is a per-chip private key burned into a read-protected eFuse block and used
//! only by the on-chip ECDSA peripheral, so it survives a full flash erase.
//!
use crate::{PanelSpec, PicoViewConfig};
use p256::ecdsa::signature::Verifier;
use p256::ecdsa::{Signature, VerifyingKey};

// Vendor-CA public key.
pub const CA_PUBKEY_SEC1: [u8; 65] = [
    0x04, 0x8c, 0x58, 0xe5, 0x1c, 0x49, 0x0c, 0x93, 0x90, 0xa0, 0x6a,
    0x67, 0xaf, 0x04, 0xbb, 0xe0, 0x92, 0xdc, 0x56, 0x0e, 0x4a, 0x45,
    0xa0, 0xb3, 0xe1, 0xa5, 0xa9, 0x3a, 0xe3, 0x58, 0xd6, 0x67, 0x91,
    0x79, 0x70, 0x4a, 0x79, 0xfb, 0x92, 0x4e, 0x81, 0xcb, 0xc9, 0x71,
    0xba, 0xae, 0x9b, 0x5a, 0x3b, 0x1b, 0x31, 0xa1, 0x81, 0x00, 0xb8,
    0xbe, 0x35, 0x86, 0xa1, 0xdc, 0xf8, 0xcc, 0xc0, 0x4b, 0x1a,
];

// Context literals (see protocol.h PV_CERT_CONTEXT / PV_AUTH_CONTEXT).
const CERT_CONTEXT: &[u8] = b"PVUS-DEVCERT-V2";
const AUTH_CONTEXT: &[u8] = b"PVUS-ATTEST-V2";

pub const NONCE_LEN: usize = 32;
const DEVICE_ID_LEN: usize = 16;
const CA_SIG_LEN: usize = 64; // vendor CA (ECDSA P-256) signature: r||s.

const EC_PUBKEY_LEN: usize = 65; // SEC1 uncompressed P-256 point.
const EC_SIG_LEN: usize = 64; // ECDSA P-256 signature: r||s.
const PUBKEY_OFF: usize = 28; // device_pubkey offset within the cert.
// Cert (`pv_device_cert_t`): header(4) + device_id(16) + issued_at(4) +
// expires_at(4) + device_pubkey(65) = TBS, then the CA's ECDSA signature.
const CERT_TBS_LEN: usize = PUBKEY_OFF + EC_PUBKEY_LEN; // 93
const CERT_LEN: usize = CERT_TBS_LEN + CA_SIG_LEN; // 157

/// One `sha256(context || parts...)` ECDSA P-256 verification against a SEC1
/// public key. `p256`'s [`Verifier`] hashes the message itself, so we pass the
/// exact preimage the signer hashed. Used for both the CA-over-cert signature
/// and the device-over-challenge signature (same primitive, different key).
fn verify_p256(pubkey_sec1: &[u8], parts: &[&[u8]], sig: &[u8]) -> Result<(), String> {
    let key = VerifyingKey::from_sec1_bytes(pubkey_sec1)
        .map_err(|e| format!("bad public key: {e}"))?;
    let sig = Signature::from_slice(sig).map_err(|e| format!("bad signature encoding: {e}"))?;
    let msg: Vec<u8> = parts.concat();
    key.verify(&msg, &sig).map_err(|_| "signature verification failed".to_string())
}

/// The attested device id from a cert's `device_id[16]` field (offset 4),
/// NUL-trimmed.
fn parse_device_id(cert: &[u8]) -> String {
    let id_raw = &cert[4..4 + DEVICE_ID_LEN];
    let id_end = id_raw.iter().position(|&b| b == 0).unwrap_or(DEVICE_ID_LEN);
    String::from_utf8_lossy(&id_raw[..id_end]).into_owned()
}

/// Verify an `AuthResponse`'s certificate + challenge signature against
/// `ca_sec1` and the challenge `nonce`. Returns the attested device id. Split
/// from [`verify_attestation`] so tests can inject a test CA.
///
/// Both signatures are ECDSA P-256/SHA-256. Verification is two steps: the CA
/// signs `sha256(CERT_CONTEXT || TBS)`, and the device signs the challenge over
/// `sha256(AUTH_CONTEXT || nonce || device_pubkey)`.
pub fn verify_attestation_with(
    ca_sec1: &[u8],
    nonce: &[u8; NONCE_LEN],
    cert: &[u8],
    challenge_sig: &[u8],
) -> Result<String, String> {
    if cert.len() != CERT_LEN {
        return Err(format!("bad certificate length: {} bytes, expected {CERT_LEN}", cert.len()));
    }
    if challenge_sig.len() != EC_SIG_LEN {
        return Err(format!(
            "bad challenge signature length: {} bytes, expected {EC_SIG_LEN}",
            challenge_sig.len()
        ));
    }
    if cert[0] != 2 {
        return Err(format!("unsupported cert version {}", cert[0]));
    }

    // 1. Certificate: CA (ECDSA P-256) signs sha256(CERT_CONTEXT || TBS).
    let tbs = &cert[..CERT_TBS_LEN];
    let ca_sig = &cert[CERT_TBS_LEN..];
    verify_p256(ca_sec1, &[CERT_CONTEXT, tbs], ca_sig)
        .map_err(|e| format!("device certificate not signed by our CA: {e}"))?;

    // 2. Challenge: device (ECDSA P-256) signs sha256(AUTH_CONTEXT || nonce ||
    //    device_pubkey), using the pubkey carried in the just-verified cert.
    let device_pubkey = &cert[PUBKEY_OFF..PUBKEY_OFF + EC_PUBKEY_LEN];
    verify_p256(device_pubkey, &[AUTH_CONTEXT, nonce, device_pubkey], challenge_sig)
        .map_err(|e| format!("challenge signature invalid: {e}"))?;

    Ok(parse_device_id(cert))
}

/// Verify against the production vendor CA embedded in this build.
pub fn verify_attestation(
    nonce: &[u8; NONCE_LEN],
    cert: &[u8],
    challenge_sig: &[u8],
) -> Result<String, String> {
    verify_attestation_with(&CA_PUBKEY_SEC1, nonce, cert, challenge_sig)
}

/// Open the device, run the handshake + attestation, then close it, and return
/// the attested device id. The manufacturing/provisioning check behind
/// `examples/attest_check.rs`.
pub fn open_for_check(cfg: &PicoViewConfig, panel: &PanelSpec) -> Result<String, String> {
    crate::init_logging();
    let (_, _, identity) =
        crate::esp32p4::Esp32P4Transport::open(cfg, panel).map_err(|e| e.to_string())?;
    match identity.device_id {
        Some(id) if identity.verified => Ok(id),
        _ => Err("device opened but is not a verified genuine unit (unprovisioned?)".to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Vectors produced by testdata/gen_vectors.py (TEST-only CA, never used for
    // real units). `cert.bin` is the 157-byte v2 `pv_device_cert_t`; `sig.bin`
    // is the 64-byte ECDSA challenge signature over the shared `nonce.bin`.
    const TEST_CA: &[u8] = include_bytes!("../testdata/test_ca_pub.sec1");
    const OTHER_CA: &[u8] = include_bytes!("../testdata/other_ca_pub.sec1");
    const CERT: &[u8] = include_bytes!("../testdata/cert.bin");
    const SIG: &[u8] = include_bytes!("../testdata/sig.bin");

    fn nonce() -> [u8; NONCE_LEN] {
        include_bytes!("../testdata/nonce.bin").to_owned()
    }

    #[test]
    fn valid_response_verifies() {
        let id = verify_attestation_with(TEST_CA, &nonce(), CERT, SIG).unwrap();
        assert_eq!(id, "PV4-TEST0001");
    }

    #[test]
    fn wrong_ca_rejected() {
        let err = verify_attestation_with(OTHER_CA, &nonce(), CERT, SIG).unwrap_err();
        assert!(err.contains("not signed by our CA"), "{err}");
    }

    #[test]
    fn production_ca_rejects_test_cert() {
        // The shipped constant must not accept certs from the test CA.
        assert!(verify_attestation(&nonce(), CERT, SIG).is_err());
    }

    #[test]
    fn wrong_nonce_rejected() {
        let mut n = nonce();
        n[0] ^= 1;
        let err = verify_attestation_with(TEST_CA, &n, CERT, SIG).unwrap_err();
        assert!(err.contains("challenge signature"), "{err}");
    }

    #[test]
    fn tampered_cert_rejected() {
        // device_id (4), device_pubkey (28), last TBS byte (92) tampering must
        // break the CA signature over the TBS.
        for byte in [4usize, 28, 92] {
            let mut c = CERT.to_vec();
            c[byte] ^= 1;
            assert!(verify_attestation_with(TEST_CA, &nonce(), &c, SIG).is_err());
        }
    }

    #[test]
    fn transplanted_signature_rejected() {
        // A valid cert paired with a signature over different bytes must fail.
        let mut s = SIG.to_vec();
        s[0] ^= 1;
        let err = verify_attestation_with(TEST_CA, &nonce(), CERT, &s).unwrap_err();
        assert!(err.contains("challenge signature"), "{err}");
    }

    #[test]
    fn wrong_length_inputs_rejected() {
        assert!(verify_attestation_with(TEST_CA, &nonce(), &CERT[..100], SIG).is_err());
        assert!(verify_attestation_with(TEST_CA, &nonce(), CERT, &SIG[..32]).is_err());
        assert!(verify_attestation_with(TEST_CA, &nonce(), &[], &[]).is_err());
    }

    #[test]
    fn unsupported_cert_version_rejected() {
        let mut c = CERT.to_vec();
        c[0] = 9; // bogus cert_version
        let err = verify_attestation_with(TEST_CA, &nonce(), &c, SIG).unwrap_err();
        assert!(err.contains("unsupported cert version"), "{err}");
    }
}
