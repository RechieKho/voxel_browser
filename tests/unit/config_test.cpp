#include <doctest/doctest.h>

#include <ostream>

#include <string>
#include <vector>

#include "vb/core/cli.hpp"
#include "vb/core/config.hpp"

using namespace vb::core;

namespace {

Args make_args(std::initializer_list<const char *> argv) {
	std::vector<char *> v;
	for (const char *s : argv) {
		v.push_back(const_cast<char *>(s));
	}
	return Args(static_cast<int>(v.size()), v.data());
}

} // namespace

TEST_CASE("server config: defaults when the document is empty") {
	auto c = parse_server_config("");
	REQUIRE(c);
	CHECK(c->port == 27015);
	CHECK(c->tick_rate == 20);
	CHECK(c->auth_mode == ConfigAuthMode::kNone);
	CHECK(c->content_pack == "content/base");
}

TEST_CASE("server config: values are read from TOML") {
	auto c = parse_server_config(R"(
		bind_address = "127.0.0.1"
		port = 28000
		max_players = 4
		tick_rate = 30
		world_seed = 123456789
		gravity = 19.5
		auth_mode = "token"
		motd = "hi"
	)");
	REQUIRE(c);
	CHECK(c->bind_address == "127.0.0.1");
	CHECK(c->port == 28000);
	CHECK(c->max_players == 4);
	CHECK(c->tick_rate == 30);
	CHECK(c->world_seed == 123456789u);
	CHECK(c->gravity == doctest::Approx(19.5));
	CHECK(c->auth_mode == ConfigAuthMode::kToken);
	CHECK(c->motd == "hi");
}

TEST_CASE("server config: malformed TOML is an error") {
	auto c = parse_server_config("port = = 5");
	CHECK_FALSE(c);
	CHECK(c.error() == CoreError::kParseError);
}

TEST_CASE("server config: out-of-range tick rate is clamped") {
	auto c = parse_server_config("tick_rate = 9000");
	REQUIRE(c);
	CHECK(c->tick_rate == 240);
}

TEST_CASE("server config: an out-of-range port is ignored, default kept") {
	auto c = parse_server_config("port = 99999");
	REQUIRE(c);
	CHECK(c->port == 27015);
}

TEST_CASE("client config: reads values and recent servers list") {
	auto c = parse_client_config(R"(
		window_width = 1920
		vsync = false
		fov = 90
		mouse_sensitivity = 0.2
		player_name = "Ada"
		recent_servers = ["a.example:27015", "b.example:27016"]
	)");
	REQUIRE(c);
	CHECK(c->window_width == 1920);
	CHECK_FALSE(c->vsync);
	CHECK(c->fov == doctest::Approx(90.0));
	CHECK(c->mouse_sensitivity == doctest::Approx(0.2));
	CHECK(c->player_name == "Ada");
	REQUIRE(c->recent_servers.size() == 2);
	CHECK(c->recent_servers[1] == "b.example:27016");
}

TEST_CASE("CLI overrides win over the file") {
	auto c = parse_server_config("port = 28000\ntick_rate = 30");
	REQUIRE(c);
	ServerConfig cfg = *c;

	const auto args = make_args({ "srv", "--port", "40000", "--motd", "cli" });
	apply_cli_overrides(cfg, args);
	CHECK(cfg.port == 40000);
	CHECK(cfg.tick_rate == 30); // untouched by CLI
	CHECK(cfg.motd == "cli");
}

TEST_CASE("client CLI overrides") {
	ClientConfig cfg;
	const auto args = make_args({ "cl", "--name", "Zed", "--fov", "100" });
	apply_cli_overrides(cfg, args);
	CHECK(cfg.player_name == "Zed");
	CHECK(cfg.fov == doctest::Approx(100.0));
}
