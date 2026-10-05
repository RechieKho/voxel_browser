#pragma once

#include <map>
#include <string>
#include <string_view>

#include "vb/auth/crypto.hpp"

namespace vb::auth {

using KeySet = std::map<std::string, PublicKey>; // kid -> key

struct JwksParse {
	KeySet keys;
	std::string error; // empty if at least one usable key was parsed
	std::size_t skipped = 0; // keys ignored (unsupported kty/alg/size, bad fields)
};

// Parses a JWKS document. Unusable entries (wrong `kty`/`crv`, `use` other
// than "sig", RSA < 2048 bits, missing `kid`) are skipped, not fatal, so one
// odd key cannot take the whole set down; zero usable keys is an error.
JwksParse parse_jwks(std::string_view json);

} // namespace vb::auth
