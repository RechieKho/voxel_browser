// voxel_browser_server — authoritative, headless.
//
// Phase 1: config loading (server.toml + CLI overrides), build banner, and a
// fixed-timestep tick loop skeleton (default 20 Hz, spec §7). A real transport
// (GameNetworkingSockets) and the ECS / world / replication systems are wired
// in from Phase 1.2 onward.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/core/config.hpp"
#include "vb/core/log.hpp"

namespace {

std::atomic_bool g_stop{ false };

extern "C" void handle_signal(int) {
	g_stop.store(true, std::memory_order_relaxed);
}

void print_usage() {
	std::cout << "Usage: voxel_browser_server [options]\n"
				 "\n"
				 "  --config <path>        server.toml to load (default server.toml)\n"
				 "  --bind <addr>          bind address override\n"
				 "  --port <n>             UDP port override\n"
				 "  --content-pack <dir>   content pack directory override\n"
				 "  --tick-rate <hz>       simulation tick rate override\n"
				 "  --max-players <n>      player cap override\n"
				 "  --seed <n>             world seed override\n"
				 "  --motd <text>          message of the day override\n"
				 "  --ticks <n>            run n ticks then exit (0 = forever)\n"
				 "  --version             print build info and exit\n"
				 "  --help                show this help\n";
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

	std::signal(SIGINT, handle_signal);
	std::signal(SIGTERM, handle_signal);

	std::cout << vb::core::describe_build() << '\n'
			  << "server: bind " << config.bind_address << ':' << config.port
			  << ", pack '" << config.content_pack << "', " << config.tick_rate
			  << " Hz, max " << config.max_players << " players\n"
			  << "server: transport/ECS/worldgen not yet implemented (Phase 1.2+)\n";

	const auto tick_dt =
			std::chrono::nanoseconds(std::chrono::seconds(1)) / config.tick_rate;
	auto next = std::chrono::steady_clock::now();
	long long tick = 0;

	while (!g_stop.load(std::memory_order_relaxed)) {
		// --- one simulation step (systems run here from Phase 1.4) -----------
		++tick;

		if (max_ticks > 0 && tick >= max_ticks) {
			break;
		}

		next += tick_dt;
		std::this_thread::sleep_until(next);
	}

	std::cout << "server: stopped after " << tick << " ticks\n";
	return EXIT_SUCCESS;
}
