// voxel_browser — the client ("browser").
//
// Phase 1: argument handling, build banner, a raylib window + render-loop
// scaffold, and a working connection handshake. `--singleplayer` spins up an
// in-process server over a loopback transport and completes the join handshake
// against it (spec §3 — one code path for single- and multiplayer).
// `--headless` performs no GL work so CI and integration tests share the path.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <raylib.h>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/net/integrated.hpp"
#include "vb/render/window.hpp"

namespace {

void print_usage() {
	std::cout << "Usage: voxel_browser [options]\n"
				 "\n"
				 "  --server <addr>   server address to connect to (default 127.0.0.1)\n"
				 "  --port <n>        server port (default 27015)\n"
				 "  --name <name>     player name (default Player)\n"
				 "  --singleplayer    run an in-process server and join it\n"
				 "  --headless        run without a window (no rendering)\n"
				 "  --frames <n>      headless: run n frames then exit (default 3)\n"
				 "  --width <n>       window width (default 1280)\n"
				 "  --height <n>      window height (default 720)\n"
				 "  --version        print build info and exit\n"
				 "  --help           show this help\n";
}

// Runs the integrated (loopback) server + client until the handshake settles.
// Returns true on join. Fills `summary` for the HUD / stdout either way.
bool run_singleplayer_handshake(const std::string &name, std::string &summary) {
	vb::net::HandshakeServerConfig server_cfg;
	server_cfg.pack_name = "base";
	server_cfg.motd = "integrated singleplayer";

	vb::net::HandshakeClientConfig client_cfg;
	client_cfg.player_name = name;
	client_cfg.client_version = vb::kVersionString;

	vb::net::IntegratedGame game(server_cfg, client_cfg);
	for (int i = 0; i < 32 && !game.client_joined() && !game.client_failed();
			++i) {
		game.tick(0.05);
	}

	if (game.client_joined()) {
		const auto &accept = game.client().join_accept();
		summary = "joined singleplayer as net id " +
				std::to_string(static_cast<std::uint32_t>(accept->your_net_id)) +
				" (seed " + std::to_string(accept->world_seed) + ")";
		return true;
	}
	summary = "singleplayer handshake failed: " + game.client().failure_reason();
	return false;
}

void draw_placeholder_world(const Camera3D &camera, const std::string &status) {
	BeginMode3D(camera);
	DrawGrid(32, 1.0f);
	DrawCube(Vector3{ 0.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, Color{ 120, 180, 90, 255 });
	DrawCubeWires(Vector3{ 0.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, DARKGRAY);
	EndMode3D();

	DrawText("voxel_browser — Phase 1 scaffold", 12, 12, 20, RAYWHITE);
	DrawText(status.c_str(), 12, 38, 18, Color{ 170, 170, 180, 255 });
	DrawFPS(12, 64);
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

	const std::string server = args.value_or("server", "127.0.0.1");
	const int port = args.int_or("port", 27015);
	const std::string name = args.value_or("name", "Player");
	const bool singleplayer = args.has("singleplayer");
	const bool headless = args.has("headless") || VB_HEADLESS_DEFAULT;

	std::cout << vb::core::describe_build() << '\n'
			  << "client: " << (headless ? "headless" : "windowed") << " mode\n";

	std::string status;
	if (singleplayer) {
		const bool ok = run_singleplayer_handshake(name, status);
		std::cout << "client: " << status << '\n';
		if (!ok) {
			return EXIT_FAILURE;
		}
	} else {
		status = "not connected (" + server + ":" + std::to_string(port) +
				") — remote transport lands with VB_WITH_NET";
		std::cout << "client: " << status << '\n';
	}

	vb::render::WindowConfig cfg;
	cfg.headless = headless;
	cfg.width = args.int_or("width", 1280);
	cfg.height = args.int_or("height", 720);
	cfg.title = "voxel_browser";
	cfg.headless_frame_limit =
			headless ? static_cast<std::uint64_t>(args.int_or("frames", 3)) : 0;

	vb::render::Window window(cfg);

	Camera3D camera{};
	camera.position = Vector3{ 8.0f, 8.0f, 8.0f };
	camera.target = Vector3{ 0.0f, 0.5f, 0.0f };
	camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
	camera.fovy = 60.0f;
	camera.projection = CAMERA_PERSPECTIVE;

	while (!window.should_close()) {
		window.begin_frame();
		if (!window.headless()) {
			draw_placeholder_world(camera, status);
		}
		window.end_frame();
	}

	std::cout << "client: exited after " << window.frame_count() << " frames\n";
	return EXIT_SUCCESS;
}
