// voxel_browser — the client ("browser").
//
// Phase 5.3: an engine-level main menu (raylib window always opens first) —
// connect screen, connecting/progress screen, error screen, settings screen,
// recent servers — wraps the streamed/meshed voxel gameplay from earlier
// phases. `--singleplayer` runs an in-process server (worldgen + replication)
// over a loopback transport and joins it (spec §3 — one code path for single-
// and multiplayer). `--headless` skips the menu entirely and connects
// immediately from CLI args/config (CI and integration tests depend on this
// exact behaviour, see `singleplayer_smoke`/`client_smoke`).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <raygui.h>
#include <raylib.h>

#include "vb/assetsync/cache.hpp"
#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/core/config.hpp"
#include "vb/core/paths.hpp"
#include "vb/net/gns_transport.hpp"
#include "vb/net/integrated.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/protocol/input.hpp"
#include "vb/protocol/world.hpp"
#include "vb/render/camera.hpp"
#include "vb/render/chunk_renderer.hpp"
#include "vb/render/entity_renderer.hpp"
#include "vb/render/main_menu.hpp"
#include "vb/render/ui_renderer.hpp"
#include "vb/render/window.hpp"
#include "vb/script/ui_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/world/raycast.hpp"
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
				 "  --asset-cache-dir <path>  asset cache directory override\n"
				 "  --singleplayer    run an in-process server and join it\n"
				 "  --headless        run without a window (no rendering, skips the menu)\n"
				 "  --frames <n>      headless: run n frames then exit (default 3)\n"
				 "  --version        print build info and exit\n"
				 "  --help           show this help\n"
				 "\n"
				 "Without --headless a window opens to the main menu; --singleplayer or\n"
				 "--server on the command line skips the menu and connects immediately\n"
				 "(dropping back to the menu on failure instead of exiting).\n";
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

// JoinGrant::spawn_pos otherwise defaults to a fixed {0, 64, 0} regardless of
// seed -- with base_height=64 and amplitude=28 the real surface ranges
// roughly [36, 92], so a fixed Y can land at or below it and spawn the player
// embedded in solid terrain outright (no fall involved). Compute a real one
// from the same seed instead.
vb::net::HandshakeServerHost sp_server_host(std::uint64_t seed) {
	vb::net::HandshakeServerHost host;
	host.on_ready = [seed](std::string_view) {
		vb::net::JoinGrant grant;
		grant.spawn_pos = vb::worldgen::default_spawn_position(make_generator(seed));
		return grant;
	};
	return host;
}

// Owns the in-process world for --singleplayer, kept alive for the whole
// session (spec §3: the integrated server is a library, not a child process).
struct Singleplayer {
	vb::world::World world{ vb::world::BlockRegistry::base() };
	vb::worldgen::WorldGenWorkerPool pool;
	vb::net::IntegratedGame game;

	Singleplayer(std::uint64_t seed, const std::string &name, int view_distance) : pool(make_generator(seed)),
																				   game(sp_server_config(seed), sp_client_config(name), sp_server_host(seed)) {
		game.server().set_world_replicator(
				std::make_unique<vb::net::WorldReplicator>(world, pool,
						vb::world::BlockRegistry::base(), view_distance, 3));
	}
};

// Real multiplayer: a GnsTransport dialing a dedicated voxel_browser_server
// (spec §8.1). `session` is empty if connect() itself failed (bad address, or
// built without VB_WITH_NET); a *later* handshake failure instead shows up as
// client->failed() once the join-wait loop runs.
struct RemoteConnection {
	vb::net::GnsTransport transport;
	vb::assetsync::ClientAssetCache asset_cache;
	std::optional<vb::net::ClientSession> session;

	RemoteConnection(const std::string &host, std::uint16_t port,
			const std::string &name, const vb::core::ClientConfig &config)
			: asset_cache(config.asset_cache_dir.empty()
							  ? vb::core::user_cache_dir() / "assets"
							  : std::filesystem::path(config.asset_cache_dir),
					  static_cast<std::uint64_t>(config.asset_cache_mb) * 1024ull * 1024ull) {
		auto conn = transport.connect(host, port);
		if (!conn) {
			return;
		}
		vb::net::HandshakeClientConfig c;
		c.player_name = name;
		c.client_version = vb::kVersionString;
		session.emplace(transport, *conn, std::move(c), &asset_cache);
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

const char *connecting_status_text(vb::net::ClientHandshakeStatus s) {
	using vb::net::ClientHandshakeStatus;
	switch (s) {
		case ClientHandshakeStatus::kConnecting:
			return "Connecting...";
		case ClientHandshakeStatus::kAuthenticating:
			return "Authenticating...";
		case ClientHandshakeStatus::kAwaitingAssetManifest:
			return "Requesting content manifest...";
		case ClientHandshakeStatus::kSyncingAssets:
			return "Downloading content pack...";
		case ClientHandshakeStatus::kSyncing:
			return "Syncing world...";
		case ClientHandshakeStatus::kJoined:
			return "Joined.";
		case ClientHandshakeStatus::kFailed:
			return "Failed.";
	}
	return "Connecting...";
}

// The pre-window, blocking connect-then-run path used by --headless (CI /
// integration-test smoke path). Deliberately unchanged from before Phase 5.3
// so `server_smoke`/`client_smoke`/`singleplayer_smoke` keep their exact
// output and timing characteristics.
int run_headless(const vb::core::ClientConfig &config, const vb::core::Args &args,
		const std::string &server, int port, bool singleplayer, int view_distance) {
	std::unique_ptr<Singleplayer> sp;
	std::unique_ptr<RemoteConnection> remote;
	vb::net::ClientSession *client = nullptr;
	vb::core::Vec3d spawn{ 0.0, 72.0, 0.0 };
	std::string status;

	if (singleplayer) {
		sp = std::make_unique<Singleplayer>(7, config.player_name, view_distance);
		client = &sp->game.client();
		for (int i = 0; i < 128 && !client->joined() && !client->failed(); ++i) {
			sp->game.tick(0.05);
		}
	} else {
		std::cout << "client: connecting to " << server << ':' << port << "...\n";
		remote = std::make_unique<RemoteConnection>(
				server, static_cast<std::uint16_t>(port), config.player_name, config);
		if (!remote->session) {
			std::cout << "client: could not connect to " << server << ':' << port
					  << " (bad address, or built without VB_WITH_NET)\n";
			return EXIT_FAILURE;
		}
		client = &*remote->session;

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!client->joined() && !client->failed() &&
				std::chrono::steady_clock::now() < deadline) {
			client->tick(0.05);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	if (!client->joined()) {
		std::cout << "client: join failed: "
				  << (client->failed() ? client->failure_reason() : "timed out")
				  << '\n';
		return EXIT_FAILURE;
	}
	vb::core::NetId net_id = vb::core::NetId::kInvalid;
	{
		const auto &accept = *client->join_accept();
		net_id = accept.your_net_id;
		spawn = accept.spawn_pos;
		status = (singleplayer ? std::string("singleplayer")
							   : ("connected to " + server + ':' +
										 std::to_string(port))) +
				" — net id " + std::to_string(static_cast<std::uint32_t>(net_id)) +
				", seed " + std::to_string(accept.world_seed);
		std::cout << "client: joined " << status << '\n';
	}

	vb::script::UiRuntime ui_runtime;
	vb::render::UiRenderer ui_renderer;
	ui_runtime.attach_session(*client);
	for (const auto &[path, bytes] : client->virtual_pack_fs()) {
		if (path.rfind("ui/", 0) != 0 || path.size() < 4 ||
				path.substr(path.size() - 4) != ".lua") {
			continue;
		}
		const std::string source(reinterpret_cast<const char *>(bytes.data()),
				bytes.size());
		const vb::script::ScriptResult result =
				ui_runtime.load_pack_file(source, path);
		if (!result.ok && result.error != vb::core::ScriptError::kDisabled) {
			std::cerr << "client: ui pack file '" << path
					  << "' failed to load: " << result.message << '\n';
		}
	}

	vb::render::WindowConfig wcfg;
	wcfg.headless = true;
	wcfg.width = static_cast<int>(config.window_width);
	wcfg.height = static_cast<int>(config.window_height);
	wcfg.vsync = config.vsync;
	wcfg.title = "voxel_browser";
	wcfg.headless_frame_limit = static_cast<std::uint64_t>(args.int_or("frames", 3));
	vb::render::Window window(wcfg);

	vb::render::FirstPersonController controller;
	controller.set_position({ spawn.x, spawn.y + 1.7, spawn.z });
	controller.set_look(0.0, -20.0);
	controller.set_sensitivity(config.mouse_sensitivity);

	vb::physics::MoveParams move_params;
	client->set_move_params(move_params);
	client->set_local_feet(spawn);
	std::uint32_t input_seq = 0;
	std::uint32_t edit_seq = 0;

	while (!window.should_close()) {
		const double dt = 1.0 / 60.0;

		if (auto opened = client->take_open_ui()) {
			ui_runtime.open(opened->ui_name, opened->ctx_json);
		}

		vb::render::LookMoveInput look_in;
		controller.update(look_in, dt);

		{
			const vb::protocol::InputCmd cmd = sample_input_cmd(++input_seq, dt,
					controller.yaw(), controller.pitch(), false);
			client->push_input(cmd);
			if (sp) {
				sp->game.tick(dt);
			} else {
				client->tick(dt);
			}
			const vb::core::Vec3d feet = client->predicted_feet();
			controller.set_position(
					{ feet.x, feet.y + move_params.eye_height, feet.z });
		}
		(void)edit_seq;

		window.begin_frame();
		window.end_frame();
	}

	std::cout << "client: exited after " << window.frame_count() << " frames\n";
	return EXIT_SUCCESS;
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

	const std::string config_path = args.value_or("config", "client.toml");
	auto loaded = vb::core::load_client_config(config_path);
	if (!loaded) {
		std::cerr << "client: bad config: " << vb::core::message(loaded.error())
				  << '\n';
		return EXIT_FAILURE;
	}
	vb::core::ClientConfig config = *loaded;
	vb::core::apply_cli_overrides(config, args);

	const std::string cli_server = args.value_or("server", "127.0.0.1");
	const int cli_port = args.int_or("port", 27015);
	const bool singleplayer = args.has("singleplayer");
	const bool headless = args.has("headless") || VB_HEADLESS_DEFAULT;
	const float fov = static_cast<float>(config.fov);
	const int view_distance =
			static_cast<int>(config.render_distance < 2 ? 2 : config.render_distance);

	std::cout << vb::core::describe_build() << '\n'
			  << "client: " << (headless ? "headless" : "windowed") << " mode\n";

	if (headless) {
		return run_headless(config, args, cli_server, cli_port, singleplayer, view_distance);
	}

	// --- windowed: main menu first (spec §5.3) -----------------------------

	vb::render::WindowConfig wcfg;
	wcfg.headless = false;
	wcfg.width = static_cast<int>(config.window_width);
	wcfg.height = static_cast<int>(config.window_height);
	wcfg.vsync = config.vsync;
	wcfg.title = "voxel_browser";
	vb::render::Window window(wcfg);

	vb::render::MainMenu menu(config);
	menu.prefill(cli_server, cli_port, config.player_name);

	enum class AppState { kMenu,
		kSettings,
		kConnecting,
		kPlaying,
		kError };
	AppState state = AppState::kMenu;
	std::string error_message;
	bool connecting_singleplayer = false;
	std::string connecting_target;
	int connect_ticks = 0;
	std::chrono::steady_clock::time_point connect_deadline;

	std::unique_ptr<Singleplayer> sp;
	std::unique_ptr<RemoteConnection> remote;
	vb::net::ClientSession *client = nullptr;
	vb::core::Vec3d spawn{ 0.0, 72.0, 0.0 };
	std::string status;

	vb::script::UiRuntime ui_runtime;
	vb::render::UiRenderer ui_renderer;
	vb::render::FirstPersonController controller;
	vb::physics::MoveParams move_params;
	std::uint32_t input_seq = 0;
	std::uint32_t edit_seq = 0;
	std::unique_ptr<vb::render::ChunkRenderer> chunk_renderer;
	std::unique_ptr<vb::render::EntityRenderer> entity_renderer;
	bool mouse_captured = false;

	// HUD chat (spec §5.4): a small scrolling log + an Enter-to-open text
	// box, plain raygui like MainMenu -- no Lua, no dependency on the pack's
	// UiRuntime chat concept (there isn't one).
	constexpr std::size_t kChatLogLimit = 8;
	constexpr int kChatBufferSize = 256;
	std::deque<std::string> chat_log;
	std::string chat_buf;
	bool chat_open = false;

	auto begin_connect = [&](bool as_singleplayer) {
		connecting_singleplayer = as_singleplayer;
		connect_ticks = 0;
		sp.reset();
		remote.reset();
		client = nullptr;
		if (as_singleplayer) {
			sp = std::make_unique<Singleplayer>(7, menu.player_name(), view_distance);
			connecting_target = "singleplayer";
			state = AppState::kConnecting;
		} else {
			connecting_target = menu.address() + ':' + std::to_string(menu.port());
			std::cout << "client: connecting to " << connecting_target << "...\n";
			remote = std::make_unique<RemoteConnection>(menu.address(),
					static_cast<std::uint16_t>(menu.port()), menu.player_name(), config);
			connect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			if (!remote->session) {
				error_message = "Could not connect to " + connecting_target +
						" (bad address, or built without VB_WITH_NET)";
				std::cout << "client: " << error_message << '\n';
				state = AppState::kError;
			} else {
				state = AppState::kConnecting;
			}
		}
	};

	if (singleplayer || args.has("server")) {
		begin_connect(singleplayer);
	}

	auto enter_playing = [&] {
		client = connecting_singleplayer ? &sp->game.client() : &*remote->session;
		const auto &accept = *client->join_accept();
		const vb::core::NetId net_id = accept.your_net_id;
		spawn = accept.spawn_pos;
		status = (connecting_singleplayer ? std::string("singleplayer")
										  : ("connected to " + connecting_target)) +
				" — net id " + std::to_string(static_cast<std::uint32_t>(net_id)) +
				", seed " + std::to_string(accept.world_seed);
		std::cout << "client: joined " << status << '\n';

		// Client UI VM (spec §10.4, Phase 4.5): a second, restricted Lua VM,
		// separate from PackRuntime's server-side one. `ui/*.lua` travels over
		// Asset Sync like any other pack file (Phase 4.4) -- load every synced
		// file under `ui/` now that join (and the asset transfer that
		// precedes it, §8.3) has completed. **Known gap:** `--singleplayer`
		// never asset-syncs (no PackRuntime/manifest on that in-process path,
		// see REMAINING_TASKS.md 4.3), so this only runs anything for real
		// multiplayer connections until that's wired.
		ui_runtime = vb::script::UiRuntime{};
		ui_runtime.attach_session(*client);
		for (const auto &[path, bytes] : client->virtual_pack_fs()) {
			if (path.rfind("ui/", 0) != 0 || path.size() < 4 ||
					path.substr(path.size() - 4) != ".lua") {
				continue;
			}
			const std::string source(reinterpret_cast<const char *>(bytes.data()),
					bytes.size());
			const vb::script::ScriptResult result =
					ui_runtime.load_pack_file(source, path);
			if (!result.ok && result.error != vb::core::ScriptError::kDisabled) {
				std::cerr << "client: ui pack file '" << path
						  << "' failed to load: " << result.message << '\n';
			}
		}

		controller = vb::render::FirstPersonController{};
		controller.set_position({ spawn.x, spawn.y + 1.7, spawn.z });
		controller.set_look(0.0, -20.0);
		controller.set_sensitivity(config.mouse_sensitivity);

		move_params = vb::physics::MoveParams{};
		client->set_move_params(move_params);
		client->set_local_feet(spawn);
		input_seq = 0;
		edit_seq = 0;
		chunk_renderer = std::make_unique<vb::render::ChunkRenderer>();
		entity_renderer = std::make_unique<vb::render::EntityRenderer>();
		mouse_captured = false;
		chat_log.clear();
		chat_buf.clear();
		chat_open = false;

		if (!connecting_singleplayer) {
			auto &recents = config.recent_servers;
			recents.erase(std::remove(recents.begin(), recents.end(), connecting_target),
					recents.end());
			recents.insert(recents.begin(), connecting_target);
			if (recents.size() > 8) {
				recents.resize(8);
			}
		}
		config.player_name = menu.player_name();
		if (auto saved = vb::core::save_client_config(config_path, config); !saved) {
			std::cerr << "client: could not save '" << config_path
					  << "': " << vb::core::message(saved.error()) << '\n';
		}

		state = AppState::kPlaying;
	};

	while (!window.should_close()) {
		const double dt = static_cast<double>(GetFrameTime());

		window.begin_frame();

		switch (state) {
			case AppState::kMenu: {
				const auto result = menu.draw_main(config.recent_servers);
				if (result.connect) {
					begin_connect(false);
				} else if (result.singleplayer) {
					begin_connect(true);
				} else if (result.open_settings) {
					menu.open_settings(config);
					state = AppState::kSettings;
				} else if (result.quit) {
					window.end_frame();
					std::cout << "client: exited from menu\n";
					return EXIT_SUCCESS;
				}
				break;
			}
			case AppState::kSettings: {
				const auto result = menu.draw_settings(config);
				if (result.save) {
					vb::core::save_client_config(config_path, config);
					state = AppState::kMenu;
				} else if (result.back) {
					state = AppState::kMenu;
				}
				break;
			}
			case AppState::kConnecting: {
				bool timed_out = false;
				if (connecting_singleplayer) {
					vb::net::ClientSession &sp_client = sp->game.client();
					for (int i = 0; i < 8 && !sp_client.joined() && !sp_client.failed() &&
							connect_ticks < 128;
							++i, ++connect_ticks) {
						sp->game.tick(0.05);
					}
					if (connect_ticks >= 128 && !sp_client.joined() && !sp_client.failed()) {
						timed_out = true;
					}
					client = &sp_client;
				} else {
					client = &*remote->session;
					client->tick(dt);
					if (std::chrono::steady_clock::now() > connect_deadline) {
						timed_out = true;
					}
				}

				if (client->joined()) {
					enter_playing();
				} else if (client->failed() || timed_out) {
					error_message = client->failed() ? client->failure_reason()
													 : std::string("connection timed out");
					std::cout << "client: join failed: " << error_message << '\n';
					state = AppState::kError;
				} else {
					const auto ui = menu.draw_connecting(connecting_singleplayer
									? "Starting singleplayer world..."
									: connecting_status_text(client->status()));
					if (ui.cancel) {
						sp.reset();
						remote.reset();
						client = nullptr;
						state = AppState::kMenu;
					}
				}
				break;
			}
			case AppState::kError: {
				const auto result = menu.draw_error(error_message);
				if (result.back) {
					state = AppState::kMenu;
				}
				break;
			}
			case AppState::kPlaying: {
				if (auto opened = client->take_open_ui()) {
					ui_runtime.open(opened->ui_name, opened->ctx_json);
				}

				for (std::string &line : client->take_chat_messages()) {
					chat_log.push_back(std::move(line));
				}
				while (chat_log.size() > kChatLogLimit) {
					chat_log.pop_front();
				}

				if (chat_open) {
					if (IsKeyPressed(KEY_ESCAPE)) {
						chat_open = false;
						chat_buf.clear();
					} else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
						if (!chat_buf.empty()) {
							client->send_chat(chat_buf);
						}
						chat_buf.clear();
						chat_open = false;
					}
				} else if (!ui_runtime.is_open() &&
						(IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
					chat_open = true;
				}

				if (ui_runtime.is_open() || chat_open) {
					mouse_captured = false;
					EnableCursor();
				} else if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE)) {
					mouse_captured = false;
					EnableCursor();
				} else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouse_captured) {
					mouse_captured = true;
					DisableCursor();
				}

				// Look only — position is authoritative, driven by input commands
				// and corrected by the server via prediction/reconciliation
				// (spec §8.4).
				vb::render::LookMoveInput look_in;
				if (mouse_captured) {
					const Vector2 md = GetMouseDelta();
					look_in.look_delta = { static_cast<double>(md.x),
						static_cast<double>(md.y) };
				}
				controller.update(look_in, dt);

				{
					const vb::protocol::InputCmd cmd = sample_input_cmd(++input_seq, dt,
							controller.yaw(), controller.pitch(), mouse_captured);
					client->push_input(cmd);
					// Singleplayer ticks the whole embedded game (client + server,
					// over loopback); a real connection just pumps this client's
					// GnsTransport -- the dedicated server ticks itself.
					if (connecting_singleplayer) {
						sp->game.tick(dt);
					} else {
						client->tick(dt);
					}
					const vb::core::Vec3d feet = client->predicted_feet();
					controller.set_position(
							{ feet.x, feet.y + move_params.eye_height, feet.z });
				}

				// Block break / place: raycast from the eye, act on click
				// (spec §5.2).
				vb::world::VoxelRayHit look_hit;
				if (mouse_captured) {
					look_hit = vb::world::raycast_voxel(client->chunk_store(),
							controller.position(), controller.forward(), 5.0);
					if (look_hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
						vb::protocol::C2SBlockEdit e;
						e.predicted_seq = ++edit_seq;
						e.action = vb::protocol::BlockEditAction::kBreak;
						e.pos = look_hit.voxel;
						client->push_block_edit(e);
					} else if (look_hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
						vb::protocol::C2SBlockEdit e;
						e.predicted_seq = ++edit_seq;
						e.action = vb::protocol::BlockEditAction::kPlace;
						e.pos = { look_hit.voxel.x + look_hit.normal.x,
							look_hit.voxel.y + look_hit.normal.y,
							look_hit.voxel.z + look_hit.normal.z };
						e.block = vb::world::base_block::stone;
						client->push_block_edit(e);
					}
				}

				std::size_t chunk_count = 0;
				std::size_t entity_count = 0;
				if (chunk_renderer) {
					chunk_renderer->sync(client->chunk_store(), /*budget*/ 8);
					chunk_count = chunk_renderer->uploaded_count();
				}
				if (entity_renderer) {
					const vb::render::CameraView camera_view{ controller.position(),
						controller.target() };
					entity_renderer->sync(*client, camera_view, dt);
					entity_count = entity_renderer->tracked_count();
				}

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

				// Chat HUD (spec §5.4): a bottom-left scrolling log, plus an
				// Enter-to-open input box (plain raygui, no Lua -- same
				// posture as MainMenu, not a UiRuntime widget).
				{
					const int line_h = 18;
					const int box_bottom = GetScreenHeight() - 16;
					int y = box_bottom -
							(chat_open ? (line_h + 8) : 0) -
							static_cast<int>(chat_log.size()) * line_h;
					for (const std::string &line : chat_log) {
						DrawText(line.c_str(), 12, y, 16, Color{ 220, 220, 220, 230 });
						y += line_h;
					}
					if (chat_open) {
						chat_buf.resize(kChatBufferSize, '\0');
						GuiTextBox(Rectangle{ 12.0f, static_cast<float>(box_bottom - line_h),
											 360.0f, static_cast<float>(line_h + 4) },
								chat_buf.data(), kChatBufferSize, true);
						chat_buf.resize(std::strlen(chat_buf.c_str()));
					}
				}

				if (ui_runtime.is_open()) {
					const auto ui_result =
							ui_renderer.draw(ui_runtime.current_name(), ui_runtime.widgets());
					for (const auto &id : ui_result.clicked) {
						ui_runtime.report_click(id);
					}
					for (const auto &[id, text] : ui_result.changed_text) {
						ui_runtime.report_change(id, text);
					}
					for (const auto &[id, idx] : ui_result.changed_list) {
						ui_runtime.report_list_change(id, idx);
					}
				}
				break;
			}
		}

		window.end_frame();
	}

	std::cout << "client: exited after " << window.frame_count() << " frames\n";
	return EXIT_SUCCESS;
}
