#include <doctest/doctest.h>

#include <string>

#include "vb/core/version_req.hpp"

using vb::core::SemVer;
using vb::core::VersionReq;

namespace {

bool matches(const char *req, SemVer v) {
	std::string err;
	const auto r = VersionReq::parse(req, &err);
	INFO("req \"" << req << "\": " << err);
	REQUIRE(r.has_value());
	return r->matches(v);
}

} // namespace

TEST_CASE("parse_semver fills missing parts and rejects junk") {
	CHECK(*vb::core::parse_semver("1.2.3") == SemVer{ 1, 2, 3 });
	CHECK(*vb::core::parse_semver("v0.5") == SemVer{ 0, 5, 0 });
	CHECK(*vb::core::parse_semver("2") == SemVer{ 2, 0, 0 });
	CHECK_FALSE(vb::core::parse_semver("").has_value());
	CHECK_FALSE(vb::core::parse_semver("1.2.3.4").has_value());
	CHECK_FALSE(vb::core::parse_semver("1.x").has_value());
	CHECK_FALSE(vb::core::parse_semver("0.5.0-3-gabc").has_value());
}

TEST_CASE("VersionReq comparators") {
	CHECK(matches("*", { 9, 9, 9 }));
	CHECK(matches(">=0.5.0", { 0, 5, 0 }));
	CHECK_FALSE(matches(">=0.5.0", { 0, 4, 9 }));
	CHECK_FALSE(matches(">0.5.0", { 0, 5, 0 }));
	CHECK(matches(">0.5.0", { 0, 5, 1 }));
	CHECK(matches("<=0.7", { 0, 7, 0 }));
	CHECK_FALSE(matches("<=0.7", { 0, 7, 1 }));
	CHECK(matches("<0.7", { 0, 6, 99 }));
	CHECK_FALSE(matches("<0.7", { 0, 7, 0 }));
	CHECK(matches("=0.5.2", { 0, 5, 2 }));
	CHECK_FALSE(matches("=0.5.2", { 0, 5, 3 }));
}

TEST_CASE("VersionReq AND, caret and tilde") {
	CHECK(matches(">=0.5.0, <0.7.0", { 0, 6, 1 }));
	CHECK_FALSE(matches(">=0.5.0, <0.7.0", { 0, 7, 0 }));
	CHECK(matches("^0.5.1", { 0, 5, 9 }));
	CHECK_FALSE(matches("^0.5.1", { 0, 6, 0 }));
	CHECK_FALSE(matches("^0.5.1", { 0, 5, 0 }));
	CHECK(matches("^1.2.3", { 1, 9, 0 }));
	CHECK_FALSE(matches("^1.2.3", { 2, 0, 0 }));
	CHECK(matches("~0.5.1", { 0, 5, 7 }));
	CHECK_FALSE(matches("~0.5.1", { 0, 6, 0 }));
	CHECK(matches("^0.0.3", { 0, 0, 3 }));
	CHECK_FALSE(matches("^0.0.3", { 0, 0, 4 }));
}

TEST_CASE("VersionReq rejects malformed text (fail closed)") {
	for (const char *bad : { "", "   ", ">=", ">=x", "0.5", ">=0.5.0,", ",>=0.5.0", ">=1.2.3.4", "latest", ">=0.5 || >=1.0" }) {
		std::string err;
		INFO("text: \"" << bad << "\"");
		CHECK_FALSE(VersionReq::parse(bad, &err).has_value());
		CHECK_FALSE(err.empty());
	}
}

TEST_CASE("VersionReq normalises and reports its lower bound") {
	CHECK(VersionReq::parse("^0.5.1")->to_string() == ">=0.5.1, <0.6.0");
	CHECK(VersionReq::parse("*")->to_string() == "*");
	CHECK(VersionReq::parse("*")->is_any());
	CHECK(*VersionReq::parse(">=0.6.0, <0.7.0")->lower_bound() == SemVer{ 0, 6, 0 });
	CHECK_FALSE(VersionReq::parse("<0.7.0")->lower_bound().has_value());
}

TEST_CASE("check_engine_req covers missing, bad, mismatched and satisfied fields") {
	const SemVer engine{ 0, 5, 2 };
	auto r = vb::core::check_engine_req("p", std::nullopt, engine);
	CHECK(r.ok);
	CHECK(r.warning);

	r = vb::core::check_engine_req("p", std::string("nope"), engine);
	CHECK_FALSE(r.ok);
	CHECK(r.message.find("invalid engine_version_req") != std::string::npos);

	r = vb::core::check_engine_req("my_pack", std::string(">=0.6.0"), engine);
	CHECK_FALSE(r.ok);
	CHECK(r.message == "pack 'my_pack' requires engine >=0.6.0 (pack.toml engine_version_req); this build is 0.5.2");

	r = vb::core::check_engine_req("p", std::string("^0.5.0"), engine);
	CHECK(r.ok);
	CHECK_FALSE(r.warning);
	CHECK(r.message.empty());
}
