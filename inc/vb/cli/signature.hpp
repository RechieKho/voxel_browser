#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vb/cli/layout.hpp"
#include "vb/cli/store.hpp" // Status

// Release signatures (dev-cli.md section 3.3): release.toml.sig holds the base64
// of a raw Ed25519 signature (RFC 8032, "pure" mode) over the exact bytes of
// release.toml, made with `openssl pkeyutl -sign -rawin` (scripts/sign_release.sh).
// The manifest names every archive's SHA-256, so authenticating it authenticates
// the whole release.

namespace vb::cli {

using PublicKey = std::array<std::uint8_t, 32>;
using SignatureBytes = std::array<std::uint8_t, 64>;

struct TrustPolicy {
	std::vector<PublicKey> keys; // empty = nothing to check against
	// With keys configured: refuse a release that carries no signature.
	bool require = false;
};

// 64 hex characters -> key.
Status parse_public_key(std::string_view hex, PublicKey &out);

// Base64 (surrounding whitespace/newlines allowed) of exactly 64 bytes.
Status decode_signature(std::string_view base64_text, SignatureBytes &out);

bool verify_ed25519(const SignatureBytes &sig, std::string_view message, const PublicKey &key);

// Checks `manifest_text` against `signature_text` (empty = the release has no
// signature). Succeeds when:
//   - no keys are configured (nothing to enforce), or
//   - the signature verifies under any configured key, or
//   - there is no signature and the policy does not require one.
// A signature that is present but wrong is always refused.
Status verify_manifest_signature(std::string_view manifest_text, std::string_view signature_text,
		const TrustPolicy &policy);

// Keys compiled into this vb (release_keys.txt).
std::vector<PublicKey> embedded_public_keys();

// Embedded keys + `trusted_keys = ["<hex>", ...]` from cli.toml; `require` is on
// whenever there are keys, unless cli.toml says `require_signature = false`.
// A malformed key in cli.toml is reported in `warning` and ignored.
TrustPolicy load_trust_policy(const Layout &layout, std::string *warning = nullptr);

} // namespace vb::cli
