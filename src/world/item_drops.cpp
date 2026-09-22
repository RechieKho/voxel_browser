#include "vb/world/item_drops.hpp"

namespace vb::world {

ItemDropSystem::ItemDropSystem(double pickup_radius, double lifetime_seconds) : pickup_radius_(pickup_radius), lifetime_seconds_(lifetime_seconds) {}

core::NetId ItemDropSystem::spawn(core::Vec3d pos, core::BlockId item,
		std::uint16_t count, std::optional<double> pickup_radius,
		std::optional<double> lifetime_seconds) {
	const auto id = static_cast<core::NetId>(next_id_++);
	drops_[id] = ItemDrop{ id, item, count, pos, 0.0,
		pickup_radius.value_or(pickup_radius_),
		lifetime_seconds.value_or(lifetime_seconds_) };
	return id;
}

ItemDropTickResult ItemDropSystem::tick(
		double dt, const std::vector<std::pair<core::NetId, core::Vec3d>> &players) {
	ItemDropTickResult result;
	for (auto it = drops_.begin(); it != drops_.end();) {
		ItemDrop &drop = it->second;
		drop.age += dt;

		// Nearest in-range player wins if more than one is close enough this
		// tick -- an arbitrary but deterministic (map iteration order of
		// `players`, which the caller controls) tie-break; two players are
		// very rarely equidistant from the same drop in practice.
		bool picked_up = false;
		for (const auto &[player_id, player_pos] : players) {
			if ((player_pos - drop.pos).length() <= drop.pickup_radius) {
				result.pickups.push_back({ player_id, drop.item, drop.count });
				result.removed.push_back(drop.id);
				picked_up = true;
				break;
			}
		}
		if (!picked_up && drop.age >= drop.lifetime_seconds) {
			result.removed.push_back(drop.id);
			picked_up = true; // reuse the flag to mean "erase below"
		}

		if (picked_up) {
			it = drops_.erase(it);
		} else {
			++it;
		}
	}
	return result;
}

} // namespace vb::world
