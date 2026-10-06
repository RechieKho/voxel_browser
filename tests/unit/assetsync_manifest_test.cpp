#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <fstream>
#include <string>

#include "vb/assetsync/manifest.hpp"

#if !VB_WITH_COMPRESSION

TEST_CASE("build_manifest reports kDisabled when built without VB_WITH_COMPRESSION") {
	const auto r = vb::assetsync::build_manifest(".");
	CHECK_FALSE(r);
	CHECK(r.error() == vb::core::AssetSyncError::kDisabled);
}

#else

using vb::assetsync::AssetKind;
using vb::assetsync::build_manifest;
using vb::assetsync::Manifest;

namespace {

std::filesystem::path make_pack(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_manifest_test_") + name);
	std::filesystem::remove_all(p);
	std::filesystem::create_directories(p / "scripts");
	std::filesystem::create_directories(p / "ui");
	{
		std::ofstream f(p / "scripts" / "init.lua", std::ios::binary);
		f << "print('hello')";
	}
	{
		std::ofstream f(p / "ui" / "menu.toml", std::ios::binary);
		f << "[menu]\n";
	}
	return p;
}

} // namespace

TEST_CASE("build_manifest is deterministic across repeated runs") {
	const auto pack = make_pack("determinism");
	const auto m1 = build_manifest(pack);
	const auto m2 = build_manifest(pack);
	REQUIRE(m1);
	REQUIRE(m2);
	CHECK(m1->manifest_hash.lo == m2->manifest_hash.lo);
	CHECK(m1->manifest_hash.hi == m2->manifest_hash.hi);
	REQUIRE(m1->entries.size() == m2->entries.size());
	for (std::size_t i = 0; i < m1->entries.size(); ++i) {
		CHECK(m1->entries[i].path == m2->entries[i].path);
	}
	CHECK(m1->entries[0].path < m1->entries[1].path); // sorted
	std::filesystem::remove_all(pack);
}

TEST_CASE("build_manifest classifies asset kinds and is content-sensitive") {
	const auto pack = make_pack("content");
	const auto m1 = build_manifest(pack);
	REQUIRE(m1);
	const auto *script = m1->find(m1->entries[0].hash); // any lookup sanity check
	REQUIRE(script != nullptr);

	bool saw_script = false, saw_ui = false;
	for (const auto &e : m1->entries) {
		if (e.path == "scripts/init.lua") {
			CHECK(e.kind == AssetKind::kScript);
			saw_script = true;
		}
		if (e.path == "ui/menu.toml") {
			CHECK(e.kind == AssetKind::kUi);
			saw_ui = true;
		}
	}
	CHECK(saw_script);
	CHECK(saw_ui);

	{
		std::ofstream f(pack / "scripts" / "init.lua", std::ios::binary | std::ios::app);
		f << " -- changed";
	}
	const auto m2 = build_manifest(pack);
	REQUIRE(m2);
	CHECK((m1->manifest_hash.lo != m2->manifest_hash.lo ||
			m1->manifest_hash.hi != m2->manifest_hash.hi));
	std::filesystem::remove_all(pack);
}

TEST_CASE("build_manifest rejects a symlink escaping the pack root") {
	const auto pack = make_pack("symlink");
	const auto outside = std::filesystem::temp_directory_path() / "vb_manifest_outside.txt";
	{
		std::ofstream f(outside, std::ios::binary);
		f << "secret";
	}
	std::error_code ec;
	std::filesystem::create_symlink(outside, pack / "escape.txt", ec);
	if (ec) {
		// Symlink creation can require elevated privileges / Developer Mode
		// on Windows -- skip rather than fail CI for an environment limit.
		std::filesystem::remove_all(pack);
		std::filesystem::remove(outside);
		return;
	}

	const auto m = build_manifest(pack);
	CHECK_FALSE(m);
	CHECK(m.error() == vb::core::AssetSyncError::kPathEscape);

	std::filesystem::remove_all(pack);
	std::filesystem::remove(outside);
}

TEST_CASE("build_manifest enforces per-file and total size caps") {
	const auto pack = make_pack("caps");
	{
		std::ofstream f(pack / "scripts" / "init.lua", std::ios::binary);
		f << std::string(1024, 'x');
	}

	const auto too_small_file = build_manifest(pack, { /*max_file_bytes*/ 100, 1'000'000 });
	CHECK_FALSE(too_small_file);
	CHECK(too_small_file.error() == vb::core::AssetSyncError::kFileTooLarge);

	const auto too_small_total = build_manifest(pack, { 1'000'000, /*max_total_bytes*/ 100 });
	CHECK_FALSE(too_small_total);
	CHECK(too_small_total.error() == vb::core::AssetSyncError::kPackTooLarge);

	std::filesystem::remove_all(pack);
}

TEST_CASE("build_manifest skips developer tooling files (.vb/, dotfiles, root README/AGENTS)") {
	const auto pack = make_pack("devfiles");
	std::filesystem::create_directories(pack / ".vb" / "lua");
	for (const char *rel : { ".vb/lua/vb.lua", ".luarc.json", ".gitignore", "README.md", "AGENTS.md",
				 "scripts/README.md" }) {
		std::ofstream f(pack / rel, std::ios::binary);
		f << "x";
	}
	const auto m = build_manifest(pack);
	REQUIRE(m);
	for (const auto &e : m->entries) {
		CHECK(e.path.rfind(".", 0) != 0);
		CHECK(e.path != "README.md");
		CHECK(e.path != "AGENTS.md");
	}
	bool nested_readme = false;
	for (const auto &e : m->entries) {
		nested_readme = nested_readme || e.path == "scripts/README.md";
	}
	CHECK(nested_readme); // only the pack-root docs are skipped
	std::filesystem::remove_all(pack);
}

TEST_CASE("build_manifest never advertises the pack-root auth.lua") {
	const auto pack = make_pack("auth_lua");
	{
		std::ofstream f(pack / "auth.lua", std::ios::binary);
		f << "return { provider = 'oidc' }";
	}
	// Only the pack ROOT file is server-only config; a same-named file in a
	// subdirectory is ordinary content.
	{
		std::ofstream f(pack / "scripts" / "auth.lua", std::ios::binary);
		f << "print('not the declaration')";
	}
	const auto m = build_manifest(pack);
	REQUIRE(m);
	bool saw_root = false, saw_nested = false;
	for (const auto &e : m->entries) {
		saw_root = saw_root || e.path == "auth.lua";
		saw_nested = saw_nested || e.path == "scripts/auth.lua";
	}
	CHECK_FALSE(saw_root);
	CHECK(saw_nested);
	std::filesystem::remove_all(pack);
}

#endif // VB_WITH_COMPRESSION
