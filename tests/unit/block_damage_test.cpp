#include <doctest/doctest.h>

#include "vb/world/block_damage.hpp"

using namespace vb::world;
using vb::core::IVec3;
using vb::core::NetId;

TEST_CASE("BlockDamageSystem: no damage_tick_fn means damage never accrues") {
	BlockDamageSystem sys;
	sys.begin({ 1, 2, 3 }, NetId{ 1 }, 10, /*tick*/ 0);
	const auto r = sys.tick(1, {}, {});
	CHECK(r.completed.empty());
	CHECK(r.changed.empty());
	CHECK(sys.states().at({ 1, 2, 3 }).damage == 0.0f);
}

TEST_CASE("BlockDamageSystem: a single contributor accrues damage and completes at max") {
	BlockDamageSystem sys;
	const IVec3 pos{ 0, 0, 0 };
	sys.begin(pos, NetId{ 1 }, 3, /*tick*/ 0);

	auto delta = [](IVec3, NetId, std::uint16_t) { return 1.0f; };
	auto t1 = sys.tick(1, delta, {});
	CHECK(t1.changed == std::vector<IVec3>{ pos });
	CHECK(t1.completed.empty());

	auto t2 = sys.tick(2, delta, {});
	CHECK(t2.changed == std::vector<IVec3>{ pos });

	auto t3 = sys.tick(3, delta, {});
	REQUIRE(t3.completed.size() == 1);
	CHECK(t3.completed[0].pos == pos);
	CHECK(t3.completed[0].contributor == NetId{ 1 });
	CHECK(sys.states().empty()); // completed entries are forgotten
}

TEST_CASE("BlockDamageSystem: two contributors sum their deltas the same tick") {
	BlockDamageSystem sys;
	const IVec3 pos{ 0, 0, 0 };
	sys.begin(pos, NetId{ 1 }, 4, 0);
	sys.begin(pos, NetId{ 2 }, 4, 0);

	auto delta = [](IVec3, NetId, std::uint16_t) { return 2.0f; };
	auto t1 = sys.tick(1, delta, {});
	REQUIRE(t1.completed.size() == 1); // 2 + 2 == max_damage in one tick
	CHECK(t1.completed[0].pos == pos);
}

TEST_CASE("BlockDamageSystem: stop removes a contributor without erasing accrued damage") {
	BlockDamageSystem sys;
	const IVec3 pos{ 0, 0, 0 };
	sys.begin(pos, NetId{ 1 }, 10, 0);
	auto delta = [](IVec3, NetId, std::uint16_t) { return 1.0f; };
	sys.tick(1, delta, {});
	CHECK(sys.states().at(pos).damage == 1.0f);

	sys.stop(pos, NetId{ 1 });
	auto t2 = sys.tick(2, delta, {}); // no contributors left -> no delta
	CHECK(sys.states().at(pos).damage == 1.0f);
	CHECK(t2.changed.empty());
}

TEST_CASE("BlockDamageSystem: damage returning to 0 with no contributors clears the entry") {
	BlockDamageSystem sys;
	const IVec3 pos{ 0, 0, 0 };
	sys.begin(pos, NetId{ 1 }, 10, 0);
	auto delta = [](IVec3, NetId, std::uint16_t) { return 1.0f; };
	sys.tick(1, delta, {});
	sys.stop(pos, NetId{ 1 });

	auto heal_to_zero = [](IVec3, float, std::uint16_t, std::uint64_t) -> std::optional<float> {
		return 0.0f;
	};
	auto t2 = sys.tick(2, {}, heal_to_zero);
	REQUIRE(t2.cleared.size() == 1);
	CHECK(t2.cleared[0] == pos);
	CHECK(sys.states().empty());
}

TEST_CASE("BlockDamageSystem: health_tick_fn's ticks_since_last_hit reflects idle time") {
	BlockDamageSystem sys;
	const IVec3 pos{ 0, 0, 0 };
	sys.begin(pos, NetId{ 1 }, 10, /*tick*/ 5);

	std::uint64_t observed_idle = 0;
	auto observe = [&](IVec3, float, std::uint16_t, std::uint64_t idle) -> std::optional<float> {
		observed_idle = idle;
		return std::nullopt;
	};
	sys.tick(8, {}, observe);
	CHECK(observed_idle == 3); // 8 - 5
}

TEST_CASE("BlockDamageSystem: remove_player drops them from every pos they contribute to") {
	BlockDamageSystem sys;
	sys.begin({ 0, 0, 0 }, NetId{ 1 }, 10, 0);
	sys.begin({ 1, 0, 0 }, NetId{ 1 }, 10, 0);
	sys.remove_player(NetId{ 1 });

	auto heal_to_zero = [](IVec3, float, std::uint16_t, std::uint64_t) -> std::optional<float> {
		return 0.0f;
	};
	auto r = sys.tick(1, {}, heal_to_zero);
	CHECK(r.cleared.empty()); // damage was already 0, so "before" == 0 too
	CHECK(sys.states().empty());
}
