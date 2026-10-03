// Client half of the development-only automation channel
// (docs/e2e-automation.md §5.1). Only included under VB_WITH_AUTOMATION.
// E2 scope: read-only queries; action commands arrive in E3.
#pragma once

#include "client_app.hpp"
#include "vb/automation/host.hpp"

namespace vb::client {

class ClientAutomationEndpoint final : public vb::automation::Endpoint {
public:
	explicit ClientAutomationEndpoint(const ClientApp &app) :
			app_(app) {}

	std::string role() const override { return "client"; }

	nlohmann::json state() override {
		const vb::net::ClientSession *c = app_.session();
		nlohmann::json s{
			{ "app_state", ClientApp::app_state_name(app_.app_state()) },
			{ "joined", c != nullptr && c->joined() },
			{ "chat", nlohmann::json(std::vector<std::string>(app_.chat().begin(), app_.chat().end())) },
			{ "entities", nlohmann::json::array() },
			{ "chunks_loaded", 0 },
		};
		if (c == nullptr) {
			return s;
		}
		if (const auto &ja = c->join_accept()) {
			s["net_id"] = static_cast<std::uint32_t>(ja->your_net_id);
		}
		if (c->joined()) {
			const auto feet = c->predicted_feet();
			s["feet"] = { feet.x, feet.y, feet.z };
		}
		s["chunks_loaded"] = c->chunk_store().size();
		const auto &names = c->players();
		for (const auto &[id, rec] : c->remote_entities()) {
			const auto it = names.find(id);
			s["entities"].push_back({
					{ "net_id", static_cast<std::uint32_t>(id) },
					{ "name", it != names.end() ? it->second : std::string() },
					{ "pos", { rec.pos.x, rec.pos.y, rec.pos.z } },
			});
		}
		return s;
	}

	std::optional<std::string> block_name_at(int x, int y, int z) override {
		const vb::net::ClientSession *c = app_.session();
		if (c == nullptr) {
			return std::nullopt;
		}
		const auto &store = c->chunk_store();
		const vb::core::IVec3 p{ x, y, z };
		if (!store.has(vb::core::chunk_of(p))) {
			return std::nullopt; // not streamed in yet
		}
		const vb::core::BlockId id = store.block_at(p);
		return store.registry().contains(id) ? std::optional<std::string>(store.registry().get(id).name)
											 : std::nullopt;
	}

private:
	const ClientApp &app_;
};

} // namespace vb::client
