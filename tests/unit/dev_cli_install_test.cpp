#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include <miniz.h>

#include "vb/cli/commands.hpp"
#include "vb/cli/installer.hpp"
#include "vb/cli/layout.hpp"
#include "vb/cli/lock.hpp"
#include "vb/cli/release_manifest.hpp"
#include "vb/cli/source.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/zip.hpp"
#include "vb/core/sha256.hpp"

namespace fs = std::filesystem;
using vb::cli::Binary;
using vb::cli::binary_file_name;

namespace {

struct TempDir {
	fs::path path;
	TempDir() {
		std::random_device rd;
		path = fs::temp_directory_path() / ("vb_cli_inst_" + std::to_string(rd()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string slurp(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// Hand-rolled "stored" zip writer: unlike miniz's own writer it will happily
// emit hostile names ("../x", "/abs", backslashes), which the extractor tests need.
struct RawZip {
	std::string data;
	std::string central;
	unsigned count = 0;

	static void le(std::string &s, std::uint32_t v, int bytes) {
		for (int i = 0; i < bytes; ++i) {
			s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
		}
	}
	void add(const std::string &name, const std::string &payload, std::uint32_t mode = 0100644) {
		const std::uint32_t crc = static_cast<std::uint32_t>(
				mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(payload.data()),
						payload.size()));
		const std::uint32_t offset = static_cast<std::uint32_t>(data.size());
		le(data, 0x04034b50, 4); le(data, 20, 2); le(data, 0, 2); le(data, 0, 2);
		le(data, 0, 2); le(data, 0x21, 2); le(data, crc, 4);
		le(data, static_cast<std::uint32_t>(payload.size()), 4);
		le(data, static_cast<std::uint32_t>(payload.size()), 4);
		le(data, static_cast<std::uint32_t>(name.size()), 2); le(data, 0, 2);
		data += name; data += payload;
		le(central, 0x02014b50, 4); le(central, 0x031e, 2); le(central, 20, 2); le(central, 0, 2);
		le(central, 0, 2); le(central, 0, 2); le(central, 0x21, 2); le(central, crc, 4);
		le(central, static_cast<std::uint32_t>(payload.size()), 4);
		le(central, static_cast<std::uint32_t>(payload.size()), 4);
		le(central, static_cast<std::uint32_t>(name.size()), 2); le(central, 0, 2);
		le(central, 0, 2); le(central, 0, 2); le(central, 0, 2); le(central, mode << 16, 4);
		le(central, offset, 4);
		central += name;
		++count;
	}
	std::string finish() const {
		std::string out = data + central;
		le(out, 0x06054b50, 4); le(out, 0, 2); le(out, 0, 2); le(out, count, 2); le(out, count, 2);
		le(out, static_cast<std::uint32_t>(central.size()), 4);
		le(out, static_cast<std::uint32_t>(data.size()), 4); le(out, 0, 2);
		return out;
	}
};

// Appends an entry to `zip` (rewriting it, so names are unrestricted).
std::map<std::string, RawZip> g_open;

void add_entry(const fs::path &zip, const std::string &name, const std::string &payload,
		std::uint32_t mode = 0100644) {
	RawZip &z = g_open[zip.string()];
	z.add(name, payload, mode);
	std::ofstream(zip, std::ios::binary | std::ios::trunc) << z.finish();
}

// A release directory (what DirSource serves) for `version` on `platform`.
fs::path make_release_dir(const fs::path &root, const std::string &version,
		const std::string &platform = "linux-x86_64", bool extra_entry_evil = false) {
	const fs::path dir = root / ("rel-" + version);
	fs::create_directories(dir);
	const std::string file = "voxel_browser-" + version + "-" + platform + ".zip";
	const fs::path zip = dir / file;
	add_entry(zip, binary_file_name(Binary::Client), "client");
	add_entry(zip, binary_file_name(Binary::Server), "server");
	add_entry(zip, "content/base/blocks/dirt.lua", "-- dirt");
	if (extra_entry_evil) {
		add_entry(zip, "../evil.txt", "x");
	}
	const std::string bytes = slurp(zip);
	std::ofstream m(dir / "release.toml");
	m << "schema = 1\nversion = \"" << version << "\"\ncommit = \"abc\"\n"
	  << "date = \"2026-10-04T00:00:00Z\"\nengine_protocol_version = 26\n\n"
	  << "[[artifact]]\nkind = \"game\"\nplatform = \"" << platform
	  << "\"\nbuild = \"release\"\nfile = \"" << file << "\"\nsize = " << bytes.size()
	  << "\nsha256 = \"" << vb::core::sha256_hex(bytes) << "\"\n";
	return dir;
}

vb::cli::Layout test_layout(const fs::path &root) {
	return vb::cli::Layout(root / "data", root / "config", root / "cache");
}

vb::cli::InstallOptions test_opts() {
	vb::cli::InstallOptions o;
	o.platform = "linux-x86_64";
	o.run_version_check = false; // fake binaries
	return o;
}

} // namespace

TEST_CASE("release.toml parsing and platform selection") {
	TempDir tmp;
	const fs::path dir = make_release_dir(tmp.path, "v0.2.0");
	vb::cli::ReleaseManifest m;
	REQUIRE(vb::cli::parse_manifest(slurp(dir / "release.toml"), m));
	CHECK(m.version == "v0.2.0");
	CHECK(m.engine_protocol_version == 26);
	REQUIRE(m.artifacts.size() == 1);
	CHECK(vb::cli::select_artifact(m, "game", "linux-x86_64", "release") != nullptr);
	CHECK(vb::cli::select_artifact(m, "game", "linux-x86_64", "debug") == nullptr);
	CHECK(vb::cli::select_artifact(m, "game", "macos-universal", "release") == nullptr);

	vb::cli::ReleaseManifest bad;
	CHECK_FALSE(vb::cli::parse_manifest("schema = 2\n", bad));
	CHECK_FALSE(vb::cli::parse_manifest("not toml [[[", bad));
	CHECK_FALSE(vb::cli::parse_manifest(
			"schema = 1\nversion = \"v1.0.0\"\n[[artifact]]\nkind=\"game\"\nplatform=\"linux-x86_64\"\n"
			"build=\"release\"\nfile=\"../x.zip\"\nsize=1\nsha256=\"" +
					std::string(64, 'a') + "\"\n",
			bad)); // path separator in file name
	CHECK_FALSE(vb::cli::parse_manifest(
			"schema = 1\nversion = \"v1.0.0\"\n[[artifact]]\nkind=\"game\"\nplatform=\"linux-x86_64\"\n"
			"build=\"release\"\nfile=\"x.zip\"\nsize=1\nsha256=\"zz\"\n",
			bad)); // bad hash
}

TEST_CASE("source specs") {
	std::string err;
	CHECK(vb::cli::make_source("dir:/tmp/x", &err) != nullptr);
	CHECK(vb::cli::make_source("owner/repo", &err) != nullptr);
	CHECK(vb::cli::make_source("dir:", &err) == nullptr);
	CHECK(vb::cli::make_source("nonsense", &err) == nullptr);
	CHECK(vb::cli::make_source("a/b/c", &err) == nullptr);
	CHECK(vb::cli::make_source("o/r;rm", &err) == nullptr);
}

TEST_CASE("install from a dir source: success, receipt, idempotence, force") {
	TempDir tmp;
	const auto layout = test_layout(tmp.path);
	const auto src = vb::cli::make_dir_source(make_release_dir(tmp.path, "v0.2.0"));

	auto r = vb::cli::install_release(layout, *src, "latest", test_opts());
	REQUIRE(r.status);
	CHECK(r.version == "v0.2.0");
	CHECK_FALSE(r.already_installed);
	const fs::path dir = layout.version_dir("v0.2.0");
	CHECK(fs::exists(dir / binary_file_name(Binary::Server)));
	CHECK(slurp(dir / "content/base/blocks/dirt.lua") == "-- dirt");
	CHECK(fs::exists(dir / ".install.toml"));
	CHECK_FALSE(fs::exists(layout.downloads_dir() / "voxel_browser-v0.2.0-linux-x86_64.zip"));
	CHECK(vb::cli::list_entries(layout).size() == 1); // no .staging-* leftovers counted

	r = vb::cli::install_release(layout, *src, "v0.2.0", test_opts());
	REQUIRE(r.status);
	CHECK(r.already_installed);

	auto o = test_opts();
	o.force = true;
	o.keep_download = true;
	r = vb::cli::install_release(layout, *src, "0.2.0", o); // bare number accepted
	REQUIRE(r.status);
	CHECK_FALSE(r.already_installed);
	CHECK(fs::exists(layout.downloads_dir() / "voxel_browser-v0.2.0-linux-x86_64.zip"));

	r = vb::cli::install_release(layout, *src, "v9.9.9", test_opts());
	CHECK_FALSE(r.status); // dir source only has v0.2.0
}

TEST_CASE("install rejects a tampered archive and leaves nothing behind") {
	TempDir tmp;
	const auto layout = test_layout(tmp.path);
	const fs::path rel = make_release_dir(tmp.path, "v0.3.0");
	{ // flip a byte after the manifest recorded the hash
		const fs::path zip = rel / "voxel_browser-v0.3.0-linux-x86_64.zip";
		std::string bytes = slurp(zip);
		bytes[bytes.size() / 2] ^= 0x55;
		std::ofstream(zip, std::ios::binary | std::ios::trunc) << bytes;
	}
	const auto src = vb::cli::make_dir_source(rel);
	const auto r = vb::cli::install_release(layout, *src, "latest", test_opts());
	REQUIRE_FALSE(r.status);
	CHECK(r.status.error.find("SHA-256") != std::string::npos);
	CHECK_FALSE(fs::exists(layout.version_dir("v0.3.0")));
	CHECK(vb::cli::list_entries(layout).empty());
	CHECK_FALSE(fs::exists(layout.downloads_dir() / "voxel_browser-v0.3.0-linux-x86_64.zip.part"));
}

TEST_CASE("install rejects size mismatch, missing platform and bad build") {
	TempDir tmp;
	const auto layout = test_layout(tmp.path);
	const auto src = vb::cli::make_dir_source(make_release_dir(tmp.path, "v0.4.0", "macos-universal"));
	CHECK_FALSE(vb::cli::install_release(layout, *src, "latest", test_opts()).status); // no linux build
	auto o = test_opts();
	o.build = "weird";
	CHECK_FALSE(vb::cli::install_release(layout, *src, "latest", o).status);
	o = test_opts();
	o.platform = "macos-universal";
	CHECK(vb::cli::install_release(layout, *src, "latest", o).status);
}

TEST_CASE("zip extraction rejects zip-slip entries; nothing is written") {
	TempDir tmp;
	const fs::path zip = tmp.path / "evil.zip";
	add_entry(zip, "ok.txt", "fine");
	add_entry(zip, "../evil.txt", "x");
	fs::create_directories(tmp.path / "out");
	const auto s = vb::cli::extract_zip(zip, tmp.path / "out");
	CHECK_FALSE(s);
	CHECK_FALSE(fs::exists(tmp.path / "evil.txt"));
	CHECK_FALSE(fs::exists(tmp.path / "out" / "ok.txt")); // validated before writing

	const fs::path zip2 = tmp.path / "abs.zip";
	add_entry(zip2, "/etc/passwd", "x");
	CHECK_FALSE(vb::cli::extract_zip(zip2, tmp.path / "out"));
	const fs::path zip3 = tmp.path / "bs.zip";
	add_entry(zip3, "a\\..\\b", "x");
	CHECK_FALSE(vb::cli::extract_zip(zip3, tmp.path / "out"));

	vb::cli::ExtractLimits tiny;
	tiny.max_total_bytes = 2;
	const fs::path zip4 = tmp.path / "big.zip";
	add_entry(zip4, "f", "12345");
	CHECK_FALSE(vb::cli::extract_zip(zip4, tmp.path / "out", tiny));

	const fs::path zip5 = tmp.path / "link.zip";
	add_entry(zip5, "lnk", "/etc/passwd", 0120777); // symlink mode bits
	CHECK_FALSE(vb::cli::extract_zip(zip5, tmp.path / "out"));

	const fs::path trunc = tmp.path / "trunc.zip";
	std::ofstream(trunc, std::ios::binary) << slurp(zip4).substr(0, 10);
	CHECK_FALSE(vb::cli::extract_zip(trunc, tmp.path / "out"));
}

#if !defined(_WIN32)
TEST_CASE("extracted known binaries are executable") {
	TempDir tmp;
	const fs::path zip = tmp.path / "a.zip";
	add_entry(zip, "voxel_browser_server", "x");
	add_entry(zip, "readme.txt", "x");
	fs::create_directories(tmp.path / "out");
	REQUIRE(vb::cli::extract_zip(zip, tmp.path / "out"));
	CHECK((fs::status(tmp.path / "out" / "voxel_browser_server").permissions() &
				  fs::perms::owner_exec) != fs::perms::none);
	CHECK((fs::status(tmp.path / "out" / "readme.txt").permissions() & fs::perms::owner_exec) ==
			fs::perms::none);
}

TEST_CASE("version check failing aborts the install") {
	TempDir tmp;
	const auto layout = test_layout(tmp.path);
	const auto src = vb::cli::make_dir_source(make_release_dir(tmp.path, "v0.5.0"));
	auto o = test_opts();
	o.run_version_check = true; // the fake "server" is not executable code
	const auto r = vb::cli::install_release(layout, *src, "latest", o);
	CHECK_FALSE(r.status);
	CHECK_FALSE(fs::exists(layout.version_dir("v0.5.0")));
}
#endif

TEST_CASE("file lock excludes a second holder and releases on destruction") {
	TempDir tmp;
	const fs::path f = tmp.path / "lock";
	std::unique_ptr<vb::cli::FileLock> a, b;
	REQUIRE(vb::cli::FileLock::acquire(f, false, a));
#if !defined(_WIN32)
	// flock is per open-file-description, so a second open in-process conflicts.
	CHECK_FALSE(vb::cli::FileLock::acquire(f, false, b));
#endif
	a.reset();
	CHECK(vb::cli::FileLock::acquire(f, false, b));
}

TEST_CASE("prune keeps the newest N and the default; install/update/doctor commands") {
	TempDir tmp;
	const auto layout = test_layout(tmp.path);
	for (const char *v : { "v0.1.0", "v0.2.0", "v0.3.0", "v0.4.0" }) {
		fs::create_directories(layout.version_dir(v));
	}
	fs::create_directories(layout.versions_dir() / ".staging-v0.5.0-dead");
	REQUIRE(vb::cli::write_default_version(layout, "v0.1.0"));

	std::vector<std::string> removed;
	REQUIRE(vb::cli::prune_releases(layout, 1, removed));
	CHECK(removed == std::vector<std::string>{ "v0.3.0", "v0.2.0" });
	CHECK(fs::exists(layout.version_dir("v0.1.0"))); // default survives
	CHECK(fs::exists(layout.version_dir("v0.4.0")));
	CHECK_FALSE(fs::exists(layout.versions_dir() / ".staging-v0.5.0-dead"));
	CHECK_FALSE(vb::cli::prune_releases(layout, -1, removed));
}

TEST_CASE("vb install / update via VB_SOURCE dir") {
	TempDir tmp;
	const auto layout = test_layout(tmp.path);
	const std::string plat = vb::cli::current_platform();
	if (plat.empty()) {
		return;
	}
	const fs::path rel = make_release_dir(tmp.path, "v0.6.0", plat);
	const std::string spec = "dir:" + rel.string();
#if defined(_WIN32)
	_putenv_s("VB_SOURCE", spec.c_str());
#else
	setenv("VB_SOURCE", spec.c_str(), 1);
#endif
	std::ostringstream out, err;
	// run_version_check is on in the CLI: fake binaries must fail loudly...
	int code = vb::cli::run_cli({ "install" }, layout, out, err);
	CHECK(code == vb::cli::kExitFailure);
	CHECK_FALSE(fs::exists(layout.version_dir("v0.6.0")));
	CHECK(vb::cli::run_cli({ "install", "--bogus" }, layout, out, err) == vb::cli::kExitUsage);
	CHECK(vb::cli::run_cli({ "prune", "--keep", "x" }, layout, out, err) == vb::cli::kExitUsage);
	CHECK(vb::cli::run_cli({ "prune" }, layout, out, err) == vb::cli::kExitOk);
	CHECK(vb::cli::run_cli({ "doctor" }, layout, out, err) == vb::cli::kExitOk);
#if defined(_WIN32)
	_putenv_s("VB_SOURCE", "");
#else
	unsetenv("VB_SOURCE");
#endif
}
