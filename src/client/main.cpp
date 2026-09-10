// voxel_browser — the client ("browser").
//
// Phase 1: build banner, raylib window + first-person camera, a debug overlay,
// and a working connection handshake. `--singleplayer` runs an in-process
// server over a loopback transport and joins it (spec §3 — one code path for
// single- and multiplayer). `--headless` does no GL work so CI and integration
// tests share this path.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include <raylib.h>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/net/integrated.hpp"
#include "vb/render/camera.hpp"
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
				 "  --fov <deg>       vertical field of view (default 70)\n"
				 "  --version        print build info and exit\n"
				 "  --help           show this help\n";
}

struct JoinInfo {
	bool joined = false;
	std::uint32_t net_id = 0;
	std::uint64_t world_seed = 0;
	vb::core::Vec3d spawn{ 0.0, 66.0, 0.0 };
	std::string detail;
};

// Runs the integrated (loopback) server + client until the handshake settles.
JoinInfo run_singleplayer_handshake(const std::string &name) {
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

	JoinInfo info;
	if (game.client_joined()) {
		const auto &accept = *game.client().join_accept();
		info.joined = true;
		info.net_id = static_cast<std::uint32_t>(accept.your_net_id);
		info.world_seed = accept.world_seed;
		info.spawn = accept.spawn_pos;
		info.detail = "joined singleplayer as net id " +
				std::to_string(info.net_id);
	} else {
		info.detail = "singleplayer handshake failed: " +
				game.client().failure_reason();
	}
	return info;
}

vb::render::LookMoveInput sample_input(bool mouse_captured) {
	vb::render::LookMoveInput in;
	if (mouse_captured) {
		const Vector2 d = GetMouseDelta();
		in.look_delta = { static_cast<double>(d.x), static_cast<double>(d.y) };
	}
	if (IsKeyDown(KEY_W)) {
		in.move_axis.z += 1.0;
	}
	if (IsKeyDown(KEY_S)) {
		in.move_axis.z -= 1.0;
	}
	if (IsKeyDown(KEY_D)) {
		in.move_axis.x += 1.0;
	}
	if (IsKeyDown(KEY_A)) {
		in.move_axis.x -= 1.0;
	}
	if (IsKeyDown(KEY_SPACE)) {
		in.move_axis.y += 1.0;
	}
	if (IsKeyDown(KEY_LEFT_CONTROL)) {
		in.move_axis.y -= 1.0;
	}
	in.sprint = IsKeyDown(KEY_LEFT_SHIFT);
	return in;
}

Camera3D to_camera(const vb::render::FirstPersonController &c, float fovy) {
	const vb::core::Vec3d p = c.position();
	const vb::core::Vec3d t = c.target();
	Camera3D cam{};
	cam.position = { static_cast<float>(p.x), static_cast<float>(p.y),
		static_cast<float>(p.z) };
	cam.target = { static_cast<float>(t.x), static_cast<float>(t.y),
		static_cast<float>(t.z) };
	cam.up = { 0.0f, 1.0f, 0.0f };
	cam.fovy = fovy;
	cam.projection = CAMERA_PERSPECTIVE;
	return cam;
}

void draw_scene(const Camera3D &camera) {
	BeginMode3D(camera);
	DrawGrid(64, 1.0f);
	DrawCube(Vector3{ 0.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, Color{ 120, 180, 90, 255 });
	DrawCubeWires(Vector3{ 0.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, DARKGRAY);
	EndMode3D();
}

void draw_overlay(const vb::render::FirstPersonController &c,
		const std::string &status, bool mouse_captured) {
	const vb::core::Vec3d p = c.position();
	char line[160];
	DrawText("voxel_browser", 12, 12, 20, RAYWHITE);
	DrawText(status.c_str(), 12, 38, 18, Color{ 170, 200, 170, 255 });

	std::snprintf(line, sizeof(line), "pos  %.1f  %.1f  %.1f", p.x, p.y, p.z);
	DrawText(line, 12, 64, 18, Color{ 170, 170, 180, 255 });
	std::snprintf(line, sizeof(line), "look yaw %.0f  pitch %.0f", c.yaw(),
			c.pitch());
	DrawText(line, 12, 86, 18, Color{ 170, 170, 180, 255 });
	DrawText(mouse_captured ? "mouse captured (Tab to release)"
							: "click to capture mouse",
			12, 108, 16, Color{ 140, 140, 150, 255 });
	DrawFPS(12, 132);
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
	const float fov = static_cast<float>(args.int_or("fov", 70));

	std::cout << vb::core::describe_build() << '\n'
			  << "client: " << (headless ? "headless" : "windowed") << " mode\n";

	JoinInfo join;
	std::string status;
	if (singleplayer) {
		join = run_singleplayer_handshake(name);
		std::cout << "client: " << join.detail << '\n';
		if (!join.joined) {
			return EXIT_FAILURE;
		}
		status = join.detail + " (seed " + std::to_string(join.world_seed) + ")";
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

	vb::render::FirstPersonController controller;
	controller.set_position({ join.spawn.x, join.spawn.y + 1.7, join.spawn.z });
	controller.set_look(0.0, -10.0);

	bool mouse_captured = false;

	while (!window.should_close()) {
		const double dt = window.headless() ? 1.0 / 60.0
											: static_cast<double>(GetFrameTime());

		if (!window.headless()) {
			if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE)) {
				mouse_captured = false;
				EnableCursor();
			} else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouse_captured) {
				mouse_captured = true;
				DisableCursor();
			}
		}

		controller.update(sample_input(mouse_captured), dt);

		window.begin_frame();
		if (!window.headless()) {
			draw_scene(to_camera(controller, fov));
			draw_overlay(controller, status, mouse_captured);
		}
		window.end_frame();
	}

	std::cout << "client: exited after " << window.frame_count() << " frames\n";
	return EXIT_SUCCESS;
}
