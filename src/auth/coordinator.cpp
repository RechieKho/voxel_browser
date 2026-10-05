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

namespace {

std::string param(const protocol::S2CAuthChallenge &c, const char *key) {
	for (const auto &[k, v] : c.params) {
		if (k == key) {
			return v;
		}
	}
	return {};
}

bool is_oidc(const protocol::S2CAuthChallenge &c) {
	return c.provider == "oidc" || c.provider == "keycloak";
}

} // namespace

SignInResult silent_sign_in(HttpFetcher &http, const protocol::S2CAuthChallenge &challenge,
		const std::string &refresh_token) {
	SignInResult r;
	r.done = true;
	if (refresh_token.empty()) {
		r.error = "Sign in again";
		return r;
	}
	if (challenge.provider == "firebase") {
		r = firebase_refresh(http, param(challenge, "api_key"), refresh_token);
	} else if (is_oidc(challenge)) {
		std::string err;
		const auto ep = oidc_discover(http, challenge.issuer, &err);
		if (!ep) {
			r.error = "Could not reach the identity provider";
			return r;
		}
		const TokenResponse t = refresh_id_token(http, *ep, challenge.client_id, refresh_token);
		r.ok = t.ok;
		r.id_token = t.id_token;
		r.refresh_token = t.refresh_token;
		r.error = t.ok ? std::string() : "Sign in again";
	} else {
		r.error = "Sign in again";
	}
	if (r.ok && r.refresh_token.empty()) {
		r.refresh_token = refresh_token; // the IdP did not rotate it
	}
	return r;
}

SignInCoordinator::SignInCoordinator(Options options) : options_(std::move(options)) {}
SignInCoordinator::~SignInCoordinator() = default;

std::function<net::TokenTicket(const protocol::S2CAuthChallenge &)>
SignInCoordinator::provider() {
	return [this](const protocol::S2CAuthChallenge &ch) -> net::TokenTicket {
		{
			std::lock_guard lock(mu_);
			challenge_ = ch;
			active_nonce_ = ch.nonce;
			phase_ = Phase::kChoosing;
			error_.clear();
			token_.clear();
			token_ready_ = false;
			cancelled_ = false;
			reauth_ = false;
			needs_trust_ = options_.store && !options_.token_file &&
					!options_.store->is_trusted(options_.server_id, ch.issuer);
			if (!needs_trust_ && !options_.token_file) {
				begin_silent_locked();
			}
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

std::function<net::TokenTicket(const protocol::S2CReauthRequest &)>
SignInCoordinator::reauth_provider() {
	return [this](const protocol::S2CReauthRequest &req) -> net::TokenTicket {
		if (options_.token_file) {
			// Re-read for every re-auth so a test can rotate or withhold it.
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
		{
			std::lock_guard lock(mu_);
			active_nonce_ = req.nonce;
			reauth_ = true;
			phase_ = Phase::kChoosing;
			error_.clear();
			token_.clear();
			token_ready_ = false;
			cancelled_ = false;
			begin_silent_locked(); // no UI if the cached refresh token still works
		}
		return [this] { return poll_ticket(); };
	};
}

void SignInCoordinator::begin_silent_locked() {
	if (!options_.store || !options_.http || !challenge_ || task_) {
		return;
	}
	const auto stored = options_.store->load(challenge_->issuer, challenge_->client_id);
	if (!stored) {
		return;
	}
	const protocol::S2CAuthChallenge ch = *challenge_;
	const std::string refresh = stored->refresh_token;
	auto http = options_.http;
	silent_ = true;
	phase_ = Phase::kWorking;
	task_ = std::make_unique<SignInTask>([http, ch, refresh](const std::atomic<bool> &) {
		return silent_sign_in(*http, ch, refresh);
	});
}

void SignInCoordinator::save_session_locked(const SignInResult &r) {
	if (!options_.store || !challenge_ || r.refresh_token.empty()) {
		return;
	}
	StoredSession s;
	s.issuer = challenge_->issuer;
	s.client_id = challenge_->client_id;
	s.provider = challenge_->provider;
	s.api_key = param(*challenge_, "api_key");
	s.refresh_token = r.refresh_token;
	s.label = label_from_id_token(r.id_token);
	options_.store->save(s);
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
	const bool was_silent = silent_;
	silent_ = false;
	if (r.ok) {
		save_session_locked(r);
		token_ = r.id_token;
		token_ready_ = true;
		phase_ = Phase::kFinished;
		reauth_ = false;
	} else {
		if (was_silent && options_.store && challenge_) {
			// The cached refresh token is no good (expired, revoked): forget it
			// and fall back to an interactive sign-in.
			options_.store->erase(challenge_->issuer, challenge_->client_id);
			error_ = "Your saved sign-in expired";
		} else {
			error_ = r.error;
		}
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

bool SignInCoordinator::needs_trust() const {
	std::lock_guard lock(mu_);
	return needs_trust_;
}

void SignInCoordinator::trust() {
	std::lock_guard lock(mu_);
	if (!needs_trust_ || !challenge_ || !options_.store) {
		return;
	}
	options_.store->trust(options_.server_id, challenge_->issuer);
	needs_trust_ = false;
	begin_silent_locked();
}

bool SignInCoordinator::reauth_prompt_active() const {
	std::lock_guard lock(mu_);
	const_cast<SignInCoordinator *>(this)->update_locked();
	return reauth_ && !cancelled_ && phase_ != Phase::kFinished && !(silent_ && task_);
}

bool SignInCoordinator::supports_browser() const {
	std::lock_guard lock(mu_);
	return challenge_ && is_oidc(*challenge_);
}

bool SignInCoordinator::supports_password() const {
	std::lock_guard lock(mu_);
	if (!challenge_ || challenge_->provider != "firebase") {
		return false;
	}
	return ("," + param(*challenge_, "sign_in") + ",").find(",password,") != std::string::npos;
}

void SignInCoordinator::start_browser() {
	std::lock_guard lock(mu_);
	if (phase_ != Phase::kChoosing || !challenge_ || task_ || needs_trust_ || !is_oidc(*challenge_)) {
		return;
	}
	OidcSignInParams p;
	p.http = options_.http;
	p.issuer = challenge_->issuer;
	p.client_id = challenge_->client_id;
	p.scopes = challenge_->scopes;
	p.nonce = active_nonce_; // the join challenge's, or the open re-auth request's
	p.open_browser = options_.open_browser;
	error_.clear();
	phase_ = Phase::kWorking;
	task_ = std::make_unique<SignInTask>(
			[p](const std::atomic<bool> &c) { return oidc_browser_sign_in(p, c); });
}

void SignInCoordinator::start_password(const std::string &email, const std::string &password) {
	std::lock_guard lock(mu_);
	if (phase_ != Phase::kChoosing || !challenge_ || task_ || needs_trust_ || !options_.http) {
		return;
	}
	const std::string api_key = param(*challenge_, "api_key");
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
