#pragma once

// Serves the golden Keycloak fixtures (tests/unit/fixtures/keycloak/, fixtures.md) as an
// HttpFetcher, so unit tests see the same bytes a real Keycloak sends instead of hand-written JSON.
// It can be told to fail, delay or swap a document mid-test. A sign-in runs on a worker thread
// while the test thread polls and reconfigures it, so every member is guarded by one mutex.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "vb/auth/http.hpp"
#include "vb/auth/jwt.hpp"

#if defined(VB_WITH_AUTH)

namespace vb::auth::testing {

inline std::filesystem::path keycloak_fixture_dir() {
	return std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "tests" / "unit" / "fixtures" / "keycloak";
}

inline std::string read_fixture(const std::string &relative) {
	std::ifstream in(keycloak_fixture_dir() / relative, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

inline nlohmann::json fixture_json(const std::string &relative) {
	return nlohmann::json::parse(read_fixture(relative));
}

// tokens.json: now, issuer, client_id, nonce, and named tokens.
struct KeycloakTokens {
	nlohmann::json doc = fixture_json("tokens.json");
	std::int64_t now() const { return doc["now"].get<std::int64_t>(); }
	std::string issuer() const { return doc["issuer"].get<std::string>(); }
	std::string client_id() const { return doc["client_id"].get<std::string>(); }
	std::string nonce() const { return doc["nonce"].get<std::string>(); }
	std::string token(const std::string &name) const { return doc["tokens"].at(name)["token"].get<std::string>(); }
	// The payload of a fixture token, for assertions on what it carries.
	nlohmann::json claims(const std::string &name) const;
};

inline nlohmann::json KeycloakTokens::claims(const std::string &name) const {
	const std::string t = token(name);
	const auto a = t.find('.');
	const auto b = t.find('.', a + 1);
	const auto bytes = base64url_decode(t.substr(a + 1, b - a - 1));
	return nlohmann::json::parse(std::string(bytes->begin(), bytes->end()));
}

class KeycloakFixtureFetcher final : public HttpFetcher {
public:
	enum class Route { kDiscovery, kJwks, kToken };

	KeycloakFixtureFetcher() : issuer_(KeycloakTokens().issuer()) {
		jwks_file_ = "jwks_rs256.json";
		discovery_file_ = "discovery.json";
		reply_fixture(Route::kToken, "token_response.json");
	}

	std::string issuer() const { return issuer_; }
	std::string discovery_url() const { return issuer_ + "/.well-known/openid-configuration"; }
	std::string jwks_url() const { return issuer_ + "/protocol/openid-connect/certs"; }
	std::string token_url() const { return issuer_ + "/protocol/openid-connect/token"; }

	// Swap what a route serves, mid-test. A file is relative to the fixture directory; an
	// `errors/*.json` file is {status, body} and is served with that status.
	void set_jwks(const std::string &file) {
		std::lock_guard<std::mutex> lock(mu_);
		jwks_file_ = file;
	}
	void set_discovery(const std::string &file) {
		std::lock_guard<std::mutex> lock(mu_);
		discovery_file_ = file;
		discovery_body_.reset();
	}
	// Serve this text as the discovery document instead (an edited copy, an HTML error page, ...);
	// an empty string goes back to the fixture file.
	void set_discovery_body(std::string body) {
		std::lock_guard<std::mutex> lock(mu_);
		if (body.empty()) {
			discovery_body_.reset();
		} else {
			discovery_body_ = std::move(body);
		}
	}
	void reply_fixture(Route route, const std::string &file) {
		HttpResult r;
		if (file.rfind("errors/", 0) == 0) {
			const auto doc = fixture_json(file);
			r = reply(doc["status"].get<int>(), doc["body"].dump());
		} else {
			r = reply(200, read_fixture(file));
		}
		std::lock_guard<std::mutex> lock(mu_);
		replies_[route] = std::move(r);
	}
	void reply_raw(Route route, int status, std::string body) {
		std::lock_guard<std::mutex> lock(mu_);
		replies_[route] = reply(status, std::move(body));
	}

	// The next `times` requests to `route` fail with `status` (0 = transport failure), then it
	// goes back to normal.
	void fail(Route route, int status, int times = 1000000) {
		std::lock_guard<std::mutex> lock(mu_);
		failures_[route] = { status, times };
	}
	void heal(Route route) {
		std::lock_guard<std::mutex> lock(mu_);
		failures_.erase(route);
	}
	void delay(Route route, std::chrono::milliseconds d) {
		std::lock_guard<std::mutex> lock(mu_);
		delays_[route] = d;
	}

	int requests(Route route) const {
		std::lock_guard<std::mutex> lock(mu_);
		const auto it = counts_.find(route);
		return it == counts_.end() ? 0 : it->second;
	}
	// A copy: the worker thread keeps appending while a test inspects it.
	std::vector<std::string> token_posts() const {
		std::lock_guard<std::mutex> lock(mu_);
		return posts_;
	}

	HttpResult get(const std::string &url) override {
		if (url == discovery_url()) {
			std::string file;
			std::optional<std::string> body;
			{
				std::lock_guard<std::mutex> lock(mu_);
				file = discovery_file_;
				body = discovery_body_;
			}
			return serve(Route::kDiscovery, reply(200, body ? *body : read_fixture(file)));
		}
		if (url == jwks_url()) {
			std::string file;
			{
				std::lock_guard<std::mutex> lock(mu_);
				file = jwks_file_;
			}
			return serve(Route::kJwks, reply(200, read_fixture(file)));
		}
		HttpResult r;
		r.error = "no fixture route for " + url;
		return r;
	}

	HttpResult post(const std::string &url, const std::string &content_type, const std::string &body) override {
		(void)content_type;
		if (url != token_url()) {
			HttpResult r;
			r.error = "no fixture route for " + url;
			return r;
		}
		HttpResult normal;
		{
			std::lock_guard<std::mutex> lock(mu_);
			posts_.push_back(body);
			normal = replies_[Route::kToken];
		}
		return serve(Route::kToken, std::move(normal));
	}

private:
	static HttpResult reply(int status, std::string body) {
		HttpResult r;
		r.status = status;
		r.body = std::move(body);
		return r;
	}
	HttpResult serve(Route route, HttpResult normal) {
		std::chrono::milliseconds wait{ 0 };
		std::optional<HttpResult> failed;
		{
			std::lock_guard<std::mutex> lock(mu_);
			++counts_[route];
			if (const auto d = delays_.find(route); d != delays_.end()) {
				wait = d->second;
			}
			if (auto f = failures_.find(route); f != failures_.end() && f->second.second > 0) {
				--f->second.second;
				HttpResult r;
				r.status = f->second.first;
				r.error = r.status == 0 ? "connection refused (fixture)" : std::string();
				r.body = r.status == 0 ? std::string() : R"({"error":"server_error"})";
				failed = std::move(r);
			}
		}
		if (wait.count() > 0) {
			std::this_thread::sleep_for(wait); // outside the lock: other requests must not queue behind it
		}
		return failed ? std::move(*failed) : std::move(normal);
	}

	std::string issuer_;
	mutable std::mutex mu_;
	std::string jwks_file_, discovery_file_;
	std::optional<std::string> discovery_body_;
	std::map<Route, HttpResult> replies_;
	std::map<Route, std::pair<int, int>> failures_;
	std::map<Route, std::chrono::milliseconds> delays_;
	std::map<Route, int> counts_;
	std::vector<std::string> posts_;
};

} // namespace vb::auth::testing

#endif // VB_WITH_AUTH
