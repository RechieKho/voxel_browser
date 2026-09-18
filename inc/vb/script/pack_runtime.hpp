#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/net/handshake.hpp"
#include "vb/net/session.hpp"
#include "vb/net/transport.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/script/vm.hpp"
#include "vb/world/block.hpp"
#include "vb/world/daynight.hpp"

// Server-side Lua registration + runtime API (spec §10.3, Phase 4.2). Owns a
// Vm, the frozen-after-load content registries, the event bus, and
// vb.after/vb.every timers.
//
// Layering: PackRuntime depends *down* on Transport/ServerSession/
// WorldReplicator/BlockRegistry -- none of them ever include this header
// (WorldReplicator's block-edit veto seam is a plain std::function struct,
// vb/net/world_replicator.hpp's BlockEditHooks).
//
// Construction is split because a join veto must wrap HandshakeServerHost
// *before* the ServerSession that owns it exists, while runtime dispatch
// needs that same ServerSession by reference:
//
//   PackRuntime rt(transport, registry, storage_path);
//   rt.load_pack_file(pack_source);
//   rt.freeze();
//   rt.install_join_veto(host);              // before constructing session
//   net::ServerSession session(transport, cfg, host);
//   net::WorldReplicator replicator(world, pool, registry, view_dist);
//   session.set_world_replicator(...);
//   rt.attach_world(*session.world_replicator());
//   rt.attach_session(session);
//   // main loop, once per tick, after session.tick(dt):
//   for (auto &j : session.take_joins())  rt.dispatch_player_join_completed(j);
//   for (auto &l : session.take_leaves()) rt.dispatch_player_leave(l);
//   rt.dispatch_tick(dt);
//
// Single-threaded: construct and drive every method from the same thread
// that calls ServerSession::tick() (the server main loop). No locking.

namespace vb::script {

class PackRuntime {
public:
	// `storage_path` is the JSON file backing vb.storage.
	PackRuntime(net::Transport &transport, world::BlockRegistry &registry,
			std::filesystem::path storage_path, VmLimits limits = {});
	~PackRuntime();
	PackRuntime(PackRuntime &&) noexcept;
	PackRuntime &operator=(PackRuntime &&) noexcept;
	PackRuntime(const PackRuntime &) = delete;
	PackRuntime &operator=(const PackRuntime &) = delete;

	// Run one pack Lua file (pack-load time only: registration calls are
	// allowed). Call freeze() once every pack file is loaded, before
	// attach_world()/attach_session().
	ScriptResult load_pack_file(std::string_view code,
			std::string_view chunk_name = "pack");
	void freeze();

	// Wraps host.authenticate so vb.on("player_join", handler) can veto a join
	// before it completes. Call after freeze(), before constructing the
	// ServerSession that will own `host`.
	void install_join_veto(net::HandshakeServerHost &host);

	// Wraps host.keybind_registry so every vb.register_keybind name reaches
	// joining clients as S2C_KeybindRegistry (Phase 6.3). Same calling
	// convention as install_join_veto: call after freeze(), before
	// constructing the ServerSession that will copy `host`.
	void install_keybind_registry(net::HandshakeServerHost &host);

	// Call once each object exists to enable the block-edit veto/on_break/
	// on_place hooks and the entity/player runtime API respectively.
	void attach_world(net::WorldReplicator &replicator);
	void attach_session(net::ServerSession &session);

	// Phase 6.7: applies a pack's `vb.physics.set_params{...}` on top of
	// `base` -- only the fields the pack actually set replace `base`'s value,
	// everything else keeps it. `base` is the engine/operator default (e.g.
	// ServerConfig::gravity already folded in), so a pack that never calls
	// vb.physics.set_params gets `base` back unchanged. Call after freeze(),
	// once, before constructing the ServerSession (its result also belongs on
	// HandshakeServerHost::move_params so the client mirrors it exactly).
	physics::MoveParams effective_move_params(physics::MoveParams base) const;

	// Phase 6.8: a pack's `vb.daynight.set_curve{keyframes = {...}}`, if it
	// ever called it -- `nullopt` (default) means no pack ever overrode the
	// curve, so the caller should send no S2C_DayNightCurve frame at all and
	// let the client keep vb::world::default_day_night_curve() (same "no
	// frame, no behavior change" posture as effective_move_params() feeding
	// HandshakeServerHost::move_params).
	std::optional<world::DayNightCurve> effective_day_night_curve() const;

	// Phase 6.8: applies a pack's `vb.daynight.set_day_length(seconds)` on top
	// of `base` (the operator's `server.toml` day_length_seconds, or
	// ServerSession's own hardcoded default) -- returns `base` unchanged if no
	// pack ever calls it, same "engine/operator default, pack overrides on
	// top" shape as effective_move_params()/ServerConfig::gravity.
	double effective_day_length_seconds(double base) const;

	// Drive from the main loop, once per tick, after ServerSession::tick():
	void dispatch_player_join_completed(const net::SessionPlayerJoined &j);
	void dispatch_player_leave(const net::SessionPlayerLeft &l);
	void dispatch_tick(double dt_seconds);

	// Generic bus hook for player_interact (no C2S message yet) -- exposed so
	// a future message handler can call it without knowing anything about
	// Lua. Returns false if any handler vetoed.
	bool dispatch_player_interact(core::NetId player, core::IVec3 target);

	// Phase 5.4/6.10: runs every vb.on("chat", handler) in registration
	// order, chaining text replacements (each handler sees the prior one's
	// output) and short-circuiting on the first `false` veto -- same shape as
	// run_player_input/ServerSession::InputHookResult.
	net::ServerSession::ChatHookResult dispatch_chat(
			core::NetId sender, std::string_view text);

	// Wired automatically by attach_session() to ServerSession's
	// C2S_UiEvent handler (Phase 4.5) -- fires vb.on("ui_event", handler)
	// non-vetoably; spec gives no veto semantics for UI events.
	void dispatch_ui_event(core::NetId player, const protocol::C2SUiEvent &event);

	bool storage_dirty() const;
	void flush_storage(); // write vb.storage to storage_path if dirty

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::script
