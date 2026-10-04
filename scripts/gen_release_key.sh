#!/bin/sh
# Create the Ed25519 key pair used to sign release.toml, and print what to do
# with it. Usage: scripts/gen_release_key.sh <private-key.pem>
#
# The private key never leaves your machine except as the repository secret
# RELEASE_SIGNING_KEY. Needs OpenSSL 3 (or any OpenSSL with Ed25519 support).
set -eu
[ $# -eq 1 ] || { echo "usage: $0 <private-key.pem>" >&2; exit 2; }
key="$1"
[ ! -e "$key" ] || { echo "$key already exists; refusing to overwrite a key" >&2; exit 1; }

umask 077
openssl genpkey -algorithm ed25519 -out "$key"
chmod 600 "$key"

der="$(mktemp)"
trap 'rm -f "$der"' EXIT
openssl pkey -in "$key" -pubout -outform DER -out "$der"
# An Ed25519 SubjectPublicKeyInfo is a fixed 12-byte header + the 32-byte key.
hex="$(od -An -v -tx1 "$der" | tr -d ' \n')"
pub_hex="${hex#302a300506032b6570032100}"
[ ${#pub_hex} -eq 64 ] || { echo "unexpected public key encoding" >&2; exit 1; }
pub_b64="$(openssl base64 -A -in "$der")"

cat <<MSG

Private key written to: $key   (keep it secret; back it up)

1. Public key for vb -> add this line to release_keys.txt:
$pub_hex

2. Public key for the installer -> set DEFAULT_PUBLIC_KEY_B64 in scripts/install.sh to:
$pub_b64

3. Store the private key as the GitHub repository secret RELEASE_SIGNING_KEY:
   gh secret set RELEASE_SIGNING_KEY < $key

Releases built after all three are in place are signed (release.toml.sig), and
every vb built from then on refuses unsigned or wrongly-signed releases.
MSG
