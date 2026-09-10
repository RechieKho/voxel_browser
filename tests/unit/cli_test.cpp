#include <doctest/doctest.h>

#include <array>

#include "vb/core/build_info.hpp"
#include "vb/core/cli.hpp"

namespace {

vb::core::Args make_args(std::initializer_list<const char *> argv) {
	std::vector<char *> v;
	for (const char *s : argv) {
		v.push_back(const_cast<char *>(s));
	}
	return vb::core::Args(static_cast<int>(v.size()), v.data());
}

} // namespace

TEST_CASE("Args parses flags, values and positionals") {
	const auto args = make_args(
			{ "prog", "--headless", "--port", "27016", "--name=Alice", "map1" });

	CHECK(args.has("headless"));
	CHECK_FALSE(args.has("verbose"));
	CHECK(args.value("port").value() == "27016");
	CHECK(args.int_or("port", 0) == 27016);
	CHECK(args.value("name").value() == "Alice");
	CHECK(args.value_or("missing", "def") == "def");
	CHECK(args.int_or("missing", 42) == 42);
	REQUIRE(args.positional().size() == 1);
	CHECK(args.positional().front() == "map1");
}

TEST_CASE("Args short flags and bad integers") {
	const auto args = make_args({ "prog", "-v", "--port", "abc" });
	CHECK(args.has("version", 'v'));
	CHECK(args.int_or("port", 27015) == 27015);
}

TEST_CASE("describe_build is non-empty and mentions the project") {
	const std::string s = vb::core::describe_build();
	CHECK(s.find("voxel_browser") != std::string::npos);
	CHECK(s.find("protocol") != std::string::npos);
}

TEST_CASE("protocol version is set") {
	CHECK(vb::kEngineProtocolVersion >= 1);
}
