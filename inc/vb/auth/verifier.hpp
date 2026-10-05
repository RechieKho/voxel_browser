#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "vb/auth/config.hpp"
#include "vb/auth/jwks.hpp"

// Pure ID-token verification (auth.md §5.3): (config, key set, token,
// expected nonce, now) -> LoginInfo or a precise error. No I/O, no clock.

namespace vb::auth {

inline constexpr std::int64_t kClockSkewSeconds = 60;
inline constexpr std::size_t kMaxClaimsJsonBytes = 8 * 1024;

enum class VerifyError : std::uint8_t {
	kNone,
	kMalformed,
	kAlgorithm, // alg not in {RS256, ES256}, or it contradicts the key type
	kMissingKid,
	kUnknownKid, // not in the key set: the caller may refresh JWKS once
	kBadSignature,
	kIssuer,
	kAudience,
	kAuthorizedParty,
	kExpired,
	kNotYetValid,
	kTooOld, // iat older than max_token_age_seconds
	kNonce,
	kSubject,
	kName,
	kClaims,
};

std::string_view to_string(VerifyError e);

// Engine-internal. Lua only ever sees provider/subject/name/claims (§6);
// issuer and the timestamps never leave C++. The raw token is never stored.
struct LoginInfo {
	Provider provider = Provider::kOidc;
	std::string issuer;
	std::string subject;
	std::string name; // sanitized, 1..32 bytes
	std::string claims_json; // JSON object holding only the allowlisted claims
	std::int64_t issued_at = 0;
	std::int64_t expires_at = 0;
};

struct VerifyResult {
	VerifyError error = VerifyError::kNone;
	std::string detail; // operator-facing, token-free
	LoginInfo login; // valid iff ok()

	bool ok() const { return error == VerifyError::kNone; }
};

// `expected_nonce`: the server nonce for this connection. A `nonce` claim, if
// the token has one, must equal it; refresh-derived and Firebase password
// tokens cannot carry one, and are bounded by the freshness rule instead.
VerifyResult verify_id_token(const AuthConfig &config, const KeySet &keys,
		std::string_view token, std::string_view expected_nonce,
		std::int64_t now_unix);

// Peeks the header `kid` without verifying anything (used to decide whether
// a JWKS refresh could help). nullopt if the token does not parse.
std::optional<std::string> peek_kid(std::string_view token);

// Coarse text safe to show the player; the precise cause stays in the log.
std::string_view public_reason(VerifyError e);

// Player-name sanitizer shared with the name_claim fallback: strips control
// characters, trims, truncates to 32 bytes on a UTF-8 boundary. Empty = unusable.
std::string sanitize_player_name(std::string_view raw);

} // namespace vb::auth
