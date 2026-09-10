#include <doctest/doctest.h>

#include <ostream>

#include <string>

#include "vb/core/error.hpp"
#include "vb/core/ids.hpp"
#include "vb/core/log.hpp"
#include "vb/core/math.hpp"
#include "vb/core/result.hpp"

using namespace vb::core;

TEST_CASE("Result carries a value or an error, never both") {
	Result<int, CoreError> ok = 42;
	REQUIRE(ok);
	CHECK(*ok == 42);
	CHECK(ok.value_or(0) == 42);

	Result<int, CoreError> bad = Err{ CoreError::kNotFound };
	REQUIRE_FALSE(bad);
	CHECK(bad.error() == CoreError::kNotFound);
	CHECK(bad.value_or(7) == 7);
}

TEST_CASE("Result<move-only> moves cleanly") {
	Result<std::string, CoreError> r = std::string("hello");
	Result<std::string, CoreError> moved = std::move(r);
	REQUIRE(moved);
	CHECK(*moved == "hello");
}

TEST_CASE("Status<void>") {
	Status<CoreError> good;
	CHECK(good);
	Status<CoreError> bad = Err{ CoreError::kIoError };
	CHECK_FALSE(bad);
	CHECK(bad.error() == CoreError::kIoError);
}

TEST_CASE("error messages are stable and total") {
	CHECK(message(CoreError::kParseError) == "parse error");
	CHECK(message(ProtocolError::kShortBuffer) == "buffer underrun");
}

TEST_CASE("chunk coordinate math handles negatives") {
	CHECK(floor_div(-1, 32) == -1);
	CHECK(floor_mod(-1, 32) == 31);

	const IVec3 block{ -1, 5, 40 };
	CHECK(chunk_of(block) == ChunkCoord{ -1, 0, 1 });
	const IVec3 l = local_of(block);
	CHECK(l == IVec3{ 31, 5, 8 });

	ChunkCoord c{ -2, 0, 3 };
	CHECK(std::hash<ChunkCoord>{}(c) == std::hash<ChunkCoord>{}(ChunkCoord{ -2, 0, 3 }));
}

TEST_CASE("AABB intersection") {
	AABB a{ { 0, 0, 0 }, { 2, 2, 2 } };
	AABB b{ { 1, 1, 1 }, { 3, 3, 3 } };
	AABB c{ { 2, 2, 2 }, { 4, 4, 4 } };
	CHECK(a.intersects(b));
	CHECK_FALSE(a.intersects(c)); // half-open: touching faces don't intersect
	CHECK(a.translated({ 1.5, 2.0, 2.0 }).intersects(c));
}

TEST_CASE("logger routes to sinks and respects the level") {
	vb::core::log::clear_sinks();
	int hits = 0;
	LogLevel last = LogLevel::kOff;
	const auto id = vb::core::log::add_sink([&](const LogRecord &r) {
		++hits;
		last = r.level;
		CHECK(r.category == "test");
	});

	vb::core::log::set_level(LogLevel::kWarn);
	VB_INFO("test", "suppressed ", 1);
	CHECK(hits == 0);
	VB_ERROR("test", "boom ", 2, '/', 3);
	CHECK(hits == 1);
	CHECK(last == LogLevel::kError);

	vb::core::log::remove_sink(id);
	vb::core::log::set_level(LogLevel::kInfo);
	vb::core::log::reset_to_default_sink();
}
