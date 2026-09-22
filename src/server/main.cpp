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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include "vb/assetsync/manifest.hpp"
#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"
#include "vb/core/config.hpp"
#include "vb/core/log.hpp"
#include "vb/net/gns_transport.hpp"
#include "vb/net/session.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
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

	// One shared, mutable registry: PackRuntime may extend it with
	// vb.register_block before it's frozen and copied into World/WorldGenerator
	// (Phase 4.2). Phase 5.1's content/base pack re-declares the Phase 2 base
	// set by name (`add_or_get` is idempotent), so ids are unchanged unless the
	// pack adds something new.
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime pack_runtime(transport, registry,
			std::filesystem::path(config.content_pack) / "storage.json");
	// Phase 6.13: read-only vb.config.get(key) -- set before load_content_pack
	// so it's already visible to registration-time (module-scope) pack code.
	pack_runtime.set_server_config(config);
	if (!vb::script::load_content_pack(pack_runtime, config.content_pack)) {
		std::cerr << "server: content pack '" << config.content_pack
				  << "' failed to load, aborting\n";
		return EXIT_FAILURE;
	}
	pack_runtime.freeze();
	// Flush any vb.storage write load_content_pack's init.lua made (e.g.
	// content/base's own boot_count demo) to disk *before* the manifest
	// below hashes storage.json's on-disk bytes -- otherwise storage_dirty
	// stays true until the first PackRuntime::dispatch_tick() (Phase 4.2's
	// deferred-flush design), which lands *after* the manifest is already
	// built. That flush then silently rewrites storage.json out from under
	// the just-hashed manifest entry: the next asset_file_bytes() read for
	// it returns the new bytes, but the manifest still advertises the old
	// (pre-flush) hash, so every connecting client's asset-sync fails that
	// one file's verification with "hash mismatch" -- deterministically, on
	// every connect, on every machine, for any pack that writes vb.storage
	// at load time. Found 2026-09-18 while investigating exactly that
	// report; see STATE.md.
	pack_runtime.flush_storage();

	// Asset manifest (Phase 4.4): built once at startup from the content
	// pack, handed to every connection by reference. A build without
	// VB_WITH_COMPRESSION (kDisabled) just skips asset sync entirely for
	// every client, same graceful degrade as VB_WITH_NET off above; any
	// other failure means the pack itself is broken/hostile and is fatal.
	auto manifest_result = vb::assetsync::build_manifest(config.content_pack,
			{ static_cast<std::uint64_t>(config.asset_max_file_mb) * 1024ull * 1024ull,
				static_cast<std::uint64_t>(config.asset_max_total_mb) * 1024ull * 1024ull });
	std::shared_ptr<const vb::assetsync::Manifest> manifest_ptr;
	if (manifest_result) {
		manifest_ptr = std::make_shared<const vb::assetsync::Manifest>(
				std::move(*manifest_result));
	} else if (manifest_result.error() != vb::core::AssetSyncError::kDisabled) {
		std::cerr << "server: failed to build asset manifest for "
				  << config.content_pack << ": "
				  << vb::core::message(manifest_result.error()) << '\n';
		return EXIT_FAILURE;
	}

	vb::world::World world(registry);
	vb::worldgen::WorldGenParams gen_params;
	gen_params.seed = seed;
	// Phase 6.14: nullptr unless a pack ever called vb.worldgen.set_pipeline,
	// in which case WorldGenerator uses the pack-driven pipeline instead of
	// its fixed default -- same opt-in shape as effective_move_params above.
	const auto worldgen_pipeline = pack_runtime.build_worldgen_pipeline(gen_params);
	const vb::worldgen::WorldGenerator generator(gen_params, registry, worldgen_pipeline);
	vb::worldgen::WorldGenWorkerPool pool(generator); // copies into the pool

	vb::net::HandshakeServerConfig hs_config;
	hs_config.pack_name = "base";
	hs_config.tick_rate = static_cast<std::uint16_t>(config.tick_rate);
	hs_config.motd = config.motd;
	hs_config.auth_mode = static_cast<vb::protocol::AuthMode>(config.auth_mode);
	hs_config.max_players = config.max_players;
	hs_config.world_seed = seed;

	// JoinGrant::spawn_pos otherwise defaults to a fixed {0, 64, 0} regardless
	// of seed -- with base_height=64 and amplitude=28 the real surface ranges
	// roughly [36, 92], so a fixed Y can land at or below it and spawn the
	// player embedded in solid terrain outright (no fall involved).
	vb::net::HandshakeServerHost host;
	host.on_ready = [generator](std::string_view) {
		vb::net::JoinGrant grant;
		grant.spawn_pos = vb::worldgen::default_spawn_position(generator);
		return grant;
	};
	// Phase 4.3: advertise the (possibly Lua-extended) registry to every
	// joining client so custom blocks aren't invisible client-side.
	host.block_registry =
			[&registry]() -> std::optional<std::vector<vb::protocol::BlockRegistryRecord>> {
		std::vector<vb::protocol::BlockRegistryRecord> out;
		out.reserve(registry.size());
		for (std::size_t i = 0; i < registry.size(); ++i) {
			const auto &t = registry.get(static_cast<vb::core::BlockId>(i));
			out.push_back({ t.name, t.solid, t.opaque, t.liquid, t.light_emission });
		}
		return out;
	};
	// Phase 6.7: fold the operator's server.toml gravity in as the base, then
	// let a pack's vb.physics.set_params{...} override on top of it (only the
	// fields it actually set) -- effective_move_params() returns `base`
	// unchanged when no pack ever calls it. Advertised to joining clients so
	// their prediction uses the exact same tunables as this authoritative
	// simulation, mirroring host.block_registry's pattern.
	vb::physics::MoveParams move_params;
	move_params.gravity = config.gravity;
	move_params = pack_runtime.effective_move_params(move_params);
	host.move_params = [move_params]() -> std::optional<vb::protocol::S2CMoveParams> {
		return vb::protocol::S2CMoveParams{ move_params.half_width, move_params.height,
			move_params.eye_height, move_params.walk_speed, move_params.sprint_speed,
			move_params.accel, move_params.air_accel, move_params.friction,
			move_params.gravity, move_params.jump_speed, move_params.terminal_velocity,
			move_params.step_height, move_params.fly_speed, move_params.fly };
	};
	// Phase 6.8: advertise a pack's vb.daynight.set_curve{...} override the
	// same way -- nullopt (no pack ever called it) sends no frame, leaving
	// every client on vb::world::default_day_night_curve() unchanged.
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
	host.asset_manifest = [manifest_ptr] { return manifest_ptr; };
	host.asset_file_bytes = [manifest_ptr, content_pack = config.content_pack](
			vb::core::AssetHash h) -> std::optional<std::vector<std::byte>> {
		if (!manifest_ptr) {
			return std::nullopt;
		}
		const auto *e = manifest_ptr->find(h);
		if (e == nullptr) {
			return std::nullopt;
		}
		std::ifstream f(std::filesystem::path(content_pack) / e->path, std::ios::binary);
		if (!f) {
			return std::nullopt;
		}
		std::vector<std::byte> buf(static_cast<std::size_t>(e->size));
		if (e->size > 0) {
			f.read(reinterpret_cast<char *>(buf.data()),
					static_cast<std::streamsize>(e->size));
		}
		return buf;
	};
	pack_runtime.install_keybind_registry(host); // before ServerSession copies `host` in
	pack_runtime.install_join_veto(host); // before ServerSession copies `host` in

	vb::net::ServerSession session(transport, hs_config, host);
	auto replicator = std::make_unique<vb::net::WorldReplicator>(world, pool,
			registry, static_cast<int>(config.view_distance), 3);
	pack_runtime.attach_world(*replicator);
	session.set_world_replicator(std::move(replicator));
	pack_runtime.attach_session(session);

	session.set_move_params(move_params);
	// Phase 6.18: a pack's vb.combat.set_params{...} overrides player:punch()'s
	// reach/hit_radius/player_damage defaults, same config-then-pack-override
	// shape as move_params above.
	session.set_punch_params(pack_runtime.effective_punch_params(
			vb::net::ServerSession::PunchParams{}));
	// Phase 6.21: a pack's vb.action.set_params{reach=...} overrides the one
	// value both WorldReplicator's block-edit reach check and
	// ServerSession::punch() now share, same config-then-pack-override shape
	// as move_params/punch_params above.
	session.world_replicator()->set_reach(pack_runtime.effective_action_params(
			vb::net::ActionParams{}).reach);
	session.set_void_kill_y(config.void_kill_y);
	// §8.3 hardening: 0 (server.toml's own default) means unlimited, same as
	// today's unset behavior -- only bites once an operator opts in.
	session.set_max_connections_per_ip(
			static_cast<int>(config.max_connections_per_ip));
	// Phase 6.8: fold server.toml's day_length_seconds in as the base, then
	// let a pack's vb.daynight.set_day_length(...) override on top of it --
	// mirrors move_params.gravity's config-then-pack-override precedent.
	session.set_day_length_seconds(
			pack_runtime.effective_day_length_seconds(config.day_length_seconds));

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
			pack_runtime.dispatch_player_join_completed(joined);
			std::cout << "server: '" << joined.name << "' joined (net id "
					  << static_cast<std::uint32_t>(joined.net_id) << ")\n";
		}
		for (const auto &left : session.take_leaves()) {
			pack_runtime.dispatch_player_leave(left);
			std::cout << "server: a player left (" << left.reason << ")\n";
		}
		pack_runtime.dispatch_tick(tick_dt_seconds);

		if (max_ticks > 0 && tick >= max_ticks) {
			break;
		}

		next += tick_dt;
		std::this_thread::sleep_until(next);
	}

	std::cout << "server: stopped after " << tick << " ticks\n";
	return EXIT_SUCCESS;
}
