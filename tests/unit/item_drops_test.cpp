#include <doctest/doctest.h>

#include <optional>

#include "vb/world/item_drops.hpp"

using namespace vb::world;
using vb::core::BlockId;
using vb::core::NetId;
using vb::core::Vec3d;

TEST_CASE("ItemDropSystem: spawn ids never collide with a low player NetId range") {
	ItemDropSystem sys;
	const NetId a = sys.spawn({ 0, 0, 0 }, BlockId{ 1 }, 1);
	const NetId b = sys.spawn({ 1, 0, 0 }, BlockId{ 1 }, 1);
	CHECK(a != b);
	// Well past any plausible player id (ServerSession counts players up
	// from 1).
	CHECK(static_cast<std::uint32_t>(a) > 1000000u);
	CHECK(sys.count() == 2);
}

TEST_CASE("ItemDropSystem: a player within pickup radius collects the drop") {
	ItemDropSystem sys(/*pickup_radius*/ 1.5, /*lifetime*/ 120.0);
	const NetId drop_id = sys.spawn({ 10, 10, 10 }, BlockId{ 3 }, 5);

	// Far away: no pickup, drop survives.
	auto far = sys.tick(0.05, { { NetId{ 1 }, Vec3d{ 0, 0, 0 } } });
	CHECK(far.pickups.empty());
	CHECK(far.removed.empty());
	CHECK(sys.count() == 1);

	// Within radius: picked up and removed.
	auto near = sys.tick(0.05, { { NetId{ 1 }, Vec3d{ 10.5, 10, 10 } } });
	REQUIRE(near.pickups.size() == 1);
	CHECK(near.pickups[0].player == NetId{ 1 });
	CHECK(near.pickups[0].item == BlockId{ 3 });
	CHECK(near.pickups[0].count == 5);
	REQUIRE(near.removed.size() == 1);
	CHECK(near.removed[0] == drop_id);
	CHECK(sys.count() == 0);
}

TEST_CASE("ItemDropSystem: an untouched drop despawns after its lifetime") {
	ItemDropSystem sys(/*pickup_radius*/ 1.0, /*lifetime*/ 1.0);
	const NetId drop_id = sys.spawn({ 0, 0, 0 }, BlockId{ 2 }, 1);

	// No players at all -- still ages.
	auto mid = sys.tick(0.6, {});
	CHECK(mid.removed.empty());
	CHECK(sys.count() == 1);

	auto expired = sys.tick(0.6, {}); // total age 1.2s > 1.0s lifetime
	CHECK(expired.pickups.empty());
	REQUIRE(expired.removed.size() == 1);
	CHECK(expired.removed[0] == drop_id);
	CHECK(sys.count() == 0);
}

TEST_CASE("ItemDropSystem: a per-drop override wins over the system default") {
	ItemDropSystem sys(/*pickup_radius*/ 1.0, /*lifetime*/ 120.0);
	// A magnet-radius drop: overrides the 1.0 default up to 10.0.
	const NetId wide = sys.spawn(
			{ 0, 0, 0 }, BlockId{ 1 }, 1, /*pickup_radius*/ 10.0);
	// A never-despawns rare drop: overrides the 120s default lifetime.
	const NetId eternal = sys.spawn({ 50, 0, 0 }, BlockId{ 2 }, 1,
			/*pickup_radius*/ std::nullopt, /*lifetime_seconds*/ 99999.0);

	// Far outside the *default* radius but inside the override.
	auto picked = sys.tick(0.05, { { NetId{ 1 }, Vec3d{ 5, 0, 0 } } });
	REQUIRE(picked.pickups.size() == 1);
	CHECK(picked.removed[0] == wide);

	// Way past the system's 120s default lifetime, but not the override.
	auto survived = sys.tick(200.0, {});
	CHECK(survived.removed.empty());
	CHECK(sys.count() == 1);
	CHECK(sys.drops().at(eternal).item == BlockId{ 2 });
}

TEST_CASE("ItemDropSystem: multiple drops tracked independently") {
	ItemDropSystem sys(/*pickup_radius*/ 0.5, /*lifetime*/ 120.0);
	sys.spawn({ 0, 0, 0 }, BlockId{ 1 }, 1);
	sys.spawn({ 100, 0, 0 }, BlockId{ 2 }, 1);
	CHECK(sys.count() == 2);

	auto result = sys.tick(0.05, { { NetId{ 1 }, Vec3d{ 0, 0, 0 } } });
	REQUIRE(result.pickups.size() == 1);
	CHECK(result.pickups[0].item == BlockId{ 1 });
	CHECK(sys.count() == 1); // the far one survives
}

TEST_CASE("ItemDropSystem: pickup range is measured to the player's body, not their feet") {
	ItemDropSystem sys(/*pickup_radius*/ 1.5, /*lifetime*/ 120.0);
	// 1.3 above the feet and 1 block ahead: ~1.64 from the feet (out of
	// range) but only 1.0 from the body segment.
	sys.spawn({ 1.0, 1.3, 0.0 }, BlockId{ 3 }, 1);
	auto r = sys.tick(0.05, { { NetId{ 1 }, Vec3d{ 0, 0, 0 } } }, 1.8);
	CHECK(r.pickups.size() == 1);
}

TEST_CASE("ItemDropSystem: a drop above the player's head stays out of range") {
	ItemDropSystem sys(/*pickup_radius*/ 1.5, /*lifetime*/ 120.0);
	sys.spawn({ 0.0, 4.0, 0.0 }, BlockId{ 3 }, 1); // 2.2 above the head
	auto r = sys.tick(0.05, { { NetId{ 1 }, Vec3d{ 0, 0, 0 } } }, 1.8);
	CHECK(r.pickups.empty());
}

namespace {
struct FloorAtZero final : BlockSolidQuery {
	vb::core::BlockId block_at(vb::core::IVec3 v) const override {
		return v.y <= 0 ? BlockId{ 1 } : BlockId::kAir;
	}
	bool solid_at(vb::core::IVec3 v) const override { return v.y <= 0; }
};
} // namespace

TEST_CASE("ItemDropSystem: a drop falls and rests on the floor") {
	ItemDropSystem sys;
	FloorAtZero floor;
	const NetId id = sys.spawn({ 0.5, 5.5, 0.5 }, BlockId{ 1 }, 1);
	bool moved = false;
	for (int i = 0; i < 100; ++i) {
		moved |= !sys.tick(0.05, {}, 1.8, &floor).moved.empty();
	}
	CHECK(moved);
	// Rests with its bottom face on the floor's top (y = 1).
	CHECK(sys.drops().at(id).pos.y == doctest::Approx(1.15));
	CHECK(sys.tick(0.05, {}, 1.8, &floor).moved.empty());
}
