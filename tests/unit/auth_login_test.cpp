// In-engine authentication, step 9.4 (architecture_spec/auth.md §5.5/§6): the
// verified login reaches pack scripts, names are made unique, a second
// sign-in of the same account kicks the older session. Driven over the
// loopback transport with a fake verifier (the real one is auth_verifier_test).

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"

#if VB_WITH_LUA

using namespace vb::net;

namespace {

std::filesystem::path temp_storage(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_auth_login_") + name + ".json");
	std::filesystem::remove(p);
	return p;
}

// Token format of the fake IdP: "<name>|<subject>|<claims-json>"; "bad" is rejected.
AuthTicket fake_verify(std::string_view token, std::string_view nonce) {
	AuthOutcome out;
	if (token == "bad" || nonce != "nonce-1") {
		out = AuthOutcome{ false, "not accepted by this server", {}, {} };
	} else {
		const std::string t(token);
		const auto p1 = t.find('|');
		const auto p2 = t.find('|', p1 + 1);
		auto login = std::make_shared<LoginData>();
		login->provider = "keycloak";
		login->issuer = "https://idp.example";
		login->name = t.substr(0, p1);
		login->subject = t.substr(p1 + 1, p2 - p1 - 1);
		login->claims_json = t.substr(p2 + 1);
		out = AuthOutcome{ true, {}, login->name, login };
	}
	return [out] { return std::optional<AuthOutcome>(out); };
}

struct AuthRig {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	std::unique_ptr<vb::script::PackRuntime> rt;
	std::unique_ptr<ServerSession> server;
	std::vector<std::unique_ptr<ClientSession>> clients;
	std::vector<std::pair<Transport *, ConnId>> links;

	AuthRig(const char *name, const std::string &pack, bool external) {
		rt = std::make_unique<vb::script::PackRuntime>(net.server(), registry,
				temp_storage(name));
		rt->set_auth_required(external);
		REQUIRE(rt->load_pack_file(pack));
		rt->freeze();
		HandshakeServerConfig cfg;
		cfg.world_seed = 7;
		HandshakeServerHost host;
		if (external) {
			cfg.auth_mode = vb::protocol::AuthMode::kExternal;
			host.auth_challenge = [] {
				vb::protocol::S2CAuthChallenge c;
				c.provider = "keycloak";
				c.nonce = "nonce-1";
				return std::optional<vb::protocol::S2CAuthChallenge>(c);
			};
			host.begin_authenticate = fake_verify;
		}
		rt->install_join_veto(host);
		server = std::make_unique<ServerSession>(net.server(), cfg, host);
		rt->attach_session(*server);
		REQUIRE(net.server().listen(0));
	}

	ClientSession &connect(const std::string &name, const std::string &token) {
		Transport &t = net.create_client();
		auto id = t.connect("x", 0);
		REQUIRE(id);
		HandshakeClientConfig cc;
		cc.player_name = name;
		cc.token = token;
		cc.client_nonce = clients.size() + 1;
		clients.push_back(std::make_unique<ClientSession>(t, *id, cc));
		links.emplace_back(&t, *id);
		return *clients.back();
	}

	void disconnect(const ClientSession &c) {
		for (std::size_t i = 0; i < clients.size(); ++i) {
			if (clients[i].get() == &c) {
				links[i].first->close(links[i].second, "bye");
			}
		}
	}

	void pump(int n = 30) {
		for (int i = 0; i < n; ++i) {
			server->tick(0.05);
			for (auto &c : clients) {
				c->tick(0.05);
			}
			for (const auto &j : server->take_joins()) {
				rt->dispatch_player_join_completed(j);
			}
			for (const auto &l : server->take_leaves()) {
				rt->dispatch_player_leave(l);
			}
		}
	}
};

constexpr const char *kRecordingPack = R"(
	join_calls = {}
	leave_logins = {}
	vb.on("player_join", function(name, login)
		join_calls[#join_calls + 1] = { name = name, login = login }
	end)
	vb.on("player_leave", function(player)
		local l = player:get_login()
		leave_logins[#leave_logins + 1] = (l == nil) and "nil" or l.subject
	end)
)";

} // namespace

TEST_CASE("auth pack: player_join and get_login see the verified, frozen login") {
	AuthRig rig("frozen", kRecordingPack, /*external=*/true);
	auto &alice = rig.connect("ignored-client-name",
			R"(alice|sub-a|{"email":"a@x.io","groups":["mod","dev"]})");
	rig.pump();
	REQUIRE(alice.joined());

	REQUIRE(rig.rt->load_pack_file(R"(
		assert(vb.auth.required() == true)
		assert(#join_calls == 1)
		local c = join_calls[1]
		-- The in-game name comes from the verified claim, not the client's.
		assert(c.name == "alice")
		local l = c.login
		assert(l ~= nil)
		assert(l.provider == "keycloak" and l.subject == "sub-a" and l.name == "alice")
		assert(l.claims.email == "a@x.io" and l.claims.groups[2] == "dev")
		-- Frozen: no writes, no swapping the metatable, no engine-internal fields.
		assert(not pcall(function() l.name = "root" end))
		assert(not pcall(function() l.claims.email = "x" end))
		assert(not pcall(function() l.claims.groups[1] = "admin" end))
		assert(getmetatable(l) == false)
		assert(l.issuer == nil and l.token == nil and l.expires_at == nil)
		local n = 0
		for k in pairs(l) do n = n + 1 end
		assert(n == 4)
	)"));

	// Every Player the script can obtain has a non-nil login, even while it
	// leaves: player_leave runs after the session dropped the connection.
	rig.disconnect(alice);
	rig.pump();
	REQUIRE(rig.rt->load_pack_file(R"(
		assert(#leave_logins == 1 and leave_logins[1] == "sub-a")
	)"));
}

TEST_CASE("plain pack: login is nil everywhere and vb.auth.required() is false") {
	AuthRig rig("plain", kRecordingPack, /*external=*/false);
	auto &bob = rig.connect("Bob", "");
	rig.pump();
	REQUIRE(bob.joined());
	rig.disconnect(bob);
	rig.pump();
	REQUIRE(rig.rt->load_pack_file(R"(
		assert(vb.auth.required() == false)
		assert(#join_calls == 1 and join_calls[1].name == "Bob" and join_calls[1].login == nil)
		assert(#leave_logins == 1 and leave_logins[1] == "nil")
	)"));
}

TEST_CASE("auth pack: a rejected token never reaches the asset manifest or the world") {
	AuthRig rig("rejected", kRecordingPack, true);
	auto &mallory = rig.connect("Mallory", "bad");
	rig.pump();
	CHECK(mallory.failed());
	CHECK_FALSE(mallory.joined());
	CHECK(rig.server->player_count() == 0);
	REQUIRE(rig.rt->load_pack_file("assert(#join_calls == 0)"));
}

TEST_CASE("auth pack: player_join can veto on the login") {
	AuthRig rig("veto", R"(
		vb.on("player_join", function(name, login)
			return login.claims.verified == true
		end)
	)",
			true);
	auto &ok = rig.connect("x", R"(good|s1|{"verified":true})");
	auto &no = rig.connect("x", R"(unverified|s2|{"verified":false})");
	rig.pump();
	CHECK(ok.joined());
	CHECK(no.failed());
}

TEST_CASE("auth pack: two people called alex get alex and alex#2") {
	AuthRig rig("names", kRecordingPack, true);
	auto &a = rig.connect("c", "alex|sub-1|{}");
	rig.pump();
	auto &b = rig.connect("c", "alex|sub-2|{}");
	rig.pump();
	REQUIRE(a.joined());
	REQUIRE(b.joined());
	REQUIRE(rig.rt->load_pack_file(R"(
		assert(#join_calls == 2)
		assert(join_calls[1].name == "alex" and join_calls[1].login.name == "alex")
		assert(join_calls[2].name == "alex#2" and join_calls[2].login.name == "alex#2")
		assert(join_calls[2].login.subject == "sub-2")
	)"));
}

TEST_CASE("auth pack: the same account signing in again kicks the older session") {
	AuthRig rig("dup", kRecordingPack, true);
	auto &first = rig.connect("c", "carol|sub-c|{}");
	rig.pump();
	REQUIRE(first.joined());
	CHECK(rig.server->player_count() == 1);

	auto &second = rig.connect("c", "carol|sub-c|{}");
	rig.pump();
	CHECK(second.joined()); // the newcomer is never refused
	CHECK(rig.server->player_count() == 1);
	REQUIRE(rig.rt->load_pack_file(R"(
		-- The older session left; the newcomer keeps the plain name (the kicked
		-- session does not make it "carol#2").
		assert(#leave_logins == 1 and leave_logins[1] == "sub-c")
		assert(join_calls[2].name == "carol")
	)"));
}

#endif // VB_WITH_LUA
