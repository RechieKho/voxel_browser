// Server half of the development-only automation channel
// (docs/e2e-automation.md §5.3). Only included under VB_WITH_AUTOMATION.
#pragma once

#include <map>
#include <string>

#include "vb/automation/host.hpp"
#include "vb/net/session.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"

namespace vb::server {

class ServerAutomationEndpoint final : public vb::automation::Endpoint {
public:
	ServerAutomationEndpoint(vb::net::ServerSession &session, const vb::world::World &world,
			const vb::world::BlockRegistry &registry, const long long &tick,
			std::uint16_t bound_port) :
			session_(session), world_(world), registry_(registry), tick_(tick), port_(bound_port) {}

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
			players.push_back(std::move(p));
		}
		return nlohmann::json{
			{ "tick", tick_ },
			{ "player_count", session_.player_count() },
			{ "players", std::move(players) },
		};
	}

	std::optional<std::string> block_name_at(int x, int y, int z) override {
		const vb::core::BlockId id = world_.get_block(vb::core::IVec3{ x, y, z });
		if (!registry_.contains(id)) {
			return std::nullopt;
		}
		return registry_.get(id).name;
	}

private:
	vb::net::ServerSession &session_;
	const vb::world::World &world_;
	const vb::world::BlockRegistry &registry_;
	const long long &tick_;
	std::uint16_t port_;
	std::map<vb::core::NetId, std::string> names_;
};

} // namespace vb::server
