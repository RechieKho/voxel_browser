#pragma once

// Phase C0 (docs/content-base-testing.md §2.3): the one setup every Layer 1
// `content/base` test needs -- a real PackRuntime + ServerSession over
// LoopbackNetwork, with content/base/ loaded through the same
// vb::script::load_content_pack path src/server/main.cpp uses, plus a single
// joined client. Extracted from content_pack_test.cpp's own (repeated)
// inline setup rather than rewriting that file's existing cases onto it (see
// docs/content-base-testing.md §2.3's "deliberately not rewritten" note).
//
// World/WorldReplicator are optional and only constructed when a test asks
// for them (`BasePackFixture::with_world()`) -- most behaviour (crafting,
// fall damage, keybinds, storage) needs no world at all, same as
// content_pack_test.cpp's crafting/zombie cases. Tests that DO need one
// (block break/place, fall damage) call with_world() right after
// construction, before any pump().

#if VB_WITH_LUA

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/protocol/input.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

namespace vb::test {

inline std::filesystem::path content_base_dir() {
	return std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "content" / "base";
}

// Fixture-unique storage files (doctest runs every TEST_CASE in the same
// process, so two cases racing on the same path would corrupt each other's
// vb.storage.json -- same reasoning as content_pack_test.cpp's own
// temp_storage() helper, just keyed by the fixture's call site).
inline std::filesystem::path content_base_storage(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_content_base_fixture_") + name + ".json");
	std::filesystem::remove(p);
	return p;
}

// Header-only (templates/inline methods only) so every TEST_CASE including
// this file gets its own instance -- no separate .cpp to add to
// tests/CMakeLists.txt.
class BasePackFixture {
public:
	// `storage_name` distinguishes this fixture's storage.json from any
	// other test's when VB_WITH_LUA; pass a name unique to the calling
	// TEST_CASE (mirrors content_pack_test.cpp's temp_storage(name) calls).
	explicit BasePackFixture(const char *storage_name, std::uint64_t world_seed = 7)
			: net_(),
			  registry_(vb::world::BlockRegistry::base()),
			  rt_(net_.server(), registry_, content_base_storage(storage_name)) {
		if (!vb::script::load_content_pack(rt_, content_base_dir())) {
			FAIL("content/base failed to load");
		}
		install_test_give();
		rt_.freeze();

		// Same order as src/server/main.cpp's real sequence: these wrap
		// `host` before ServerSession's constructor copies it, so a joining
		// client actually receives S2C_KeybindRegistry/S2C_EntityKindRegistry
		// (keybinds.lua's base:pause/base:inventory, entities/player.lua's
		// and entities/dropped_item.lua's `represents` visual kinds).
		vb::net::HandshakeServerHost host;
		rt_.install_keybind_registry(host);
		rt_.install_entity_kind_registry(host);
		rt_.install_join_veto(host);

		vb::net::HandshakeServerConfig cfg;
		cfg.world_seed = world_seed;
		server_ = std::make_unique<vb::net::ServerSession>(net_.server(), cfg, host);
		rt_.attach_session(*server_);

		if (!net_.server().listen(0)) {
			FAIL("loopback listen failed");
		}

		vb::net::Transport &ta = net_.create_client();
		auto conn = ta.connect("x", 0);
		if (!conn) {
			FAIL("loopback connect failed");
		}
		client_ = std::make_unique<vb::net::ClientSession>(ta, *conn,
				vb::net::HandshakeClientConfig{ "A", "", "v", 1 });

		pump(16);
		if (!client_->joined()) {
			FAIL("test client never joined");
		}
		player_id_ = client_->join_accept()->your_net_id;
	}

	// Attaches a real World + WorldReplicator (synchronous worldgen, no
	// background threads) for tests that need a real block-edit/fall path.
	// Call right after construction, before any pump(); mirrors
	// pack_runtime_integration_test.cpp's own World/WorldReplicator setup.
	void with_world(int view_distance_chunks = 2, int vertical_view_chunks = 3) {
		namespace wg = vb::worldgen;
		world_ = std::make_unique<vb::world::World>(registry_);
		pool_ = std::make_unique<wg::WorldGenWorkerPool>(
				wg::WorldGenerator(wg::WorldGenParams{}, registry_),
				wg::WorldGenWorkerPool::kSynchronous);
		auto replicator = std::make_unique<vb::net::WorldReplicator>(
				*world_, *pool_, registry_, view_distance_chunks, vertical_view_chunks);
		rt_.attach_world(*replicator);
		server_->set_world_replicator(std::move(replicator));
		// Re-pump so the client mirrors whatever chunk the join spawn put it
		// in before a test starts issuing block edits/physics near its own
		// position.
		pump(8);
	}

	void pump(int n) {
		for (int i = 0; i < n; ++i) {
			server_->tick(0.05);
			client_->tick(0.05);
			for (auto &c : extra_clients_) {
				c->tick(0.05);
			}
		}
	}

	// Runs arbitrary Lua against the already-loaded pack's shared globals
	// (same test-only seam content_pack_test.cpp's crafting case uses) --
	// acquisition shortcuts only, never a reimplementation of the behaviour
	// under test (docs/content-base-testing.md §2.4).
	void lua(std::string_view code) {
		if (!rt_.load_pack_file(code)) {
			FAIL("fixture Lua failed: " << code);
		}
	}

	void chat(std::string_view text) {
		client_->send_chat(text);
		pump(2);
	}

	std::vector<std::string> drain_messages() { return client_->take_chat_messages(); }

	std::string last_message() {
		const auto msgs = drain_messages();
		return msgs.empty() ? std::string() : msgs.back();
	}

	int count_of(const char *block_name) const {
		const vb::core::BlockId id = registry_.find(block_name);
		int total = 0;
		for (const auto &slot : client_->inventory()) {
			if (slot.item == id) {
				total += slot.count;
			}
		}
		return total;
	}

	// Test-only acquisition shortcut: gives the joined player `count` of
	// `block_name` directly, bypassing crafting/drops/pickup -- for tests
	// whose subject is something else entirely (fall damage, keybinds,
	// storage, placing). Routed through one chat handler registered once in
	// the constructor (install_test_give()), same "/testgive-wood" pattern
	// content_pack_test.cpp's crafting case already uses, generalized to any
	// block name instead of a single hardcoded one.
	void give(const char *block_name, int count) {
		const std::string cmd = std::string("/__give ") + block_name + " " +
				std::to_string(count);
		chat(cmd);
		pump(2);
	}

	void send_input(const vb::protocol::InputCmd &cmd) { client_->push_input(cmd); }

	// Rising-edge helpers over send_input: hold `button_bit` for
	// `ticks_held` ticks (default 1 == a single click), leaving it held
	// until release() -- mirrors mechanics.lua's own was_down[]-keyed
	// rising-edge contract (one logical press, however many ticks the
	// button stays down).
	void press(std::uint8_t button_bit, int ticks_held = 1) {
		vb::protocol::InputCmd cmd;
		cmd.dt = 0.05f;
		cmd.buttons = button_bit;
		for (int i = 0; i < ticks_held; ++i) {
			cmd.seq = ++input_seq_;
			send_input(cmd);
			pump(1);
		}
	}

	void release() {
		vb::protocol::InputCmd cmd;
		cmd.dt = 0.05f;
		cmd.buttons = 0;
		cmd.seq = ++input_seq_;
		send_input(cmd);
		pump(1);
	}

	// Pack-defined keybind helpers (keybinds.lua): send one InputCmd with
	// `keybind_index` set in the keybinds bitset, then release -- mirrors
	// press()/release() above but for the keybinds bitfield instead of
	// buttons. `keybind_index` is the bit position (registration order),
	// not a name -- callers look it up once via keybind_bit().
	void press_keybind(std::uint32_t keybind_index, int ticks_held = 1) {
		vb::protocol::InputCmd cmd;
		cmd.dt = 0.05f;
		cmd.keybinds = 1u << keybind_index;
		for (int i = 0; i < ticks_held; ++i) {
			cmd.seq = ++input_seq_;
			send_input(cmd);
			pump(1);
		}
	}

	void release_keybind() {
		vb::protocol::InputCmd cmd;
		cmd.dt = 0.05f;
		cmd.seq = ++input_seq_;
		send_input(cmd);
		pump(1);
	}

	std::uint32_t keybind_bit(const char *name) const {
		const auto &names = client_->registered_keybinds();
		for (std::size_t i = 0; i < names.size(); ++i) {
			if (names[i] == name) {
				return static_cast<std::uint32_t>(i);
			}
		}
		FAIL("keybind not registered: " << name);
		return 0;
	}

	// Places/breaks a block through the real validated edit pipeline
	// (ServerSession::apply_script_block_edit -> WorldReplicator::
	// apply_block_edit -> on_place/on_break hooks), the same primitive
	// mechanics.lua's player:place_block()/player:punch() ultimately drive
	// -- just without needing a rising-edge input cmd or a wire round trip.
	// Positions the test player at `pos` (well within the engine's reach
	// default) immediately before the edit, so callers never need to
	// reason about reach themselves. Requires with_world().
	bool place(vb::core::IVec3 pos, const char *block_name) {
		return place(pos, registry_.find(block_name));
	}

	bool place(vb::core::IVec3 pos, vb::core::BlockId block) {
		server_->set_player_state(player_id_,
				{ pos.x + 0.5, pos.y + 0.5, pos.z + 0.5 });
		// apply_block_edit's placement path requires the target to already
		// be air (see WorldReplicator::apply_block_edit) -- a sea-level
		// target may still be base:water from worldgen, so clear it first.
		// A bare world_.set_block (not apply_script_block_edit) sidesteps
		// on_break entirely: water has none anyway, and callers testing an
		// actual on_break drop always target a column surface_y() already
		// guarantees is air above (real terrain, not a lake).
		if (world_->get_block(pos) != vb::core::BlockId::kAir) {
			world_->set_block(pos, vb::core::BlockId::kAir);
		}
		const bool ok = server_->apply_script_block_edit(
				player_id_, vb::protocol::BlockEditAction::kPlace, pos, block);
		pump(2);
		return ok;
	}

	bool break_block(vb::core::IVec3 pos) {
		server_->set_player_state(player_id_,
				{ pos.x + 0.5, pos.y + 0.5, pos.z + 0.5 });
		const bool ok = server_->apply_script_block_edit(
				player_id_, vb::protocol::BlockEditAction::kBreak, pos);
		pump(2);
		return ok;
	}

	// Aimed input helpers for mechanics.lua's real player:punch()/
	// place_block() rising-edge path (as opposed to place()/break_block()
	// above, which call apply_script_block_edit directly) -- computes
	// yaw/pitch from the player's *current* position to `target`, inverting
	// core::forward_from_yaw_pitch (vb/core/math.hpp), which is exactly what
	// ServerSession::punch() uses to build its raycast direction.
	vb::protocol::InputCmd aimed_cmd(vb::core::Vec3d target, std::uint8_t buttons) const {
		auto move = server_->player_move_state(player_id_);
		vb::core::Vec3d eye = move->position;
		eye.y += 1.62; // physics::MoveParams default eye_height; content/base never overrides it
		const double dx = target.x - eye.x;
		const double dy = target.y - eye.y;
		const double dz = target.z - eye.z;
		const double horiz = std::sqrt(dx * dx + dz * dz);
		const double len = std::sqrt(horiz * horiz + dy * dy);
		constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;
		vb::protocol::InputCmd cmd;
		cmd.dt = 0.05f;
		cmd.buttons = buttons;
		cmd.pitch = static_cast<float>(std::asin(dy / len) * kRad2Deg);
		cmd.yaw = static_cast<float>(std::atan2(dx, -dz) * kRad2Deg);
		return cmd;
	}

	// Warms up the player's stored look rotation toward `target` *before* a
	// press_aimed() sequence. Necessary because ServerSession::
	// handle_input_batch (src/net/session.cpp) dispatches vb.on
	// ("player_input", ...) -- and therefore mechanics.lua's punch()/
	// place_block() calls -- *before* writing this same cmd's yaw/pitch
	// into the player's stored Rotation; punch()/place_block()'s own
	// raycast reads that stored (one-cmd-stale) rotation. Without a prior
	// aimed cmd to settle it first, press_aimed()'s very first (rising-
	// edge) tick would fire using whatever rotation preceded it -- for a
	// freshly joined test player, {0, 0}, not `target` at all.
	void aim_at(vb::core::Vec3d target) {
		auto cmd = aimed_cmd(target, 0);
		cmd.seq = ++input_seq_;
		send_input(cmd);
		pump(1);
	}

	void press_aimed(vb::core::Vec3d target, std::uint8_t button_bit, int ticks_held = 1) {
		for (int i = 0; i < ticks_held; ++i) {
			auto cmd = aimed_cmd(target, button_bit);
			cmd.seq = ++input_seq_;
			send_input(cmd);
			pump(1);
		}
	}

	void release_aimed(vb::core::Vec3d target) {
		auto cmd = aimed_cmd(target, 0);
		cmd.seq = ++input_seq_;
		send_input(cmd);
		pump(1);
	}

	bool debug_has_chunk(vb::core::IVec3 pos) const {
		return world_->has_chunk(vb::core::chunk_of(pos));
	}
	vb::core::BlockId debug_get_block(vb::core::IVec3 pos) const { return world_->get_block(pos); }
	vb::core::BlockId debug_world_find(const char *name) const { return world_->registry().find(name); }

	// Positions the player straight above `target_cell` (an air cell with a
	// solid floor at target_cell.y - 1, e.g. dry_target()'s own return),
	// `eye_to_floor` metres above that floor's top face, looking straight
	// down -- the geometry mechanics.lua's real secondary (place) path
	// needs: vb.world.raycast() only ever returns a hit against a *solid*
	// surface (hit + its face normal), never against the empty target
	// cell itself, so aiming into open air at the target directly (as
	// punch's press_aimed()/aim_at() do against a real block) can't work
	// for placing -- the ray has to land on the floor below and resolve to
	// floor + normal(0,1,0) == target_cell. Also warms up the look
	// rotation (see aim_at()'s own comment on the one-cmd dispatch lag).
	void stand_above_and_aim_down(vb::core::IVec3 target_cell, double eye_to_floor) {
		const double floor_top = target_cell.y; // target_cell.y - 1 is the floor; its top face is target_cell.y
		const vb::core::Vec3d feet{ target_cell.x + 0.5,
			floor_top + eye_to_floor - 1.62, target_cell.z + 0.5 };
		server_->set_player_state(player_id_, feet);
		const vb::core::Vec3d floor_point{
			target_cell.x + 0.5, floor_top - 0.01, target_cell.z + 0.5 };
		aim_at(floor_point);
	}

	// surface_y(x, z) + 1, guaranteed air (clearing base:water first if
	// worldgen put the column at/under sea level) -- the one cell every
	// placing test in content_base_behaviour_test.cpp targets, including
	// the ones that drive mechanics.lua's real place_block() path (unlike
	// place() above, which clears automatically but bypasses that path).
	vb::core::IVec3 dry_target(int x = 0, int z = 0) {
		const vb::core::IVec3 cell{ x, surface_y(x, z) + 1, z };
		if (world_->get_block(cell) != vb::core::BlockId::kAir) {
			world_->set_block(cell, vb::core::BlockId::kAir);
		}
		return cell;
	}

	// Direct world read-back for tests checking a place()/break_block()/
	// punch-driven edit's actual effect. Requires with_world().
	bool world_solid_at(vb::core::IVec3 pos) const { return world_->solid_at(pos); }

	// Finds the first solid voxel at (x, z), scanning downward -- same
	// approach pack_runtime_integration_test.cpp's surface_voxel() uses.
	// Requires with_world() and at least one pump() so the column has
	// actually generated. The cell directly above may be base:water rather
	// than air (sea level) -- place() below clears it automatically.
	int surface_y(int x, int z) const {
		for (int y = 80; y > -16; --y) {
			if (world_->solid_at({ x, y, z })) {
				return y;
			}
		}
		FAIL("no solid ground found in column");
		return 0;
	}

	// Drives exactly one real `player_landed` dispatch with `impact_speed`
	// (m/s), by directly puppeting position/velocity around a single
	// physics tick rather than simulating a multi-second real fall (which
	// can't hit an exact impact speed deterministically). Requires
	// with_world().
	//
	// Step 1 moves the player into open air and processes one input cmd so
	// the engine's own ground-collision check recomputes on_ground = false
	// (see src/net/session.cpp's step_movement call) -- the join spawn (or
	// a prior landing) may have left it true. Step 2 then places the player
	// just above the ground with velocity set to exactly -impact_speed and
	// processes one more cmd: ServerSession::handle_input_batch captures
	// `fall_speed_before = -velocity.y` *before* integrating this tick's
	// gravity (src/net/session.cpp), so the on_landed_ callback fires with
	// exactly `impact_speed`, and the fall distance (impact_speed * dt +
	// 0.5 * gravity * dt^2, comfortably > the small gap left above ground)
	// guarantees the same tick's integration actually reaches the ground.
	void land(double impact_speed, int x = 0, int z = 0) {
		const double ground_top = surface_y(x, z) + 1.0;
		const vb::core::Vec3d airborne{ x + 0.5, ground_top + 10.0, z + 0.5 };
		server_->set_player_state(player_id_, airborne);
		press(0, 1); // any cmd: recomputes on_ground = false at this position
		release();

		server_->set_player_state(player_id_, { x + 0.5, ground_top + 0.05, z + 0.5 });
		server_->set_player_velocity(player_id_, { 0, -impact_speed, 0 });
		press(0, 1);
		release();
	}

	vb::world::BlockRegistry &registry() { return registry_; }
	vb::script::PackRuntime &runtime() { return rt_; }
	vb::net::ServerSession &server() { return *server_; }
	vb::net::ClientSession &client() { return *client_; }
	vb::core::NetId player_id() const { return player_id_; }

	// A second (third, ...) connected+joined client over the same
	// LoopbackNetwork, for tests that need to observe the first client from
	// outside (visual-kind replication, multiplayer-visible drops). Owned by
	// the fixture, kept alive for its whole lifetime.
	vb::net::ClientSession &add_client(const char *name) {
		vb::net::Transport &t = net_.create_client();
		auto conn = t.connect("x", 0);
		if (!conn) {
			FAIL("loopback connect failed");
		}
		extra_clients_.push_back(std::make_unique<vb::net::ClientSession>(
				t, *conn, vb::net::HandshakeClientConfig{ name, "", "v", 1 }));
		auto &c = *extra_clients_.back();
		pump(16);
		if (!c.joined()) {
			FAIL("second test client never joined");
		}
		return c;
	}

private:
	// Registered once (constructor-time, before freeze()) rather than from
	// give(): the engine has no "re-run this handler with different
	// captured args" primitive, and `vb.on` handlers stack on repeated
	// registration -- one parse/give/count triple per call would leave N
	// stale handlers behind after N give() calls in the same test.
	//
	// vb.register_block throws unconditionally once `frozen` is true (the
	// guard is checked at *call* time, not registration time -- it doesn't
	// matter that this handler itself was registered before freeze()), so
	// it can't be used as a runtime "find id by name" lookup the way
	// content_base_data_test.cpp's biome spy re-registers things before
	// load. blocks/*.lua already captured every id content/base ever gives
	// a name to as a plain global (base_dirt_id, base_stone_id, ...) by the
	// time this installs (constructor calls it right after
	// load_content_pack); snapshot those into a name -> id table instead.
	void install_test_give() {
		rt_.load_pack_file(R"(
			local __give_ids = {
				["base:dirt"] = base_dirt_id,
				["base:stone"] = base_stone_id,
				["base:sand"] = base_sand_id,
				["base:wood"] = base_wood_id,
				["base:leaves"] = base_leaves_id,
				["base:planks"] = base_planks_id,
				["base:sticks"] = base_sticks_id,
			}
			vb.on("chat", function(player, text)
				local name, count = text:match("^/__give%s+(%S+)%s+(%d+)$")
				if not name then return true end
				local id = __give_ids[name]
				if id then
					player:give({ item = id, count = tonumber(count) })
				end
				return false
			end)
		)");
	}

	vb::net::LoopbackNetwork net_;
	vb::world::BlockRegistry registry_;
	vb::script::PackRuntime rt_;
	std::unique_ptr<vb::net::ServerSession> server_;
	std::unique_ptr<vb::net::ClientSession> client_;
	std::vector<std::unique_ptr<vb::net::ClientSession>> extra_clients_;
	std::unique_ptr<vb::world::World> world_;
	std::unique_ptr<vb::worldgen::WorldGenWorkerPool> pool_;
	vb::core::NetId player_id_ = vb::core::NetId::kInvalid;
	std::uint32_t input_seq_ = 0;
};

} // namespace vb::test

#endif // VB_WITH_LUA
