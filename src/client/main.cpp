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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <raygui.h>
#include <raylib.h>

#include "vb/assetsync/cache.hpp"
#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/core/config.hpp"
#include "vb/core/ids.hpp" // kChunkDim
#include "vb/core/paths.hpp"
#include "vb/net/gns_transport.hpp"
#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
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
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/script/ui_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/world/daynight.hpp"
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

vb::worldgen::WorldGenerator make_generator(
		std::uint64_t seed, const vb::world::BlockRegistry &registry,
		std::shared_ptr<const vb::worldgen::PackWorldGenPipeline> pipeline = nullptr) {
	vb::worldgen::WorldGenParams p;
	p.seed = seed;
	return vb::worldgen::WorldGenerator(p, registry, std::move(pipeline));
}

// The spawn-position calculation only needs the well-known base block ids
// (air/stone/dirt/.../water), which a pack re-declaring those names by
// `add_or_get` never changes -- safe to always use the fixed base() registry
// here even when the real world uses a pack-extended one.
vb::worldgen::WorldGenerator make_generator(std::uint64_t seed) {
	return make_generator(seed, vb::world::BlockRegistry::base());
}

vb::net::HandshakeServerConfig sp_server_config(std::uint64_t seed, int view_distance) {
	vb::net::HandshakeServerConfig c;
	c.pack_name = "base";
	c.motd = "integrated singleplayer";
	c.world_seed = seed;
	c.view_distance = static_cast<std::uint32_t>(view_distance);
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

// Default content pack --singleplayer loads (matches server.toml.example's
// own default) -- there's no client-side config for this yet, so it's fixed.
constexpr const char *kSingleplayerContentPack = "content/base";

vb::script::PackRuntime make_singleplayer_pack_runtime(
		vb::net::Transport &transport, vb::world::BlockRegistry &registry) {
	vb::script::PackRuntime rt(transport, registry,
			std::filesystem::path(kSingleplayerContentPack) / "storage.json");
	if (!vb::script::load_content_pack(rt, kSingleplayerContentPack)) {
		std::cerr << "client: singleplayer content pack '"
				  << kSingleplayerContentPack << "' failed to load -- "
				  << "running with the hardcoded base block set only\n";
	}
	rt.freeze();
	return rt;
}

// Real texture/atlas system, --singleplayer only: no Asset Sync exists on
// this in-process path (see RemoteConnection's own asset_cache for the real-
// multiplayer equivalent), so texture bytes are read straight off disk
// instead of resolved through a synced virtual FS -- only the handful of
// paths the registry actually references, not the whole content pack tree.
vb::render::VirtualFs load_textures_from_disk(
		const vb::world::BlockRegistry &registry, const std::filesystem::path &content_root) {
	vb::render::VirtualFs vfs;
	for (std::size_t i = 0; i < registry.size(); ++i) {
		const vb::world::BlockType &type = registry.get(static_cast<vb::core::BlockId>(i));
		if (type.texture.empty()) {
			continue;
		}
		std::ifstream f(content_root / type.texture, std::ios::binary | std::ios::ate);
		if (!f) {
			continue; // missing on disk -- TextureAtlas::build() falls back gracefully
		}
		const auto size = static_cast<std::size_t>(f.tellg());
		f.seekg(0);
		std::vector<std::byte> bytes(size);
		f.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
		vfs[type.texture] = std::move(bytes);
	}
	return vfs;
}

// Same idea as load_textures_from_disk() above, for entity kinds' `visual`
// spritesheets (entity-management follow-up) instead of block textures --
// --singleplayer has no asset-sync virtual FS to read these back out of, so
// this reads them straight off the same on-disk content pack the integrated
// server loaded from.
vb::render::VirtualFs load_entity_textures_from_disk(
		const std::vector<vb::protocol::EntityKindRegistryRecord> &kinds,
		const std::filesystem::path &content_root) {
	vb::render::VirtualFs vfs;
	for (const auto &kind : kinds) {
		if (!kind.visual) {
			continue;
		}
		std::ifstream f(content_root / kind.visual->texture, std::ios::binary | std::ios::ate);
		if (!f) {
			continue; // missing on disk -- set_kind_visual() falls back to the placeholder
		}
		const auto size = static_cast<std::size_t>(f.tellg());
		f.seekg(0);
		std::vector<std::byte> bytes(size);
		f.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
		vfs[kind.visual->texture] = std::move(bytes);
	}
	return vfs;
}

vb::net::HandshakeServerHost make_singleplayer_host(std::uint64_t seed,
		vb::script::PackRuntime &pack_runtime,
		const vb::world::BlockRegistry &registry,
		const vb::physics::MoveParams &move_params) {
	vb::net::HandshakeServerHost host = sp_server_host(seed);
	pack_runtime.install_join_veto(host); // before ServerSession copies `host`
	// Entity-management follow-up to Phase 6.1: mirrors src/server/main.cpp's
	// own install_entity_kind_registry call exactly -- without this,
	// --singleplayer's script entities would render as the flat placeholder
	// regardless of what a pack's vb.register_entity{width=, height=} asked
	// for, the same "silently missing" gap host.block_registry above closes
	// for custom blocks.
	pack_runtime.install_entity_kind_registry(host); // before ServerSession copies `host`
	// Phase 6.7: mirrors src/server/main.cpp's own host.move_params exactly --
	// without this, --singleplayer's client-side prediction would silently
	// keep vb::physics::MoveParams's hardcoded defaults even when a pack
	// overrides them via vb.physics.set_params.
	host.move_params =
			[move_params]() -> std::optional<vb::protocol::S2CMoveParams> {
		return vb::protocol::S2CMoveParams{ move_params.half_width,
			move_params.height, move_params.eye_height, move_params.walk_speed,
			move_params.sprint_speed, move_params.accel, move_params.air_accel,
			move_params.friction, move_params.gravity, move_params.jump_speed,
			move_params.terminal_velocity, move_params.step_height,
			move_params.fly_speed, move_params.fly };
	};
	// Phase 4.3: without this, a joining client stays on its own base()
	// registry and any pack-added block (planks/sticks from crafting.lua)
	// resolves to nothing client-side -- name lookups fall back to "?" in
	// the hotbar even though give()/take() work fine either way (inventory
	// slots are just numeric ids). Mirrors src/server/main.cpp's own
	// host.block_registry callback exactly.
	host.block_registry =
			[&registry]() -> std::optional<std::vector<vb::protocol::BlockRegistryRecord>> {
		std::vector<vb::protocol::BlockRegistryRecord> out;
		out.reserve(registry.size());
		for (std::size_t i = 0; i < registry.size(); ++i) {
			const auto &t = registry.get(static_cast<vb::core::BlockId>(i));
			out.push_back({ t.name, t.solid, t.opaque, t.liquid, t.light_emission, t.texture });
		}
		return out;
	};
	// Phase 6.8: mirrors src/server/main.cpp's own host.day_night_curve
	// exactly -- nullopt (no pack called vb.daynight.set_curve) sends no
	// frame, leaving --singleplayer on the same built-in gradient a
	// dedicated server's clients get.
	const std::optional<vb::world::DayNightCurve> day_night_curve =
			pack_runtime.effective_day_night_curve();
	host.day_night_curve =
			[day_night_curve]() -> std::optional<std::vector<vb::protocol::DayNightKeyframeRecord>> {
		if (!day_night_curve) {
			return std::nullopt;
		}
		std::vector<vb::protocol::DayNightKeyframeRecord> out;
		out.reserve(day_night_curve->keyframes.size());
		for (const auto &k : day_night_curve->keyframes) {
			out.push_back({ k.tick, k.brightness, k.color.r, k.color.g, k.color.b });
		}
		return out;
	};
	return host;
}

// Owns the in-process world for --singleplayer, kept alive for the whole
// session (spec §3: the integrated server is a library, not a child
// process). Built directly from LoopbackNetwork/ServerSession/ClientSession
// -- the same pieces `vb::net::IntegratedGame` wraps -- rather than through
// IntegratedGame itself: a `PackRuntime` needs the raw server-side
// `Transport&` (to send chat/give/etc. messages) and needs
// `install_join_veto()` to run *before* `ServerSession` is constructed,
// neither of which IntegratedGame's all-in-one constructor exposes a hook
// for. This closes the "`--singleplayer` doesn't asset-sync/run the content
// pack's Lua at all" gap `REMAINING_TASKS.md` had tracked since Phase 5.1 --
// singleplayer now loads and runs `content/base` exactly like a real
// dedicated server does (chat, crafting, item drops, custom blocks), just
// over a loopback transport instead of real UDP.
struct Singleplayer {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime pack_runtime;
	// Phase 6.7: no ServerConfig/server.toml on this in-process path, so the
	// engine default (vb::physics::MoveParams{}) is the base a pack's
	// vb.physics.set_params{...} overrides on top of -- computed right after
	// pack_runtime (declaration order == init order) so it's ready both for
	// make_singleplayer_host below and set_move_params() in the body.
	vb::physics::MoveParams move_params;
	vb::world::World world;
	vb::worldgen::WorldGenWorkerPool pool;
	vb::net::ServerSession server;
	std::optional<vb::net::ClientSession> client_session;

	Singleplayer(std::uint64_t seed, const std::string &name, int view_distance) : pack_runtime(make_singleplayer_pack_runtime(net.server(), registry)),
																				   move_params(pack_runtime.effective_move_params(vb::physics::MoveParams{})),
																				   world(registry),
																				   // Phase 6.14: mirrors src/server/main.cpp's own
																				   // build_worldgen_pipeline call -- nullptr unless a pack called
																				   // vb.worldgen.set_pipeline, in which case --singleplayer's
																				   // terrain matches a dedicated server's.
																				   pool(make_generator(seed, registry,
																						   pack_runtime.build_worldgen_pipeline(
																								   vb::worldgen::WorldGenParams{ seed }))),
																				   server(net.server(), sp_server_config(seed, view_distance),
																						   make_singleplayer_host(seed, pack_runtime, registry, move_params)) {
		server.set_move_params(move_params);
		// Phase 6.18: mirrors src/server/main.cpp's own set_punch_params call.
		server.set_punch_params(pack_runtime.effective_punch_params(
				vb::net::ServerSession::PunchParams{}));
		// Phase 6.8: no server.toml on this in-process path either, so
		// ServerSession's own hardcoded default (kDefaultDayLengthSeconds,
		// matching its member initializer) is the base a pack's
		// vb.daynight.set_day_length(...) overrides on top of (mirrors
		// move_params above).
		server.set_day_length_seconds(pack_runtime.effective_day_length_seconds(
				vb::net::kDefaultDayLengthSeconds));
		auto listening = net.server().listen(0);
		(void)listening; // loopback listen never fails on a fresh network

		auto replicator = std::make_unique<vb::net::WorldReplicator>(
				world, pool, registry, view_distance, 3);
		pack_runtime.attach_world(*replicator);
		// Phase 6.21: mirrors src/server/main.cpp's own set_reach call -- the
		// same value both this replicator's block-edit reach and
		// server.punch()'s combat reach read.
		replicator->set_reach(
				pack_runtime.effective_action_params(vb::net::ActionParams{}).reach);
		server.set_world_replicator(std::move(replicator));
		pack_runtime.attach_session(server);

		vb::net::Transport &client_transport = net.create_client();
		auto conn = client_transport.connect("integrated", 0);
		if (conn) {
			client_session.emplace(
					client_transport, *conn, sp_client_config(name));
		} else {
			std::cerr << "client: singleplayer failed to connect to its own "
						 "loopback server\n";
		}
	}

	vb::net::ClientSession &client() { return *client_session; }

	// Server tick rate (spec §7's fixed simulation rate; matches
	// HandshakeServerConfig::tick_rate's default -- sp_server_config() doesn't
	// override it). A dedicated server (src/server/main.cpp) sleep_until()s
	// between ticks, so it's naturally paced at this rate; the integrated
	// server here is instead driven by the client's render loop, so tick()
	// accumulates the variable frame dt and steps the server at this fixed
	// rate itself -- otherwise physics/worldgen determinism and replication
	// cadence would depend on framerate, unlike every other server.
	static constexpr double kFixedDt = 1.0 / 20.0;
	// Caps how many fixed steps one frame will catch up on (e.g. after a
	// stall from asset loading or a debugger breakpoint) -- runs behind at
	// that point instead of spiralling into an ever-growing catch-up burst.
	static constexpr int kMaxStepsPerFrame = 5;
	double tick_accum_ = 0.0;

	// Advances the client every frame (render-rate prediction/interpolation),
	// and the server + pack runtime's join/leave/tick dispatch at the fixed
	// rate above -- mirrors src/server/main.cpp's own tick loop so a pack
	// behaves identically whether it's driven by a real dedicated server or
	// this in-process one.
	// Per-real-tick chunk ingest+relight budget (mirrors
	// chunk_lifecycle.cpp's own kIngestBudgetPerTick default) -- see
	// set_ingest_budget()'s doc for why this needs shrinking per catch-up
	// step below instead of being spent in full on every one of them.
	static constexpr std::size_t kBaseChunkIngestBudget = 32;

	void tick(double dt) {
		if (client_session) {
			client_session->tick(dt);
		}

		tick_accum_ += dt;
		// How many fixed steps this frame is about to run, capped the same way
		// the loop below caps itself -- known up front since it's a pure
		// function of tick_accum_/kFixedDt, so the ingest budget can be spread
		// across them before the first step runs instead of after the fact.
		const int expected_steps = std::min(kMaxStepsPerFrame,
				static_cast<int>(tick_accum_ / kFixedDt));
		if (vb::net::WorldReplicator *wr = server.world_replicator()) {
			const std::size_t per_step_budget = expected_steps > 1
					? std::max<std::size_t>(1,
							  kBaseChunkIngestBudget / static_cast<std::size_t>(expected_steps))
					: kBaseChunkIngestBudget;
			wr->set_chunk_ingest_budget(per_step_budget);
		}
		int steps = 0;
		while (tick_accum_ >= kFixedDt && steps < kMaxStepsPerFrame) {
			server.tick(kFixedDt);
			for (auto &j : server.take_joins()) {
				pack_runtime.dispatch_player_join_completed(j);
			}
			for (auto &l : server.take_leaves()) {
				pack_runtime.dispatch_player_leave(l);
			}
			pack_runtime.dispatch_tick(kFixedDt);
			tick_accum_ -= kFixedDt;
			++steps;
		}
		if (steps == kMaxStepsPerFrame) {
			tick_accum_ = 0.0; // drop the backlog rather than spiral
		}
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
			const std::string &name, const vb::core::ClientConfig &config) : asset_cache(config.asset_cache_dir.empty()
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

// Phase 6.17: physical-key-to-action mapping for the axes/buttons the engine
// itself always understands (InputCmd::move/buttons -- distinct from
// vb.register_keybind's pack-defined slots, Phase 6.3, which cover only
// discrete named actions a pack invents). These used to be raylib key
// literals mixed directly into sample_input_cmd's branching with no seam at
// all; pulling them into one small table is the actual "decouple hardcoded
// movement" -- sample_input_cmd itself no longer hardcodes which physical
// key means what, and a future client settings screen (Phase 5.3, still not
// attempted) has exactly one place to rebind. Note this is a *client-local*
// physical-key mapping, not a network-visible one -- what the resulting
// InputCmd.move/buttons/yaw/pitch actually *do* is already fully
// pack-overridable server-side via vb.on("player_input", ...), independent
// of which key produced them.
struct MovementBindings {
	int forward = KEY_W;
	int back = KEY_S;
	int left = KEY_A;
	int right = KEY_D;
	int jump = KEY_SPACE;
	int sprint = KEY_LEFT_SHIFT;
};

// Phase 6.19: the engine pre-registers 8 action names ("move_forward",
// "move_back", "move_left", "move_right", "jump", "sprint", "primary",
// "secondary") into the same Phase 6.3 keybind registry every pack-custom
// vb.register_keybind() name goes into (see PackRuntime::Impl::Impl in
// src/script/pack_runtime.cpp) -- so they're enumerable via
// S2C_KeybindRegistry like any other keybind, and a pack's
// vb.on("player_input", ...) can read e.g. input.keybinds["move_forward"]
// the same way it reads a custom one. This is purely additive:
// InputCmd::move/buttons (and MovementBindings' physical keys) are
// unchanged, so physics/movement code isn't affected. Lookup is by name in
// whatever S2C_KeybindRegistry the server actually sent, never assumed to
// be bits 0-7.
void set_engine_keybind(vb::protocol::InputCmd &cmd, const char *name,
		bool held, const std::vector<std::string> &keybind_names) {
	for (std::size_t i = 0; i < keybind_names.size(); ++i) {
		if (keybind_names[i] == name) {
			if (held) {
				cmd.keybinds |= (1u << i);
			}
			return;
		}
	}
}

// Phase 7.4: closes the gap `content/base/ui/pause.lua`/`ui/inventory.lua`'s
// own header comments flagged ("nothing opens this yet") and
// `content/examples/kitchen_sink/keybinds.lua` hit for its own custom
// screen ("no base-pack/client UI wires these yet") -- Phase 6.3's
// vb.register_keybind gives a pack a *named* bit in InputCmd.keybinds, but
// nothing on the client ever mapped a physical key to a pack-registered
// custom name (only the 8 pre-registered engine names above get one, via
// MovementBindings). This is a minimal hardcoded default table, not a real
// settings-screen UI (Phase 5.3's keybindings screen only covers the 6
// MovementBindings axes) -- a future rebind screen for these is a separate
// step past this one, same as that item's own scope note. Unlike the
// engine-name lookup above, these are read unconditionally (not gated on
// mouse_captured below): opening a pause/inventory screen must work whether
// or not the mouse is currently captured for looking around.
struct CustomKeybind {
	const char *name;
	int key;
};
constexpr CustomKeybind kCustomKeybinds[] = {
	{ "base:pause", KEY_ESCAPE },
	{ "base:inventory", KEY_E },
};

vb::protocol::InputCmd sample_input_cmd(std::uint32_t seq, double dt, double yaw,
		double pitch, bool mouse_captured, const MovementBindings &bindings,
		const std::vector<std::string> &keybind_names = {}) {
	vb::protocol::InputCmd cmd;
	cmd.seq = seq;
	cmd.dt = static_cast<float>(dt);
	cmd.yaw = static_cast<float>(yaw);
	cmd.pitch = static_cast<float>(pitch);
	if (mouse_captured) {
		const bool forward = IsKeyDown(bindings.forward);
		const bool back = IsKeyDown(bindings.back);
		const bool right = IsKeyDown(bindings.right);
		const bool left = IsKeyDown(bindings.left);
		const bool jump = IsKeyDown(bindings.jump);
		const bool sprint = IsKeyDown(bindings.sprint);
		// Phase 6.17: block breaking/placing is no longer an engine default
		// (see the removed hold-to-break timer further below in this file) --
		// the client's only job is to report these as raw held-button state,
		// exactly like jump/sprint above. Whether holding "primary" over a
		// voxel does anything at all is entirely up to a content pack's
		// vb.on("player_input", ...) handler (content/base/mechanics.lua).
		const bool primary = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
		const bool secondary = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

		if (forward) {
			cmd.move.z += 1.0f;
		}
		if (back) {
			cmd.move.z -= 1.0f;
		}
		if (right) {
			cmd.move.x += 1.0f;
		}
		if (left) {
			cmd.move.x -= 1.0f;
		}
		if (jump) {
			cmd.buttons |= vb::protocol::kInputJump;
		}
		if (sprint) {
			cmd.buttons |= vb::protocol::kInputSprint;
		}
		if (primary) {
			cmd.buttons |= vb::protocol::kInputPrimary;
		}
		if (secondary) {
			cmd.buttons |= vb::protocol::kInputSecondary;
		}

		set_engine_keybind(cmd, "move_forward", forward, keybind_names);
		set_engine_keybind(cmd, "move_back", back, keybind_names);
		set_engine_keybind(cmd, "move_left", left, keybind_names);
		set_engine_keybind(cmd, "move_right", right, keybind_names);
		set_engine_keybind(cmd, "jump", jump, keybind_names);
		set_engine_keybind(cmd, "sprint", sprint, keybind_names);
		set_engine_keybind(cmd, "primary", primary, keybind_names);
		set_engine_keybind(cmd, "secondary", secondary, keybind_names);
	}
	for (const CustomKeybind &kb : kCustomKeybinds) {
		set_engine_keybind(cmd, kb.name, IsKeyDown(kb.key), keybind_names);
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
		std::size_t entity_count, bool mouse_captured,
		std::uint32_t time_of_day) {
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
	// kTicksPerDay (24000) / 24h conveniently gives 1000 ticks/hour.
	std::snprintf(line, sizeof(line), "time %02u:%02u",
			time_of_day / 1000u, (time_of_day % 1000u) * 60u / 1000u);
	DrawText(line, 12, 108, 16, Color{ 170, 170, 180, 255 });
	DrawText(mouse_captured ? "mouse captured (Tab to release)"
							: "click to capture mouse",
			12, 130, 16, Color{ 140, 140, 150, 255 });
	DrawFPS(12, 154);
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
		client = &sp->client();
		for (int i = 0; i < 128 && !client->joined() && !client->failed(); ++i) {
			sp->tick(0.05);
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

	// Phase 6.7: read back whatever set_move_params() already holds (the
	// engine default, or the real S2C_MoveParams applied while joining --
	// it arrives alongside S2C_JoinAccept, so by the time join_accept() is
	// non-null above it's already been applied) rather than overwrite it
	// with a fresh default here.
	const vb::physics::MoveParams move_params = client->move_params();
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
					controller.yaw(), controller.pitch(), false, MovementBindings{});
			client->push_input(cmd);
			if (sp) {
				sp->tick(dt);
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
	// The player's own preference (Settings screen / client.toml), never
	// mutated. `view_distance` below starts equal to it but may be clamped
	// down per-connection once a real server's own (possibly smaller) view
	// distance is known -- see begin_connect/enter_playing in the windowed
	// path. Singleplayer always uses this unclamped value: the client IS
	// the server there, so there's nothing external to clamp against.
	const int configured_view_distance =
			static_cast<int>(config.render_distance < 2 ? 2 : config.render_distance);

	std::cout << vb::core::describe_build() << '\n'
			  << "client: " << (headless ? "headless" : "windowed") << " mode\n";

	if (headless) {
		return run_headless(config, args, cli_server, cli_port, singleplayer, configured_view_distance);
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
		kKeybindings,
		kConnecting,
		kLoading,
		kPlaying,
		kError };
	AppState state = AppState::kMenu;
	std::string error_message;
	// The view distance actually in effect this connection -- starts at
	// configured_view_distance every time begin_connect() runs, then
	// enter_playing() clamps it down to a real server's own (possibly
	// smaller) S2CServerInfo::view_distance once that's known. Drives
	// kLoading's expected-chunk-count estimate and the default fog distance
	// below, so both always agree with whatever this connection can
	// actually stream in.
	int view_distance = configured_view_distance;
	bool connecting_singleplayer = false;
	std::string connecting_target;
	int connect_ticks = 0;
	std::chrono::steady_clock::time_point connect_deadline;
	// Phase 7.1: hard cap on how long kLoading waits for the initial view-box
	// of chunks to stream in before letting the player through anyway (e.g. a
	// server whose own view_distance is smaller than this client guessed, or
	// a slow connection) -- getting out of the way beats blocking forever.
	// `loading_deadline` is a *stall* deadline, not a flat one: it's pushed
	// forward every time `loading_last_uploaded` (the previous frame's
	// uploaded_count) advances, so a large view distance that's genuinely
	// still meshing/uploading chunks -- just slowly -- isn't cut off mid-load
	// (an earlier flat 8s deadline handed off to kPlaying while most of the
	// default view_distance=8 box, ~2000 chunks, was still unmeshed). Only a
	// real stall -- e.g. a server whose own view_distance is smaller than
	// this client guessed, so `fraction` can never reach 1.0 -- lets it fire.
	// `loading_hard_deadline` is the absolute backstop against a pathological
	// server that trickles in just enough chunks each tick to keep resetting
	// the stall deadline forever.
	std::chrono::steady_clock::time_point loading_deadline;
	std::chrono::steady_clock::time_point loading_hard_deadline;
	std::size_t loading_last_uploaded = 0;
	// 2000ms measured as too tight in practice: `loading_deadline` starts
	// counting the instant kLoading is entered, before the server has sent a
	// single chunk -- the very first batch out of a cold worldgen pool (no
	// cached chunks yet, every one of the view box's chunks generated fresh)
	// can itself take longer than 2s, so `loaded_chunks` was still 0 when the
	// stall deadline fired and kPlaying was entered with (visibly) nothing
	// loaded -- reported as "the world does not finish loading when the
	// loading screen disappears". 5s gives the worldgen -> mesh -> upload
	// pipeline room to produce its first real batch without making a
	// genuinely dead connection (no server, wrong port) wait much longer
	// before `loading_hard_deadline` would have caught it anyway.
	constexpr std::chrono::milliseconds kLoadingStallTimeout{ 5000 };
	constexpr std::chrono::seconds kLoadingHardTimeout{ 30 };

	std::unique_ptr<Singleplayer> sp;
	std::unique_ptr<RemoteConnection> remote;
	vb::net::ClientSession *client = nullptr;
	vb::core::Vec3d spawn{ 0.0, 72.0, 0.0 };
	std::string status;

	vb::script::UiRuntime ui_runtime;
	vb::render::UiRenderer ui_renderer;
	// A separate UiRenderer instance for the always-on HUD (below): drawing
	// both the modal screen and the HUD through one UiRenderer would thrash
	// its per-widget-id text/list edit caches every frame (it clears them
	// whenever the drawn ui_name changes, which "hud" vs. the modal name
	// would do twice a frame).
	vb::render::UiRenderer hud_renderer;
	vb::render::FirstPersonController controller;
	vb::physics::MoveParams move_params;
	std::uint32_t input_seq = 0;
	std::unique_ptr<vb::render::ChunkRenderer> chunk_renderer;
	std::unique_ptr<vb::render::EntityRenderer> entity_renderer;
	bool mouse_captured = false;

	// Phase 6.17: the client-local physical-key-to-action map for movement +
	// break/place (see MovementBindings' own comment above). One instance,
	// same defaults every frame -- a future settings screen would mutate
	// this instead of inventing a second mechanism.
	MovementBindings movement_bindings{ config.key_forward, config.key_back,
		config.key_left, config.key_right, config.key_jump, config.key_sprint };

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
		// Undo any clamp a previous connection's enter_playing() applied --
		// a fresh connection (even a retry of the same server) starts back
		// at the player's own full preference until this one's own
		// S2CServerInfo says otherwise.
		view_distance = configured_view_distance;
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
		client = connecting_singleplayer ? &sp->client() : &*remote->session;
		// Bind this connection's effective view distance to whatever the
		// server actually just told us (S2CServerInfo::view_distance,
		// always present, unlike the opt-in fog/move-params messages) --
		// never wider than the player's own configured_view_distance, so a
		// server advertising a larger box than the player asked for doesn't
		// silently raise their own setting. A no-op for singleplayer:
		// sp_server_config() above already echoes this same
		// configured_view_distance back as the server's own.
		if (const auto &info = client->server_info()) {
			view_distance = std::min(configured_view_distance,
					static_cast<int>(info->view_distance));
		}
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
		// Asset Sync like any other pack file (Phase 4.4) for a real
		// multiplayer connection; `--singleplayer` never asset-syncs (no
		// PackRuntime/manifest on that in-process path, REMAINING_TASKS.md
		// 4.3), so it instead reads `ui/*.lua` directly off disk from the
		// same `kSingleplayerContentPack` the integrated server's PackRuntime
		// already loads (client and server share one machine/filesystem
		// there, so there's nothing to "sync") -- otherwise the HUD below
		// (and every other Lua-defined screen) would silently never load in
		// the most common dev/test path.
		ui_runtime = vb::script::UiRuntime{};
		ui_runtime.attach_session(*client);
		std::vector<std::pair<std::string, std::string>> ui_sources;
		if (connecting_singleplayer) {
			const std::filesystem::path ui_dir =
					std::filesystem::path(kSingleplayerContentPack) / "ui";
			std::error_code ec;
			if (std::filesystem::is_directory(ui_dir, ec)) {
				for (const auto &entry : std::filesystem::directory_iterator(ui_dir, ec)) {
					if (entry.path().extension() != ".lua") {
						continue;
					}
					std::ifstream f(entry.path(), std::ios::binary);
					if (!f) {
						continue;
					}
					std::ostringstream ss;
					ss << f.rdbuf();
					ui_sources.emplace_back(
							"ui/" + entry.path().filename().string(), ss.str());
				}
			}
		} else {
			for (const auto &[path, bytes] : client->virtual_pack_fs()) {
				if (path.rfind("ui/", 0) != 0 || path.size() < 4 ||
						path.substr(path.size() - 4) != ".lua") {
					continue;
				}
				ui_sources.emplace_back(path,
						std::string(reinterpret_cast<const char *>(bytes.data()),
								bytes.size()));
			}
		}
		for (const auto &[path, source] : ui_sources) {
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

		// Phase 6.7: read back what's already applied (S2C_MoveParams arrives
		// alongside S2C_JoinAccept, so it's already in the session by now)
		// instead of stomping it back to the engine default.
		move_params = client->move_params();
		client->set_local_feet(spawn);
		input_seq = 0;
		chunk_renderer = std::make_unique<vb::render::ChunkRenderer>();
		// Real texture/atlas system: built once per session, right after the
		// block registry (S2C_BlockRegistry, already applied by now -- see
		// client->move_params() above reading back another join-time
		// message the same way) and every referenced texture's bytes are
		// available, and before kLoading starts streaming/meshing any chunk
		// -- so every chunk mesh this session uploads already gets real
		// atlas UVs from its very first upload, no re-upload-on-atlas-
		// arrival case to handle. `remote` resolves texture paths against
		// its already-synced Asset Sync virtual FS; `sp` (--singleplayer)
		// has no asset sync at all (client + server share one in-process
		// registry/content pack), so it reads the same
		// `kSingleplayerContentPack` the integrated server's PackRuntime
		// loaded from, straight off disk instead.
		{
			const vb::render::VirtualFs vfs = remote ? remote->asset_cache.virtual_fs()
													  : load_textures_from_disk(client->chunk_store().registry(),
																kSingleplayerContentPack);
			vb::render::TextureAtlas atlas =
					vb::render::TextureAtlas::build(client->chunk_store().registry(), vfs);
			std::vector<vb::render::AtlasRect> rects;
			std::vector<Color> averages;
			rects.reserve(atlas.block_count());
			averages.reserve(atlas.block_count());
			for (std::size_t i = 0; i < atlas.block_count(); ++i) {
				const auto id = static_cast<vb::core::BlockId>(i);
				rects.push_back(atlas.rect_for(id));
				averages.push_back(atlas.average_color_for(id));
			}
			chunk_renderer->set_atlas(atlas.upload(), std::move(rects), std::move(averages));
		}
		entity_renderer = std::make_unique<vb::render::EntityRenderer>();
		// Entity-management follow-up: build any registered kind's real
		// spritesheet (S2C_EntityKindRegistry.visual) the same session the
		// block texture atlas above was built -- both are one-shot,
		// join-time setup reading from the same synced/disk content pack. A
		// kind that never set `visual = {...}` is untouched, keeping its
		// flat placeholder billboard exactly as before this existed.
		{
			const vb::render::VirtualFs entity_vfs = remote
					? remote->asset_cache.virtual_fs()
					: load_entity_textures_from_disk(client->entity_kind_registry(), kSingleplayerContentPack);
			const auto &entity_kinds = client->entity_kind_registry();
			for (std::size_t i = 0; i < entity_kinds.size(); ++i) {
				const auto &rec = entity_kinds[i];
				if (rec.visual) {
					entity_renderer->set_kind_visual(
							static_cast<vb::core::EntityKindId>(i + 1), *rec.visual, entity_vfs);
				}
			}
		}
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

		// Phase 7.1: a loading screen between "joined" and "first playable
		// frame" instead of dropping straight into kPlaying -- the first
		// frames after join are otherwise an emptier-than-usual world (chunks
		// still streaming in), rendered with no indication that's expected.
		loading_deadline = std::chrono::steady_clock::now() + kLoadingStallTimeout;
		loading_hard_deadline = std::chrono::steady_clock::now() + kLoadingHardTimeout;
		loading_last_uploaded = 0;
		state = AppState::kLoading;
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
				} else if (result.open_keybindings) {
					menu.open_keybindings(config);
					state = AppState::kKeybindings;
				}
				break;
			}
			case AppState::kKeybindings: {
				const auto result = menu.draw_keybindings(config);
				if (result.save) {
					movement_bindings = MovementBindings{ config.key_forward,
						config.key_back, config.key_left, config.key_right,
						config.key_jump, config.key_sprint };
					vb::core::save_client_config(config_path, config);
					state = AppState::kSettings;
				} else if (result.back) {
					state = AppState::kSettings;
				}
				break;
			}
			case AppState::kConnecting: {
				bool timed_out = false;
				if (connecting_singleplayer) {
					vb::net::ClientSession &sp_client = sp->client();
					for (int i = 0; i < 8 && !sp_client.joined() && !sp_client.failed() &&
							connect_ticks < 128;
							++i, ++connect_ticks) {
						sp->tick(0.05);
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
			case AppState::kLoading: {
				// Phase 7.1: keep pumping the session/server so the initial
				// view-box of chunks actually streams in and gets meshed --
				// this screen isn't a passive wait, it's what's advancing the
				// load. A higher submit/upload budget than kPlaying's steady-
				// state 8/frame (spec: get through this quickly).
				if (connecting_singleplayer) {
					sp->tick(dt);
				} else {
					client->tick(dt);
				}
				if (chunk_renderer) {
					chunk_renderer->sync(client->chunk_store(), /*submit*/ 64, /*upload*/ 16);
				}

				// Expected chunk count mirrors WorldReplicator's own
				// chunks_in_view box (radius=view_distance, vertical
				// radius=3, see src/net/world_replicator.cpp /
				// Singleplayer's own construction above) -- an approximation
				// for a real server (whose own view_distance this client
				// doesn't know ahead of time), clamped to 1.0 below so a
				// smaller real box still reads as "done", not stuck.
				const std::size_t expected = static_cast<std::size_t>(2 * view_distance + 1) *
						static_cast<std::size_t>(2 * view_distance + 1) * 7u;
				// Progress must track what's actually visible on screen, not
				// just chunk data having arrived (client->chunk_store().size()
				// reaches `expected` well before ChunkRenderer has meshed and
				// GPU-uploaded that many chunks, since meshing is async and
				// upload is budgeted -- using store size here let the loading
				// screen hit 100% and hand off to kPlaying while the world
				// behind it was still sky-colored, unmeshed chunks).
				const std::size_t loaded_chunks =
						chunk_renderer ? chunk_renderer->uploaded_count() : 0;
				const float fraction = expected == 0
						? 1.0f
						: static_cast<float>(loaded_chunks) / static_cast<float>(expected);

				std::string operator_title;
				if (const auto &info = client->server_info()) {
					operator_title = info->motd;
				}
				menu.draw_loading(fraction, operator_title);

				const auto now = std::chrono::steady_clock::now();
				if (loaded_chunks > loading_last_uploaded) {
					loading_last_uploaded = loaded_chunks;
					loading_deadline = now + kLoadingStallTimeout;
				}
				if (fraction >= 1.0f || now > loading_deadline ||
						now > loading_hard_deadline) {
					state = AppState::kPlaying;
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
							controller.yaw(), controller.pitch(), mouse_captured,
							movement_bindings, client->registered_keybinds());
					client->push_input(cmd);
					// Singleplayer ticks the whole embedded game (client + server,
					// over loopback); a real connection just pumps this client's
					// GnsTransport -- the dedicated server ticks itself.
					if (connecting_singleplayer) {
						sp->tick(dt);
					} else {
						client->tick(dt);
					}
					const vb::core::Vec3d feet = client->predicted_feet();
					controller.set_position(
							{ feet.x, feet.y + move_params.eye_height, feet.z });
				}

				// Phase 6.17/6.20: neither breaking nor placing is a client-
				// authoritative hardcoded action any more -- the client only
				// reports raw input (buttons.primary/secondary, set above in
				// sample_input_cmd) and does its own raycast purely for the
				// crosshair-highlight visual below; content/base/mechanics.lua
				// decides *when* and *what* (player:break_block()/
				// player:place_block()) server-side, once a pack opts in at
				// all -- neither is an engine default any more.
				vb::world::VoxelRayHit look_hit;
				if (mouse_captured) {
					look_hit = vb::world::raycast_voxel(client->chunk_store(),
							controller.position(), controller.forward(), 5.0);
				}

				// Raw state only -- "engine provides raw state, Lua deals
				// with presentation". client.break_progress() now awaits the
				// still-deferred damage-*value* replication half of Phase 6.5
				// (REMAINING_TASKS.md 6.17): the engine no longer runs its
				// own local hold timer to approximate this from, so there's
				// nothing authoritative to report yet -- content/base/ui/
				// hud.lua simply won't draw a bar until that lands.
				ui_runtime.set_break_progress(std::nullopt);
				ui_runtime.set_screen_size(GetScreenWidth(), GetScreenHeight());

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

				// Day/night sky (spec §5.4): a simple gradient driven by the
				// server's time_of_day clock (S2C_JoinAccept's initial value,
				// kept current by periodic S2C_TimeOfDay updates). Overwrites
				// window.begin_frame()'s flat dark clear for this state only.
				const vb::world::SkyColor sky = vb::world::sky_color_for_time(
						client->time_of_day(), client->day_night_curve());
				ClearBackground(Color{ sky.r, sky.g, sky.b, 255 });

				// Phase 7.2: distance fog, always blending into the same sky
				// color computed above (never an independently drifting
				// tint -- see REMAINING_TASKS.md Phase 7.2). A pack's
				// vb.render.set_fog{start=, end=} overrides the distances;
				// absent that, the default matches this client's own
				// view_distance so fog fades in right around where chunks
				// stop streaming in, rather than at an arbitrary distance.
				if (chunk_renderer) {
					float fog_start;
					float fog_end;
					if (const auto &fog = client->fog_override()) {
						fog_start = fog->fog_start;
						fog_end = fog->fog_end;
					} else {
						fog_end = static_cast<float>(view_distance * vb::core::kChunkDim);
						fog_start = fog_end * 0.6f;
					}
					// A pack's vb.render.set_fog override can name any
					// distance it likes -- nothing about it is checked
					// against how far this client actually keeps chunks
					// loaded. An override longer than that reach would
					// show a hard, unfogged edge right where the world
					// stops rendering instead of the soft fade fog exists
					// to provide; bind the two together by clamping
					// fog_end to the real view distance regardless of
					// source (the engine default above is already exactly
					// at that bound, so this is a no-op for it -- only an
					// override can ever be pulled in). fog_start is
					// clamped to match so it can't end up past a
					// just-lowered fog_end.
					const float max_fog_distance = static_cast<float>(view_distance * vb::core::kChunkDim);
					fog_end = std::min(fog_end, max_fog_distance);
					fog_start = std::min(fog_start, fog_end);
					// Phase 7.3: "underwater" is the same sky-color fog
					// mechanism, just a much closer distance preset -- no
					// separate tint/color system (REMAINING_TASKS.md 7.3's
					// decision). Triggered whenever the camera's own eye
					// voxel is a liquid block, overriding whichever
					// fog_start/fog_end were picked above (default or a
					// pack's vb.render.set_fog override alike) so surfacing
					// always restores normal visibility immediately.
					const vb::core::Vec3d eye = controller.position();
					const vb::core::IVec3 eye_voxel{
						static_cast<int>(std::floor(eye.x)),
						static_cast<int>(std::floor(eye.y)),
						static_cast<int>(std::floor(eye.z))
					};
					const vb::core::BlockId eye_block =
							client->chunk_store().block_at(eye_voxel);
					vb::world::SkyColor fog_color = sky;
					if (client->chunk_store().registry().is_liquid(eye_block)) {
						fog_end = 8.0f;
						fog_start = 2.0f;
						// Phase 7.5: underwater fog defaults to the submerged
						// liquid's own texture's average color instead of
						// echoing the sky -- water should tint the murk
						// itself, not whatever time of day it happens to be.
						const Color tint = chunk_renderer->underwater_tint(eye_block);
						fog_color = vb::world::SkyColor{ tint.r, tint.g, tint.b };
					}
					chunk_renderer->set_fog(controller.position(), fog_color, fog_start, fog_end);
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
						mouse_captured, client->time_of_day());

				// The HUD (spec §5.4-adjacent, Phase 6.16): an always-on,
				// pack-defined overlay drawn every frame regardless of
				// whether a modal ui_runtime screen is also open. Today this
				// is how the hold-to-break progress bar is drawn --
				// content/base/ui/hud.lua reads client.break_progress() (set
				// above) and decides whether/how to show it; the engine
				// itself no longer draws a single pixel of it.
				hud_renderer.draw("hud", ui_runtime.render_hud());

				// Player list (spec §5.4): top-right, this client's name plus
				// everyone S2C_PlayerList/S2C_PlayerJoin/S2C_PlayerLeave says
				// is currently playing. Always visible (no toggle key --
				// keeping it simple and avoiding a clash with Tab, already
				// bound to mouse-capture release).
				{
					const auto &players = client->players();
					const int line_h = 18;
					int y = 12;
					char header[64];
					std::snprintf(header, sizeof(header), "players (%zu)",
							players.size() + 1);
					const int text_w = MeasureText(header, 16);
					DrawText(header, GetScreenWidth() - text_w - 12, y, 16,
							Color{ 200, 200, 210, 230 });
					y += line_h;
					if (!config.player_name.empty()) {
						const int name_w = MeasureText(config.player_name.c_str(), 16);
						DrawText(config.player_name.c_str(),
								GetScreenWidth() - name_w - 12, y, 16,
								Color{ 170, 220, 170, 230 });
						y += line_h;
					}
					for (const auto &[id, name] : players) {
						(void)id;
						const int name_w = MeasureText(name.c_str(), 16);
						DrawText(name.c_str(), GetScreenWidth() - name_w - 12, y, 16,
								Color{ 200, 200, 210, 230 });
						y += line_h;
					}
				}

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

				// Hotbar (spec §5.1): a real inventory sync now exists
				// (S2C_Inventory) even though there's no dedicated slot-select
				// input yet -- just render every slot the server last sent,
				// bottom-center, block name + count. Textures/atlas (5.1's own
				// deferred item) aren't wired to anything client-side yet, so
				// this is text-only like ui/inventory.lua's own known gap.
				{
					const auto &inv = client->inventory();
					if (!inv.empty()) {
						const auto &registry = client->chunk_store().registry();
						constexpr int kSlotW = 96;
						constexpr int kSlotH = 40;
						constexpr int kGap = 6;
						const int total_w = static_cast<int>(inv.size()) * (kSlotW + kGap) - kGap;
						int x = (GetScreenWidth() - total_w) / 2;
						const int y = GetScreenHeight() - kSlotH - 16;
						for (const auto &slot : inv) {
							DrawRectangle(x, y, kSlotW, kSlotH, Color{ 30, 30, 34, 200 });
							DrawRectangleLines(x, y, kSlotW, kSlotH, Color{ 90, 90, 100, 230 });
							std::string name = registry.contains(slot.item)
									? registry.get(slot.item).name
									: "?";
							char line[64];
							std::snprintf(line, sizeof(line), "%s x%u", name.c_str(),
									static_cast<unsigned>(slot.count));
							DrawText(line, x + 6, y + 12, 14, Color{ 220, 220, 220, 230 });
							x += kSlotW + kGap;
						}
					}
				}

				if (ui_runtime.is_open()) {
					const auto &widgets = ui_runtime.render_frame();
					const auto ui_result =
							ui_renderer.draw(ui_runtime.current_name(), widgets);
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
