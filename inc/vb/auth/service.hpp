#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "vb/auth/config.hpp"
#include "vb/auth/http.hpp"
#include "vb/auth/jwks.hpp"
#include "vb/auth/verifier.hpp"

// Server-side authentication service (auth.md §5.1/§5.3): owns the cached
// key set (OIDC discovery + JWKS, background refresh with a rate limit and
// one in-flight fetch) and turns "token + connection nonce" into a pollable
// verification, so the handshake never blocks on the network.

namespace vb::auth {

// Final outcome of one verification.
struct Verdict {
	bool ok = false;
	LoginInfo login; // valid iff ok
	std::string reason; // coarse text safe to show the player
	std::string detail; // precise cause for the server log; never has token bytes
};

class AuthService {
public:
	struct Options {
		// Spawn the background worker in start(). Tests set this false and
		// drive pump() themselves for determinism.
		bool start_thread = true;
		std::function<std::int64_t()> clock; // unix seconds; default: system clock
		std::int64_t refresh_min_interval_seconds = 60; // unknown-kid refresh limit
		std::int64_t verify_wait_seconds = 10; // max a pending verify waits for keys
	};

	AuthService(AuthConfig config, std::shared_ptr<HttpFetcher> http, Options options);
	AuthService(AuthConfig config, std::shared_ptr<HttpFetcher> http)
		: AuthService(std::move(config), std::move(http), Options{}) {}
	~AuthService();
	AuthService(const AuthService &) = delete;
	AuthService &operator=(const AuthService &) = delete;

	// Starts the prefetch worker (no-op with start_thread == false).
	void start();
	// Performs one due unit of background work (discovery or JWKS fetch)
	// synchronously. Returns true if it did something. Called by the worker.
	bool pump();

	// True once a non-empty key set has been loaded.
	bool ready() const;

	// A fresh per-connection challenge nonce (32 random bytes, base64url);
	// empty string if the OS entropy source failed (callers fail closed).
	std::string new_nonce() const;

	class Pending {
	public:
		// nullopt = still waiting on a JWKS fetch; call again next tick.
		std::optional<Verdict> poll();

	private:
		friend class AuthService;
		Pending(AuthService &svc, std::string token, std::string nonce)
			: svc_(svc), token_(std::move(token)), nonce_(std::move(nonce)) {}
		AuthService &svc_;
		std::string token_;
		std::string nonce_;
		bool waiting_ = false;
		std::uint64_t generation_at_wait_ = 0;
		std::int64_t deadline_ = 0;
	};

	// The pending verification must not outlive the service. The token is
	// consumed (kept only inside the Pending, dropped when it resolves).
	std::shared_ptr<Pending> begin(std::string token, std::string nonce);

	// Verifies against the current key set without ever waiting or refreshing.
	Verdict verify_now(std::string_view token, std::string_view nonce) const;

	const AuthConfig &config() const { return config_; }

private:
	std::int64_t now() const;
	std::shared_ptr<const KeySet> keys() const;
	std::uint64_t generation() const;
	bool request_refresh(); // false if rate-limited / nothing to refresh with
	Verdict finish(const VerifyResult &r) const;
	void worker_loop();

	AuthConfig config_;
	std::shared_ptr<HttpFetcher> http_;
	Options options_;

	mutable std::mutex mu_;
	std::shared_ptr<const KeySet> keys_;
	std::string jwks_uri_;
	std::uint64_t generation_ = 0; // bumped after every finished JWKS attempt
	bool refresh_requested_ = false;
	bool fetching_ = false; // at most one in-flight discovery/JWKS fetch
	std::int64_t last_jwks_attempt_ = -1;
	std::int64_t next_retry_ = 0;
	std::int64_t backoff_seconds_ = 10;

	std::thread worker_;
	std::condition_variable cv_;
	bool stop_ = false;
};

} // namespace vb::auth
