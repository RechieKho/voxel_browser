// Server half of the development-only automation channel
// (docs/e2e-automation.md §5.3, contract in docs/automation-protocol.md).
// Only included under VB_WITH_AUTOMATION.
//
// The server is the source of truth, so these commands exist for test *setup*
// (build a scene, move/equip/heal a player) and *verification* (state, block
// views). They are admin powers by design -- which is why none of this is
// compiled into a production binary (§7).
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "vb/automation/host.hpp"
#include "vb/net/session.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"

namespace vb::server {

class ServerAutomationEndpoint final : public vb::automation::Endpoint {
public:
	ServerAutomationEndpoint(vb::net::ServerSession &session, vb::world::World &world,
			const vb::world::BlockRegistry &registry, vb::script::PackRuntime &pack,
			const long long &tick, std::uint16_t bound_port) :
			session_(session), world_(world), registry_(registry), pack_(pack), tick_(tick), port_(bound_port) {}

	void player_joined(vb::core::NetId id, std::string name) { names_[id] = std::move(name); }
	void player_left(vb::core::NetId id) { names_.erase(id); }

	std::string role() const override { return "server"; }

	nlohmann::json hello_info() const override {
		return nlohmann::json{ { "port", port_ } };
	}

	nlohmann::json state() override {
		nlohmann::json players = nlohmann::json::array();
		for (const auto &[id, name] : names_) {
			nlohmann::json p{ { "name", name }, { "net_id", static_cast<std::uint32_t>(id) } };
			if (auto ms = session_.player_move_state(id)) {
				p["pos"] = { ms->position.x, ms->position.y, ms->position.z };
			}
			if (auto h = session_.player_health(id)) {
				p["health"] = h->first;
				p["max_health"] = h->second;
			}
			// External auth: who the engine verified this player to be. User
			// data only (never a token); absent when the server isn't authenticating.
			if (const auto login = session_.player_login(id)) {
				p["login"] = { { "provider", login->provider }, { "subject", login->subject },
					{ "name", login->name } };
			}
			players.push_back(std::move(p));
		}
		return nlohmann::json{
			{ "tick", tick_ },
			{ "time_of_day", session_.time_of_day() },
			{ "player_count", session_.player_count() },
			{ "players", std::move(players) },
		};
	}

	bool chunk_loaded_at(int x, int y, int z) override {
		return world_.has_chunk(vb::core::chunk_of(vb::core::IVec3{ x, y, z }));
	}

	std::optional<std::string> block_name_at(int x, int y, int z) override {
		// Same contract as the client: an unloaded chunk has no block (World::get_block
		// would claim air), so `block_is` is false there instead of misleadingly true.
		if (!chunk_loaded_at(x, y, z)) {
			return std::nullopt;
		}
		const vb::core::BlockId id = world_.get_block(vb::core::IVec3{ x, y, z });
		if (!registry_.contains(id)) {
			return std::nullopt;
		}
		return registry_.get(id).name;
	}

	std::optional<vb::automation::Reply> command(const vb::automation::Request &req) override {
		using nlohmann::json;
		using vb::automation::Reply;
		const json &a = req.args;
		auto bad = [](std::string m) { return Reply::error("bad_request", std::move(m)); };

		if (req.cmd == "block_at") {
			vb::core::IVec3 p;
			if (!read_pos(a.value("pos", json()), p)) {
				return bad("block_at: needs 'pos' [x,y,z]");
			}
			const auto name = block_name_at(p.x, p.y, p.z);
			return Reply::success(json{ { "block", name ? json(*name) : json() } });
		}
		if (req.cmd == "set_block") {
			vb::core::IVec3 p;
			vb::core::BlockId id;
			if (!read_pos(a.value("pos", json()), p) || !read_block(a, id)) {
				return bad("set_block: needs 'pos' [x,y,z] and a registered block name 'block'");
			}
			// World::set_block would create an empty chunk on demand; never do that
			// from automation -- an edit outside the loaded world is a test bug.
			if (!world_.has_chunk(vb::core::chunk_of(p))) {
				return Reply::error("not_loaded", "no chunk is loaded at that position",
						json{ { "pos", json::array({ p.x, p.y, p.z }) } });
			}
			return Reply::success(json{ { "changed", world_.set_block(p, id) } });
		}
		if (req.cmd == "fill") {
			vb::core::IVec3 lo, hi;
			vb::core::BlockId id;
			if (!read_pos(a.value("from", json()), lo) || !read_pos(a.value("to", json()), hi) ||
					!read_block(a, id)) {
				return bad("fill: needs 'from' [x,y,z], 'to' [x,y,z] and a registered block name 'block'");
			}
			const vb::core::IVec3 mn{ std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z) };
			const vb::core::IVec3 mx{ std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z) };
			const long long volume = static_cast<long long>(mx.x - mn.x + 1) * (mx.y - mn.y + 1) *
					(mx.z - mn.z + 1);
			if (volume > kMaxFillVolume) {
				return bad("fill: volume " + std::to_string(volume) + " exceeds the " +
						std::to_string(kMaxFillVolume) + " block limit");
			}
			long long changed = 0, unchanged = 0, skipped = 0;
			for (int x = mn.x; x <= mx.x; ++x) {
				for (int y = mn.y; y <= mx.y; ++y) {
					for (int z = mn.z; z <= mx.z; ++z) {
						const vb::core::IVec3 p{ x, y, z };
						if (!world_.has_chunk(vb::core::chunk_of(p))) {
							++skipped;
						} else if (world_.set_block(p, id)) {
							++changed;
						} else {
							++unchanged;
						}
					}
				}
			}
			if (changed + unchanged == 0) {
				// Edits outside every loaded chunk are dropped, not queued: say so
				// loudly rather than let a test build its scene in the void.
				return Reply::error("not_loaded", "no chunk is loaded anywhere in that box",
						json{ { "not_loaded", skipped } });
			}
			// A box that only partly overlaps loaded chunks is not an error, but is reported.
			return Reply::success(json{ { "changed", changed }, { "unchanged", unchanged },
					{ "not_loaded", skipped } });
		}
		if (req.cmd == "teleport") {
			vb::core::NetId id;
			vb::core::Vec3d pos;
			if (auto err = find_player(a, id)) {
				return err;
			}
			if (!read_vec3(a.value("pos", json()), pos)) {
				return bad("teleport: needs 'pos' [x,y,z]");
			}
			session_.teleport_player(id, pos);
			return Reply::success();
		}
		if (req.cmd == "give") {
			vb::core::NetId id;
			if (auto err = find_player(a, id)) {
				return err;
			}
			if (!a.contains("item") || !a["item"].is_string()) {
				return bad("give: needs string 'item'");
			}
			const long long count = a.value("count", 1LL);
			if (count < 1 || count > 65535) {
				return bad("give: 'count' must be 1..65535");
			}
			if (!pack_.admin_give(id, a["item"].get<std::string>(), static_cast<std::uint16_t>(count))) {
				return Reply::error("unknown_item", "no registered block named '" + a["item"].get<std::string>() + "'");
			}
			return Reply::success();
		}
		if (req.cmd == "set_time") {
			const long long t = a.value("ticks", -1LL);
			if (t < 0 || t >= 24000) {
				return bad("set_time: 'ticks' must be 0..23999");
			}
			session_.set_time_of_day(static_cast<std::uint32_t>(t));
			return Reply::success();
		}
		if (req.cmd == "set_health") {
			vb::core::NetId id;
			if (auto err = find_player(a, id)) {
				return err;
			}
			if (!a.contains("value") || !a["value"].is_number()) {
				return bad("set_health: needs numeric 'value'");
			}
			session_.set_player_health(id, a["value"].get<float>());
			return Reply::success();
		}
		if (req.cmd == "kick") {
			vb::core::NetId id;
			if (auto err = find_player(a, id)) {
				return err;
			}
			session_.kick_player(id, a.value("reason", std::string("kicked by automation")));
			return Reply::success();
		}
		if (req.cmd == "run_lua") {
			if (!a.contains("code") || !a["code"].is_string()) {
				return bad("run_lua: needs string 'code'");
			}
			const auto r = pack_.load_pack_file(a["code"].get<std::string>(), "automation");
			if (!r.ok) {
				return Reply::error("lua_error", r.message);
			}
			return Reply::success();
		}
		return std::nullopt;
	}

private:
	static constexpr long long kMaxFillVolume = 100000;

	static bool read_vec3(const nlohmann::json &j, vb::core::Vec3d &out) {
		if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) {
			return false;
		}
		out = { j[0].get<double>(), j[1].get<double>(), j[2].get<double>() };
		return true;
	}
	static bool read_pos(const nlohmann::json &j, vb::core::IVec3 &out) {
		vb::core::Vec3d v;
		if (!read_vec3(j, v)) {
			return false;
		}
		out = { static_cast<int>(std::floor(v.x)), static_cast<int>(std::floor(v.y)), static_cast<int>(std::floor(v.z)) };
		return true;
	}
	bool read_block(const nlohmann::json &args, vb::core::BlockId &out) const {
		if (!args.contains("block") || !args["block"].is_string()) {
			return false;
		}
		const std::string name = args["block"].get<std::string>();
		const vb::core::BlockId id = registry_.find(name);
		// find() returns kAir for an unknown name; only the real air block may map to it.
		if (id == vb::core::BlockId::kAir && name != "base:air") {
			return false;
		}
		out = id;
		return true;
	}
	// "player" is a name or a net id. Returns an error reply, or nullopt with `out` set.
	std::optional<vb::automation::Reply> find_player(const nlohmann::json &args, vb::core::NetId &out) const {
		using vb::automation::Reply;
		const nlohmann::json &p = args.value("player", nlohmann::json());
		for (const auto &[id, name] : names_) {
			if ((p.is_string() && p.get<std::string>() == name) ||
					(p.is_number_integer() && p.get<std::uint32_t>() == static_cast<std::uint32_t>(id))) {
				out = id;
				return std::nullopt;
			}
		}
		if (!p.is_string() && !p.is_number_integer()) {
			return Reply::error("bad_request", "needs 'player' (name or net id)");
		}
		nlohmann::json known = nlohmann::json::array();
		for (const auto &[id, name] : names_) {
			(void)id;
			known.push_back(name);
		}
		return Reply::error("no_player", "no such player", nlohmann::json{ { "players", known } });
	}

	vb::net::ServerSession &session_;
	vb::world::World &world_;
	const vb::world::BlockRegistry &registry_;
	vb::script::PackRuntime &pack_;
	const long long &tick_;
	std::uint16_t port_;
	std::map<vb::core::NetId, std::string> names_;
};

} // namespace vb::server
