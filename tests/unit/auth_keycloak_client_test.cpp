// The client side of the Keycloak fixtures (docs/auth-keycloak-testing.md, K3.3): discovery, every
// error body the token endpoint can answer, and how SignInCoordinator reacts -- a refresh token
// Keycloak refuses is forgotten, one an outage could not check is kept and retried.
//
// Fixtures: tests/unit/fixtures/keycloak/ (see fixtures.md there).

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "auth_keycloak_fixture.hpp"
#include "vb/auth/coordinator.hpp"
#include "vb/auth/oidc_client.hpp"
#include "vb/auth/session_store.hpp"
#include "vb/net/handshake.hpp"

#if defined(VB_WITH_AUTH)

using namespace vb::auth;
using vb::auth::testing::KeycloakFixtureFetcher;
using vb::auth::testing::KeycloakTokens;
using nlohmann::json;
using Route = KeycloakFixtureFetcher::Route;

namespace {

const KeycloakTokens &tokens() {
	static const KeycloakTokens t;
	return t;
}

} // namespace


TEST_CASE("keycloak client: discovery accepts the real document and names the cause when it refuses") {
	KeycloakFixtureFetcher http;
	std::string err;
	const auto ep = oidc_discover(http, http.issuer(), &err);
	REQUIRE(ep);
	CHECK(ep->authorization_endpoint == http.issuer() + "/protocol/openid-connect/auth");
	CHECK(ep->token_endpoint == http.token_url());

	json doc = testing::fixture_json("discovery.json");
	doc["issuer"] = "https://evil.example/realms/e2e";
	http.set_discovery_body(doc.dump());
	CHECK_FALSE(oidc_discover(http, http.issuer(), &err));
	CHECK(err.find("unexpected issuer") != std::string::npos);

	doc = testing::fixture_json("discovery.json");
	doc["token_endpoint"] = "http://keycloak.example.test/token";
	http.set_discovery_body(doc.dump());
	CHECK_FALSE(oidc_discover(http, http.issuer(), &err));
	CHECK(err.find("not acceptable") != std::string::npos);

	http.set_discovery_body({});
	http.fail(Route::kDiscovery, 503);
	CHECK_FALSE(oidc_discover(http, http.issuer(), &err));
	CHECK(err.find("could not reach") != std::string::npos);
}

namespace {

vb::protocol::S2CAuthChallenge keycloak_challenge() {
	vb::protocol::S2CAuthChallenge ch;
	ch.provider = "keycloak";
	ch.issuer = tokens().issuer();
	ch.client_id = tokens().client_id();
	ch.scopes = { "openid", "profile" };
	ch.nonce = "join-nonce";
	return ch;
}

std::filesystem::path fresh_store_dir(const char *name) {
	auto d = std::filesystem::temp_directory_path() / (std::string("vb_auth_kc_") + name);
	std::filesystem::remove_all(d);
	return d;
}

std::shared_ptr<SessionStore> store_with_refresh_token(const std::filesystem::path &dir, const std::string &server) {
	auto store = std::make_shared<SessionStore>(dir);
	StoredSession s;
	s.issuer = tokens().issuer();
	s.client_id = tokens().client_id();
	s.provider = "keycloak";
	s.refresh_token = "R-stored";
	store->save(s);
	store->trust(server, tokens().issuer());
	return store;
}

template <class Pred>
bool wait_until(Pred pred, int ms = 3000) {
	for (int i = 0; i < ms / 5; ++i) {
		if (pred()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return pred();
}

} // namespace

TEST_CASE("keycloak client: every error body maps to 'sign in again', and none of it leaks") {
	const char *permanent[] = { "session_not_active", "token_not_active", "invalid_refresh_token", "user_disabled",
		"refresh_reuse_exceeded", "code_not_valid", "pkce_failed", "unauthorized_client", "unsupported_grant_type" };
	for (const char *name : permanent) {
		CAPTURE(name);
		KeycloakFixtureFetcher http;
		http.reply_fixture(Route::kToken, std::string("errors/") + name + ".json");
		const auto r = silent_sign_in(http, keycloak_challenge(), "R-stored");
		CHECK_FALSE(r.ok);
		CHECK_FALSE(r.retryable);
		CHECK(r.error == "Sign in again");
		CHECK(r.id_token.empty());
	}
	// the code-exchange path relays only the RFC 6749 error code, never Keycloak's description
	KeycloakFixtureFetcher http;
	http.reply_fixture(Route::kToken, "errors/pkce_failed.json");
	OidcEndpoints ep;
	ep.token_endpoint = http.token_url();
	const auto bad = exchange_code(http, ep, "vb-e2e", "http://127.0.0.1:5/cb", "c", "v");
	CHECK_FALSE(bad.ok);
	CHECK(bad.error.find("invalid_grant") != std::string::npos);
	CHECK(bad.error.find("PKCE") == std::string::npos);
	CHECK_FALSE(bad.retryable);
}

TEST_CASE("keycloak client: a refresh answered with the real token response succeeds and rotates the token") {
	KeycloakFixtureFetcher http;
	http.reply_fixture(Route::kToken, "refresh_response.json");
	const auto r = silent_sign_in(http, keycloak_challenge(), "R-stored");
	REQUIRE(r.ok);
	CHECK(r.refresh_token == testing::fixture_json("refresh_response.json")["refresh_token"].get<std::string>());
	CHECK(r.refresh_token != "R-stored");
	CHECK(r.id_token == tokens().token("id_refresh_no_nonce"));
	CHECK(http.token_posts().back().find("grant_type=refresh_token") != std::string::npos);
}

TEST_CASE("keycloak client: 5xx, 429 and an unreachable IdP are retryable, not a verdict on the token") {
	for (const int status : { 0, 500, 502, 503, 504, 429 }) {
		CAPTURE(status);
		KeycloakFixtureFetcher http;
		http.fail(Route::kToken, status);
		const auto r = silent_sign_in(http, keycloak_challenge(), "R-stored");
		CHECK_FALSE(r.ok);
		CHECK(r.retryable);
		CHECK(r.error == "Could not reach the identity provider");
	}
	KeycloakFixtureFetcher down;
	down.fail(Route::kDiscovery, 0); // discovery failing is not the token's fault either
	const auto r = silent_sign_in(down, keycloak_challenge(), "R-stored");
	CHECK(r.retryable);
}

TEST_CASE("keycloak coordinator: a refresh token Keycloak refuses is forgotten, the player is asked again") {
	for (const char *name : { "session_not_active", "token_not_active", "invalid_refresh_token", "user_disabled" }) {
		CAPTURE(name);
		const auto dir = fresh_store_dir(name);
		auto store = store_with_refresh_token(dir, "play.example:27015");
		auto http = std::make_shared<KeycloakFixtureFetcher>();
		http->reply_fixture(Route::kToken, std::string("errors/") + name + ".json");
		SignInCoordinator::Options o;
		o.http = http;
		o.store = store;
		o.server_id = "play.example:27015";
		SignInCoordinator coord(std::move(o));
		auto ticket = coord.provider()(keycloak_challenge());
		REQUIRE(wait_until([&] { return coord.phase() != SignInCoordinator::Phase::kWorking; }));
		CHECK(coord.phase() == SignInCoordinator::Phase::kChoosing);
		CHECK(coord.last_error() == "Your saved sign-in expired");
		CHECK_FALSE(store->load(tokens().issuer(), tokens().client_id()));
		CHECK_FALSE(ticket().done);
		std::filesystem::remove_all(dir);
	}
}

TEST_CASE("keycloak coordinator: an IdP outage at sign-in keeps the stored refresh token") {
	const auto dir = fresh_store_dir("outage_join");
	auto store = store_with_refresh_token(dir, "play.example:27015");
	auto http = std::make_shared<KeycloakFixtureFetcher>();
	http->fail(Route::kToken, 503);
	SignInCoordinator::Options o;
	o.http = http;
	o.store = store;
	o.server_id = "play.example:27015";
	SignInCoordinator coord(std::move(o));
	auto ticket = coord.provider()(keycloak_challenge());
	REQUIRE(wait_until([&] { return coord.phase() != SignInCoordinator::Phase::kWorking; }));
	CHECK(coord.phase() == SignInCoordinator::Phase::kChoosing);
	CHECK(coord.last_error() == "Could not reach the identity provider");
	REQUIRE(store->load(tokens().issuer(), tokens().client_id())); // kept
	CHECK(store->load(tokens().issuer(), tokens().client_id())->refresh_token == "R-stored");
	std::filesystem::remove_all(dir);
}

TEST_CASE("keycloak coordinator: a re-auth retries quietly through an outage and recovers") {
	const auto dir = fresh_store_dir("outage_reauth");
	auto store = store_with_refresh_token(dir, "play.example:27015");
	auto http = std::make_shared<KeycloakFixtureFetcher>();
	http->reply_fixture(Route::kToken, "refresh_response.json");
	SignInCoordinator::Options o;
	o.http = http;
	o.store = store;
	o.server_id = "play.example:27015";
	o.reauth_retry_interval = std::chrono::milliseconds(20);
	SignInCoordinator coord(std::move(o));
	auto join = coord.provider()(keycloak_challenge()); // the join sign-in itself succeeds silently
	REQUIRE(wait_until([&] { return join().done; }));
	http->fail(Route::kToken, 0, 2); // now two transport failures, then Keycloak is back

	vb::protocol::S2CReauthRequest req;
	req.nonce = "reauth-nonce";
	req.grace_seconds = 120;
	const int before = static_cast<int>(http->token_posts().size());
	auto ticket = coord.reauth_provider()(req);
	vb::net::TokenPoll p;
	REQUIRE(wait_until([&] { return (p = ticket()).done; }));
	CHECK(p.token == tokens().token("id_refresh_no_nonce"));
	CHECK(static_cast<int>(http->token_posts().size()) - before >= 3); // failed, failed, succeeded
	CHECK_FALSE(coord.reauth_prompt_active());
	REQUIRE(store->load(tokens().issuer(), tokens().client_id()));
	CHECK(store->load(tokens().issuer(), tokens().client_id())->refresh_token != "R-stored"); // rotated
	std::filesystem::remove_all(dir);
}

TEST_CASE("keycloak coordinator: a long outage raises the prompt after a few misses, still keeps the token") {
	const auto dir = fresh_store_dir("outage_long");
	auto store = store_with_refresh_token(dir, "play.example:27015");
	auto http = std::make_shared<KeycloakFixtureFetcher>();
	http->fail(Route::kToken, 503);
	SignInCoordinator::Options o;
	o.http = http;
	o.store = store;
	o.server_id = "play.example:27015";
	o.reauth_retry_interval = std::chrono::milliseconds(150);
	SignInCoordinator coord(std::move(o));
	(void)coord.provider()(keycloak_challenge()); // the join attempt fails the same way (token kept)
	REQUIRE(wait_until([&] { return coord.phase() != SignInCoordinator::Phase::kWorking; }));

	vb::protocol::S2CReauthRequest req;
	req.nonce = "reauth-nonce";
	req.grace_seconds = 120;
	auto ticket = coord.reauth_provider()(req);
	// first miss: quiet retry, no scary banner
	REQUIRE(wait_until([&] { return http->token_posts().size() >= 2 && coord.phase() != SignInCoordinator::Phase::kWorking; }));
	CHECK_FALSE(coord.reauth_prompt_active());
	CHECK_FALSE(ticket().done);
	// after three misses in a row the player may as well be told
	REQUIRE(wait_until([&] {
		(void)ticket();
		return http->token_posts().size() >= 5 && coord.reauth_prompt_active();
	}, 5000));
	REQUIRE(store->load(tokens().issuer(), tokens().client_id())); // never forgotten over an outage
	// a new request from the server starts the count afresh
	ticket = coord.reauth_provider()(req);
	CHECK_FALSE(ticket().done);
	std::filesystem::remove_all(dir);
}

#endif // VB_WITH_AUTH
