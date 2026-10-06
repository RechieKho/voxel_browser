#include "vb/world/item_drops.hpp"

#include <algorithm>
#include <cmath>

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

namespace {

constexpr double kDropGravity = 20.0; // m/s^2
constexpr double kDropTerminal = 30.0;
constexpr double kDropHalfSize = 0.15; // drop centre -> its bottom face

// Falls `drop` one step; true if its position changed.
bool fall(ItemDrop &drop, double dt, const BlockSolidQuery &world) {
	const auto solid_below = [&](double y) {
		return world.solid_at({ static_cast<int>(std::floor(drop.pos.x)),
			static_cast<int>(std::floor(y - kDropHalfSize)),
			static_cast<int>(std::floor(drop.pos.z)) });
	};
	if (drop.vel_y == 0.0 && solid_below(drop.pos.y - 0.02)) {
		return false; // resting
	}
	drop.vel_y = std::max(drop.vel_y - kDropGravity * dt, -kDropTerminal);
	const double ny = drop.pos.y + drop.vel_y * dt;
	if (solid_below(ny)) {
		drop.pos.y = std::floor(ny - kDropHalfSize) + 1.0 + kDropHalfSize;
		drop.vel_y = 0.0;
	} else {
		drop.pos.y = ny;
	}
	return true;
}

// Distance from `p` to the vertical segment [feet, feet + height].
double distance_to_body(core::Vec3d p, core::Vec3d feet, double height) {
	const double dy = p.y < feet.y ? feet.y - p.y
			: p.y > feet.y + height ? p.y - (feet.y + height)
									: 0.0;
	const double dx = p.x - feet.x;
	const double dz = p.z - feet.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

ItemDropTickResult ItemDropSystem::tick(double dt,
		const std::vector<std::pair<core::NetId, core::Vec3d>> &players,
		double player_height, const BlockSolidQuery *world) {
	ItemDropTickResult result;
	for (auto it = drops_.begin(); it != drops_.end();) {
		ItemDrop &drop = it->second;
		drop.age += dt;
		if (world != nullptr && fall(drop, dt, *world)) {
			result.moved.push_back(drop.id);
		}

		// Nearest in-range player wins if more than one is close enough this
		// tick -- an arbitrary but deterministic (map iteration order of
		// `players`, which the caller controls) tie-break; two players are
		// very rarely equidistant from the same drop in practice.
		bool picked_up = false;
		for (const auto &[player_id, player_pos] : players) {
			if (distance_to_body(drop.pos, player_pos, player_height) <=
					drop.pickup_radius) {
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
