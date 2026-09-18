#include "vb/script/pack_runtime.hpp"

#if !VB_WITH_LUA

// Stub build: scripting compiled out. Every entry point is a no-op / reports
// kDisabled, mirroring vm.cpp's disabled-build pattern.

namespace vb::script {

struct PackRuntime::Impl {};

PackRuntime::PackRuntime(net::Transport &, world::BlockRegistry &,
		std::filesystem::path, VmLimits)
		: impl_(nullptr) {}
PackRuntime::~PackRuntime() = default;
PackRuntime::PackRuntime(PackRuntime &&) noexcept = default;
PackRuntime &PackRuntime::operator=(PackRuntime &&) noexcept = default;

ScriptResult PackRuntime::load_pack_file(std::string_view, std::string_view) {
	return { false, core::ScriptError::kDisabled,
		"scripting disabled (built without VB_WITH_LUA)" };
}
void PackRuntime::freeze() {}
void PackRuntime::install_join_veto(net::HandshakeServerHost &) {}
void PackRuntime::install_keybind_registry(net::HandshakeServerHost &) {}
void PackRuntime::attach_world(net::WorldReplicator &) {}
void PackRuntime::attach_session(net::ServerSession &) {}
physics::MoveParams PackRuntime::effective_move_params(physics::MoveParams base) const {
	return base;
}
std::optional<world::DayNightCurve> PackRuntime::effective_day_night_curve() const {
	return std::nullopt;
}
double PackRuntime::effective_day_length_seconds(double base) const {
	return base;
}
void PackRuntime::dispatch_player_join_completed(const net::SessionPlayerJoined &) {}
void PackRuntime::dispatch_player_leave(const net::SessionPlayerLeft &) {}
void PackRuntime::dispatch_tick(double) {}
bool PackRuntime::dispatch_chat(core::NetId, std::string_view) { return true; }
bool PackRuntime::dispatch_player_interact(core::NetId, core::IVec3) {
	return true;
}
void PackRuntime::dispatch_ui_event(core::NetId, const protocol::C2SUiEvent &) {}
bool PackRuntime::storage_dirty() const { return false; }
void PackRuntime::flush_storage() {}

} // namespace vb::script

#else

#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "vb/core/log.hpp"
#include "vb/core/sha256.hpp"
#include "vb/ecs/components.hpp"
#include "vb/protocol/chat.hpp"
#include "vb/protocol/input.hpp"
#include "vb/protocol/inventory.hpp"
#include "vb/script/db.hpp"
#include "vb/script/vm_internal.hpp"
#include "vb/world/raycast.hpp"

namespace vb::script {

namespace {

sol::object json_to_lua(sol::state_view lua, const nlohmann::json &j) {
	switch (j.type()) {
		case nlohmann::json::value_t::null:
			return sol::make_object(lua, sol::lua_nil);
		case nlohmann::json::value_t::boolean:
			return sol::make_object(lua, j.get<bool>());
		case nlohmann::json::value_t::number_integer:
		case nlohmann::json::value_t::number_unsigned:
		case nlohmann::json::value_t::number_float:
			return sol::make_object(lua, j.get<double>());
		case nlohmann::json::value_t::string:
			return sol::make_object(lua, j.get<std::string>());
		case nlohmann::json::value_t::array: {
			sol::table t = lua.create_table();
			int i = 1;
			for (const auto &e : j) {
				t[i++] = json_to_lua(lua, e);
			}
			return t;
		}
		case nlohmann::json::value_t::object: {
			sol::table t = lua.create_table();
			for (const auto &[k, v] : j.items()) {
				t[k] = json_to_lua(lua, v);
			}
			return t;
		}
		default:
			return sol::make_object(lua, sol::lua_nil);
	}
}

nlohmann::json lua_to_json(const sol::object &obj) {
	switch (obj.get_type()) {
		case sol::type::lua_nil:
		case sol::type::none:
			return nullptr;
		case sol::type::boolean:
			return obj.as<bool>();
		case sol::type::number:
			return obj.as<double>();
		case sol::type::string:
			return obj.as<std::string>();
		case sol::type::table: {
			sol::table t = obj.as<sol::table>();
			std::size_t count = 0;
			for (const auto &kv : t) {
				(void)kv;
				++count;
			}
			bool is_array = count > 0;
			for (std::size_t i = 1; i <= count && is_array; ++i) {
				if (!t[i].valid()) {
					is_array = false;
				}
			}
			if (is_array) {
				nlohmann::json arr = nlohmann::json::array();
				for (std::size_t i = 1; i <= count; ++i) {
					arr.push_back(lua_to_json(t[i]));
				}
				return arr;
			}
			nlohmann::json j = nlohmann::json::object();
			for (const auto &kv : t) {
				if (kv.first.is<std::string>()) {
					j[kv.first.as<std::string>()] = lua_to_json(kv.second);
				}
			}
			return j;
		}
		default:
			return nullptr;
	}
}

constexpr int kMaxTimerCatchUpFires = 8; // anti-stall guard for vb.every after a stall

// Phase 6.3: builds the Lua-facing `input` table vb.on("player_input", ...)
// receives, from the current working values (not the raw wire bitmask) --
// only registered keybind names ever appear as `keybinds` keys.
sol::table build_input_table(sol::state &lua, core::Vec3f move, float yaw,
		float pitch, std::uint8_t buttons, std::uint32_t keybinds,
		const std::vector<std::string> &keybind_names) {
	sol::table t = lua.create_table();
	sol::table move_t = lua.create_table();
	move_t["x"] = move.x;
	move_t["y"] = move.y;
	move_t["z"] = move.z;
	t["move"] = move_t;
	t["yaw"] = yaw;
	t["pitch"] = pitch;

	sol::table buttons_t = lua.create_table();
	buttons_t["jump"] = (buttons & protocol::kInputJump) != 0;
	buttons_t["sprint"] = (buttons & protocol::kInputSprint) != 0;
	buttons_t["primary"] = (buttons & protocol::kInputPrimary) != 0;
	buttons_t["secondary"] = (buttons & protocol::kInputSecondary) != 0;
	buttons_t["fly_up"] = (buttons & protocol::kInputFlyUp) != 0;
	buttons_t["fly_down"] = (buttons & protocol::kInputFlyDown) != 0;
	t["buttons"] = buttons_t;

	sol::table keybinds_t = lua.create_table();
	for (std::size_t i = 0; i < keybind_names.size(); ++i) {
		keybinds_t[keybind_names[i]] = (keybinds & (1u << i)) != 0;
	}
	t["keybinds"] = keybinds_t;
	return t;
}

// Reconstructs a buttons bitmask from a handler's returned `buttons` table
// (if present); any field the pack omits keeps its bit from `fallback`.
std::uint8_t buttons_from_table(const sol::table &t, std::uint8_t fallback) {
	sol::object bo = t["buttons"];
	if (bo.get_type() != sol::type::table) {
		return fallback;
	}
	sol::table bt = bo.as<sol::table>();
	std::uint8_t out = 0;
	if (bt.get_or("jump", (fallback & protocol::kInputJump) != 0)) {
		out |= protocol::kInputJump;
	}
	if (bt.get_or("sprint", (fallback & protocol::kInputSprint) != 0)) {
		out |= protocol::kInputSprint;
	}
	if (bt.get_or("primary", (fallback & protocol::kInputPrimary) != 0)) {
		out |= protocol::kInputPrimary;
	}
	if (bt.get_or("secondary", (fallback & protocol::kInputSecondary) != 0)) {
		out |= protocol::kInputSecondary;
	}
	if (bt.get_or("fly_up", (fallback & protocol::kInputFlyUp) != 0)) {
		out |= protocol::kInputFlyUp;
	}
	if (bt.get_or("fly_down", (fallback & protocol::kInputFlyDown) != 0)) {
		out |= protocol::kInputFlyDown;
	}
	return out;
}

// Same idea for the `keybinds` sub-table, only ever consulting registered
// names -- an unregistered key can't be represented here any more than on
// the wire.
std::uint32_t keybinds_from_table(const sol::table &t, std::uint32_t fallback,
		const std::vector<std::string> &keybind_names) {
	sol::object ko = t["keybinds"];
	if (ko.get_type() != sol::type::table) {
		return fallback;
	}
	sol::table kt = ko.as<sol::table>();
	std::uint32_t out = 0;
	for (std::size_t i = 0; i < keybind_names.size(); ++i) {
		const bool was_set = (fallback & (1u << i)) != 0;
		if (kt.get_or(keybind_names[i], was_set)) {
			out |= (1u << i);
		}
	}
	return out;
}

} // namespace

struct BlockDef {
	std::string name;
	core::BlockId id = core::BlockId::kAir;
	sol::protected_function on_break;
	sol::protected_function on_place;
};

struct ItemDef {
	std::string name;
	sol::table raw;
};

struct EntityKindDef {
	std::string name;
	core::EntityKindId id = core::EntityKindId::kInvalid;
	sol::protected_function on_spawn;
	sol::protected_function on_tick;
	sol::protected_function on_hit;
	sol::protected_function on_death;
};

// Phase 6.1: one spawned `vb.world.spawn(kind, pos)` instance. `self` is a
// plain Lua table (the spec's `ScriptState`) that on_spawn/on_tick/on_hit/
// on_death all receive as their first argument and that persists across
// calls, so it works as real instance state -- not just a fresh handle
// rebuilt per call the way PlayerHandle is. Its metatable's __index points at
// the shared `entity_methods` table (built once in install_bindings), so
// base-component accessors (get_pos/set_pos/...) are reachable as
// `self:get_pos()` while arbitrary fields (`self.hp = 10`) live directly on
// the table with no collision unless a pack picks a method's exact name --
// the "arbitrary fields for custom data, accessors for engine-owned
// components" split REMAINING_TASKS.md 6.1 leaned toward.
//
// No generic EnTT registry backs this (same posture as ItemDropSystem,
// world/item_drops.hpp): kind/self/position live in this map, replicated
// through ServerSession::spawn_script_entity's interest-grid entry exactly
// like a dropped item is, with no dedicated wire message.
struct ScriptEntity {
	std::size_t kind_index = 0; // index into Impl::entity_kinds
	sol::table self;
	core::Vec3d pos{};
};

struct BiomeDef {
	std::string name;
	sol::table raw;
};

struct CraftDef {
	sol::table raw;
};

struct PackRuntime::Impl {
	net::Transport &transport;
	world::BlockRegistry &registry;
	std::filesystem::path storage_path;
	// Phase 6.4: vb.db, a generic per-key store distinct from the single
	// pack-global `storage` blob below -- see vb/script/db.hpp. Declared
	// after storage_path (construction order == declaration order) so its
	// root can be derived from storage_path's parent directory.
	ScriptDb db;
	Vm vm;
	bool frozen = false;
	net::WorldReplicator *replicator = nullptr;
	net::ServerSession *session = nullptr;

	nlohmann::json storage;
	bool storage_dirty_flag = false;

	std::vector<BlockDef> blocks;
	std::vector<ItemDef> items;
	std::vector<EntityKindDef> entity_kinds;
	std::vector<BiomeDef> biomes;
	std::vector<CraftDef> crafts;
	// Phase 6.3: vb.register_keybind names, order == bit index into every
	// InputCmd::keybinds -- capped at S2CKeybindRegistry::kMaxKeybinds so the
	// bitset always fits one uint32_t.
	std::vector<std::string> keybind_names;
	// Phase 6.7: the raw table passed to vb.physics.set_params{...}, if a
	// pack ever calls it. Read field-by-field (with get_or) in
	// effective_move_params() rather than converted eagerly, so a field the
	// pack didn't set naturally falls back to whatever base MoveParams the
	// caller passes in at that point -- not a fixed literal baked in here.
	std::optional<sol::table> move_params_table;

	// Phase 6.8: the raw table passed to vb.daynight.set_curve{keyframes =
	// {...}}, if a pack ever calls it. Parsed into a world::DayNightCurve in
	// effective_day_night_curve() rather than eagerly, matching
	// move_params_table's posture above.
	std::optional<sol::table> day_night_curve_table;
	// Phase 6.8: the value passed to vb.daynight.set_day_length(seconds), if
	// a pack ever calls it.
	std::optional<double> day_length_seconds_override;

	// Phase 6.1: spawned vb.register_entity instances, keyed by the NetId
	// ServerSession::spawn_script_entity handed back. entity_mt is the shared
	// metatable (__index = entity_methods) applied to every spawned `self`.
	std::unordered_map<core::NetId, ScriptEntity> entities;
	sol::table entity_methods;
	sol::table entity_mt;

	std::unordered_map<std::string, std::vector<sol::protected_function>> handlers;

	struct Timer {
		double remaining;
		double period;
		bool repeating;
		sol::protected_function fn;
		bool cancelled = false;
	};
	std::vector<Timer> timers;

	std::unordered_map<core::NetId, ecs::Inventory> inventories;
	std::unordered_map<core::NetId, std::string> player_names;

	Impl(net::Transport &t, world::BlockRegistry &reg,
			std::filesystem::path path, VmLimits limits);

	sol::state &lua_state() { return vm.native_impl().lua; }

	void install_bindings();
	void dispatch_tick(double dt);
	void flush_storage();
	bool on_block_edit_before(core::NetId editor, core::IVec3 pos,
			core::BlockId existing, core::BlockId new_block, bool is_break);
	void on_block_edit_after(core::NetId editor, core::IVec3 pos,
			core::BlockId removed, core::BlockId placed, bool is_break);
	// Phase 5.1: push a full S2C_Inventory snapshot to `id`'s connection, if
	// one exists. No-op (not an error) if the session/connection isn't ready
	// yet -- same posture as send_message/open_ui below.
	void sync_inventory(core::NetId id);
	// Phase 6.9: the one place that actually adds items to an inventory --
	// fills existing under-cap slots for `item` first (registry's
	// `max_stack`, or the engine default for an id the registry doesn't
	// know), then starts as many new slots as needed for the remainder.
	// Shared by PlayerHandle::give() and the item-pickup handler
	// (attach_session()) so picking something up stacks identically to a
	// script handing it to you directly. Does not call sync_inventory --
	// callers push their own snapshot once, after any other bookkeeping.
	void give_item(core::NetId id, core::BlockId item, std::uint16_t count);
	// Phase 6.6: spawns every slot of `id`'s inventory as a dropped item at
	// `pos` and empties it. Called by run_respawn_handler when a
	// vb.on("player_death", ...) handler's returned table asks for
	// drop_inventory = true.
	void drop_all_items(core::NetId id, core::Vec3d pos);
	// Phase 6.6: calls the first registered vb.on("player_death", ...)
	// handler (if any) and turns its returned table into a
	// ServerSession::RespawnDecision; falls back to a full heal at the join
	// spawn point if no handler is registered or none returns a table.
	net::ServerSession::RespawnDecision run_respawn_handler(
			core::NetId id, std::string_view cause, float health_before);

	// Phase 6.3: runs every vb.on("player_input", handler) in registration
	// order, chaining replacements (each handler sees the prior one's
	// output) and short-circuiting on the first `false` veto. Builds the
	// Lua-facing input table (move/yaw/pitch/buttons/keybinds-by-name) fresh
	// per handler call from the current working values.
	net::ServerSession::InputHookResult run_player_input(
			core::NetId id, const protocol::InputCmd &cmd);

	// Phase 6.5 (spec §10.7): shared block-damage breaking. See
	// net::ServerSession::BlockBreakHooks for the calling contract each of
	// these implements.
	bool run_block_break_begin(core::NetId player, core::IVec3 pos);
	float run_block_break_tick(
			core::NetId player, core::IVec3 pos, std::uint16_t max_damage);
	std::optional<float> run_block_health_tick(core::IVec3 pos, float damage,
			std::uint16_t max_damage, std::uint64_t ticks_since_last_hit);

	// Phase 6.1: vb.world.spawn / self:damage / self:remove dispatch. See
	// ScriptEntity's comment above for the overall design.
	core::NetId self_net_id(const sol::table &self) const;
	void dispatch_entity_tick(double dt);
	void dispatch_entity_hit(core::NetId id, double amount, std::string_view cause);
	void despawn_entity(core::NetId id, std::string_view cause);

	template <typename... Args>
	void fire(const std::string &event, Args &&...args) {
		auto it = handlers.find(event);
		if (it == handlers.end()) {
			return;
		}
		for (auto &fn : it->second) {
			if (!fn.valid()) {
				continue;
			}
			vm.begin_call_budget();
			sol::protected_function_result r = fn(args...);
			if (!r.valid()) {
				const sol::error e = r;
				VB_WARN("script", "vb.on('", event, "') handler error: ", e.what());
			}
		}
	}

	template <typename... Args>
	bool run_veto(const std::string &event, Args &&...args) {
		auto it = handlers.find(event);
		if (it == handlers.end()) {
			return true;
		}
		for (auto &fn : it->second) {
			if (!fn.valid()) {
				continue;
			}
			vm.begin_call_budget();
			sol::protected_function_result r = fn(args...);
			if (!r.valid()) {
				const sol::error e = r;
				VB_WARN("script", "vb.on('", event, "') handler error: ", e.what());
				continue;
			}
			const sol::object ret = r;
			if (ret.valid() && ret.get_type() == sol::type::boolean &&
					!ret.as<bool>()) {
				return false;
			}
		}
		return true;
	}
};

// Lightweight Lua-visible handle for a connected player. Not a persistent
// object -- constructed fresh per dispatch call, so it can never dangle
// (Impl outlives every dispatch call). No generic non-player "entity"
// concept exists yet (Phase 3.1 defers the EnTT registry), so this one type
// covers both the spec's `entity:` and `player:` method surfaces.
struct PlayerHandle {
	core::NetId net_id = core::NetId::kInvalid;
	PackRuntime::Impl *rt = nullptr;

	sol::object get_pos(sol::this_state ts) const {
		sol::state_view lua(ts);
		if (rt->session == nullptr) {
			throw sol::error("entity:get_pos(): session not attached yet");
		}
		const auto st = rt->session->player_move_state(net_id);
		if (!st) {
			throw sol::error("entity:get_pos(): entity is gone");
		}
		sol::table t = lua.create_table();
		t["x"] = st->position.x;
		t["y"] = st->position.y;
		t["z"] = st->position.z;
		return t;
	}

	void set_velocity(double x, double y, double z) const {
		if (rt->session == nullptr) {
			throw sol::error("entity:set_velocity(): session not attached yet");
		}
		rt->session->set_player_velocity(net_id, { x, y, z });
	}

	void remove() const {
		VB_WARN("script", "entity:remove() is a no-op for player-backed "
				"handles -- no generic entity registry exists yet "
				"(Phase 3.1)");
	}

	sol::object get_inventory(sol::this_state ts) const {
		sol::state_view lua(ts);
		const ecs::Inventory &inv = rt->inventories[net_id];
		sol::table t = lua.create_table();
		int i = 1;
		for (const auto &stack : inv.slots) {
			sol::table s = lua.create_table();
			s["item"] = static_cast<std::uint16_t>(stack.item);
			s["count"] = stack.count;
			t[i++] = s;
		}
		return t;
	}

	void send_message(std::string_view text) const {
		if (rt->session == nullptr) {
			return;
		}
		const net::ConnId conn = rt->session->conn_for_player(net_id);
		if (conn == net::ConnId::kInvalid) {
			return;
		}
		net::send_message(rt->transport, conn, protocol::S2CChat{ std::string(text) });
	}

	void open_ui(std::string_view name, sol::optional<sol::table> ctx) const {
		if (rt->session == nullptr) {
			return;
		}
		const net::ConnId conn = rt->session->conn_for_player(net_id);
		if (conn == net::ConnId::kInvalid) {
			return;
		}
		std::string ctx_json = "{}";
		if (ctx) {
			ctx_json = lua_to_json(*ctx).dump();
		}
		net::send_message(rt->transport, conn,
				protocol::S2COpenUi{ std::string(name), std::move(ctx_json) });
	}

	void give(sol::table itemstack) const {
		const auto item = itemstack.get_or("item", static_cast<std::uint16_t>(0));
		const auto count = itemstack.get_or("count", static_cast<std::uint16_t>(0));
		rt->give_item(net_id, static_cast<core::BlockId>(item), count);
		rt->sync_inventory(net_id);
	}

	// The generic counterpart to give() -- removes up to `count` of `item`
	// across however many slots hold it, only if the player has enough in
	// total (all-or-nothing, no partial consumption). Not in the original
	// spec (§10.3 lists only `give`), added because content-side systems
	// like crafting (content/base/crafting.lua) need a way to spend
	// ingredients; this stays a generic inventory primitive, not anything
	// crafting-specific -- the engine has no idea what a "recipe" is.
	bool take(sol::table itemstack) const {
		const auto item =
				static_cast<core::BlockId>(itemstack.get_or("item", static_cast<std::uint16_t>(0)));
		const auto count = itemstack.get_or("count", static_cast<std::uint16_t>(0));
		std::vector<ecs::ItemStack> &slots = rt->inventories[net_id].slots;
		std::uint32_t available = 0;
		for (const auto &s : slots) {
			if (s.item == item) {
				available += s.count;
			}
		}
		if (available < count) {
			return false;
		}
		std::uint16_t remaining = count;
		for (auto it = slots.begin(); it != slots.end() && remaining > 0;) {
			if (it->item != item) {
				++it;
				continue;
			}
			const std::uint16_t taken = std::min(remaining, it->count);
			it->count -= taken;
			remaining -= taken;
			if (it->count == 0) {
				it = slots.erase(it);
			} else {
				++it;
			}
		}
		rt->sync_inventory(net_id);
		return true;
	}

	std::string get_name() const {
		if (rt->session == nullptr) {
			return {};
		}
		return std::string(rt->session->player_name(net_id));
	}

	// Phase 6.6: the one way to reduce a player's health from Lua. `cause` is
	// an opaque string (e.g. "fall", "pvp") threaded through unchanged to a
	// vb.on("player_death", ...) handler once health reaches 0 -- the engine
	// takes no position on what "fall"/"pvp" mean.
	void damage(float amount, sol::optional<std::string> cause) const {
		if (rt->session == nullptr) {
			throw sol::error("entity:damage(): session not attached yet");
		}
		rt->session->damage_player(net_id, amount, cause.value_or(std::string{}));
	}
};

PackRuntime::Impl::Impl(net::Transport &t, world::BlockRegistry &reg,
		std::filesystem::path path, VmLimits limits)
		: transport(t), registry(reg), storage_path(std::move(path)),
		  db(storage_path.parent_path() / "db"), vm(limits) {
	std::ifstream in(storage_path);
	if (in) {
		try {
			in >> storage;
		} catch (const std::exception &e) {
			VB_WARN("script", "vb.storage: '", storage_path.string(),
					"' is malformed JSON (", e.what(), "), starting empty");
			storage = nlohmann::json::object();
		}
	} else {
		storage = nlohmann::json::object();
	}
	install_bindings();
}

void PackRuntime::Impl::install_bindings() {
	sol::state &lua = lua_state();

	lua.new_usertype<PlayerHandle>("Player", "get_pos", &PlayerHandle::get_pos,
			"set_velocity", &PlayerHandle::set_velocity, "remove",
			&PlayerHandle::remove, "get_inventory", &PlayerHandle::get_inventory,
			"send_message", &PlayerHandle::send_message, "open_ui",
			&PlayerHandle::open_ui, "give", &PlayerHandle::give, "take",
			&PlayerHandle::take, "get_name", &PlayerHandle::get_name, "damage",
			&PlayerHandle::damage);

	sol::table vb = lua.create_named_table("vb");

	vb["register_block"] = [this](sol::table def) -> std::uint16_t {
		if (frozen) {
			throw sol::error("vb.register_block: registry already frozen");
		}
		const std::string name = def.get_or("name", std::string{});
		if (name.empty()) {
			throw sol::error("vb.register_block: 'name' is required");
		}
		world::BlockType type;
		type.solid = def.get_or("solid", true);
		type.opaque = def.get_or("opaque", true);
		type.liquid = def.get_or("liquid", false);
		type.light_emission =
				static_cast<std::uint8_t>(def.get_or("light", 0));
		// Phase 6.5 (spec §10.7): 0 (default) = today's instant break.
		type.max_damage = static_cast<std::uint16_t>(def.get_or("max_damage", 0));
		// Phase 6.9 (spec §11.1): stack cap for this item, engine default
		// unless overridden.
		type.max_stack = static_cast<std::uint16_t>(
				def.get_or("max_stack", static_cast<int>(world::kDefaultMaxStackSize)));
		const core::BlockId id = registry.add_or_get(name, type);
		auto it = std::find_if(blocks.begin(), blocks.end(),
				[&](const BlockDef &b) { return b.name == name; });
		if (it == blocks.end()) {
			blocks.push_back({ name, id, {}, {} });
			it = blocks.end() - 1;
		}
		it->id = id;
		it->on_break = def.get_or("on_break", sol::protected_function{});
		it->on_place = def.get_or("on_place", sol::protected_function{});
		return static_cast<std::uint16_t>(id);
	};

	vb["register_item"] = [this](sol::table def) {
		if (frozen) {
			throw sol::error("vb.register_item: registry already frozen");
		}
		items.push_back({ def.get_or("name", std::string{}), def });
	};

	vb["register_entity"] = [this](sol::table def) -> std::uint16_t {
		if (frozen) {
			throw sol::error("vb.register_entity: registry already frozen");
		}
		const std::string name = def.get_or("name", std::string{});
		if (name.empty()) {
			throw sol::error("vb.register_entity: 'name' is required");
		}
		for (const auto &e : entity_kinds) {
			if (e.name == name) {
				return static_cast<std::uint16_t>(e.id);
			}
		}
		EntityKindDef e;
		e.name = name;
		e.id = static_cast<core::EntityKindId>(entity_kinds.size() + 1);
		e.on_spawn = def.get_or("on_spawn", sol::protected_function{});
		e.on_tick = def.get_or("on_tick", sol::protected_function{});
		e.on_hit = def.get_or("on_hit", sol::protected_function{});
		e.on_death = def.get_or("on_death", sol::protected_function{});
		entity_kinds.push_back(std::move(e));
		return static_cast<std::uint16_t>(entity_kinds.back().id);
	};

	vb["register_biome"] = [this](sol::table def) {
		if (frozen) {
			throw sol::error("vb.register_biome: registry already frozen");
		}
		biomes.push_back({ def.get_or("name", std::string{}), def });
	};

	vb["register_craft"] = [this](sol::table def) {
		if (frozen) {
			throw sol::error("vb.register_craft: registry already frozen");
		}
		crafts.push_back({ def });
	};

	// Phase 6.3: closed-schema custom keybind. Idempotent by name (like
	// register_entity), order-assigned index = bit position in every
	// InputCmd::keybinds -- capped so the bitset always fits one uint32_t.
	vb["register_keybind"] = [this](const std::string &name) -> std::uint16_t {
		if (frozen) {
			throw sol::error("vb.register_keybind: registry already frozen");
		}
		if (name.empty()) {
			throw sol::error("vb.register_keybind: 'name' is required");
		}
		for (std::size_t i = 0; i < keybind_names.size(); ++i) {
			if (keybind_names[i] == name) {
				return static_cast<std::uint16_t>(i);
			}
		}
		if (keybind_names.size() >= protocol::S2CKeybindRegistry::kMaxKeybinds) {
			throw sol::error("vb.register_keybind: at most " +
					std::to_string(protocol::S2CKeybindRegistry::kMaxKeybinds) +
					" keybinds may be registered");
		}
		keybind_names.push_back(name);
		return static_cast<std::uint16_t>(keybind_names.size() - 1);
	};

	// Phase 6.7: overrides the engine's physics::MoveParams tunables
	// (gravity, walk/sprint speed, jump, step height, fly speed, ...).
	// Global, not per-entity-kind -- no entity kind besides the player
	// runs step_movement today, so a per-kind table would have nowhere to
	// apply beyond the one kind that exists. Only whichever fields the
	// table actually sets are used (see effective_move_params()); calling
	// it more than once replaces the whole table, it doesn't merge with an
	// earlier call.
	sol::table physics_tbl = lua.create_table();
	vb["physics"] = physics_tbl;
	physics_tbl["set_params"] = [this](sol::table def) {
		if (frozen) {
			throw sol::error("vb.physics.set_params: registry already frozen");
		}
		move_params_table = def;
	};

	// Phase 6.8: overrides the engine's default 4-keyframe day/night sky
	// gradient (vb::world::default_day_night_curve()). `keyframes` is a plain
	// array of {tick, brightness, color = {r, g, b}} tables, parsed lazily in
	// effective_day_night_curve() -- calling this more than once replaces the
	// whole curve, it doesn't merge with an earlier call (same posture as
	// vb.physics.set_params above).
	sol::table daynight_tbl = lua.create_table();
	vb["daynight"] = daynight_tbl;
	daynight_tbl["set_curve"] = [this](sol::table def) {
		if (frozen) {
			throw sol::error("vb.daynight.set_curve: registry already frozen");
		}
		sol::optional<sol::table> keyframes = def["keyframes"];
		if (!keyframes || keyframes->size() == 0) {
			throw sol::error(
					"vb.daynight.set_curve: 'keyframes' must be a non-empty array");
		}
		day_night_curve_table = def;
	};
	// Phase 6.8: overrides the real seconds one in-game day/night cycle takes
	// (ServerConfig::day_length_seconds is the base this stacks on top of).
	daynight_tbl["set_day_length"] = [this](double seconds) {
		if (frozen) {
			throw sol::error("vb.daynight.set_day_length: registry already frozen");
		}
		if (!(seconds > 0.0)) {
			throw sol::error("vb.daynight.set_day_length: 'seconds' must be > 0");
		}
		day_length_seconds_override = seconds;
	};

	sol::table world_tbl = lua.create_table();
	vb["world"] = world_tbl;

	world_tbl["get_block"] = [this](int x, int y, int z) -> std::uint16_t {
		if (replicator == nullptr) {
			throw sol::error("vb.world.get_block: world not attached yet");
		}
		return static_cast<std::uint16_t>(
				replicator->world().get_block({ x, y, z }));
	};

	// Known limitation: doesn't run apply_block_edit's relight cascade, so a
	// scripted edit can desync lighting until something else touches the
	// chunk. No consumer exists this phase.
	world_tbl["set_block"] = [this](int x, int y, int z, std::uint16_t id) {
		if (replicator == nullptr) {
			throw sol::error("vb.world.set_block: world not attached yet");
		}
		const auto bid = static_cast<core::BlockId>(id);
		if (!registry.contains(bid)) {
			throw sol::error("vb.world.set_block: unknown block id");
		}
		replicator->world().set_block({ x, y, z }, bid);
	};

	world_tbl["raycast"] = [this](sol::table origin, sol::table dir,
									   double max_dist,
									   sol::this_state ts) -> sol::object {
		sol::state_view sv(ts);
		if (replicator == nullptr) {
			throw sol::error("vb.world.raycast: world not attached yet");
		}
		const core::Vec3d o{ origin.get_or("x", 0.0), origin.get_or("y", 0.0),
			origin.get_or("z", 0.0) };
		const core::Vec3d d{ dir.get_or("x", 0.0), dir.get_or("y", 0.0),
			dir.get_or("z", 0.0) };
		const world::VoxelRayHit hit =
				world::raycast_voxel(replicator->world(), o, d, max_dist);
		if (!hit.hit) {
			return sol::make_object(sv, sol::lua_nil);
		}
		sol::table t = sv.create_table();
		t["hit"] = true;
		t["x"] = hit.voxel.x;
		t["y"] = hit.voxel.y;
		t["z"] = hit.voxel.z;
		t["nx"] = hit.normal.x;
		t["ny"] = hit.normal.y;
		t["nz"] = hit.normal.z;
		return t;
	};

	// Phase 5.1 dropped-item entity: a real, working world spawn ahead of the
	// generic EnTT-backed `vb.world.spawn` above (still a no-op -- waits on
	// Phase 3.1). Deliberately its own binding, not routed through `spawn`,
	// since it isn't a `vb.register_entity` kind at all -- just a hardcoded
	// ItemDropSystem entry (vb::world::ItemDropSystem, src/net/session.cpp).
	world_tbl["spawn_item_drop"] = [this](sol::table pos, std::uint16_t item,
											   std::uint16_t count) {
		if (session == nullptr) {
			throw sol::error("vb.world.spawn_item_drop: session not attached yet");
		}
		const core::Vec3d p{ pos.get_or("x", 0.0), pos.get_or("y", 0.0),
			pos.get_or("z", 0.0) };
		session->spawn_item_drop(p, static_cast<core::BlockId>(item), count);
	};

	// Phase 6.1: shared instance-method table for every spawned `self`, set
	// as the metatable of each so `self:get_pos()`/`self:damage(...)`/... work
	// while arbitrary fields (`self.hp = 10`) stay free on the table itself.
	entity_methods = lua.create_table();
	entity_methods["get_pos"] = [this](sol::table self, sol::this_state ts) -> sol::object {
		sol::state_view sv(ts);
		auto it = entities.find(self_net_id(self));
		if (it == entities.end()) {
			throw sol::error("entity:get_pos(): entity is gone");
		}
		sol::table t = sv.create_table();
		t["x"] = it->second.pos.x;
		t["y"] = it->second.pos.y;
		t["z"] = it->second.pos.z;
		return t;
	};
	entity_methods["set_pos"] = [this](sol::table self, double x, double y, double z) {
		const core::NetId id = self_net_id(self);
		auto it = entities.find(id);
		if (it == entities.end()) {
			throw sol::error("entity:set_pos(): entity is gone");
		}
		it->second.pos = { x, y, z };
		if (session != nullptr) {
			session->set_script_entity_state(id, it->second.pos);
		}
	};
	entity_methods["get_kind"] = [this](sol::table self) -> std::string {
		auto it = entities.find(self_net_id(self));
		if (it == entities.end()) {
			return {};
		}
		return entity_kinds[it->second.kind_index].name;
	};
	// Notification-only: the engine tracks no health for generic entities
	// (same "engine takes no position" posture as 6.5's block-damage design)
	// -- this just fires the kind's on_hit so a pack can implement whatever
	// health/aggro/knockback logic it wants.
	entity_methods["damage"] = [this](sol::table self, double amount,
											sol::optional<std::string> cause) {
		dispatch_entity_hit(self_net_id(self), amount, cause.value_or(std::string{}));
	};
	entity_methods["remove"] = [this](sol::table self, sol::optional<std::string> cause) {
		despawn_entity(self_net_id(self), cause.value_or(std::string{}));
	};
	entity_mt = lua.create_table();
	entity_mt["__index"] = entity_methods;

	world_tbl["spawn"] = [this](const std::string &kind, sol::table pos,
										sol::this_state ts) -> sol::object {
		sol::state_view sv(ts);
		auto kind_it = std::find_if(entity_kinds.begin(), entity_kinds.end(),
				[&](const EntityKindDef &e) { return e.name == kind; });
		if (kind_it == entity_kinds.end()) {
			throw sol::error("vb.world.spawn: unknown entity kind '" + kind + "'");
		}
		if (session == nullptr) {
			throw sol::error("vb.world.spawn: session not attached yet");
		}
		const core::Vec3d p{ pos.get_or("x", 0.0), pos.get_or("y", 0.0),
			pos.get_or("z", 0.0) };
		const core::NetId id = session->spawn_script_entity(kind_it->id, p);
		sol::table self = sv.create_table();
		self[sol::metatable_key] = entity_mt;
		self["__net_id"] = static_cast<double>(static_cast<std::uint32_t>(id));
		const std::size_t kind_index =
				static_cast<std::size_t>(kind_it - entity_kinds.begin());
		entities[id] = ScriptEntity{ kind_index, self, p };
		if (kind_it->on_spawn.valid()) {
			vm.begin_call_budget();
			sol::protected_function_result r = kind_it->on_spawn(self);
			if (!r.valid()) {
				const sol::error e = r;
				VB_WARN("script", "entity on_spawn handler error: ", e.what());
			}
		}
		return self;
	};

	static const std::set<std::string> kValidEvents = { "player_join",
		"player_leave", "block_break", "block_place", "player_interact",
		"chat", "tick", "ui_event", "player_death", "player_input",
		"block_break_begin", "block_break_tick", "block_health_tick" };
	vb["on"] = [this](const std::string &event, sol::protected_function fn) {
		if (kValidEvents.find(event) == kValidEvents.end()) {
			throw sol::error("vb.on: unknown event '" + event + "'");
		}
		handlers[event].push_back(std::move(fn));
	};

	vb["after"] = [this](double seconds, sol::protected_function fn) {
		timers.push_back({ seconds, seconds, false, std::move(fn), false });
	};
	vb["every"] = [this](double seconds, sol::protected_function fn) {
		timers.push_back({ seconds, seconds, true, std::move(fn), false });
	};

	sol::table storage_proxy = lua.create_table();
	sol::table storage_meta = lua.create_table();
	storage_meta[sol::meta_function::index] =
			[this](sol::table, const std::string &key,
					sol::this_state ts) -> sol::object {
		sol::state_view sv(ts);
		if (!storage.contains(key)) {
			return sol::make_object(sv, sol::lua_nil);
		}
		return json_to_lua(sv, storage.at(key));
	};
	storage_meta[sol::meta_function::new_index] =
			[this](sol::table, const std::string &key, sol::object value) {
		storage[key] = lua_to_json(value);
		storage_dirty_flag = true;
	};
	storage_proxy[sol::metatable_key] = storage_meta;
	vb["storage"] = storage_proxy;

	// Phase 6.4: vb.db -- a generic per-key store, distinct from the
	// pack-global vb.storage above. `key` is whatever the script chooses
	// ("user:" .. name, "session:" .. token, ...); values round-trip through
	// the same json_to_lua/lua_to_json used by vb.storage so tables/numbers/
	// strings/booleans all persist correctly, not just strings.
	sol::table db_tbl = lua.create_table();
	db_tbl["get"] = [this](const std::string &key,
							sol::this_state ts) -> sol::object {
		sol::state_view sv(ts);
		const std::optional<std::string> raw = db.get(key);
		if (!raw) {
			return sol::make_object(sv, sol::lua_nil);
		}
		const nlohmann::json parsed =
				nlohmann::json::parse(*raw, nullptr, false);
		if (parsed.is_discarded()) {
			return sol::make_object(sv, sol::lua_nil);
		}
		return json_to_lua(sv, parsed);
	};
	db_tbl["set"] = [this](const std::string &key, sol::object value) {
		db.set(key, lua_to_json(value).dump());
	};
	db_tbl["delete"] = [this](const std::string &key) { db.erase(key); };
	vb["db"] = db_tbl;

	// Phase 6.4: vb.crypto.hash -- a minimal primitive so a pack implementing
	// its own login (built on vb.db above) doesn't have to roll credential
	// hashing in pure Lua; the sandbox strips os/io deliberately (§10.2). The
	// engine itself still takes no position on auth as a concept.
	sol::table crypto_tbl = lua.create_table();
	crypto_tbl["hash"] = [](const std::string &data) -> std::string {
		return core::sha256_hex(data);
	};
	vb["crypto"] = crypto_tbl;
}

core::NetId PackRuntime::Impl::self_net_id(const sol::table &self) const {
	return static_cast<core::NetId>(
			static_cast<std::uint32_t>(self.get_or("__net_id", 0.0)));
}

// Phase 6.1's ScriptPreTickSystem-equivalent: fires on_tick(self, dt) for
// every currently-spawned entity, once per PackRuntime::dispatch_tick. Ids are
// snapshotted first since a handler may spawn/remove entities (including
// itself) mid-iteration.
void PackRuntime::Impl::dispatch_entity_tick(double dt) {
	std::vector<core::NetId> ids;
	ids.reserve(entities.size());
	for (const auto &[id, e] : entities) {
		(void)e;
		ids.push_back(id);
	}
	for (const core::NetId id : ids) {
		const auto it = entities.find(id);
		if (it == entities.end()) {
			continue; // removed by an earlier handler this tick
		}
		const EntityKindDef &kind = entity_kinds[it->second.kind_index];
		if (!kind.on_tick.valid()) {
			continue;
		}
		vm.begin_call_budget();
		sol::protected_function_result r = kind.on_tick(it->second.self, dt);
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "entity on_tick handler error: ", e.what());
		}
	}
}

void PackRuntime::Impl::dispatch_entity_hit(
		core::NetId id, double amount, std::string_view cause) {
	const auto it = entities.find(id);
	if (it == entities.end()) {
		return;
	}
	const EntityKindDef &kind = entity_kinds[it->second.kind_index];
	if (!kind.on_hit.valid()) {
		return;
	}
	vm.begin_call_budget();
	sol::protected_function_result r =
			kind.on_hit(it->second.self, amount, std::string(cause));
	if (!r.valid()) {
		const sol::error e = r;
		VB_WARN("script", "entity on_hit handler error: ", e.what());
	}
}

// Phase 6.1's ScriptPostTickSystem-equivalent (the "entity despawn commit"
// half): fires on_death(self, cause), then removes the entity from both this
// map and the interest grid, so it stops replicating.
void PackRuntime::Impl::despawn_entity(core::NetId id, std::string_view cause) {
	const auto it = entities.find(id);
	if (it == entities.end()) {
		return;
	}
	const EntityKindDef &kind = entity_kinds[it->second.kind_index];
	if (kind.on_death.valid()) {
		vm.begin_call_budget();
		sol::protected_function_result r =
				kind.on_death(it->second.self, std::string(cause));
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "entity on_death handler error: ", e.what());
		}
	}
	if (session != nullptr) {
		session->remove_script_entity(id);
	}
	entities.erase(it);
}

void PackRuntime::Impl::dispatch_tick(double dt) {
	fire("tick", dt);
	dispatch_entity_tick(dt);

	for (auto &t : timers) {
		if (t.cancelled) {
			continue;
		}
		t.remaining -= dt;
		int fires = 0;
		while (t.remaining <= 0.0) {
			vm.begin_call_budget();
			sol::protected_function_result r = t.fn();
			if (!r.valid()) {
				const sol::error e = r;
				VB_WARN("script", "timer handler error: ", e.what());
			}
			++fires;
			if (!t.repeating) {
				t.cancelled = true;
				break;
			}
			t.remaining += t.period;
			if (fires >= kMaxTimerCatchUpFires) {
				break;
			}
		}
	}
	timers.erase(std::remove_if(timers.begin(), timers.end(),
						 [](const Timer &t) { return t.cancelled; }),
			timers.end());

	if (storage_dirty_flag) {
		flush_storage();
	}
}

void PackRuntime::Impl::flush_storage() {
	std::ofstream out(storage_path, std::ios::trunc);
	if (!out) {
		VB_WARN("script", "vb.storage: failed to open '", storage_path.string(),
				"' for writing");
		return;
	}
	out << storage.dump();
	storage_dirty_flag = false;
}

bool PackRuntime::Impl::on_block_edit_before(core::NetId editor,
		core::IVec3 pos, core::BlockId existing, core::BlockId new_block,
		bool is_break) {
	(void)existing;
	(void)new_block;
	sol::table pos_tbl = lua_state().create_table();
	pos_tbl["x"] = pos.x;
	pos_tbl["y"] = pos.y;
	pos_tbl["z"] = pos.z;
	PlayerHandle p{ editor, this };
	return run_veto(is_break ? "block_break" : "block_place", p, pos_tbl);
}

void PackRuntime::Impl::on_block_edit_after(core::NetId editor,
		core::IVec3 pos, core::BlockId removed, core::BlockId placed,
		bool is_break) {
	const core::BlockId affected = is_break ? removed : placed;
	for (auto &bd : blocks) {
		if (bd.id != affected) {
			continue;
		}
		sol::protected_function &cb = is_break ? bd.on_break : bd.on_place;
		if (!cb.valid()) {
			break;
		}
		sol::table pos_tbl = lua_state().create_table();
		pos_tbl["x"] = pos.x;
		pos_tbl["y"] = pos.y;
		pos_tbl["z"] = pos.z;
		sol::table ctx = lua_state().create_table();
		ctx["pos"] = pos_tbl;
		ctx["player"] = PlayerHandle{ editor, this };
		vm.begin_call_budget();
		sol::protected_function_result r = cb(ctx);
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "block on_break/on_place handler error: ", e.what());
		} else {
			const sol::object ret = r;
			if (ret.valid() && ret.get_type() != sol::type::lua_nil) {
				VB_DEBUG("script", "block callback returned a value (drop) "
						"-- not materialized yet, Phase 5.1 items");
			}
		}
		break;
	}
}

void PackRuntime::Impl::sync_inventory(core::NetId id) {
	if (session == nullptr) {
		return;
	}
	const net::ConnId conn = session->conn_for_player(id);
	if (conn == net::ConnId::kInvalid) {
		return;
	}
	protocol::S2CInventory msg;
	for (const auto &stack : inventories[id].slots) {
		msg.slots.push_back({ stack.item, stack.count });
	}
	net::send_message(transport, conn, msg);
}

void PackRuntime::Impl::give_item(
		core::NetId id, core::BlockId item, std::uint16_t count) {
	if (count == 0) {
		return;
	}
	const std::uint16_t max_stack = registry.contains(item)
			? registry.get(item).max_stack
			: world::kDefaultMaxStackSize;
	std::vector<ecs::ItemStack> &slots = inventories[id].slots;
	std::uint32_t remaining = count;
	for (auto &s : slots) {
		if (remaining == 0) {
			break;
		}
		if (s.item != item || s.count >= max_stack) {
			continue;
		}
		const auto added = static_cast<std::uint16_t>(
				std::min<std::uint32_t>(max_stack - s.count, remaining));
		s.count = static_cast<std::uint16_t>(s.count + added);
		remaining -= added;
	}
	while (remaining > 0) {
		const auto added = static_cast<std::uint16_t>(
				std::min<std::uint32_t>(max_stack, remaining));
		slots.push_back({ item, added });
		remaining -= added;
	}
}

void PackRuntime::Impl::drop_all_items(core::NetId id, core::Vec3d pos) {
	if (session == nullptr) {
		return;
	}
	for (const auto &stack : inventories[id].slots) {
		session->spawn_item_drop(pos, stack.item, stack.count);
	}
	inventories[id].slots.clear();
	sync_inventory(id);
}

net::ServerSession::RespawnDecision PackRuntime::Impl::run_respawn_handler(
		core::NetId id, std::string_view cause, float health_before) {
	net::ServerSession::RespawnDecision decision{ 20.0f, session->spawn_point(id),
		"* you died and respawned" };

	auto it = handlers.find("player_death");
	if (it == handlers.end()) {
		return decision;
	}
	// Captured before the caller (check_respawns) overwrites the player's
	// position with the respawn point -- this is where they actually died,
	// used as the drop location so a dropped inventory doesn't just land at
	// the respawn point and get instantly re-picked-up there (pickup_radius
	// covers it).
	const core::Vec3d death_pos =
			session->player_move_state(id).value_or(physics::MoveState{}).position;
	PlayerHandle p{ id, this };
	for (auto &fn : it->second) {
		if (!fn.valid()) {
			continue;
		}
		vm.begin_call_budget();
		sol::protected_function_result r = fn(p, std::string(cause), health_before);
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "vb.on('player_death') handler error: ", e.what());
			continue;
		}
		const sol::object ret = r;
		if (!ret.valid() || ret.get_type() != sol::type::table) {
			continue;
		}
		sol::table t = ret.as<sol::table>();
		decision.heal_to = t.get_or("heal", decision.heal_to);
		if (sol::optional<sol::table> pos_tbl = t.get<sol::optional<sol::table>>("pos")) {
			decision.pos = { pos_tbl->get_or("x", decision.pos.x),
				pos_tbl->get_or("y", decision.pos.y),
				pos_tbl->get_or("z", decision.pos.z) };
		}
		decision.message = t.get_or("message", decision.message);
		if (t.get_or("drop_inventory", false)) {
			drop_all_items(id, death_pos);
		}
		break; // first handler that returns a decision table wins
	}
	return decision;
}

net::ServerSession::InputHookResult PackRuntime::Impl::run_player_input(
		core::NetId id, const protocol::InputCmd &cmd) {
	net::ServerSession::InputHookResult result;
	auto it = handlers.find("player_input");
	if (it == handlers.end()) {
		return result; // no handlers registered: pass through unchanged
	}

	core::Vec3f move = cmd.move;
	float yaw = cmd.yaw;
	float pitch = cmd.pitch;
	std::uint8_t buttons = cmd.buttons;
	std::uint32_t keybinds = cmd.keybinds;
	bool changed = false;

	PlayerHandle p{ id, this };
	for (auto &fn : it->second) {
		if (!fn.valid()) {
			continue;
		}
		sol::table input_t = build_input_table(
				lua_state(), move, yaw, pitch, buttons, keybinds, keybind_names);
		vm.begin_call_budget();
		sol::protected_function_result r = fn(p, input_t);
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "vb.on('player_input') handler error: ", e.what());
			continue;
		}
		const sol::object ret = r;
		if (ret.valid() && ret.get_type() == sol::type::boolean && !ret.as<bool>()) {
			result.veto = true;
			return result; // first veto wins, same as run_veto
		}
		if (!ret.valid() || ret.get_type() != sol::type::table) {
			continue; // true/nil/other: pass through unchanged
		}
		sol::table t = ret.as<sol::table>();
		if (sol::optional<sol::table> move_tbl =
						t.get<sol::optional<sol::table>>("move")) {
			move.x = move_tbl->get_or("x", move.x);
			move.y = move_tbl->get_or("y", move.y);
			move.z = move_tbl->get_or("z", move.z);
			changed = true;
		}
		if (sol::optional<float> y = t.get<sol::optional<float>>("yaw")) {
			yaw = *y;
			changed = true;
		}
		if (sol::optional<float> pi = t.get<sol::optional<float>>("pitch")) {
			pitch = *pi;
			changed = true;
		}
		const std::uint8_t new_buttons = buttons_from_table(t, buttons);
		changed = changed || new_buttons != buttons;
		buttons = new_buttons;
		const std::uint32_t new_keybinds =
				keybinds_from_table(t, keybinds, keybind_names);
		changed = changed || new_keybinds != keybinds;
		keybinds = new_keybinds;
	}

	if (changed) {
		result.replacement =
				net::ServerSession::PlayerInputOverride{ move, yaw, pitch, buttons,
					keybinds };
	}
	return result;
}

namespace {
sol::table make_pos_table(sol::state &lua, core::IVec3 pos) {
	sol::table t = lua.create_table();
	t["x"] = pos.x;
	t["y"] = pos.y;
	t["z"] = pos.z;
	return t;
}
} // namespace

bool PackRuntime::Impl::run_block_break_begin(core::NetId player, core::IVec3 pos) {
	PlayerHandle p{ player, this };
	return run_veto("block_break_begin", p, make_pos_table(lua_state(), pos));
}

// Phase 6.5: sums every registered handler's returned delta, matching §10.7's
// "the engine sums all concurrent contributors' deltas" -- concurrency there
// is across *players* (one call each, from ServerSession), while multiple
// handlers for the *same* call is an orthogonal, less-expected case; summing
// both the same way keeps this simple and never silently drops a delta.
float PackRuntime::Impl::run_block_break_tick(
		core::NetId player, core::IVec3 pos, std::uint16_t max_damage) {
	const auto it = handlers.find("block_break_tick");
	if (it == handlers.end()) {
		return 0.0f; // no policy registered -- damage never accrues (§10.7)
	}
	PlayerHandle p{ player, this };
	sol::table pos_tbl = make_pos_table(lua_state(), pos);
	float total = 0.0f;
	for (auto &fn : it->second) {
		if (!fn.valid()) {
			continue;
		}
		vm.begin_call_budget();
		sol::protected_function_result r = fn(p, pos_tbl, max_damage);
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "vb.on('block_break_tick') handler error: ", e.what());
			continue;
		}
		const sol::object ret = r;
		if (ret.valid() && ret.get_type() == sol::type::number) {
			total += ret.as<float>();
		}
	}
	return total;
}

// Phase 6.5: the last handler to return a number wins (chained, like
// run_player_input's replacement pipeline) -- nullopt (no handler, or every
// handler returned nothing) means "unchanged", i.e. permanent damage, no
// healing at all, matching §10.7's "no handler registered" default exactly.
std::optional<float> PackRuntime::Impl::run_block_health_tick(core::IVec3 pos,
		float damage, std::uint16_t max_damage, std::uint64_t ticks_since_last_hit) {
	const auto it = handlers.find("block_health_tick");
	if (it == handlers.end()) {
		return std::nullopt;
	}
	sol::table pos_tbl = make_pos_table(lua_state(), pos);
	std::optional<float> replacement;
	for (auto &fn : it->second) {
		if (!fn.valid()) {
			continue;
		}
		vm.begin_call_budget();
		sol::protected_function_result r = fn(pos_tbl, damage, max_damage,
				static_cast<double>(ticks_since_last_hit));
		if (!r.valid()) {
			const sol::error e = r;
			VB_WARN("script", "vb.on('block_health_tick') handler error: ", e.what());
			continue;
		}
		const sol::object ret = r;
		if (ret.valid() && ret.get_type() == sol::type::number) {
			damage = ret.as<float>();
			replacement = damage;
		}
	}
	return replacement;
}

PackRuntime::PackRuntime(net::Transport &transport,
		world::BlockRegistry &registry, std::filesystem::path storage_path,
		VmLimits limits)
		: impl_(std::make_unique<Impl>(transport, registry,
				  std::move(storage_path), limits)) {}
PackRuntime::~PackRuntime() = default;
PackRuntime::PackRuntime(PackRuntime &&) noexcept = default;
PackRuntime &PackRuntime::operator=(PackRuntime &&) noexcept = default;

ScriptResult PackRuntime::load_pack_file(std::string_view code,
		std::string_view chunk_name) {
	return impl_->vm.do_string(code, chunk_name);
}

void PackRuntime::freeze() {
	impl_->frozen = true;
	if (impl_->registry.size() > world::BlockRegistry::base().size()) {
		VB_INFO("script", "pack registered ", impl_->registry.size(),
				" blocks beyond the base set -- reaches clients only if the "
				"host wires HandshakeServerHost::block_registry from this "
				"registry (Phase 4.3; src/server/main.cpp does)");
	}
}

void PackRuntime::install_join_veto(net::HandshakeServerHost &host) {
	Impl *self = impl_.get();
	auto user_auth = host.authenticate;
	host.authenticate = [self, user_auth](std::string_view name,
										  std::string_view token) -> net::AuthOutcome {
		net::AuthOutcome outcome = user_auth(name, token);
		if (!outcome.ok) {
			return outcome;
		}
		if (!self->run_veto("player_join", std::string(name))) {
			return { false, "denied by pack" };
		}
		return outcome;
	};
}

void PackRuntime::install_keybind_registry(net::HandshakeServerHost &host) {
	Impl *self = impl_.get();
	host.keybind_registry =
			[self]() -> std::optional<std::vector<std::string>> {
		if (self->keybind_names.empty()) {
			return std::nullopt;
		}
		return self->keybind_names;
	};
}

void PackRuntime::attach_world(net::WorldReplicator &replicator) {
	impl_->replicator = &replicator;
	Impl *self = impl_.get();
	net::BlockEditHooks hooks;
	hooks.before_edit = [self](core::NetId editor, core::IVec3 pos,
									core::BlockId existing,
									core::BlockId new_block, bool is_break) {
		return self->on_block_edit_before(editor, pos, existing, new_block,
				is_break);
	};
	hooks.after_edit = [self](core::NetId editor, core::IVec3 pos,
									core::BlockId removed, core::BlockId placed,
									bool is_break) {
		self->on_block_edit_after(editor, pos, removed, placed, is_break);
	};
	replicator.set_block_edit_hooks(std::move(hooks));
}

physics::MoveParams PackRuntime::effective_move_params(physics::MoveParams base) const {
	if (!impl_->move_params_table) {
		return base;
	}
	const sol::table &def = *impl_->move_params_table;
	physics::MoveParams out = base;
	out.half_width = def.get_or("half_width", out.half_width);
	out.height = def.get_or("height", out.height);
	out.eye_height = def.get_or("eye_height", out.eye_height);
	out.walk_speed = def.get_or("walk_speed", out.walk_speed);
	out.sprint_speed = def.get_or("sprint_speed", out.sprint_speed);
	out.accel = def.get_or("accel", out.accel);
	out.air_accel = def.get_or("air_accel", out.air_accel);
	out.friction = def.get_or("friction", out.friction);
	out.gravity = def.get_or("gravity", out.gravity);
	out.jump_speed = def.get_or("jump_speed", out.jump_speed);
	out.terminal_velocity = def.get_or("terminal_velocity", out.terminal_velocity);
	out.step_height = def.get_or("step_height", out.step_height);
	out.fly_speed = def.get_or("fly_speed", out.fly_speed);
	out.fly = def.get_or("fly", out.fly);
	return out;
}

std::optional<world::DayNightCurve> PackRuntime::effective_day_night_curve() const {
	if (!impl_->day_night_curve_table) {
		return std::nullopt;
	}
	const sol::table &def = *impl_->day_night_curve_table;
	const sol::table keyframes = def["keyframes"];
	world::DayNightCurve curve;
	curve.keyframes.reserve(keyframes.size());
	for (std::size_t i = 1; i <= keyframes.size(); ++i) {
		const sol::table kf = keyframes[i];
		world::DayNightKeyframe out;
		out.tick = kf.get_or("tick", 0u);
		out.brightness = kf.get_or("brightness", 1.0);
		const sol::optional<sol::table> color = kf["color"];
		if (color) {
			out.color.r = color->get_or(1, std::uint8_t{ 0 });
			out.color.g = color->get_or(2, std::uint8_t{ 0 });
			out.color.b = color->get_or(3, std::uint8_t{ 0 });
		}
		curve.keyframes.push_back(out);
	}
	return curve;
}

double PackRuntime::effective_day_length_seconds(double base) const {
	return impl_->day_length_seconds_override.value_or(base);
}

void PackRuntime::attach_session(net::ServerSession &session) {
	impl_->session = &session;
	session.set_ui_event_handler(
			[this](core::NetId player, const protocol::C2SUiEvent &e) {
		dispatch_ui_event(player, e);
	});
	session.set_chat_handler([this](core::NetId sender, std::string_view text) {
		return dispatch_chat(sender, text);
	});
	// Phase 5.1: a player walking over a dropped item (ItemDropSystem,
	// ServerSession::update_item_drops) credits their inventory exactly like
	// player:give() does, including the same live S2C_Inventory push --
	// picking something up should look identical to a script handing it to
	// you directly.
	Impl *self = impl_.get();
	session.set_item_pickup_handler(
			[self](core::NetId player, core::BlockId item, std::uint16_t count) {
		self->give_item(player, item, count);
		self->sync_inventory(player);
	});
	// Phase 6.6: only installed when a pack actually registered
	// vb.on("player_death", ...) (all pack loading finished before
	// attach_session() runs, so `handlers` is already final) -- otherwise
	// ServerSession keeps its own built-in fallback, matching every
	// pre-6.6 caller/test exactly.
	if (self->handlers.count("player_death") != 0) {
		session.set_respawn_handler([self](core::NetId id, std::string_view cause,
											   float health_before) {
			return self->run_respawn_handler(id, cause, health_before);
		});
	}
	// Phase 6.3: only installed when a pack actually registered
	// vb.on("player_input", ...) -- packs that never use this channel pay
	// zero extra cost in the hot per-tick handle_input_batch loop.
	if (self->handlers.count("player_input") != 0) {
		session.set_input_handler(
				[self](core::NetId id, const protocol::InputCmd &cmd) {
			return self->run_player_input(id, cmd);
		});
	}
	// Phase 6.5: only installed when a pack registered at least one of the
	// three block-damage events -- a pack that never opts in pays zero extra
	// cost in ServerSession's per-tick damage-map walk (it stays empty since
	// `begin` is never installed to admit a contributor in the first place).
	if (self->handlers.count("block_break_begin") != 0 ||
			self->handlers.count("block_break_tick") != 0 ||
			self->handlers.count("block_health_tick") != 0) {
		net::ServerSession::BlockBreakHooks hooks;
		hooks.begin = [self](core::NetId player, core::IVec3 pos, core::BlockId) {
			return self->run_block_break_begin(player, pos);
		};
		hooks.tick_damage = [self](core::NetId player, core::IVec3 pos,
										  core::BlockId, std::uint16_t max_damage) {
			return self->run_block_break_tick(player, pos, max_damage);
		};
		hooks.health_tick = [self](core::IVec3 pos, core::BlockId, float damage,
										  std::uint16_t max_damage,
										  std::uint64_t idle) {
			return self->run_block_health_tick(pos, damage, max_damage, idle);
		};
		session.set_block_break_hooks(std::move(hooks));
	}
}

void PackRuntime::dispatch_player_join_completed(
		const net::SessionPlayerJoined &j) {
	impl_->player_names[j.net_id] = j.name;
}

void PackRuntime::dispatch_player_leave(const net::SessionPlayerLeft &l) {
	PlayerHandle p{ l.net_id, impl_.get() };
	impl_->fire("player_leave", p);
	impl_->player_names.erase(l.net_id);
	impl_->inventories.erase(l.net_id);
}

void PackRuntime::dispatch_tick(double dt_seconds) {
	impl_->dispatch_tick(dt_seconds);
}

bool PackRuntime::dispatch_chat(core::NetId sender, std::string_view text) {
	PlayerHandle p{ sender, impl_.get() };
	return impl_->run_veto("chat", p, std::string(text));
}

void PackRuntime::dispatch_ui_event(core::NetId player,
		const protocol::C2SUiEvent &event) {
	PlayerHandle p{ player, impl_.get() };
	nlohmann::json parsed;
	try {
		parsed = nlohmann::json::parse(event.value_json);
	} catch (const nlohmann::json::parse_error &) {
		parsed = nullptr;
	}
	sol::object value = json_to_lua(impl_->lua_state(), parsed);
	impl_->fire("ui_event", p, event.ui_name, event.widget_id, event.event_kind,
			value);
}

bool PackRuntime::dispatch_player_interact(core::NetId player,
		core::IVec3 target) {
	PlayerHandle p{ player, impl_.get() };
	sol::table t = impl_->lua_state().create_table();
	t["x"] = target.x;
	t["y"] = target.y;
	t["z"] = target.z;
	return impl_->run_veto("player_interact", p, t);
}

bool PackRuntime::storage_dirty() const { return impl_->storage_dirty_flag; }

void PackRuntime::flush_storage() { impl_->flush_storage(); }

} // namespace vb::script

#endif // VB_WITH_LUA
