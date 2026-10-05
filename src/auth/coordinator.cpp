#include "vb/auth/coordinator.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "vb/auth/firebase.hpp"
#include "vb/auth/jwt.hpp"
#include "vb/auth/oidc_client.hpp"

namespace vb::auth {

std::optional<std::string> read_token_file(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return std::nullopt;
	}
	std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (data.size() > kMaxJwtBytes + 64) {
		return std::nullopt;
	}
	auto is_space = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
	while (!data.empty() && is_space(data.back())) {
		data.pop_back();
	}
	std::size_t b = 0;
	while (b < data.size() && is_space(data[b])) {
		++b;
	}
	data.erase(0, b);
	if (data.empty() || data.size() > kMaxJwtBytes) {
		return std::nullopt;
	}
	return data;
}

SignInCoordinator::SignInCoordinator(Options options) : options_(std::move(options)) {}
SignInCoordinator::~SignInCoordinator() = default;

std::function<net::TokenTicket(const protocol::S2CAuthChallenge &)>
SignInCoordinator::provider() {
	return [this](const protocol::S2CAuthChallenge &ch) -> net::TokenTicket {
		{
			std::lock_guard lock(mu_);
			challenge_ = ch;
			phase_ = Phase::kChoosing;
			error_.clear();
			token_.clear();
			token_ready_ = false;
			cancelled_ = false;
		}
		if (options_.token_file) {
			const auto path = *options_.token_file;
			return [path]() -> net::TokenPoll {
				net::TokenPoll p;
				p.done = true;
				if (auto t = read_token_file(path)) {
					p.token = std::move(*t);
				} else {
					p.error = "could not read the sign-in token file";
				}
				return p;
			};
		}
		return [this] { return poll_ticket(); };
	};
}

void SignInCoordinator::update_locked() {
	if (!task_) {
		return;
	}
	const SignInResult r = task_->poll();
	if (!r.done) {
		return;
	}
	task_.reset();
	if (r.ok) {
		token_ = r.id_token;
		token_ready_ = true;
		phase_ = Phase::kFinished;
	} else {
		error_ = r.error;
		phase_ = Phase::kChoosing; // retryable
	}
}

net::TokenPoll SignInCoordinator::poll_ticket() {
	std::lock_guard lock(mu_);
	update_locked();
	net::TokenPoll p;
	if (cancelled_) {
		p.done = true;
		p.error = "sign-in cancelled";
	} else if (token_ready_) {
		p.done = true;
		p.token = std::move(token_);
		token_.clear();
		token_ready_ = false;
	}
	return p;
}

SignInCoordinator::Phase SignInCoordinator::phase() const {
	std::lock_guard lock(mu_);
	const_cast<SignInCoordinator *>(this)->update_locked();
	return phase_;
}

std::optional<protocol::S2CAuthChallenge> SignInCoordinator::challenge() const {
	std::lock_guard lock(mu_);
	return challenge_;
}

std::string SignInCoordinator::last_error() const {
	std::lock_guard lock(mu_);
	return error_;
}

bool SignInCoordinator::supports_browser() const {
	std::lock_guard lock(mu_);
	return challenge_ && (challenge_->provider == "oidc" || challenge_->provider == "keycloak");
}

bool SignInCoordinator::supports_password() const {
	std::lock_guard lock(mu_);
	if (!challenge_ || challenge_->provider != "firebase") {
		return false;
	}
	for (const auto &[k, v] : challenge_->params) {
		if (k == "sign_in" && ("," + v + ",").find(",password,") != std::string::npos) {
			return true;
		}
	}
	return false;
}

void SignInCoordinator::start_browser() {
	std::lock_guard lock(mu_);
	if (phase_ != Phase::kChoosing || !challenge_ || task_ ||
			(challenge_->provider != "oidc" && challenge_->provider != "keycloak")) {
		return;
	}
	OidcSignInParams p;
	p.http = options_.http;
	p.issuer = challenge_->issuer;
	p.client_id = challenge_->client_id;
	p.scopes = challenge_->scopes;
	p.nonce = challenge_->nonce;
	p.open_browser = options_.open_browser;
	error_.clear();
	phase_ = Phase::kWorking;
	task_ = std::make_unique<SignInTask>(
			[p](const std::atomic<bool> &c) { return oidc_browser_sign_in(p, c); });
}

void SignInCoordinator::start_password(const std::string &email, const std::string &password) {
	std::lock_guard lock(mu_);
	if (phase_ != Phase::kChoosing || !challenge_ || task_ || !options_.http) {
		return;
	}
	std::string api_key;
	for (const auto &[k, v] : challenge_->params) {
		if (k == "api_key") {
			api_key = v;
		}
	}
	error_.clear();
	phase_ = Phase::kWorking;
	auto http = options_.http;
	task_ = std::make_unique<SignInTask>(
			[http, api_key, email, password](const std::atomic<bool> &) {
				return firebase_password_sign_in(*http, api_key, email, password);
			});
}

void SignInCoordinator::cancel() {
	std::lock_guard lock(mu_);
	cancelled_ = true;
	if (task_) {
		task_->cancel();
		parked_.push_back(std::move(task_));
	}
	phase_ = Phase::kFinished;
}

} // namespace vb::auth
