#!/bin/sh
# Sign release.toml. Usage: scripts/sign_release.sh <release.toml> <private-key.pem> [out]
# Writes <release.toml>.sig (base64 of the raw 64-byte Ed25519 signature) unless
# [out] is given, then checks it against the key's own public half.
set -eu
[ $# -ge 2 ] && [ $# -le 3 ] || { echo "usage: $0 <release.toml> <private-key.pem> [out]" >&2; exit 2; }
manifest="$1"; key="$2"; out="${3:-$manifest.sig}"
[ -f "$manifest" ] || { echo "$manifest not found" >&2; exit 1; }
[ -f "$key" ] || { echo "$key not found" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
openssl pkeyutl -sign -inkey "$key" -rawin -in "$manifest" -out "$tmp/sig"
[ "$(wc -c < "$tmp/sig" | tr -d ' ')" = 64 ] || { echo "unexpected signature size" >&2; exit 1; }

openssl pkey -in "$key" -pubout -out "$tmp/pub.pem"
openssl pkeyutl -verify -pubin -inkey "$tmp/pub.pem" -rawin -in "$manifest" -sigfile "$tmp/sig" >/dev/null \
	|| { echo "self-check of the new signature failed" >&2; exit 1; }

openssl base64 -A -in "$tmp/sig" > "$out"
echo >> "$out"
echo "wrote $out"
