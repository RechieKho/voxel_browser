#pragma once

#include <string>

// The verified identity of a player on an authenticating server (auth.md
// §5.5/§6). Engine-internal bag of *user data only*: no token, ever. `issuer`
// + `subject` is the identity key; Lua sees provider/subject/name/claims.

namespace vb::net {

struct LoginData {
	std::string provider; // "oidc" | "keycloak" | "firebase"
	std::string issuer; // engine-internal, never exposed to Lua
	std::string subject;
	std::string name; // the in-game name (after collision suffixing)
	std::string claims_json; // JSON object of the allowlisted claims
};

} // namespace vb::net
