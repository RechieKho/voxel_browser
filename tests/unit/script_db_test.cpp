#include <doctest/doctest.h>

#include <filesystem>
#include <random>
#include <string>

#include "vb/script/db.hpp"

using namespace vb::script;

namespace {

std::filesystem::path temp_db_dir() {
	std::mt19937 rng(std::random_device{}());
	const std::filesystem::path p = std::filesystem::temp_directory_path() /
			("vb_script_db_test_" + std::to_string(rng()));
	std::filesystem::remove_all(p);
	return p;
}

} // namespace

TEST_CASE("ScriptDb: get on a missing key returns nullopt") {
	const std::filesystem::path dir = temp_db_dir();
	ScriptDb db(dir);
	CHECK_FALSE(db.get("nope").has_value());
	std::filesystem::remove_all(dir);
}

TEST_CASE("ScriptDb: set then get round-trips the value") {
	const std::filesystem::path dir = temp_db_dir();
	ScriptDb db(dir);
	db.set("user:alice", "{\"level\":3}");
	const auto v = db.get("user:alice");
	REQUIRE(v.has_value());
	CHECK(*v == "{\"level\":3}");
	std::filesystem::remove_all(dir);
}

TEST_CASE("ScriptDb: set overwrites an existing key") {
	const std::filesystem::path dir = temp_db_dir();
	ScriptDb db(dir);
	db.set("k", "first");
	db.set("k", "second");
	const auto v = db.get("k");
	REQUIRE(v.has_value());
	CHECK(*v == "second");
	std::filesystem::remove_all(dir);
}

TEST_CASE("ScriptDb: delete removes the key") {
	const std::filesystem::path dir = temp_db_dir();
	ScriptDb db(dir);
	db.set("k", "v");
	REQUIRE(db.get("k").has_value());
	db.erase("k");
	CHECK_FALSE(db.get("k").has_value());
	std::filesystem::remove_all(dir);
}

TEST_CASE("ScriptDb: delete on a missing key is a harmless no-op") {
	const std::filesystem::path dir = temp_db_dir();
	ScriptDb db(dir);
	db.erase("never-set");
	CHECK_FALSE(db.get("never-set").has_value());
	std::filesystem::remove_all(dir);
}

TEST_CASE("ScriptDb: distinct keys don't collide") {
	const std::filesystem::path dir = temp_db_dir();
	ScriptDb db(dir);
	db.set("session:a", "1");
	db.set("session:b", "2");
	CHECK(*db.get("session:a") == "1");
	CHECK(*db.get("session:b") == "2");
	std::filesystem::remove_all(dir);
}

TEST_CASE("ScriptDb: persists across instances over the same directory") {
	const std::filesystem::path dir = temp_db_dir();
	{
		ScriptDb db(dir);
		db.set("k", "persisted");
	}
	{
		ScriptDb db(dir);
		const auto v = db.get("k");
		REQUIRE(v.has_value());
		CHECK(*v == "persisted");
	}
	std::filesystem::remove_all(dir);
}
