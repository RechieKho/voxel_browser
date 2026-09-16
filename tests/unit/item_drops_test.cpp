#include <doctest/doctest.h>

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
