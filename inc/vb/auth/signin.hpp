#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Background sign-in plumbing for the client (auth.md §7): the render loop
// keeps drawing while discovery, the browser round trip or an Identity Toolkit
// call runs on a worker. The result carries tokens, so it is consumed once and
// never logged.

namespace vb::auth {

struct SignInResult {
	bool done = false;
	bool ok = false;
	std::string id_token;
	std::string refresh_token; // may be empty
	std::string error; // player-facing, token-free
	// A transient failure (IdP unreachable, 5xx, 429): the stored refresh token is
	// not to blame and must be kept. Always false when ok.
	bool retryable = false;
};

class SignInTask {
public:
	using Work = std::function<SignInResult(const std::atomic<bool> &cancelled)>;

	explicit SignInTask(Work work);
	~SignInTask(); // cancels and joins
	SignInTask(const SignInTask &) = delete;
	SignInTask &operator=(const SignInTask &) = delete;

	void cancel();
	// done == false while still running.
	SignInResult poll() const;

private:
	mutable std::mutex mu_;
	SignInResult result_;
	std::atomic<bool> cancelled_{ false };
	std::thread worker_;
};

} // namespace vb::auth
