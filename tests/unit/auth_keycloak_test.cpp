// In-engine authentication against Keycloak-shaped data (docs/auth-keycloak-testing.md, K3).
//
// Every document and token here was produced by the MockKeycloak emulator and stored under
// tests/unit/fixtures/keycloak/ (see fixtures.md there); the real-Keycloak CI workflow checks the
// emulator against a real Keycloak, so these cases see the bytes a real one sends.

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "auth_keycloak_fixture.hpp"
#include "vb/auth/config.hpp"
#include "vb/auth/jwks.hpp"
#include "vb/auth/jwt.hpp"
#include "vb/auth/service.hpp"
#include "vb/auth/verifier.hpp"

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

AuthConfig keycloak_config(std::vector<std::string> claims = { "email", "groups", "realm_access" }) {
	AuthConfig c;
	c.provider = Provider::kKeycloak;
	c.issuer = tokens().issuer();
	c.client_id = tokens().client_id();
	c.name_claim = "preferred_username";
	c.claims = std::move(claims);
	c.max_token_age_seconds = 3600; // tests move the clock by minutes; the fixtures are fixed in time
	return c;
}

KeySet keys_from(const std::string &fixture) {
	const JwksParse p = parse_jwks(testing::read_fixture(fixture));
	REQUIRE(p.error.empty());
	return p.keys;
}

VerifyResult verify(const std::string &token_name, const KeySet &keys, const AuthConfig &cfg = keycloak_config(),
		const std::string &nonce = tokens().nonce()) {
	return verify_id_token(cfg, keys, tokens().token(token_name), nonce, tokens().now());
}

} // namespace

// ---------------------------------------------------------------------------------------------
// accept

TEST_CASE("keycloak: a real-shaped RS256 ID token is accepted") {
	const auto r = verify("id_rs256", keys_from("jwks_rs256.json"));
	REQUIRE_MESSAGE(r.ok(), r.detail);
	CHECK(r.login.name == "alice");
	CHECK(r.login.issuer == tokens().issuer());
	CHECK(r.login.subject == tokens().claims("id_rs256")["sub"].get<std::string>());
	CHECK(r.login.provider == Provider::kKeycloak);
}

TEST_CASE("keycloak: an ES256 realm works") {
	const auto r = verify("id_es256", keys_from("jwks_es256.json"));
	REQUIRE_MESSAGE(r.ok(), r.detail);
	CHECK(r.login.name == "alice");
	// ... and an RSA key set does not accept it (alg/key-type confusion)
	CHECK(verify("id_es256", keys_from("jwks_rs256.json")).error != VerifyError::kNone);
}

TEST_CASE("keycloak: aud as an array with azp set to our client is accepted") {
	const auto claims = tokens().claims("id_audience_array");
	REQUIRE(claims["aud"].is_array());
	CHECK(claims["azp"] == tokens().client_id());
	const auto r = verify("id_audience_array", keys_from("jwks_rs256.json"));
	REQUIRE_MESSAGE(r.ok(), r.detail);
}

TEST_CASE("keycloak: a refresh-derived token has no nonce and is accepted with any expected nonce") {
	const auto claims = tokens().claims("id_refresh_no_nonce");
	CHECK_FALSE(claims.contains("nonce"));
	CHECK(verify("id_refresh_no_nonce", keys_from("jwks_rs256.json"), keycloak_config(), "whatever").ok());
	CHECK(verify("id_refresh_no_nonce", keys_from("jwks_rs256.json"), keycloak_config(), "").ok());
	// while the code-flow token is bound to its connection
	CHECK(verify("id_rs256", keys_from("jwks_rs256.json"), keycloak_config(), "someone-elses").error ==
			VerifyError::kNonce);
}

// ---------------------------------------------------------------------------------------------
// reject

TEST_CASE("keycloak: azp naming another client, and an aud without ours, are rejected") {
	const KeySet keys = keys_from("jwks_rs256.json");
	CHECK(verify("id_azp_other", keys).error == VerifyError::kAuthorizedParty);
	CHECK(verify("id_aud_without_client", keys).error == VerifyError::kAudience);
}

TEST_CASE("keycloak: a token signed by a retired key is refused, one by the new key accepted") {
	const KeySet retired = keys_from("jwks_retired.json");
	CHECK(retired.size() == 1);
	CHECK(verify("id_old_key_after_rotation", retired).error == VerifyError::kUnknownKid);
	CHECK(verify("id_new_key", retired).ok());
	// while the old key is still published (rotation, not retirement) both verify
	const KeySet both = keys_from("jwks_rotated.json");
	CHECK(both.size() == 2);
	CHECK(verify("id_old_key_after_rotation", both).ok());
	CHECK(verify("id_new_key", both).ok());
}

TEST_CASE("keycloak: an issuer with a trailing slash is not our issuer") {
	CHECK(verify("id_issuer_trailing_slash", keys_from("jwks_rs256.json")).error == VerifyError::kIssuer);
}

// Rule 1b (decided in docs/auth-keycloak-testing.md §6 Q1): an access or logout token is not a login,
// even when an audience mapper makes it pass aud/azp/iss and the realm key signed it.
TEST_CASE("keycloak: only typ == ID is a login (rule 1b)") {
	const KeySet keys = keys_from("jwks_rs256.json");

	// The dangerous case: audience mapper on, so the access token's aud contains our client.
	const auto access = tokens().claims("access_token_with_audience");
	REQUIRE(access["aud"].is_array());
	CHECK(access["azp"] == tokens().client_id());
	const auto r = verify("access_token_with_audience", keys_from("jwks_rs256.json"));
	CHECK(r.error == VerifyError::kTokenType);
	CHECK(r.detail.find("Bearer") != std::string::npos); // the log says which type it was
	CHECK(public_reason(r.error) == "not accepted by this server");

	CHECK(verify("access_token", keys).error != VerifyError::kNone); // plain access token: aud "account"
	CHECK(verify("id_typ_bearer", keys).error == VerifyError::kTokenType);
	CHECK(verify("id_typ_logout", keys).error == VerifyError::kTokenType);
	CHECK(verify("id_typ_missing", keys).error == VerifyError::kTokenType);
	CHECK(verify("id_header_at_jwt", keys).error == VerifyError::kTokenType);
	CHECK(to_string(VerifyError::kTokenType) == "token_type");
	CHECK(verify("id_rs256", keys).ok());
}

TEST_CASE("typ rule: at+jwt in the JWS header is refused for every preset, any case") {
	KeySet keys = keys_from("jwks_rs256.json");
	// Re-use a fixture token's payload under a header we control is not possible without the key,
	// so use the fixture whose header says at+jwt and flip the preset.
	for (const Provider p : { Provider::kOidc, Provider::kKeycloak, Provider::kFirebase }) {
		AuthConfig c = keycloak_config();
		c.provider = p;
		const auto r = verify("id_header_at_jwt", keys, c);
		CHECK(r.error == VerifyError::kTokenType);
	}
	// A non-Keycloak preset does not demand typ == ID: the same payload without the header marker passes.
	AuthConfig oidc = keycloak_config();
	oidc.provider = Provider::kOidc;
	CHECK(verify("id_typ_missing", keys, oidc).ok());
	CHECK(verify("id_typ_bearer", keys, oidc).ok());
}

// ---------------------------------------------------------------------------------------------
// claims

TEST_CASE("keycloak: realm_access.roles and full-path groups reach Lua intact when allowlisted") {
	const KeySet keys = keys_from("jwks_rs256.json");
	const auto r = verify("id_audience_array", keys, keycloak_config({ "groups", "realm_access", "email" }));
	REQUIRE_MESSAGE(r.ok(), r.detail);
	const json exposed = json::parse(r.login.claims_json);
	CHECK(exposed["groups"] == json::array({ "/admins", "/players" }));
	CHECK(exposed["realm_access"]["roles"].is_array());
	CHECK(exposed["realm_access"]["roles"].size() == 4); // 3 defaults + moderator
	CHECK(exposed["email"] == "alice@example.test");

	// not listed => dropped
	const auto bare = verify("id_audience_array", keys, keycloak_config({ "email" }));
	REQUIRE(bare.ok());
	const json only = json::parse(bare.login.claims_json);
	CHECK(only.size() == 1);
	CHECK_FALSE(only.contains("groups"));
	CHECK_FALSE(only.contains("realm_access"));
}

TEST_CASE("keycloak: 300 groups are refused whole when allowlisted, never truncated") {
	const KeySet keys = keys_from("jwks_rs256.json");
	const std::string &t = tokens().token("id_many_groups");
	CHECK(t.size() < kMaxJwtBytes); // it fits the wire cap: the claims cap is what decides
	const auto with_groups = verify("id_many_groups", keys, keycloak_config({ "groups" }));
	CHECK(with_groups.error == VerifyError::kClaims);
	CHECK(with_groups.login.claims_json.empty());
	// the same token is fine when the pack does not ask for groups
	const auto without = verify("id_many_groups", keys, keycloak_config({ "email" }));
	CHECK(without.ok());
}

// ---------------------------------------------------------------------------------------------
// JWKS (AuthService + fixtures)

namespace {

struct KeycloakService {
	std::shared_ptr<KeycloakFixtureFetcher> http = std::make_shared<KeycloakFixtureFetcher>();
	std::int64_t clock = tokens().now();
	std::unique_ptr<AuthService> svc;

	KeycloakService() {
		AuthService::Options o;
		o.start_thread = false;
		o.clock = [this] { return clock; };
		svc = std::make_unique<AuthService>(keycloak_config(), http, o);
	}
	void load() {
		while (svc->pump()) {
		}
	}
	Verdict verify_now(const std::string &name) { return svc->verify_now(tokens().token(name), tokens().nonce()); }
};

} // namespace

TEST_CASE("keycloak service: the discovery document and JWKS load; the enc key is ignored") {
	KeycloakService f;
	f.load();
	REQUIRE(f.svc->ready());
	CHECK(f.http->requests(Route::kDiscovery) == 1);
	CHECK(f.http->requests(Route::kJwks) == 1);
	CHECK(f.verify_now("id_rs256").ok);
	// the published RSA-OAEP "enc" entry must not be usable as a signing key
	const json jwks = testing::fixture_json("jwks_rs256.json");
	REQUIRE(jwks["keys"].size() == 2);
	CHECK(jwks["keys"][1]["use"] == "enc");
	CHECK(parse_jwks(jwks.dump()).keys.size() == 1);
	CHECK(parse_jwks(jwks.dump()).skipped == 1);
}

TEST_CASE("keycloak service: a new kid after rotation triggers exactly one refresh") {
	KeycloakService f;
	f.load();
	REQUIRE(f.svc->ready());
	REQUIRE(f.http->requests(Route::kJwks) == 1);

	f.http->set_jwks("jwks_rotated.json"); // the realm rotated its key
	f.clock += 61;                         // past the unknown-kid rate limit
	auto first = f.svc->begin(tokens().token("id_new_key"), tokens().nonce());
	auto second = f.svc->begin(tokens().token("id_new_key"), tokens().nonce());
	CHECK_FALSE(first->poll().has_value());
	CHECK_FALSE(second->poll().has_value()); // both wait for the same fetch
	f.load();
	CHECK(f.http->requests(Route::kJwks) == 2);
	const auto v1 = first->poll();
	const auto v2 = second->poll();
	REQUIRE(v1.has_value());
	REQUIRE(v2.has_value());
	CHECK(v1->ok);
	CHECK(v2->ok);

	// the old key is still published, so old-key tokens verify without any further fetch
	CHECK(f.verify_now("id_old_key_after_rotation").ok);
	CHECK(f.http->requests(Route::kJwks) == 2);
}

TEST_CASE("keycloak service: repeated unknown kids cannot make the server hammer the IdP") {
	KeycloakService f;
	f.http->set_jwks("jwks_retired.json"); // the realm's old key is gone for good
	f.load();
	REQUIRE(f.svc->ready());
	REQUIRE(f.http->requests(Route::kJwks) == 1);
	auto attempt = [&] {
		const auto v = f.svc->begin(tokens().token("id_old_key_after_rotation"), tokens().nonce())->poll();
		return v.has_value() ? std::optional<bool>(v->ok) : std::nullopt;
	};
	for (int i = 0; i < 10; ++i) { // 50 s: inside the 60 s window, so the kid fails at once, no fetch
		CHECK(attempt() == std::optional<bool>(false));
		f.clock += 5;
	}
	f.load();
	CHECK(f.http->requests(Route::kJwks) == 1);

	f.clock += 20; // the window has passed: one refresh is allowed
	CHECK_FALSE(attempt().has_value()); // waits for it
	f.load();
	CHECK(f.http->requests(Route::kJwks) == 2);
	for (int i = 0; i < 10; ++i) { // and the next 50 s are quiet again
		CHECK(attempt() == std::optional<bool>(false));
		f.clock += 5;
	}
	f.load();
	CHECK(f.http->requests(Route::kJwks) == 2);
}

TEST_CASE("keycloak service: a JWKS outage keeps the keys it has and fails closed for the unknown kid") {
	KeycloakService f;
	f.load();
	REQUIRE(f.svc->ready());
	f.http->fail(Route::kJwks, 503);
	f.http->set_jwks("jwks_rotated.json");
	f.clock += 61;
	auto p = f.svc->begin(tokens().token("id_new_key"), tokens().nonce());
	CHECK_FALSE(p->poll().has_value());
	f.load();
	const auto v = p->poll();
	REQUIRE(v.has_value());
	CHECK_FALSE(v->ok);                // unknown kid, refresh failed: fails closed
	CHECK(f.svc->ready());             // ... but the cached keys still serve everyone else
	CHECK(f.verify_now("id_rs256").ok);

	f.http->heal(Route::kJwks); // Keycloak is back
	f.clock += 61; // past the backoff and the rate limit, still inside the fixtures' exp
	auto again = f.svc->begin(tokens().token("id_new_key"), tokens().nonce());
	CHECK_FALSE(again->poll().has_value());
	f.load();
	const auto v2 = again->poll();
	REQUIRE(v2.has_value());
	CHECK(v2->ok);
}

TEST_CASE("keycloak service: discovery problems fail closed") {
	auto discovery_with = [](auto edit) {
		json doc = testing::fixture_json("discovery.json");
		edit(doc);
		return doc.dump();
	};
	struct Case {
		const char *what;
		std::string body;
	};
	const Case cases[] = {
		{ "issuer mismatch", discovery_with([](json &d) { d["issuer"] = "https://evil.example/realms/e2e"; }) },
		{ "missing jwks_uri", discovery_with([](json &d) { d.erase("jwks_uri"); }) },
		{ "http jwks_uri off loopback", discovery_with([](json &d) { d["jwks_uri"] = "http://keycloak.example.test/certs"; }) },
		{ "not json", "<html>502 Bad Gateway</html>" },
	};
	for (const Case &c : cases) {
		CAPTURE(c.what);
		KeycloakService f;
		f.http->set_discovery_body(c.body);
		f.load();
		CHECK_FALSE(f.svc->ready());
		CHECK(f.http->requests(Route::kJwks) == 0); // never fetches keys from a document it distrusts
		const Verdict v = f.verify_now("id_rs256");
		CHECK_FALSE(v.ok);
		CHECK(v.reason == "authentication service unavailable");
	}
	{ // the IdP simply being down looks the same, and recovers
		KeycloakService f;
		f.http->fail(Route::kDiscovery, 0);
		f.load();
		CHECK_FALSE(f.svc->ready());
		f.http->heal(Route::kDiscovery);
		f.clock += 61; // past the backoff and the rate limit, still inside the fixtures' exp
		f.load();
		CHECK(f.svc->ready());
		CHECK(f.verify_now("id_rs256").ok);
	}
}

#endif // VB_WITH_AUTH
