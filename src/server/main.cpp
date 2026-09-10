// voxel_browser_server — authoritative, headless.
//
// Phase 0: argument handling, build banner, and a fixed-timestep tick loop
// skeleton (default 20 Hz, see ARCHITECTURE_SPEC.md §7). Networking, ECS, world
// generation and replication are wired in from Phase 1 onward.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"

namespace {

std::atomic_bool g_stop{ false };

extern "C" void handle_signal(int) {
	g_stop.store(true, std::memory_order_relaxed);
}

void print_usage() {
	std::cout << "Usage: voxel_browser_server [options]\n"
				 "\n"
				 "  --bind <addr>          bind address (default 0.0.0.0)\n"
				 "  --port <n>             UDP port (default 27015)\n"
				 "  --content-pack <dir>   content pack directory (default content/base)\n"
				 "  --tick-rate <hz>       simulation tick rate (default 20)\n"
				 "  --ticks <n>            run n ticks then exit (0 = run forever)\n"
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

	const std::string bind = args.value_or("bind", "0.0.0.0");
	const int port = args.int_or("port", 27015);
	const std::string pack = args.value_or("content-pack", "content/base");
	int tick_rate = args.int_or("tick-rate", 20);
	if (tick_rate < 1 || tick_rate > 240) {
		std::cerr << "server: tick-rate out of range (1..240), using 20\n";
		tick_rate = 20;
	}
	const long long max_ticks = args.int_or("ticks", 0);

	std::signal(SIGINT, handle_signal);
	std::signal(SIGTERM, handle_signal);

	std::cout << vb::core::describe_build() << '\n'
			  << "server: listening target " << bind << ':' << port << '\n'
			  << "server: content pack '" << pack << "'\n"
			  << "server: tick rate " << tick_rate << " Hz\n"
			  << "server: transport/ECS/worldgen not yet implemented (Phase 1+)\n";

	const auto tick_dt = std::chrono::nanoseconds(std::chrono::seconds(1)) / tick_rate;
	auto next = std::chrono::steady_clock::now();
	long long tick = 0;

	while (!g_stop.load(std::memory_order_relaxed)) {
		// --- one simulation step (systems run here from Phase 1) -------------
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
