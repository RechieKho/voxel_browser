#include "vb/world/block_damage.hpp"

namespace vb::world {

void BlockDamageSystem::begin(core::IVec3 pos, core::NetId player,
		std::uint16_t max_damage, std::uint64_t tick) {
	BlockDamageState &s = states_[pos];
	if (s.max_damage == 0) {
		s.max_damage = max_damage;
	}
	s.last_touched_tick = tick;
	for (const core::NetId id : s.contributors) {
		if (id == player) {
			return;
		}
	}
	s.contributors.push_back(player);
}

void BlockDamageSystem::stop(core::IVec3 pos, core::NetId player) {
	const auto it = states_.find(pos);
	if (it == states_.end()) {
		return;
	}
	std::vector<core::NetId> &c = it->second.contributors;
	for (auto cit = c.begin(); cit != c.end(); ++cit) {
		if (*cit == player) {
			c.erase(cit);
			return;
		}
	}
}

void BlockDamageSystem::remove_player(core::NetId player) {
	for (auto &[pos, state] : states_) {
		(void)pos;
		std::vector<core::NetId> &c = state.contributors;
		for (auto it = c.begin(); it != c.end();) {
			if (*it == player) {
				it = c.erase(it);
			} else {
				++it;
			}
		}
	}
}

BlockDamageTickResult BlockDamageSystem::tick(std::uint64_t server_tick,
		const std::function<float(core::IVec3, core::NetId, std::uint16_t)>
				&damage_tick_fn,
		const std::function<std::optional<float>(
				core::IVec3, float, std::uint16_t, std::uint64_t)>
				&health_tick_fn) {
	BlockDamageTickResult result;
	for (auto it = states_.begin(); it != states_.end();) {
		const core::IVec3 pos = it->first;
		BlockDamageState &s = it->second;
		const float before = s.damage;

		if (damage_tick_fn) {
			float delta = 0.0f;
			for (const core::NetId contributor : s.contributors) {
				delta += damage_tick_fn(pos, contributor, s.max_damage);
			}
			if (delta != 0.0f) {
				s.damage = core::clamp(
						s.damage + delta, 0.0f, static_cast<float>(s.max_damage));
				s.last_touched_tick = server_tick;
			}
		}
		if (health_tick_fn) {
			const std::uint64_t idle = server_tick - s.last_touched_tick;
			if (const std::optional<float> replacement =
							health_tick_fn(pos, s.damage, s.max_damage, idle)) {
				s.damage = core::clamp(
						*replacement, 0.0f, static_cast<float>(s.max_damage));
			}
		}

		if (s.max_damage > 0 && s.damage >= static_cast<float>(s.max_damage)) {
			const core::NetId contributor =
					s.contributors.empty() ? core::NetId::kInvalid : s.contributors.front();
			result.completed.push_back({ pos, contributor });
			it = states_.erase(it);
			continue;
		}
		if (s.damage <= 0.0f && s.contributors.empty()) {
			if (before != 0.0f) {
				result.cleared.push_back(pos);
			}
			it = states_.erase(it);
			continue;
		}
		if (s.damage != before) {
			result.changed.push_back(pos);
		}
		++it;
	}
	return result;
}

} // namespace vb::world
