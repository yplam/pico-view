#!/usr/bin/env bash
# One-time vendor-CA key ceremony. Run OFFLINE; store the output outside any
# repo (see README.md "Key ceremony & custody").
#
#   ./gen_ca.sh ~/secure/pico-view-keys
set -euo pipefail

keydir="${1:?usage: gen_ca.sh <keydir>}"
mkdir -p "$keydir"
chmod 700 "$keydir"

ca="$keydir/vendor_ca.pem"
ca_pub="$keydir/vendor_ca_pub.pem"

if [[ -e "$ca" ]]; then
    echo "refusing to overwrite existing $ca" >&2
    exit 1
fi

umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$ca"
openssl ec -in "$ca" -pubout -out "$ca_pub" 2>/dev/null

echo "CA written to $ca (keep offline; back it up encrypted)"
echo
echo "Rust constant for crates/pico-view/src/auth.rs:"
echo
exec "$(dirname "$0")/export_ca_pubkey.py" "$ca_pub"
