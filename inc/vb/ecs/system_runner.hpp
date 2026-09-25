#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

// Generic, explicitly-ordered system runner (spec §6/§7.2). A thin wrapper
// around "call these functions on the registry, in this order" -- no
// threading, no priorities, no dependency graph. Matches how
// `ServerSession::tick()` already runs its phases serially; this just gives
// that phase list a name and a single, greppable, inspectable home instead
// of an implicit call sequence.

namespace vb::ecs {

struct TickContext {
	double dt_seconds = 0.0;
	std::uint32_t server_tick = 0;
};

class SystemRunner {
public:
	using System = std::function<void(entt::registry &, const TickContext &)>;

	// Appends a system; run order == registration order.
	void add(std::string name, System fn);

	// Runs every registered system, in order, against `registry`.
	void run(entt::registry &registry, const TickContext &ctx) const;

	// Registered system names, in run order (introspection/tests).
	const std::vector<std::string> &names() const { return names_; }

private:
	struct Entry {
		std::string name;
		System fn;
	};

	std::vector<Entry> systems_;
	std::vector<std::string> names_; // kept in lockstep with systems_
};

} // namespace vb::ecs
