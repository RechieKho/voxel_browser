#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "vb/auth/http.hpp"
#include "vb/auth/signin.hpp"
#include "vb/net/handshake.hpp"

// Client-side glue between the handshake's sign-in ticket and the UI
// (auth.md §7). The UI reads phase()/challenge() and calls start_browser() /
// start_password() / cancel(); the handshake polls the ticket from
// provider(). A failed attempt returns to kChoosing with last_error() so a
// mistyped password or a closed browser tab can be retried without
// reconnecting; only cancel() (or the server's auth timeout) ends the join.

namespace vb::auth {

// Reads a whole token from `path` (trims whitespace; <= 16 KiB). nullopt if
// the file is missing/empty/oversized. Used by --auth-token-file, re-read for
// every sign-in so tests can rotate or withhold the token.
std::optional<std::string> read_token_file(const std::filesystem::path &path);

class SignInCoordinator {
public:
	enum class Phase : std::uint8_t {
		kIdle, // no challenge yet
		kChoosing, // waiting for the player to pick a method / retry
		kWorking, // browser round trip or REST call in flight
		kFinished, // token produced, or cancelled
	};

	struct Options {
		std::shared_ptr<HttpFetcher> http;
		// Headless/automation: skip the UI and use this file's token.
		std::optional<std::filesystem::path> token_file;
		std::function<bool(const std::string &)> open_browser; // default: system browser
	};

	explicit SignInCoordinator(Options options);
	~SignInCoordinator();

	// Plug into ClientSession::set_sign_in_provider. The coordinator must
	// outlive the session.
	std::function<net::TokenTicket(const protocol::S2CAuthChallenge &)> provider();

	Phase phase() const;
	std::optional<protocol::S2CAuthChallenge> challenge() const;
	std::string last_error() const; // from the previous attempt, empty if none

	bool supports_browser() const; // oidc / keycloak
	bool supports_password() const; // firebase with "password" in sign_in
	void start_browser();
	void start_password(const std::string &email, const std::string &password);
	void cancel();

private:
	net::TokenPoll poll_ticket();
	void update_locked();

	Options options_;
	mutable std::mutex mu_;
	Phase phase_ = Phase::kIdle;
	std::optional<protocol::S2CAuthChallenge> challenge_;
	std::string error_;
	std::string token_;
	bool token_ready_ = false;
	bool cancelled_ = false;
	std::unique_ptr<SignInTask> task_;
	// A cancelled task is parked (not joined under the lock, which could stall
	// the UI behind a slow HTTP call) and joined when the coordinator dies.
	std::vector<std::unique_ptr<SignInTask>> parked_;
};

} // namespace vb::auth
