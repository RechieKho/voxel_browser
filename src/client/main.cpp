// voxel_browser — the client ("browser").
//
// Phase 0: argument handling, build banner, a raylib window and a render-loop
// scaffold drawing a placeholder grid until chunk meshing lands (Phase 2).
// A --headless build/run performs no GL work so CI and integration tests can
// exercise the same code path without a GPU.

#include <cstdlib>
#include <iostream>
#include <string>

#include <raylib.h>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/render/window.hpp"

namespace {

void print_usage() {
	std::cout << "Usage: voxel_browser [options]\n"
				 "\n"
				 "  --server <addr>   server address to connect to (default 127.0.0.1)\n"
				 "  --port <n>        server port (default 27015)\n"
				 "  --name <name>     player name (default Player)\n"
				 "  --headless        run without a window (no rendering)\n"
				 "  --frames <n>      headless: run n frames then exit (default 3)\n"
				 "  --width <n>       window width (default 1280)\n"
				 "  --height <n>      window height (default 720)\n"
				 "  --version        print build info and exit\n"
				 "  --help           show this help\n";
}

void draw_placeholder_world(const Camera3D &camera, const std::string &status) {
	BeginMode3D(camera);
	DrawGrid(32, 1.0f);
	DrawCube(Vector3{ 0.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, Color{ 120, 180, 90, 255 });
	DrawCubeWires(Vector3{ 0.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, DARKGRAY);
	EndMode3D();

	DrawText("voxel_browser — Phase 0 scaffold", 12, 12, 20, RAYWHITE);
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
	const bool headless = args.has("headless") || VB_HEADLESS_DEFAULT;

	std::cout << vb::core::describe_build() << '\n'
			  << "client: target server " << server << ':' << port
			  << " as '" << name << "'\n"
			  << "client: " << (headless ? "headless" : "windowed")
			  << " mode; networking not yet implemented (Phase 1+)\n";

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

	const std::string status = "disconnected — " + server + ":" + std::to_string(port);

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
