#include "vb/auth/service.hpp"

#include <algorithm>
#include <chrono>

#include <nlohmann/json.hpp>

#include "vb/auth/crypto.hpp"
#include "vb/auth/jwt.hpp"
#include "vb/core/log.hpp"

namespace vb::auth {

namespace {

constexpr const char *kFirebaseJwks =
		"https://www.googleapis.com/service_accounts/v1/jwk/securetoken@system.gserviceaccount.com";

std::string trim_slash(std::string s) {
	while (!s.empty() && s.back() == '/') {
		s.pop_back();
	}
	return s;
}

} // namespace

AuthService::AuthService(AuthConfig config, std::shared_ptr<HttpFetcher> http,
		Options options) :
		config_(std::move(config)),
		http_(std::move(http)),
		options_(std::move(options)),
		keys_(std::make_shared<const KeySet>()) {
	if (config_.provider == Provider::kFirebase) {
		jwks_uri_ = kFirebaseJwks;
	}
}

AuthService::~AuthService() {
	{
		std::lock_guard lock(mu_);
		stop_ = true;
	}
	cv_.notify_all();
	if (worker_.joinable()) {
		worker_.join();
	}
}

std::int64_t AuthService::now() const {
	if (options_.clock) {
		return options_.clock();
	}
	return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
}

std::shared_ptr<const KeySet> AuthService::keys() const {
	std::lock_guard lock(mu_);
	return keys_;
}

std::uint64_t AuthService::generation() const {
	std::lock_guard lock(mu_);
	return generation_;
}

bool AuthService::ready() const { return !keys()->empty(); }

void AuthService::start() {
	if (!options_.start_thread || worker_.joinable()) {
		return;
	}
	worker_ = std::thread([this] { worker_loop(); });
}

void AuthService::worker_loop() {
	for (;;) {
		pump();
		std::unique_lock lock(mu_);
		if (cv_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; })) {
			return;
		}
	}
}

std::string AuthService::new_nonce() const {
	const auto bytes = random_bytes(32);
	if (bytes.empty()) {
		return {};
	}
	return base64url_encode(bytes);
}

bool AuthService::request_refresh() {
	std::lock_guard lock(mu_);
	if (fetching_ || refresh_requested_) {
		return true; // a fetch is already coming; wait for it
	}
	const std::int64_t t = now();
	if (last_jwks_attempt_ >= 0 &&
			t < last_jwks_attempt_ + options_.refresh_min_interval_seconds) {
		return false; // rate-limited
	}
	refresh_requested_ = true;
	cv_.notify_all();
	return true;
}

bool AuthService::pump() {
	enum class Work { kNone,
		kDiscovery,
		kJwks } work = Work::kNone;
	std::string url;
	const std::int64_t t = now();
	{
		std::lock_guard lock(mu_);
		if (fetching_ || stop_ || !http_) {
			return false;
		}
		if (jwks_uri_.empty()) {
			if (t >= next_retry_) {
				work = Work::kDiscovery;
				url = trim_slash(config_.issuer) + "/.well-known/openid-configuration";
			}
		} else if (keys_->empty()) {
			if (t >= next_retry_) {
				work = Work::kJwks;
				url = jwks_uri_;
			}
		} else if (refresh_requested_) {
			work = Work::kJwks; // the rate limit was applied in request_refresh()
			url = jwks_uri_;
		}
		if (work == Work::kNone) {
			return false;
		}
		fetching_ = true;
		if (work == Work::kJwks) {
			last_jwks_attempt_ = t;
		}
	}

	std::string error;
	std::string discovered_uri;
	KeySet fresh;
	const HttpResult res = http_->get(url);
	if (res.status != 200) {
		error = res.status == 0 ? "request failed: " + res.error
								: "HTTP " + std::to_string(res.status);
	} else if (work == Work::kDiscovery) {
		const nlohmann::json doc = nlohmann::json::parse(res.body, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) {
			error = "discovery document is not a JSON object";
		} else if (!doc.contains("issuer") || !doc["issuer"].is_string() ||
				trim_slash(doc["issuer"].get<std::string>()) != trim_slash(config_.issuer)) {
			// OIDC Discovery §4.3: the document's issuer must equal the one
			// it was fetched for, or a hostile host could redirect keys.
			error = "discovery issuer does not match the configured issuer";
		} else if (!doc.contains("jwks_uri") || !doc["jwks_uri"].is_string() ||
				!url_allowed(doc["jwks_uri"].get<std::string>())) {
			error = "discovery has no acceptable jwks_uri";
		} else {
			discovered_uri = doc["jwks_uri"].get<std::string>();
		}
	} else {
		JwksParse parsed = parse_jwks(res.body);
		if (!parsed.error.empty()) {
			error = parsed.error;
		} else {
			fresh = std::move(parsed.keys);
		}
	}

	{
		std::lock_guard lock(mu_);
		fetching_ = false;
		if (work == Work::kDiscovery) {
			if (error.empty()) {
				jwks_uri_ = discovered_uri;
				backoff_seconds_ = 10;
				next_retry_ = 0;
			} else {
				next_retry_ = t + backoff_seconds_;
				backoff_seconds_ = std::min<std::int64_t>(backoff_seconds_ * 2, 300);
			}
		} else {
			refresh_requested_ = false;
			if (error.empty()) {
				keys_ = std::make_shared<const KeySet>(std::move(fresh));
				backoff_seconds_ = 10;
				next_retry_ = 0;
			} else {
				next_retry_ = t + backoff_seconds_;
				backoff_seconds_ = std::min<std::int64_t>(backoff_seconds_ * 2, 300);
			}
			++generation_;
		}
	}
	if (error.empty()) {
		VB_INFO("auth", work == Work::kDiscovery ? "OIDC discovery ok for " : "JWKS loaded for ",
				config_.issuer);
	} else {
		VB_WARN("auth", "key fetch failed for ", config_.issuer, ": ", error);
	}
	return true;
}

Verdict AuthService::finish(const VerifyResult &r) const {
	Verdict v;
	v.ok = r.ok();
	if (v.ok) {
		v.login = r.login;
	} else {
		v.reason = std::string(public_reason(r.error));
		v.detail = std::string(to_string(r.error)) + ": " + r.detail;
	}
	return v;
}

Verdict AuthService::verify_now(std::string_view token, std::string_view nonce) const {
	return finish(verify_id_token(config_, *keys(), token, nonce, now()));
}

std::shared_ptr<AuthService::Pending> AuthService::begin(std::string token,
		std::string nonce) {
	return std::shared_ptr<Pending>(new Pending(*this, std::move(token), std::move(nonce)));
}

std::optional<Verdict> AuthService::Pending::poll() {
	if (!waiting_) {
		const auto snapshot = svc_.keys();
		const VerifyResult r =
				verify_id_token(svc_.config_, *snapshot, token_, nonce_, svc_.now());
		if (r.error != VerifyError::kUnknownKid) {
			token_.clear();
			return svc_.finish(r);
		}
		// Unknown kid: wait for the first key load, or ask for one rate-limited
		// refresh (§5.3 rule 2). Never loop on a hostile kid.
		if (!snapshot->empty() && !svc_.request_refresh()) {
			token_.clear();
			return svc_.finish(r);
		}
		waiting_ = true;
		generation_at_wait_ = svc_.generation();
		deadline_ = svc_.now() + svc_.options_.verify_wait_seconds;
		if (snapshot->empty()) {
			// Cold start: make sure a load is attempted promptly.
			svc_.cv_.notify_all();
		}
		return std::nullopt;
	}

	if (svc_.generation() > generation_at_wait_) {
		const VerifyResult r =
				verify_id_token(svc_.config_, *svc_.keys(), token_, nonce_, svc_.now());
		token_.clear();
		return svc_.finish(r);
	}
	if (svc_.now() > deadline_) {
		token_.clear();
		VerifyResult r;
		r.error = VerifyError::kUnknownKid;
		r.detail = "timed out waiting for the key set";
		return svc_.finish(r);
	}
	return std::nullopt;
}

} // namespace vb::auth
