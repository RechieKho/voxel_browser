// voxel_browser — the client ("browser").
//
// Phase 2: raylib window + first-person camera + debug overlay, a working
// connection handshake, and streamed/meshed voxel terrain. `--singleplayer`
// runs an in-process server (worldgen + replication) over a loopback transport
// and joins it (spec §3 — one code path for single- and multiplayer).
// `--headless` does no GL work so CI and integration tests share this path.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include <raylib.h>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/core/config.hpp"
#include "vb/net/integrated.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/protocol/input.hpp"
#include "vb/protocol/world.hpp"
#include "vb/render/camera.hpp"
#include "vb/render/chunk_renderer.hpp"
#include "vb/render/entity_renderer.hpp"
#include "vb/render/window.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

namespace {

void print_usage() {
	std::cout << "Usage: voxel_browser [options]\n"
				 "\n"
				 "  --config <path>   client.toml to load (default client.toml)\n"
				 "  --server <addr>   server address to connect to (default 127.0.0.1)\n"
				 "  --port <n>        server port (default 27015)\n"
				 "  --name <name>     player name override\n"
				 "  --fov <deg>       vertical field of view override\n"
				 "  --render-distance <n>  view distance override (chunks)\n"
				 "  --singleplayer    run an in-process server and join it\n"
				 "  --headless        run without a window (no rendering)\n"
				 "  --frames <n>      headless: run n frames then exit (default 3)\n"
				 "  --version        print build info and exit\n"
				 "  --help           show this help\n";
}

vb::worldgen::WorldGenerator make_generator(std::uint64_t seed) {
	vb::worldgen::WorldGenParams p;
	p.seed = seed;
	return vb::worldgen::WorldGenerator(p, vb::world::BlockRegistry::base());
}

vb::net::HandshakeServerConfig sp_server_config(std::uint64_t seed) {
	vb::net::HandshakeServerConfig c;
	c.pack_name = "base";
	c.motd = "integrated singleplayer";
	c.world_seed = seed;
	return c;
}

vb::net::HandshakeClientConfig sp_client_config(const std::string &name) {
	vb::net::HandshakeClientConfig c;
	c.player_name = name;
	c.client_version = vb::kVersionString;
	return c;
}

// Owns the in-process world for --singleplayer, kept alive for the whole
// session (spec §3: the integrated server is a library, not a child process).
struct Singleplayer {
	vb::world::World world{ vb::world::BlockRegistry::base() };
	vb::worldgen::WorldGenWorkerPool pool;
	vb::net::IntegratedGame game;

	Singleplayer(std::uint64_t seed, const std::string &name, int view_distance) : pool(make_generator(seed)),
																				   game(sp_server_config(seed), sp_client_config(name)) {
		game.server().set_world_replicator(
				std::make_unique<vb::net::WorldReplicator>(world, pool,
						vb::world::BlockRegistry::base(), view_distance, 3));
	}
};

vb::protocol::InputCmd sample_input_cmd(std::uint32_t seq, double dt, double yaw,
		double pitch, bool mouse_captured) {
	vb::protocol::InputCmd cmd;
	cmd.seq = seq;
	cmd.dt = static_cast<float>(dt);
	cmd.yaw = static_cast<float>(yaw);
	cmd.pitch = static_cast<float>(pitch);
	if (mouse_captured) {
		if (IsKeyDown(KEY_W)) {
			cmd.move.z += 1.0f;
		}
		if (IsKeyDown(KEY_S)) {
			cmd.move.z -= 1.0f;
		}
		if (IsKeyDown(KEY_D)) {
			cmd.move.x += 1.0f;
		}
		if (IsKeyDown(KEY_A)) {
			cmd.move.x -= 1.0f;
		}
		if (IsKeyDown(KEY_SPACE)) {
			cmd.buttons |= vb::protocol::kInputJump;
		}
		if (IsKeyDown(KEY_LEFT_SHIFT)) {
			cmd.buttons |= vb::protocol::kInputSprint;
		}
	}
	return cmd;
}

struct VoxelRayHit {
	bool hit = false;
	vb::core::IVec3 voxel{};
	vb::core::IVec3 normal{};
};

// Amanatides & Woo voxel grid traversal from `origin` along `dir` (unit).
VoxelRayHit raycast_voxel(const vb::world::BlockSolidQuery &world,
		vb::core::Vec3d origin, vb::core::Vec3d dir, double max_dist) {
	const double inf = std::numeric_limits<double>::infinity();
	auto fl = [](double v) { return static_cast<int>(std::floor(v)); };
	auto step_of = [](double v) { return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0); };

	int x = fl(origin.x), y = fl(origin.y), z = fl(origin.z);
	const int sx = step_of(dir.x), sy = step_of(dir.y), sz = step_of(dir.z);

	auto t_first = [&](double o, double d, int s) {
		if (s == 0) {
			return inf;
		}
		const double edge = s > 0 ? std::floor(o) + 1.0 - o : o - std::floor(o);
		return edge / std::fabs(d);
	};
	double t_max_x = t_first(origin.x, dir.x, sx);
	double t_max_y = t_first(origin.y, dir.y, sy);
	double t_max_z = t_first(origin.z, dir.z, sz);
	const double t_dx = sx == 0 ? inf : 1.0 / std::fabs(dir.x);
	const double t_dy = sy == 0 ? inf : 1.0 / std::fabs(dir.y);
	const double t_dz = sz == 0 ? inf : 1.0 / std::fabs(dir.z);

	vb::core::IVec3 normal{};
	double t = 0.0;
	for (int i = 0; i < 512 && t <= max_dist; ++i) {
		if (world.solid_at({ x, y, z })) {
			return { true, { x, y, z }, normal };
		}
		if (t_max_x < t_max_y && t_max_x < t_max_z) {
			x += sx;
			t = t_max_x;
			t_max_x += t_dx;
			normal = { -sx, 0, 0 };
		} else if (t_max_y < t_max_z) {
			y += sy;
			t = t_max_y;
			t_max_y += t_dy;
			normal = { 0, -sy, 0 };
		} else {
			z += sz;
			t = t_max_z;
			t_max_z += t_dz;
			normal = { 0, 0, -sz };
		}
	}
	return {};
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

void draw_overlay(const vb::render::FirstPersonController &c,
		const std::string &status, std::size_t chunk_count,
		std::size_t entity_count, bool mouse_captured) {
	const vb::core::Vec3d p = c.position();
	char line[160];
	DrawText("voxel_browser", 12, 12, 20, RAYWHITE);
	DrawText(status.c_str(), 12, 38, 18, Color{ 170, 200, 170, 255 });
	std::snprintf(line, sizeof(line),
			"pos  %.1f  %.1f  %.1f   chunks %zu   entities %zu", p.x, p.y, p.z,
			chunk_count, entity_count);
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

	auto loaded =
			vb::core::load_client_config(args.value_or("config", "client.toml"));
	if (!loaded) {
		std::cerr << "client: bad config: " << vb::core::message(loaded.error())
				  << '\n';
		return EXIT_FAILURE;
	}
	vb::core::ClientConfig config = *loaded;
	vb::core::apply_cli_overrides(config, args);

	const std::string server = args.value_or("server", "127.0.0.1");
	const int port = args.int_or("port", 27015);
	const bool singleplayer = args.has("singleplayer");
	const bool headless = args.has("headless") || VB_HEADLESS_DEFAULT;
	const float fov = static_cast<float>(config.fov);
	const int view_distance =
			static_cast<int>(config.render_distance < 2 ? 2 : config.render_distance);

	std::cout << vb::core::describe_build() << '\n'
			  << "client: " << (headless ? "headless" : "windowed") << " mode\n";

	std::unique_ptr<Singleplayer> sp;
	vb::core::NetId net_id = vb::core::NetId::kInvalid;
	vb::core::Vec3d spawn{ 0.0, 72.0, 0.0 };
	std::string status;

	if (singleplayer) {
		sp = std::make_unique<Singleplayer>(7, config.player_name, view_distance);
		for (int i = 0; i < 128 && !sp->game.client_joined() &&
				!sp->game.client_failed();
				++i) {
			sp->game.tick(0.05);
		}
		if (!sp->game.client_joined()) {
			std::cout << "client: singleplayer join failed: "
					  << sp->game.client().failure_reason() << '\n';
			return EXIT_FAILURE;
		}
		const auto &accept = *sp->game.client().join_accept();
		net_id = accept.your_net_id;
		spawn = accept.spawn_pos;
		status = "singleplayer — net id " +
				std::to_string(static_cast<std::uint32_t>(net_id)) + ", seed " +
				std::to_string(accept.world_seed);
		std::cout << "client: joined " << status << '\n';
	} else {
		status = "not connected (" + server + ":" + std::to_string(port) +
				") — remote transport lands with VB_WITH_NET";
		std::cout << "client: " << status << '\n';
	}

	vb::render::WindowConfig wcfg;
	wcfg.headless = headless;
	wcfg.width = static_cast<int>(config.window_width);
	wcfg.height = static_cast<int>(config.window_height);
	wcfg.vsync = config.vsync;
	wcfg.title = "voxel_browser";
	wcfg.headless_frame_limit =
			headless ? static_cast<std::uint64_t>(args.int_or("frames", 3)) : 0;
	vb::render::Window window(wcfg);

	vb::render::FirstPersonController controller;
	controller.set_position({ spawn.x, spawn.y + 1.7, spawn.z });
	controller.set_look(0.0, -20.0);
	controller.set_sensitivity(config.mouse_sensitivity);

	vb::physics::MoveParams move_params;
	if (sp) {
		sp->game.client().set_move_params(move_params);
		sp->game.client().set_local_feet(spawn);
	}
	std::uint32_t input_seq = 0;
	std::uint32_t edit_seq = 0;

	std::unique_ptr<vb::render::ChunkRenderer> chunk_renderer;
	std::unique_ptr<vb::render::EntityRenderer> entity_renderer;
	if (!window.headless()) {
		chunk_renderer = std::make_unique<vb::render::ChunkRenderer>();
		entity_renderer = std::make_unique<vb::render::EntityRenderer>();
	}

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

		// Look only — position is authoritative, driven by input commands and
		// corrected by the server via prediction/reconciliation (spec §8.4).
		vb::render::LookMoveInput look_in;
		if (mouse_captured) {
			const Vector2 md = GetMouseDelta();
			look_in.look_delta = { static_cast<double>(md.x),
				static_cast<double>(md.y) };
		}
		controller.update(look_in, dt);

		if (sp) {
			const vb::protocol::InputCmd cmd = sample_input_cmd(++input_seq, dt,
					controller.yaw(), controller.pitch(), mouse_captured);
			sp->game.client().push_input(cmd);
			sp->game.tick(dt);
			const vb::core::Vec3d feet = sp->game.client().predicted_feet();
			controller.set_position(
					{ feet.x, feet.y + move_params.eye_height, feet.z });
		}

		// Block break / place: raycast from the eye, act on click (spec §5.2).
		VoxelRayHit look_hit;
		if (sp && mouse_captured && !window.headless()) {
			look_hit = raycast_voxel(sp->game.client().chunk_store(),
					controller.position(), controller.forward(), 5.0);
			if (look_hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
				vb::protocol::C2SBlockEdit e;
				e.predicted_seq = ++edit_seq;
				e.action = vb::protocol::BlockEditAction::kBreak;
				e.pos = look_hit.voxel;
				sp->game.client().push_block_edit(e);
			} else if (look_hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
				vb::protocol::C2SBlockEdit e;
				e.predicted_seq = ++edit_seq;
				e.action = vb::protocol::BlockEditAction::kPlace;
				e.pos = { look_hit.voxel.x + look_hit.normal.x,
					look_hit.voxel.y + look_hit.normal.y,
					look_hit.voxel.z + look_hit.normal.z };
				e.block = vb::world::base_block::stone;
				sp->game.client().push_block_edit(e);
			}
		}

		std::size_t chunk_count = 0;
		std::size_t entity_count = 0;
		if (chunk_renderer && sp) {
			chunk_renderer->sync(sp->game.client().chunk_store(), /*budget*/ 8);
			chunk_count = chunk_renderer->uploaded_count();
		}
		if (entity_renderer && sp) {
			const vb::render::CameraView camera_view{ controller.position(),
				controller.target() };
			entity_renderer->sync(sp->game.client(), camera_view, dt);
			entity_count = entity_renderer->tracked_count();
		}

		window.begin_frame();
		if (!window.headless()) {
			const Camera3D camera = to_camera(controller, fov);
			BeginMode3D(camera);
			DrawGrid(64, 4.0f);
			if (chunk_renderer) {
				chunk_renderer->draw();
			}
			if (entity_renderer) {
				entity_renderer->draw(
						{ controller.position(), controller.target() });
			}
			if (look_hit.hit) {
				DrawCubeWires({ static_cast<float>(look_hit.voxel.x) + 0.5f,
									  static_cast<float>(look_hit.voxel.y) + 0.5f,
									  static_cast<float>(look_hit.voxel.z) + 0.5f },
						1.02f, 1.02f, 1.02f, BLACK);
			}
			EndMode3D();
			draw_overlay(controller, status, chunk_count, entity_count,
					mouse_captured);
		}
		window.end_frame();
	}

	std::cout << "client: exited after " << window.frame_count() << " frames\n";
	return EXIT_SUCCESS;
}
