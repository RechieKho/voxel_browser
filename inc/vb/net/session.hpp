#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <memory>
#include <unordered_map>
#include <utility>

#include <entt/entt.hpp>

#include "vb/assetsync/cache.hpp"
#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/ecs/components.hpp"
#include "vb/ecs/system_runner.hpp"
#include "vb/net/handshake.hpp"
#include "vb/net/transport.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/protocol/chat.hpp"
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/input.hpp"
#include "vb/protocol/inventory.hpp"
#include "vb/protocol/snapshot.hpp"
#include "vb/replication/interest.hpp"
#include "vb/world/block_damage.hpp"
#include "vb/world/client_chunk_store.hpp"
#include "vb/world/daynight.hpp"
#include "vb/world/item_drops.hpp"

// Sessions glue a Transport to the handshake FSMs and present a small
// game-facing API: the server loop pulls join/leave events, the client loop
// polls a status. Both are driven by one tick() per frame/tick.
//
// Transport backend is injected, so the same ServerSession runs behind a
// LoopbackTransport (integrated singleplayer, tests) or a GnsTransport
// (dedicated server) with no code change.

namespace vb::net {

// Day/night cycle (spec §5.4): ServerSession's built-in real-seconds-per-day
// default, also the base --singleplayer's in-process host feeds into
// PackRuntime::effective_day_length_seconds() (Phase 6.8) since it has no
// server.toml to read a config value from.
inline constexpr double kDefaultDayLengthSeconds = 1200.0;

// --- server ---------------------------------------------------------------

struct SessionPlayerJoined {
	ConnId conn = ConnId::kInvalid;
	core::NetId net_id = core::NetId::kInvalid;
	std::string name;
};

struct SessionPlayerLeft {
	ConnId conn = ConnId::kInvalid;
	core::NetId net_id = core::NetId::kInvalid;
	std::string reason;
};

class ServerSession {
public:
	ServerSession(Transport &transport, HandshakeServerConfig config,
			HandshakeServerHost host = {});

	void tick(double dt_seconds);

	std::vector<SessionPlayerJoined> take_joins();
	std::vector<SessionPlayerLeft> take_leaves();

	std::size_t player_count() const { return playing_; }
	std::size_t pending_count() const { return conns_.size() - playing_; }
	std::uint32_t server_tick() const { return server_tick_; }

	// Feed authoritative entity state into the replication grid (Phase 3's
	// movement systems will call this; tests set it directly).
	void set_player_state(core::NetId id, core::Vec3d pos, core::Vec2f rot = {},
			core::Vec3f vel = {});

	void set_interest_radius_cells(int cells) { interest_radius_cells_ = cells; }

	// Day/night cycle (spec §5.4): real seconds for one full in-game day.
	// Default matches world::advance_time_of_day's expectations; set before
	// players join if a pack/host wants a different pace.
	void set_day_length_seconds(double seconds) { day_length_seconds_ = seconds; }
	std::uint32_t time_of_day() const {
		return static_cast<std::uint32_t>(time_of_day_ticks_);
	}

	// Movement tunables applied to every player (spec §7.3). Set before players
	// join; a Lua pack overrides per entity kind in Phase 4.
	void set_move_params(physics::MoveParams p) { move_params_ = p; }

	// Authoritative feet position of a player (for tests / teleports). Input
	// batches are the normal drive path. nullopt if `id` isn't a playing
	// connection.
	std::optional<physics::MoveState> player_move_state(core::NetId id) const;

	// Phase 4.2 (Lua entity/player API): directly set a connected player's
	// authoritative velocity. No-op if `id` isn't a playing connection.
	void set_player_velocity(core::NetId id, core::Vec3d vel);

	// Transport-level connection for a playing net id (kInvalid if not
	// found/not playing) — lets a script host send arbitrary framed messages
	// (chat / open_ui) without ServerSession knowing their contents.
	ConnId conn_for_player(core::NetId id) const;

	// Phase 6.17: performs a block edit exactly as if it had arrived as a
	// real C2S_BlockEdit from `editor`, without a wire frame -- lets a script
	// host implement its own breaking/placing policy (e.g. content/base's
	// Lua hold-to-break; the engine no longer has a built-in one) while
	// reusing the one validated pipeline (reach check, block-edit hooks,
	// drops, relight, delta fan-out to every mirroring client, including
	// the editor's own). Returns the resulting S2C_BlockEditResult's
	// `accepted` flag; false (no-op) without a WorldReplicator attached.
	bool apply_script_block_edit(core::NetId editor,
			protocol::BlockEditAction action, core::IVec3 pos,
			core::BlockId block = core::BlockId::kAir);

	// Display name of a playing net id ("" if not found/not playing).
	std::string_view player_name(core::NetId id) const;

	// Optional: attach world replication (chunk streaming). Without it the
	// session only replicates entities.
	void set_world_replicator(std::unique_ptr<WorldReplicator> replicator) {
		replicator_ = std::move(replicator);
	}
	WorldReplicator *world_replicator() { return replicator_.get(); }

	// Phase 4.5: routes a playing connection's C2S_UiEvent up to a script
	// host, without ServerSession knowing anything about Lua. Unset (the
	// default) leaves UI events silently ignored, same posture as the
	// "unknown post-join message" comment this replaces for kC2SUiEvent.
	void set_ui_event_handler(
			std::function<void(core::NetId, const protocol::C2SUiEvent &)> handler) {
		on_ui_event_ = std::move(handler);
	}

	// Phase 6.10: veto-or-replace shape mirroring PlayerInputOverride/
	// InputHookResult below -- a pack can suppress a chat line outright
	// (`veto`) or rewrite its text (`replacement_text`, e.g. profanity
	// filtering or custom formatting) before ServerSession broadcasts it.
	struct ChatHookResult {
		bool veto = false;
		std::optional<std::string> replacement_text;
	};

	// Phase 5.4/6.10: routes a playing connection's C2S_Chat up to a script
	// host before ServerSession broadcasts it. Unset (the default -- e.g.
	// `--singleplayer`, which has no PackRuntime) means every chat line is
	// broadcast unchanged, same "no handler, no side effect" posture as
	// set_input_handler.
	void set_chat_handler(
			std::function<ChatHookResult(core::NetId, std::string_view)> handler) {
		on_chat_ = std::move(handler);
	}

	// Death/respawn (spec §5.4): a player whose feet fall below this world Y
	// (the "void") is killed instantly and respawned at their spawn point.
	// Health also generically triggers a respawn at 0 -- nothing decrements
	// it yet besides the void check (no combat system exists), but the path
	// is shared so a future damage source gets respawn for free.
	void set_void_kill_y(double y) { void_kill_y_ = y; }

	// Per-IP connection cap (§8.3 hardening, tracked in REMAINING_TASKS.md's
	// 1.3): `0` (default) = unlimited, matching every other optional policy
	// knob in this class. Enforced in tick()'s kConnected handling, using
	// Transport::remote_address() -- LoopbackTransport always returns
	// nullopt there (no real network identity in-process), so this is
	// effectively a no-op over loopback/singleplayer regardless of the
	// configured value; it only bites over a real GnsTransport.
	void set_max_connections_per_ip(int n) { max_connections_per_ip_ = n; }

	// Phase 6.6: generic damage primitive -- the only way to reduce a
	// player's health besides the void-kill check above. `cause` is opaque
	// to the engine (e.g. "fall", "pvp", "void") and threaded through
	// unchanged to the respawn handler below. No-op if `id` isn't a playing
	// connection or is already at 0 health awaiting this tick's respawn.
	void damage_player(core::NetId id, float amount, std::string_view cause);

	// Authoritative feet position `id` was granted at join (spec §8.3's
	// JoinGrant::spawn_pos) -- the same fixed point respawns used to reuse
	// forever before the respawn handler below existed. A pack's respawn
	// handler can read this as a default respawn point, or ignore it
	// entirely for its own checkpoint/bed logic. {} if `id` isn't playing.
	core::Vec3d spawn_point(core::NetId id) const;

	// Phase 6.6: what to do once a player's health reaches 0, decided by the
	// respawn handler below rather than hardcoded. Inventory-drop and any
	// other side effect is the handler's own business (ServerSession has no
	// concept of inventory) -- it gets the full decision here, but the
	// *doing* of a drop, if any, happens on the handler's side before it
	// returns.
	struct RespawnDecision {
		float heal_to = 0.0f;
		core::Vec3d pos{};
		std::string message; // sent as a private S2C_Chat line if non-empty
	};

	// Fires once per respawn (health reaching 0, for any cause -- void-kill
	// included) with (player, cause, health_before), and decides the new
	// health/position/message. Unset (the default -- e.g. `--singleplayer`
	// before a PackRuntime attaches one) falls back to the original
	// behavior: full heal, teleport to the join spawn point, fixed message
	// -- so every pre-6.6 caller/test is unaffected.
	void set_respawn_handler(std::function<RespawnDecision(
					core::NetId, std::string_view, float)>
					handler) {
		on_respawn_ = std::move(handler);
	}

	// Phase 6.3 (vb.on("player_input", handler)): a handler-chosen override
	// for one InputCmd's move/yaw/pitch/buttons, applied before movement
	// integration. `veto` drops this cmd's effect on movement/rotation
	// entirely (its seq is still consumed/acked, so it isn't reprocessed
	// forever); `replacement`, if set, replaces the cmd used for this tick.
	struct PlayerInputOverride {
		core::Vec3f move;
		float yaw = 0.0f;
		float pitch = 0.0f;
		std::uint8_t buttons = 0;
		std::uint32_t keybinds = 0;
	};
	struct InputHookResult {
		bool veto = false;
		std::optional<PlayerInputOverride> replacement;
	};

	// Unset (the default -- e.g. `--singleplayer` before a PackRuntime
	// attaches one) means every InputCmd passes through unchanged, same
	// "no handler, no side effect" posture as set_chat_handler.
	void set_input_handler(std::function<InputHookResult(
					core::NetId, const protocol::InputCmd &)>
					handler) {
		on_input_ = std::move(handler);
	}

	// Dropped-item entities (spec §5.1). Spawns one at `pos`, replicated
	// generically through the interest grid like any other entity -- no
	// dedicated wire message. Auto-collected when a player's feet come
	// within the system's pickup radius (see ItemDropSystem); the pickup
	// handler below is how that reaches a player's actual inventory.
	core::NetId spawn_item_drop(
			core::Vec3d pos, core::BlockId item, std::uint16_t count);

	// Phase 5.1: routes a player walking over a dropped item up to a script
	// host's inventory, without ServerSession knowing anything about Lua.
	// Unset (the default -- e.g. `--singleplayer`, which has no PackRuntime)
	// means picked-up items vanish with no effect, same "no handler, no
	// side effect" posture as set_chat_handler/set_ui_event_handler.
	void set_item_pickup_handler(
			std::function<void(core::NetId, core::BlockId, std::uint16_t)> handler) {
		on_item_pickup_ = std::move(handler);
	}

	// Phase 6.5 (spec §10.7): shared block-damage breaking hooks. Unset
	// fields mean the corresponding half is a no-op -- e.g. no `begin` means
	// every C2S_BlockBreakBegin is rejected outright (no pack attached), and
	// no `tick_damage` means damage never accrues even for an accepted
	// begin, matching "the engine ships zero built-in policy" (§10.7).
	struct BlockBreakHooks {
		std::function<bool(core::NetId, core::IVec3, core::BlockId)> begin;
		std::function<float(
				core::NetId, core::IVec3, core::BlockId, std::uint16_t)>
				tick_damage;
		std::function<std::optional<float>(
				core::IVec3, core::BlockId, float, std::uint16_t, std::uint64_t)>
				health_tick;
	};
	void set_block_break_hooks(BlockBreakHooks hooks) {
		block_break_hooks_ = std::move(hooks);
	}

	// Phase 7.3 (spec: REMAINING_TASKS' "generic region-enter/exit hook"):
	// fires once per player per crossing of a `BlockType::region` block's
	// boundary, not per-tick -- water is the first block to opt in, but this
	// is a generic occupancy tracker, not a liquid-specific one. Unset
	// fields (or both unset) mean update_region_occupancy() below is a no-op
	// -- same "no handler installed, zero side effect and zero extra
	// per-tick cost" posture as BlockBreakHooks above.
	struct RegionHooks {
		std::function<void(core::NetId, core::IVec3, core::BlockId)> enter;
		std::function<void(core::NetId, core::IVec3, core::BlockId)> exit;
	};
	void set_region_hooks(RegionHooks hooks) { region_hooks_ = std::move(hooks); }

	// Phase 6.1 (vb.register_entity / vb.world.spawn): a generic Lua-kind
	// entity, replicated the exact same way spawn_item_drop's entries are --
	// no dedicated wire message, just another interest-grid entry keyed by a
	// NetId from its own id range (disjoint from both players, which start at
	// 1, and item drops, which start at 0x8000'0000 -- see item_drops.hpp).
	// ServerSession has no idea these are Lua-backed; PackRuntime owns the
	// per-instance `self` table and on_spawn/on_tick/on_hit/on_death
	// dispatch entirely on its own side.
	core::NetId spawn_script_entity(core::EntityKindId kind, core::Vec3d pos);
	void set_script_entity_state(core::NetId id, core::Vec3d pos,
			core::Vec2f rot = {}, core::Vec3f vel = {});
	void remove_script_entity(core::NetId id);

	// Phase 6.18 (Growtopia-style combat): tunables for punch() below. One
	// discrete swing per call -- edge-triggering (only calling punch() on a
	// rising "attack key" edge, not every tick it's held) is entirely the
	// caller's job, same posture as every other "engine ships a default,
	// pack can override individual fields" knob (vb.physics.set_params,
	// 6.7). Global, not per-entity-kind, since no entity kind besides the
	// player throws punches today.
	struct PunchParams {
		// Phase 6.21: reach moved out to the shared ActionParams::reach
		// (world_replicator.hpp) -- punch() reads world_replicator()->reach()
		// so combat reach and block-edit reach are always the same one
		// pack-overridable value (vb.action.set_params{reach=...}), not two
		// independently-overridable numbers that could drift apart.
		float hit_radius = 0.6f; // capsule radius around a player's torso point
		float player_damage = 1.0f; // PvP damage per punch landing on a player
		// Self-heal (spec-equivalent to 6.5's BlockDamageSystem heal hook, but
		// a built-in engine default here instead of "zero policy until a pack
		// supplies one" -- punching has no begin/stop lifecycle for a pack to
		// hang a heal policy off of, so the engine ships one directly).
		// A block with no punches landed on it idles indefinitely; once one
		// exists, `heal_after_seconds` of no *new* punches lets it start
		// healing, then it loses one punch every `heal_interval_seconds`
		// until back to 0 (fully repaired) or hit again (resets both timers).
		// A negative `heal_after_seconds` disables healing entirely -- punch
		// counts then only ever go away by actually breaking the block.
		double heal_after_seconds = 4.0;
		double heal_interval_seconds = 1.5;
	};
	void set_punch_params(PunchParams p) { punch_params_ = p; }
	const PunchParams &punch_params() const { return punch_params_; }

	struct PunchResult {
		bool hit_player = false;
		core::NetId target = core::NetId::kInvalid; // valid iff hit_player
		bool hit_block = false;
		core::IVec3 block_pos{}; // valid iff hit_block
		std::uint16_t block_punches = 0; // accumulated hits, iff hit_block
		bool block_broken = false; // iff hit_block and this punch broke it
	};

	// Resolves one discrete punch from `puncher`: raycasts blocks and nearby
	// players along `puncher`'s current look direction (authoritative
	// yaw/pitch + eye position, not anything client-reported) and picks
	// whichever is closer -- "hit whatever's directly in front of you,"
	// same as the real client's own crosshair raycast, just without a
	// camera object. A player hit applies instant PvP damage
	// (damage_player()); a block hit increments a sparse per-position punch
	// counter and, once it reaches the target's BlockType::max_damage
	// (0 = break on the very first punch, same "unset" meaning it always
	// had), commits the break through apply_script_block_edit() below --
	// the exact same validated pipeline (reach check, hooks, drops, relight,
	// fan-out) a real C2S_BlockEdit uses. Returns a default/empty
	// PunchResult (both `hit_player`/`hit_block` false) if `puncher` isn't a
	// playing connection or nothing is within reach.
	PunchResult punch(core::NetId puncher);

private:
	struct Conn {
		explicit Conn(ServerHandshake hs) : handshake(std::move(hs)) {}
		ServerHandshake handshake;
		// Set once, from Transport::remote_address() at kConnected time --
		// nullopt over LoopbackTransport (no real network identity), a real
		// IP string over GnsTransport. Used only for the per-IP connection
		// cap (set_max_connections_per_ip).
		std::optional<std::string> remote_address;
		double age = 0.0;
		bool playing = false;
		bool input_driven = false;
		core::NetId net_id = core::NetId::kInvalid;
		// The player's ecs::Position/Velocity/Rotation/Collider/PlayerInput/
		// Health/PlayerTag/NetReplicated components live in `registry_` -- see
		// the comment there. Only valid once `playing` (created on join
		// completion, destroyed on disconnect); entt::null until then.
		entt::entity entity{ entt::null };
		std::vector<core::NetId> last_visible;
		core::Vec3d spawn_pos{};
		// Set by apply_damage() the instant health reaches 0; consumed and
		// cleared by check_respawns() when it calls the respawn handler.
		std::string death_cause;
		float death_health_before = 0.0f;
	};

	void drop(ConnId conn, const std::string &reason);
	const world::BlockSolidQuery &world_query() const;
	void handle_input_batch(Conn &conn, const protocol::C2SInputBatch &batch);
	void handle_block_edit(ConnId conn, Conn &state,
			const protocol::Frame &frame);
	void handle_chat(Conn &state, const protocol::Frame &frame);
	void handle_block_break_begin(ConnId conn, Conn &state,
			const protocol::Frame &frame);
	void handle_block_break_stop(Conn &state, const protocol::Frame &frame);
	void apply_damage(Conn &state, float amount, std::string_view cause);
	void check_respawns();
	void update_item_drops(double dt_seconds);
	void update_block_damage();
	void update_block_punch_healing(double dt_seconds);
	void update_region_occupancy();
	void broadcast_snapshots();
	void broadcast_world();
	void broadcast_time_of_day();

	// SystemRunner (spec §6/§7.2): named, ordered phases of tick(), built
	// once on first tick() call. system_network_io/system_handshake_timeouts/
	// system_advance_time_of_day just give the equivalent former inline
	// tick() blocks a name+slot; system_sync_interest is the one genuinely
	// new system (see its definition).
	void build_systems();
	void system_network_io();
	void system_handshake_timeouts(double dt_seconds);
	void system_advance_time_of_day(double dt_seconds);
	void system_sync_interest();

	Transport &transport_;
	HandshakeServerConfig config_;
	HandshakeServerHost host_;
	// One entity per playing connection (spec §7.1's base components --
	// Position/Velocity/Rotation/Collider/PlayerInput/Health/PlayerTag/
	// NetReplicated), created in tick()'s join-completion handling and
	// destroyed on disconnect, plus one entity per spawned script entity
	// (Position/EntityKind/NetReplicated, see spawn_script_entity). Iterated
	// generically by system_sync_interest() via SystemRunner (systems_,
	// below) -- see ARCHITECTURE_SPEC.md §6/§7.2.
	entt::registry registry_;
	ecs::SystemRunner systems_;
	// spawn_script_entity()'s NetId -> registry entity, so
	// set_script_entity_state()/remove_script_entity() can find the entity
	// again by the id PackRuntime already tracks its Lua-side state under.
	std::unordered_map<core::NetId, entt::entity> script_entities_;
	std::map<ConnId, Conn> conns_;
	replication::InterestGrid interest_;
	std::unique_ptr<WorldReplicator> replicator_;
	std::function<void(core::NetId, const protocol::C2SUiEvent &)> on_ui_event_;
	std::function<ChatHookResult(core::NetId, std::string_view)> on_chat_;
	world::ItemDropSystem item_drops_;
	std::function<void(core::NetId, core::BlockId, std::uint16_t)> on_item_pickup_;
	world::BlockDamageSystem block_damage_;
	BlockBreakHooks block_break_hooks_;
	// Phase 7.3: last-known region-block occupancy per playing net id --
	// absent = "not currently inside a region block". Compared each tick in
	// update_region_occupancy() to fire enter/exit exactly on the crossing,
	// not every tick spent inside one.
	std::unordered_map<core::NetId, std::pair<core::IVec3, core::BlockId>>
			region_occupancy_;
	RegionHooks region_hooks_;
	// Phase 6.18: sparse pos -> punch/heal state, distinct from
	// world::BlockDamageSystem above -- that system's begin/tick/stop
	// lifecycle models a *held*, continuous action across many ticks (5.2's
	// original hold-to-break); a punch is one atomic event with no "holding"
	// concept at all, so it needs no begin/stop, just "add one, check the
	// threshold" plus this struct's own idle-based self-heal. An entry is
	// removed the instant it breaks (apply_script_block_edit erases the
	// world entry, this map along with it), heals fully back to 0 (nothing
	// left to track), or never added in the first place for a max_damage ==
	// 0 (instant-break) block.
	struct PunchDamageState {
		std::uint16_t punches = 0;
		double idle_seconds = 0.0; // time since the last punch landed here
		// Seconds accumulated toward the next -1 heal step, only once
		// idle_seconds has crossed PunchParams::heal_after_seconds.
		double heal_progress = 0.0;
	};
	std::unordered_map<core::IVec3, PunchDamageState> block_punch_counts_;
	PunchParams punch_params_;
	physics::MoveParams move_params_;
	int interest_radius_cells_ = 2;
	double time_of_day_ticks_ = 0.0;
	double day_length_seconds_ = kDefaultDayLengthSeconds; // 20 real minutes/day
	double time_of_day_broadcast_accum_ = 0.0;
	double void_kill_y_ = -64.0;
	int max_connections_per_ip_ = 0; // 0 = unlimited
	std::function<RespawnDecision(core::NetId, std::string_view, float)>
			on_respawn_;
	std::function<InputHookResult(core::NetId, const protocol::InputCmd &)>
			on_input_;
	std::uint32_t server_tick_ = 0;
	std::size_t playing_ = 0;
	std::uint32_t next_net_id_ = 1;
	// Phase 6.1: starts well past any plausible player NetId (1, counting up)
	// and well short of ItemDropSystem's 0x8000'0000 range, so all three id
	// spaces stay disjoint without sharing a counter.
	std::uint32_t next_script_entity_id_ = 0x4000'0000u;
	std::vector<TransportEvent> scratch_;
	std::vector<SessionPlayerJoined> joins_;
	std::vector<SessionPlayerLeft> leaves_;
};

// --- client --------------------------------------------------------------

class ClientSession {
public:
	// `conn` is the ConnId returned by Transport::connect(). `cache` is
	// optional (default nullptr): when supplied, ClientHandshake's asset-sync
	// hooks are wired to it for real (Phase 4.4); when null, asset sync
	// behaves as already-synced (ClientHandshake's own no-op defaults).
	ClientSession(Transport &transport, ConnId conn, HandshakeClientConfig config,
			assetsync::ClientAssetCache *cache = nullptr);

	void tick(double dt_seconds);

	ClientHandshakeStatus status() const { return handshake_.status(); }
	bool joined() const {
		return handshake_.status() == ClientHandshakeStatus::kJoined;
	}
	bool failed() const {
		return handshake_.status() == ClientHandshakeStatus::kFailed ||
				!failure_reason_.empty();
	}
	const std::string &failure_reason() const { return failure_reason_; }

	const std::optional<protocol::S2CServerInfo> &server_info() const {
		return handshake_.server_info();
	}
	const std::optional<protocol::S2CJoinAccept> &join_accept() const {
		return handshake_.join_accept();
	}

	// Replicated view of other entities (spec §8.4). Updated from every
	// S2C_EntitySnapshot once joined.
	const std::unordered_map<core::NetId, protocol::EntityRecord> &
	remote_entities() const {
		return remote_;
	}
	std::uint32_t last_server_tick() const { return last_server_tick_; }

	// --- local-player prediction (spec §8.4) -----------------------------

	void set_move_params(physics::MoveParams p) { move_params_ = p; }
	// Whatever set_move_params() last set -- the engine default until (if
	// ever) a real S2C_MoveParams frame arrives and apply_move_params()
	// overwrites it (Phase 6.7). Callers that keep their own copy of
	// MoveParams for non-prediction purposes (e.g. eye height for the
	// camera) should re-read this rather than assume their local default
	// still matches what the session is actually predicting with.
	const physics::MoveParams &move_params() const { return move_params_; }
	// Seed the predicted state from S2C_JoinAccept spawn_pos.
	void set_local_feet(core::Vec3d feet) { predicted_.position = feet; }

	// Sample one input: predict locally against the chunk mirror, keep it in the
	// unacked history, and send a batch of recent commands on lane 4.
	void push_input(const protocol::InputCmd &cmd);

	// --- block editing (spec §5.2) --------------------------------------

	// Optimistically apply an edit to the local chunk mirror, remember it for
	// rollback, and send it. The server confirms with S2C_BlockEditResult and
	// the authoritative S2C_ChunkDelta.
	void push_block_edit(const protocol::C2SBlockEdit &edit);
	std::size_t pending_edit_count() const { return pending_edits_.size(); }

	// Phase 6.5 (spec §10.7): shared block-damage breaking. No optimistic
	// local apply here (unlike push_block_edit) -- there's nothing to predict
	// until the server actually commits the break, which arrives as an
	// ordinary S2C_ChunkDelta through the existing path. A future block-
	// selection UI sends `begin` once per newly-targeted max_damage>0 block
	// and `stop` when released/re-targeted/out of reach.
	void send_block_break_begin(core::IVec3 pos, core::IVec3 face) {
		send_message(transport_, conn_, protocol::C2SBlockBreakBegin{ pos, face });
	}
	void send_block_break_stop(core::IVec3 pos) {
		send_message(transport_, conn_, protocol::C2SBlockBreakStop{ pos });
	}

	const physics::MoveState &predicted_state() const { return predicted_; }
	core::Vec3d predicted_feet() const { return predicted_.position; }
	std::uint32_t last_acked_input_seq() const { return last_acked_seq_; }
	std::size_t unacked_input_count() const { return history_.size(); }

	// Remote entity position for rendering, interpolated at
	// server_time_est - 100 ms (spec §8.4). Falls back to the raw snapshot pos
	// when only one sample is known.
	core::Vec3d interpolated_pos(core::NetId id) const;

	// Replicated chunk mirror (spec §11.2), populated from S2C_Chunk* messages.
	const world::ClientChunkStore &chunk_store() const { return chunks_; }
	world::ClientChunkStore &chunk_store() { return chunks_; }

	// Virtual pack filesystem assembled by the asset-sync cache (Phase 4.4),
	// path -> bytes. Empty if no cache was supplied or sync hasn't finished.
	// Nothing consumes this yet (Lua require / texture loader land later).
	const std::unordered_map<std::string, std::vector<std::byte>> &
	virtual_pack_fs() const;

	// Pack-registered custom keybind names (spec §10.6, Phase 6.3), in
	// registration order == bit position for InputCmd::keybinds. Empty until
	// (and unless) an S2C_KeybindRegistry arrives -- a host that never opts
	// in leaves this empty forever, same "no frame, no behavior change"
	// posture as chunk_store()'s block registry.
	const std::vector<std::string> &registered_keybinds() const {
		return keybind_names_;
	}

	// Pack-registered `vb.register_entity{...}` kinds (entity-management
	// follow-up to Phase 6.1), index == EntityKindId - 1. Empty until (and
	// unless) an S2C_EntityKindRegistry arrives -- a host that never opts in
	// leaves this empty forever, same "no frame, no behavior change" posture
	// as registered_keybinds()/chunk_store()'s block registry.
	const std::vector<protocol::EntityKindRegistryRecord> &
	entity_kind_registry() const {
		return entity_kinds_;
	}

	// Looks up a script entity's registered kind record by EntityRecord::kind.
	// Returns nullptr for core::EntityKindId::kInvalid (every player) or an
	// id with no matching S2C_EntityKindRegistry entry (host never opted in,
	// or a stale id) -- callers fall back to a generic placeholder either way,
	// same "missing = default" posture as every other opt-in registry.
	const protocol::EntityKindRegistryRecord *entity_kind(
			core::EntityKindId id) const {
		if (id == core::EntityKindId::kInvalid) {
			return nullptr;
		}
		const auto index = static_cast<std::size_t>(id) - 1;
		if (index >= entity_kinds_.size()) {
			return nullptr;
		}
		return &entity_kinds_[index];
	}

	// --- client UI VM (spec §10.4, Phase 4.5) ---------------------------

	// Drains a pending S2C_OpenUi, if one arrived since the last call.
	std::optional<protocol::S2COpenUi> take_open_ui();

	// Sends one C2S_UiEvent (a UiRuntime widget callback calling
	// ui.send_event/ui.close). No optimistic local state, unlike block
	// edits -- just a pass-through RPC to the server's Lua VM.
	void send_ui_event(const protocol::C2SUiEvent &event) {
		send_message(transport_, conn_, event);
	}

	// --- chat (spec §5.4) -------------------------------------------------

	// Sends one C2S_Chat line typed into the HUD chat box.
	void send_chat(std::string_view text) {
		send_message(transport_, conn_, protocol::C2SChat{ std::string(text) });
	}

	// Drains chat lines (already server-formatted "<name>: <text>") received
	// since the last call, oldest first. Join/leave notices also land here as
	// "* <name> joined/left the game" lines (spec §5.4's "join-leave
	// messages"), interleaved with real chat in receipt order.
	std::vector<std::string> take_chat_messages();

	// Everyone else currently known to be playing (net id -> display name),
	// kept in sync by S2C_PlayerList/S2C_PlayerJoin/S2C_PlayerLeave. Does not
	// include this client's own name.
	const std::unordered_map<core::NetId, std::string> &players() const {
		return players_;
	}

	// The day/night gradient to render with (spec §5.4, Phase 6.8): the
	// engine default until (if ever) a real S2C_DayNightCurve frame arrives
	// and apply_day_night_curve() overwrites it -- same "no frame, no
	// behavior change" posture as move_params()/registered_keybinds().
	const world::DayNightCurve &day_night_curve() const {
		return day_night_curve_;
	}

	// A pack's fog distance override (spec §7.2, Phase 7.2), if a real
	// S2C_FogParams frame ever arrived and apply_fog_params() set it --
	// `nullopt` otherwise, meaning the caller should compute its own default
	// fog distance from its own view_distance config (unlike
	// day_night_curve()/move_params() there's no server-side universal
	// default to fall back on here, so this stays optional rather than
	// defaulting to an empty struct).
	const std::optional<protocol::S2CFogParams> &fog_override() const {
		return fog_override_;
	}

	// This player's inventory (spec §5.1), kept in sync by S2C_Inventory.
	// Empty until the first snapshot arrives (e.g. before any player:give()).
	const std::vector<protocol::InventorySlot> &inventory() const {
		return inventory_;
	}

	// Day/night cycle (spec §5.4): S2C_JoinAccept's value until the first
	// periodic S2C_TimeOfDay update arrives, then the latest of those. Ticks
	// into the day cycle -- see vb::world::daynight.hpp for the convention.
	std::uint32_t time_of_day() const {
		if (time_of_day_override_) {
			return *time_of_day_override_;
		}
		if (join_accept()) {
			return join_accept()->time_of_day;
		}
		return 0;
	}

private:
	// Handle a post-join gameplay message (snapshot / chunk). Returns true if
	// consumed.
	bool apply_gameplay_frame(const protocol::Frame &frame);
	void apply_block_registry(const protocol::S2CBlockRegistry &msg);
	void apply_keybind_registry(const protocol::S2CKeybindRegistry &msg);
	void apply_entity_kind_registry(const protocol::S2CEntityKindRegistry &msg);
	void apply_move_params(const protocol::S2CMoveParams &msg);
	void apply_day_night_curve(const protocol::S2CDayNightCurve &msg);
	void apply_fog_params(const protocol::S2CFogParams &msg);
	void apply_snapshot(const protocol::S2CEntitySnapshot &snap);
	void reconcile(const protocol::EntityRecord &authoritative,
			std::uint32_t acked_seq);
	void handle_block_edit_result(const protocol::S2CBlockEditResult &res);
	void forget_pending_edits_for(core::ChunkCoord coord);

	struct PendingEdit {
		std::uint32_t seq = 0;
		core::IVec3 pos{};
		core::BlockId prev = core::BlockId::kAir;
		core::ChunkCoord coord{};
	};

	Transport &transport_;
	ConnId conn_;
	ClientHandshake handshake_;
	bool started_ = false;
	std::string failure_reason_;
	std::vector<TransportEvent> scratch_;
	std::unordered_map<core::NetId, protocol::EntityRecord> remote_;
	// Client-side lightweight ECS mirror (spec §6): one entity per remote
	// net id, holding ecs::InterpBuffer (prev/cur sample for render
	// interpolation -- replaces the old bespoke RemoteSample struct) and
	// ecs::EntityKind. remote_ above stays the flat "latest record per net
	// id" view (unchanged public remote_entities() API, still used by
	// entity_renderer.cpp and tests); this registry is the interpolation
	// bookkeeping's real home instead of an ad hoc parallel map.
	entt::registry entity_registry_;
	std::unordered_map<core::NetId, entt::entity> net_to_entity_;
	world::ClientChunkStore chunks_{ world::BlockRegistry::base() };
	std::uint32_t last_server_tick_ = 0;
	assetsync::ClientAssetCache *asset_cache_ = nullptr; // not owned; may be null
	std::optional<protocol::S2COpenUi> pending_open_ui_;
	std::vector<std::string> pending_chat_;
	std::unordered_map<core::NetId, std::string> players_;
	std::optional<std::uint32_t> time_of_day_override_;
	std::vector<protocol::InventorySlot> inventory_;
	std::vector<std::string> keybind_names_;
	std::vector<protocol::EntityKindRegistryRecord> entity_kinds_;
	world::DayNightCurve day_night_curve_; // empty = default_day_night_curve()
	std::optional<protocol::S2CFogParams> fog_override_;

	physics::MoveState predicted_;
	physics::MoveParams move_params_;
	std::vector<protocol::InputCmd> history_; // unacked, ascending seq
	std::uint32_t last_acked_seq_ = 0;
	std::vector<PendingEdit> pending_edits_;
};

} // namespace vb::net
