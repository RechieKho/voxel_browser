#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Compact-JWS plumbing for the server-side token verifier (auth.md §5.3):
// base64url and the structural parse of `header.payload.signature`. No
// cryptography here; the parse never trusts anything it reads.

namespace vb::auth {

// Hard cap on a token we are willing to look at (matches C2S_Auth's cap).
inline constexpr std::size_t kMaxJwtBytes = 16u * 1024u;

// RFC 4648 §5, unpadded. Decoding rejects padding, whitespace, the standard
// alphabet's '+' '/' and any non-canonical trailing bits.
std::string base64url_encode(std::span<const std::uint8_t> data);
std::optional<std::vector<std::uint8_t>> base64url_decode(std::string_view in);

// The parts of a compact JWS, with header and payload kept as raw JSON text
// (decoded from base64url, not yet interpreted) so this header stays free of
// the JSON library.
struct ParsedJws {
	std::string header_json;
	std::string payload_json;
	std::string signing_input; // "<b64 header>.<b64 payload>", what the signature covers
	std::vector<std::uint8_t> signature;
};

struct JwsParse {
	std::optional<ParsedJws> jws;
	std::string error; // operator-facing; never contains token bytes
};

JwsParse parse_jws(std::string_view token);

// "<jwt len=N>" -- the only form in which a token may appear in a log line.
std::string redact_token(std::string_view token);

} // namespace vb::auth
