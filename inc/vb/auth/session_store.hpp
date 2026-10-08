#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Client-side persistence for sign-in (auth.md §7): the refresh-token cache
// per (server, issuer, client_id) and the first-use trust list per (server,
// issuer). Each server is its own sign-in: two servers that happen to use the
// same identity provider never share a saved login, and signing out of one
// leaves the others alone. Refresh tokens are credentials: files are created
// owner-only (0600) and the token is never logged. An OS keychain is a later
// step.

namespace vb::auth {

struct StoredSession {
	std::string server; // "host:port" (or "singleplayer"), as the client connected
	std::string issuer;
	std::string client_id;
	std::string provider; // "oidc" | "keycloak" | "firebase"
	std::string api_key; // firebase refresh needs it; public by design
	std::string refresh_token;
	std::string label; // "Signed in as ..." text (display only, unverified)
};

class SessionStore {
public:
	// `dir` is created on demand (usually user_config_dir()/"auth"). Saved
	// logins from before they were per server (no "server" field) are deleted
	// here: they would otherwise sign the player in to any server using that
	// provider.
	explicit SessionStore(std::filesystem::path dir);

	std::optional<StoredSession> load(const std::string &server, const std::string &issuer,
			const std::string &client_id) const;
	bool save(const StoredSession &session);
	void erase(const std::string &server, const std::string &issuer, const std::string &client_id);
	std::vector<StoredSession> list() const;
	std::vector<StoredSession> list_for(const std::string &server) const;
	void sign_out(const std::string &server); // every saved login for this server
	void clear(); // sign out of everything (keeps the trust list)

	// "<server> may ask me to sign in with <issuer>" -- remembered per pair.
	bool is_trusted(const std::string &server, const std::string &issuer) const;
	bool trust(const std::string &server, const std::string &issuer);

	const std::filesystem::path &dir() const { return dir_; }

private:
	std::filesystem::path path_for(const std::string &server, const std::string &issuer,
			const std::string &client_id) const;
	std::filesystem::path dir_;
};

// A display label from the (unverified) ID-token payload: preferred_username,
// name, email, then sub. For the menu only; never used for identity.
std::string label_from_id_token(const std::string &id_token);

} // namespace vb::auth
