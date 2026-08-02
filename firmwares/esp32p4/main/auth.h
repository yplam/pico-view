// Device attestation: lets the host recognize this unit as genuine hardware.
//
// Nothing here is required to make the display or touch work -- a board with no
// provisioned identity reports that fact and the host drives it exactly the
// same, just labelled unverified. See AUTH_CONTRACT.md.
//
// Provisioning (see ../provisioning/) burns a per-device ECDSA P-256 private key
// into a read-protected eFuse key block and writes a vendor-CA-signed device
// certificate to the `devid` partition. At runtime the host sends an
// AuthChallenge (fresh nonce) and we reply with the cert plus a
// hardware-produced signature over it. See protocol.h for the signed-message
// layouts, the cert format and the eFuse-key rationale.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

// Call once from app_main, before the rx task starts (the cached state is
// read-only afterwards, so the challenge handler needs no locking). Locates
// the eFuse identity key, loads the cert from `devid`, and cross-checks the
// cert's public key against the key the peripheral actually holds. Logs what
// it finds; never fails the boot -- an unprovisioned unit simply runs without
// the auth capability.
void pv_auth_init(void);

// True when the identity key and a matching cert are both present ->
// the HELLO_ACK advertises the auth capability.
bool pv_auth_provisioned(void);

// Handle an AuthChallenge nonce (rx-dispatcher thread). Always fills *out with
// a well-formed response: status PV_AUTH_OK with cert+signature on success,
// otherwise a diagnostic status with cert/signature zeroed.
void pv_auth_handle_challenge(const uint8_t *payload, uint32_t len,
                              pv_auth_response_t *out);
