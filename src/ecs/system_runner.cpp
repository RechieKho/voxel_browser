#include "vb/ecs/system_runner.hpp"

#include "vb/core/log.hpp"

namespace vb::ecs {

void SystemRunner::add(std::string name, System fn) {
	names_.push_back(name);
	systems_.push_back(Entry{ std::move(name), std::move(fn) });
}

void SystemRunner::run(entt::registry &registry, const TickContext &ctx) const {
	for (const auto &entry : systems_) {
		VB_DEBUG("ecs", "system '", entry.name, "' tick=", ctx.server_tick);
		entry.fn(registry, ctx);
	}
}

} // namespace vb::ecs
