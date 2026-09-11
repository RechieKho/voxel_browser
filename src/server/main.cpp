// voxel_browser_server — authoritative, headless.
//
// Real transport (GameNetworkingSockets over UDP, spec §8.1) + world/session
// wiring landed in Phase 1.2: this binary generates terrain, streams it, and
// simulates players exactly like the `--singleplayer` integrated path, just
// over a real listen socket instead of loopback. Lua content (Phase 4) still
// needs to land before anything gameplay-facing is pack-defined.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/core/config.hpp"
#include "vb/core/log.hpp"
#include "vb/net/gns_transport.hpp"
#include "vb/net/session.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

namespace {

std::atomic_bool g_stop{ false };

extern "C" void handle_signal(int) {
	g_stop.store(true, std::memory_order_relaxed);
}

void print_usage() {
	std::cout << "Usage: voxel_browser_server [options]\n"
				 "\n"
				 "  --config <path>        server.toml to load (default server.toml)\n"
				 "  --bind <addr>          bind address override (currently always binds any interface)\n"
				 "  --port <n>             UDP port override\n"
				 "  --content-pack <dir>   content pack directory override\n"
				 "  --tick-rate <hz>       simulation tick rate override\n"
				 "  --max-players <n>      player cap override\n"
				 "  --seed <n>             world seed override (0 = random)\n"
				 "  --motd <text>          message of the day override\n"
				 "  --ticks <n>            run n ticks then exit (0 = forever)\n"
				 "  --version             print build info and exit\n"
				 "  --help                show this help\n";
}

std::uint64_t random_seed() {
	std::random_device rd;
	return (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
}

} // namespace

int main(int argc, char **argv) {
	const vb::core::Args args(argc, argv);

	if (args.has("help", 'h')) {
		print_usage();
		return EXIT_SUCCESS;
	}
	if (args.has("version", 'v')) {
		std::cout << vb::core::describe_build() << '\n';
		return EXIT_SUCCESS;
	}

	const std::string config_path = args.value_or("config", "server.toml");
	auto loaded = vb::core::load_server_config(config_path);
	if (!loaded) {
		std::cerr << "server: failed to load " << config_path << ": "
				  << vb::core::message(loaded.error()) << '\n';
		return EXIT_FAILURE;
	}
	vb::core::ServerConfig config = *loaded;
	vb::core::apply_cli_overrides(config, args);

	const long long max_ticks = args.int_or("ticks", 0);
	const std::uint64_t seed = config.world_seed != 0 ? config.world_seed : random_seed();

	vb::world::World world(vb::world::BlockRegistry::base());
	vb::worldgen::WorldGenParams gen_params;
	gen_params.seed = seed;
	vb::worldgen::WorldGenWorkerPool pool(
			vb::worldgen::WorldGenerator(gen_params, vb::world::BlockRegistry::base()));

	vb::net::GnsTransport transport;
	auto listen_status = transport.listen(config.port);
	if (!listen_status) {
		// A build without VB_WITH_NET can still run the simulation (worldgen,
		// ticking, etc.) for testing/CI purposes -- it just can never accept a
		// real connection. Any other failure (port in use, bind denied) is
		// fatal: there is no point ticking a server nobody can ever reach.
		if (listen_status.error() == vb::core::NetError::kBackendUnavailable) {
			std::cerr << "server: built without VB_WITH_NET -- running the "
						 "simulation with no network backend (no one can connect)\n";
		} else {
			std::cerr << "server: failed to listen on " << config.bind_address
					  << ':' << config.port << ": "
					  << vb::core::message(listen_status.error()) << '\n';
			return EXIT_FAILURE;
		}
	}

	vb::net::HandshakeServerConfig hs_config;
	hs_config.pack_name = "base";
	hs_config.tick_rate = static_cast<std::uint16_t>(config.tick_rate);
	hs_config.motd = config.motd;
	hs_config.auth_mode = static_cast<vb::protocol::AuthMode>(config.auth_mode);
	hs_config.max_players = config.max_players;
	hs_config.world_seed = seed;

	vb::net::ServerSession session(transport, hs_config);
	session.set_world_replicator(std::make_unique<vb::net::WorldReplicator>(world,
			pool, vb::world::BlockRegistry::base(),
			static_cast<int>(config.view_distance), 3));

	vb::physics::MoveParams move_params;
	move_params.gravity = config.gravity;
	session.set_move_params(move_params);

	std::signal(SIGINT, handle_signal);
	std::signal(SIGTERM, handle_signal);

	std::cout << vb::core::describe_build() << '\n'
			  << "server: bind " << config.bind_address << ':' << config.port
			  << " (bound port " << transport.bound_port() << "), pack '"
			  << config.content_pack << "', " << config.tick_rate << " Hz, max "
			  << config.max_players << " players, seed " << seed << '\n';

	const auto tick_dt =
			std::chrono::nanoseconds(std::chrono::seconds(1)) / config.tick_rate;
	const double tick_dt_seconds = std::chrono::duration<double>(tick_dt).count();
	auto next = std::chrono::steady_clock::now();
	long long tick = 0;

	while (!g_stop.load(std::memory_order_relaxed)) {
		++tick;
		session.tick(tick_dt_seconds);

		for (const auto &joined : session.take_joins()) {
			std::cout << "server: '" << joined.name << "' joined (net id "
					  << static_cast<std::uint32_t>(joined.net_id) << ")\n";
		}
		for (const auto &left : session.take_leaves()) {
			std::cout << "server: a player left (" << left.reason << ")\n";
		}

		if (max_ticks > 0 && tick >= max_ticks) {
			break;
		}

		next += tick_dt;
		std::this_thread::sleep_until(next);
	}

	std::cout << "server: stopped after " << tick << " ticks\n";
	return EXIT_SUCCESS;
}
