// Phase 8.6: hardening -- release signatures, service definitions, protocol
// compatibility warnings, arm64 platforms.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <miniz.h>

extern "C" {
#include "ed25519.h"
}

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h> // getpid (not pulled in transitively on macOS)
#endif

#include "vb/cli/commands.hpp"
#include "vb/cli/compat.hpp"
#include "vb/cli/installer.hpp"
#include "vb/cli/instance.hpp"
#include "vb/cli/layout.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/release_manifest.hpp"
#include "vb/cli/self_update.hpp"
#include "vb/cli/service.hpp"
#include "vb/cli/signature.hpp"
#include "vb/cli/source.hpp"
#include "vb/cli/store.hpp"
#include "vb/core/sha256.hpp"

namespace fs = std::filesystem;
using namespace vb::cli;

namespace {

struct TempDir {
	fs::path path;
	TempDir() {
		std::random_device rd;
		path = fs::temp_directory_path() / ("vb_hard_test_" + std::to_string(rd()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

Layout test_layout(const fs::path &root) {
	return Layout(root / "data", root / "config", root / "cache");
}

void write(const fs::path &p, const std::string &text) {
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

std::string slurp(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	std::ostringstream os;
	os << f.rdbuf();
	return os.str();
}

struct Run {
	int code;
	std::string out;
	std::string err;
};

Run run_vb(const Layout &layout, std::vector<std::string> args) {
	std::ostringstream out, err;
	const int code = run_cli(args, layout, out, err);
	return { code, out.str(), err.str() };
}

std::string hex(const unsigned char *p, std::size_t n) {
	static const char *digits = "0123456789abcdef";
	std::string out;
	for (std::size_t i = 0; i < n; ++i) {
		out += digits[p[i] >> 4];
		out += digits[p[i] & 15];
	}
	return out;
}

std::string base64(const unsigned char *p, std::size_t n) {
	static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (std::size_t i = 0; i < n; i += 3) {
		const unsigned v = (static_cast<unsigned>(p[i]) << 16) |
				(i + 1 < n ? static_cast<unsigned>(p[i + 1]) << 8 : 0u) |
				(i + 2 < n ? static_cast<unsigned>(p[i + 2]) : 0u);
		out += tbl[(v >> 18) & 63];
		out += tbl[(v >> 12) & 63];
		out += i + 1 < n ? tbl[(v >> 6) & 63] : '=';
		out += i + 2 < n ? tbl[v & 63] : '=';
	}
	return out;
}

// A throwaway signing identity, derived from a seed so tests are deterministic.
struct TestKey {
	unsigned char pub[32]{};
	unsigned char priv[64]{};
	explicit TestKey(unsigned char seed_byte) {
		unsigned char seed[32];
		std::fill(seed, seed + 32, seed_byte);
		ed25519_create_keypair(pub, priv, seed);
	}
	PublicKey public_key() const {
		PublicKey k{};
		std::copy(pub, pub + 32, k.begin());
		return k;
	}
	std::string public_hex() const { return hex(pub, 32); }
	std::string sign(const std::string &message) const {
		unsigned char sig[64];
		ed25519_sign(sig, reinterpret_cast<const unsigned char *>(message.data()), message.size(), pub, priv);
		return base64(sig, 64) + "\n";
	}
};

TrustPolicy policy_for(const TestKey &key, bool require = true) {
	TrustPolicy p;
	p.keys.push_back(key.public_key());
	p.require = require;
	return p;
}

std::string vb_name() {
#if defined(_WIN32)
	return "vb.exe";
#else
	return "vb";
#endif
}

void make_zip(const fs::path &zip, const std::string &name, const std::string &payload) {
	REQUIRE(mz_zip_add_mem_to_archive_file_in_place(zip.string().c_str(), name.c_str(),
					payload.data(), payload.size(), nullptr, 0, MZ_BEST_SPEED) != 0);
}

// A release dir holding both a game archive and a vb archive for `platform`,
// its manifest, and (when `key` is given) a signature over exactly those bytes.
fs::path make_release(const fs::path &root, const std::string &version, const TestKey *key,
		const std::string &platform = "linux-x86_64", const std::string &vb_payload = "NEW vb") {
	const fs::path dir = root / ("rel-" + version);
	fs::create_directories(dir);
	const std::string game = "voxel_browser-" + version + "-" + platform + ".zip";
	const std::string cli = "vb-" + version + "-" + platform + ".zip";
	make_zip(dir / game, binary_file_name(Binary::Client), "client");
	{
		mz_zip_add_mem_to_archive_file_in_place((dir / game).string().c_str(),
				binary_file_name(Binary::Server).c_str(), "server", 6, nullptr, 0, MZ_BEST_SPEED);
	}
	make_zip(dir / cli, vb_name(), vb_payload);
	const std::string game_bytes = slurp(dir / game);
	const std::string cli_bytes = slurp(dir / cli);
	std::ostringstream m;
	m << "schema = 1\nversion = \"" << version << "\"\ncommit = \"abc\"\n"
	  << "date = \"2026-10-04T00:00:00Z\"\nengine_protocol_version = 26\n\n"
	  << "[[artifact]]\nkind = \"game\"\nplatform = \"" << platform << "\"\nbuild = \"release\"\n"
	  << "file = \"" << game << "\"\nsize = " << game_bytes.size() << "\nsha256 = \""
	  << vb::core::sha256_hex(game_bytes) << "\"\n\n"
	  << "[[artifact]]\nkind = \"cli\"\nplatform = \"" << platform << "\"\nbuild = \"release\"\n"
	  << "file = \"" << cli << "\"\nsize = " << cli_bytes.size() << "\nsha256 = \""
	  << vb::core::sha256_hex(cli_bytes) << "\"\n";
	write(dir / "release.toml", m.str());
	if (key != nullptr) {
		write(dir / "release.toml.sig", key->sign(m.str()));
	}
	return dir;
}

InstallOptions install_opts(const TrustPolicy &trust) {
	InstallOptions o;
	o.platform = "linux-x86_64";
	o.run_version_check = false; // placeholder binaries
	o.trust = trust;
	return o;
}

} // namespace

// ---- signatures --------------------------------------------------------------

TEST_CASE("an OpenSSL-made Ed25519 signature verifies (interop vector)") {
	// Produced with:  openssl pkeyutl -sign -inkey k.pem -rawin -in msg   (scripts/sign_release.sh)
	const std::string message = "schema = 1\nversion = \"v1.2.3\"\n";
	const std::string sig_b64 =
			"9vQY8nfJLbwTqGq2S8NLz5hNwWMaXsWaargxSX/WzrvZ3f6jB/vGOQfnN58k5s8lopMevtsrCGJMOj2C0ty2Dg==\n";
	PublicKey key{};
	REQUIRE(parse_public_key("a328faff6f210bd40a5e4a3f54c3da78822fa478652e708701ae925c2946fb59", key));

	TrustPolicy policy;
	policy.keys = { key };
	policy.require = true;
	CHECK(verify_manifest_signature(message, sig_b64, policy));
	CHECK_FALSE(verify_manifest_signature(message + " ", sig_b64, policy));
	CHECK_FALSE(verify_manifest_signature("schema = 1\nversion = \"v1.2.4\"\n", sig_b64, policy));

	std::string flipped = sig_b64;
	flipped[3] = flipped[3] == 'A' ? 'B' : 'A';
	CHECK_FALSE(verify_manifest_signature(message, flipped, policy));
}

TEST_CASE("signature decoding and key parsing") {
	SignatureBytes sig{};
	CHECK_FALSE(decode_signature("", sig));
	CHECK_FALSE(decode_signature("not base64!!", sig));
	CHECK_FALSE(decode_signature("AAAA", sig)); // valid base64, wrong length
	CHECK_FALSE(decode_signature(std::string(88, 'A') + "AAAA", sig)); // too long
	const std::string ok = base64(std::array<unsigned char, 64>{}.data(), 64);
	CHECK(decode_signature(ok, sig));
	CHECK(decode_signature("  " + ok + "\r\n\t", sig)); // surrounding whitespace is fine

	PublicKey key{};
	CHECK_FALSE(parse_public_key("", key));
	CHECK_FALSE(parse_public_key(std::string(63, 'a'), key));
	CHECK_FALSE(parse_public_key(std::string(64, 'g'), key));
	CHECK(parse_public_key(std::string(64, 'A'), key)); // case-insensitive hex
	CHECK(key[0] == 0xaa);
}

TEST_CASE("verify_manifest_signature follows the trust policy") {
	const TestKey alice(1), bob(2), mallory(3);
	const std::string manifest = "schema = 1\nversion = \"v1.0.0\"\n";

	SUBCASE("no keys configured: nothing is enforced") {
		TrustPolicy none;
		CHECK(verify_manifest_signature(manifest, "", none));
		CHECK(verify_manifest_signature(manifest, "garbage", none));
		none.require = true; // require without keys is meaningless, not an error
		CHECK(verify_manifest_signature(manifest, "", none));
	}
	SUBCASE("a valid signature from any trusted key") {
		TrustPolicy p = policy_for(alice);
		p.keys.push_back(bob.public_key()); // e.g. mid-rotation
		CHECK(verify_manifest_signature(manifest, alice.sign(manifest), p));
		CHECK(verify_manifest_signature(manifest, bob.sign(manifest), p));
		const Status s = verify_manifest_signature(manifest, mallory.sign(manifest), p);
		CHECK_FALSE(s);
		CHECK(s.error.find("does not match any trusted key") != std::string::npos);
	}
	SUBCASE("missing signature") {
		const Status required = verify_manifest_signature(manifest, "", policy_for(alice, true));
		CHECK_FALSE(required);
		CHECK(required.error.find("not signed") != std::string::npos);
		CHECK(verify_manifest_signature(manifest, "  \n", policy_for(alice, false)));
	}
	SUBCASE("a present-but-wrong signature is refused even when signatures are optional") {
		CHECK_FALSE(verify_manifest_signature(manifest, mallory.sign(manifest), policy_for(alice, false)));
		CHECK_FALSE(verify_manifest_signature(manifest, "AAAA", policy_for(alice, false)));
		CHECK_FALSE(verify_manifest_signature(manifest + "x", alice.sign(manifest), policy_for(alice, false)));
	}
}

TEST_CASE("load_trust_policy reads cli.toml") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const TestKey alice(1);

	std::string warning;
	TrustPolicy p = load_trust_policy(l, &warning); // no cli.toml, no embedded key
	CHECK(p.keys.size() == embedded_public_keys().size());
	const bool built_in_keys = !p.keys.empty();
	CHECK(p.require == built_in_keys);

	write(l.cli_toml(), "trusted_keys = [\"" + alice.public_hex() + "\"]\n");
	p = load_trust_policy(l, &warning);
	CHECK(p.keys.size() == embedded_public_keys().size() + 1);
	CHECK(p.require); // keys => required by default
	CHECK(warning.empty());

	write(l.cli_toml(), "trusted_keys = [\"" + alice.public_hex() + "\", \"" + alice.public_hex() + "\"]\n"
																									"require_signature = false\n");
	p = load_trust_policy(l, &warning);
	CHECK(p.keys.size() == embedded_public_keys().size() + 1); // duplicates collapse
	CHECK_FALSE(p.require);

	write(l.cli_toml(), "trusted_keys = [\"nope\", 5]\n");
	p = load_trust_policy(l, &warning);
	CHECK_FALSE(warning.empty()); // malformed entries are reported, not trusted
	CHECK(p.keys.size() == embedded_public_keys().size());
}

TEST_CASE("install enforces release signatures") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const TestKey release_key(7), other_key(8);

	SUBCASE("signed by the trusted key installs") {
		const auto src = make_dir_source(make_release(t.path, "v0.2.0", &release_key));
		const InstallResult r = install_release(l, *src, "latest", install_opts(policy_for(release_key)));
		REQUIRE(r.status);
		CHECK(fs::exists(l.version_dir("v0.2.0")));
		// The receipt records the protocol the release declared.
		CHECK(slurp(l.version_dir("v0.2.0") / ".install.toml").find("engine_protocol_version = 26") !=
				std::string::npos);
	}
	SUBCASE("signed by someone else is refused, nothing installed") {
		const auto src = make_dir_source(make_release(t.path, "v0.2.0", &other_key));
		const InstallResult r = install_release(l, *src, "latest", install_opts(policy_for(release_key)));
		CHECK_FALSE(r.status);
		CHECK(r.status.error.find("trusted key") != std::string::npos);
		CHECK_FALSE(fs::exists(l.version_dir("v0.2.0")));
		CHECK_FALSE(fs::exists(l.downloads_dir() / "voxel_browser-v0.2.0-linux-x86_64.zip"));
	}
	SUBCASE("a manifest edited after signing is refused") {
		const fs::path dir = make_release(t.path, "v0.2.0", &release_key);
		std::string manifest = slurp(dir / "release.toml");
		manifest.replace(manifest.find("abc"), 3, "evi"); // e.g. a swapped commit / hash
		write(dir / "release.toml", manifest);
		const auto src = make_dir_source(dir);
		CHECK_FALSE(install_release(l, *src, "latest", install_opts(policy_for(release_key))).status);
		CHECK_FALSE(fs::exists(l.version_dir("v0.2.0")));
	}
	SUBCASE("unsigned: refused when required, allowed when not") {
		const auto src = make_dir_source(make_release(t.path, "v0.2.0", nullptr));
		const InstallResult required =
				install_release(l, *src, "latest", install_opts(policy_for(release_key, true)));
		CHECK_FALSE(required.status);
		CHECK(required.status.error.find("not signed") != std::string::npos);
		CHECK_FALSE(fs::exists(l.version_dir("v0.2.0")));
		CHECK(install_release(l, *src, "latest", install_opts(policy_for(release_key, false))).status);
	}
	SUBCASE("no trusted keys: signatures are ignored entirely") {
		const auto src = make_dir_source(make_release(t.path, "v0.2.0", &other_key));
		CHECK(install_release(l, *src, "latest", install_opts(TrustPolicy{})).status);
	}
}

TEST_CASE("self update enforces release signatures too") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const TestKey release_key(7), other_key(8);
	const fs::path target = t.path / "bin" / vb_name();
	write(target, "OLD vb");

	SelfUpdateOptions opts;
	opts.target = target;
	opts.platform = "linux-x86_64";
	opts.current_version = "v0.1.0";
	opts.run_version_check = false;
	opts.trust = policy_for(release_key);

	const auto good = make_dir_source(make_release(t.path / "good", "v0.2.0", &release_key));
	const auto bad = make_dir_source(make_release(t.path / "bad", "v0.2.0", &other_key, "linux-x86_64", "EVIL"));
	const auto unsigned_release = make_dir_source(make_release(t.path / "uns", "v0.2.0", nullptr));

	CHECK_FALSE(self_update(l, *bad, opts).status);
	CHECK_FALSE(self_update(l, *unsigned_release, opts).status);
	CHECK(slurp(target) == "OLD vb"); // neither touched the binary
	REQUIRE(self_update(l, *good, opts).status);
	CHECK(slurp(target) == "NEW vb");
}

TEST_CASE("doctor reports the signature policy; install reads cli.toml keys") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const TestKey release_key(7);
	CHECK(run_vb(l, { "doctor" }).out.find("release signatures:") != std::string::npos);

	const fs::path dir = make_release(t.path, "v0.2.0", nullptr); // unsigned
	write(l.cli_toml(), "source = \"dir:" + dir.generic_string() + "\"\ntrusted_keys = [\"" + release_key.public_hex() + "\"]\n");
	CHECK(run_vb(l, { "doctor" }).out.find("required, 1 trusted key") != std::string::npos);
	const Run refused = run_vb(l, { "install" });
	CHECK(refused.code == kExitFailure);
	CHECK(refused.err.find("not signed") != std::string::npos);
	CHECK_FALSE(fs::exists(l.version_dir("v0.2.0")));
}

// ---- service definitions -------------------------------------------------------

namespace {
ServiceSpec spec_for(const std::string &name = "survival") {
	ServiceSpec s;
	s.name = name;
	s.vb_exe = "/opt/vb tools/vb";
	s.instance_dir = "/home/me/.local/share/voxel_browser/servers/" + name;
	s.log_file = s.instance_dir / "logs" / "server.log";
	return s;
}
} // namespace

TEST_CASE("systemd unit") {
	ServiceSpec s = spec_for();
	std::string unit = render_service(ServiceKind::Systemd, s);
	CHECK(unit.find("[Unit]") != std::string::npos);
	CHECK(unit.find("ExecStart=\"/opt/vb tools/vb\" server start \"survival\" --foreground") != std::string::npos);
	CHECK(unit.find("WorkingDirectory=\"/home/me/.local/share/voxel_browser/servers/survival\"") !=
			std::string::npos);
	CHECK(unit.find("KillMode=mixed") != std::string::npos); // signal vb first; it forwards to the server
	CHECK(unit.find("TimeoutStopSec=60") != std::string::npos);
	CHECK(unit.find("Restart=on-failure") != std::string::npos);
	CHECK(unit.find("WantedBy=default.target") != std::string::npos); // a *user* unit
	CHECK(unit.find("append:/home/me/.local/share/voxel_browser/servers/survival/logs/server.log") !=
			std::string::npos);
	CHECK(unit.find("Environment=") == std::string::npos);

	// systemd expands % and $ and splits on spaces: all must stay literal.
	s.vb_exe = "/p/100%/we$ird \"x\"/vb";
	s.vb_home = "/data/my home";
	unit = render_service(ServiceKind::Systemd, s);
	CHECK(unit.find("\"/p/100%%/we$$ird \\\"x\\\"/vb\"") != std::string::npos);
	CHECK(unit.find("Environment=\"VB_HOME=/data/my home\"") != std::string::npos);
}

TEST_CASE("launchd agent") {
	ServiceSpec s = spec_for("a&b");
	std::string plist = render_service(ServiceKind::Launchd, s);
	CHECK(plist.find("<key>Label</key>") != std::string::npos);
	CHECK(plist.find("<string>com.voxelbrowser.server.a&amp;b</string>") != std::string::npos);
	CHECK(plist.find("<string>--foreground</string>") != std::string::npos);
	CHECK(plist.find("<string>/opt/vb tools/vb</string>") != std::string::npos);
	CHECK(plist.find("<key>RunAtLoad</key>") != std::string::npos);
	CHECK(plist.find("<key>SuccessfulExit</key>\n\t\t<false/>") != std::string::npos); // restart on crash only
	CHECK(plist.find("<key>EnvironmentVariables</key>") == std::string::npos);
	CHECK(plist.find("a&b") == std::string::npos); // never raw
	s.vb_home = "/x";
	plist = render_service(ServiceKind::Launchd, s);
	CHECK(plist.find("<key>VB_HOME</key>\n\t\t<string>/x</string>") != std::string::npos);
}

TEST_CASE("generated XML never puts the instance name in a comment") {
	// "--" is illegal inside an XML comment, and instance names may contain it.
	const ServiceSpec s = spec_for("a--b");
	for (const ServiceKind kind : { ServiceKind::Launchd, ServiceKind::TaskScheduler }) {
		const std::string text = render_service(kind, s);
		std::size_t from = 0;
		while (true) {
			const std::size_t open = text.find("<!--", from);
			if (open == std::string::npos) {
				break;
			}
			const std::size_t close = text.find("-->", open);
			REQUIRE(close != std::string::npos);
			CHECK(text.substr(open + 4, close - open - 4).find("--") == std::string::npos);
			from = close + 3;
		}
	}
}

TEST_CASE("task scheduler definition") {
	ServiceSpec s = spec_for();
	s.vb_exe = "C:\\Program Files\\vb & co\\vb.exe";
	std::string xml = render_service(ServiceKind::TaskScheduler, s);
	CHECK(xml.find("<Command>C:\\Program Files\\vb &amp; co\\vb.exe</Command>") != std::string::npos);
	CHECK(xml.find("<Arguments>server start survival --foreground</Arguments>") != std::string::npos);
	CHECK(xml.find("<LogonTrigger>") != std::string::npos);
	CHECK(xml.find("<RunLevel>LeastPrivilege</RunLevel>") != std::string::npos); // never elevated
	CHECK(xml.find("<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>") != std::string::npos); // no 72h cutoff
	CHECK(xml.find("<RestartOnFailure>") != std::string::npos);
	s.vb_home = "D:\\vbdata";
	xml = render_service(ServiceKind::TaskScheduler, s);
	CHECK(xml.find("<Command>cmd.exe</Command>") != std::string::npos);
	CHECK(xml.find("set &quot;VB_HOME=D:\\vbdata&quot; &amp;&amp;") != std::string::npos);
}

TEST_CASE("service kinds, file names and hints") {
	ServiceKind k{};
	CHECK(parse_service_kind("systemd", k));
	CHECK(k == ServiceKind::Systemd);
	CHECK(parse_service_kind("launchd", k));
	CHECK(k == ServiceKind::Launchd);
	CHECK(parse_service_kind("task", k));
	CHECK(k == ServiceKind::TaskScheduler);
	CHECK_FALSE(parse_service_kind("sysv", k));
	CHECK(service_file_name(ServiceKind::Systemd, "x") == "voxel-browser-x.service");
	CHECK(service_file_name(ServiceKind::Launchd, "x") == "com.voxelbrowser.server.x.plist");
	CHECK(service_install_hint(ServiceKind::Systemd, spec_for()).find("systemctl --user") != std::string::npos);
	CHECK(service_install_hint(ServiceKind::Launchd, spec_for()).find("launchctl bootstrap") != std::string::npos);
	CHECK(service_install_hint(ServiceKind::TaskScheduler, spec_for()).find("schtasks /Create") !=
			std::string::npos);
}

TEST_CASE("vb server service print") {
	TempDir t;
	const Layout l = test_layout(t.path);
	CHECK(run_vb(l, { "server", "service" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "service", "print" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "service", "print", "missing" }).code == kExitFailure);
	REQUIRE(run_vb(l, { "server", "new", "s" }).code == kExitOk);
	CHECK(run_vb(l, { "server", "service", "print", "s", "--platform", "sysv" }).code == kExitUsage);

	const Run unit = run_vb(l, { "server", "service", "print", "s", "--platform", "systemd" });
	if (unit.code == kExitOk) { // needs vb's own path (not available on exotic platforms)
		CHECK(unit.out.find("server start \"s\" --foreground") != std::string::npos);
		CHECK(unit.err.find("systemctl --user") != std::string::npos); // instructions stay off stdout
		CHECK(unit.out.find("systemctl") == std::string::npos);
		CHECK(unit.out.find(l.servers_dir().string()) != std::string::npos);
	}
	const Run xml = run_vb(l, { "server", "service", "print", "s", "--platform", "task" });
	if (xml.code == kExitOk) {
		CHECK(xml.out.find("<Task version=\"1.4\"") != std::string::npos);
	}
}

// ---- protocol compatibility ------------------------------------------------------

TEST_CASE("connect targets") {
	ConnectTarget t;
	REQUIRE(parse_connect_target("play.example.com:27020", t));
	CHECK(t.host == "play.example.com");
	CHECK(t.port == 27020);
	REQUIRE(parse_connect_target("localhost", t));
	CHECK(t.host == "localhost");
	CHECK(t.port == 27015); // the engine's default
	REQUIRE(parse_connect_target("[::1]:9000", t));
	CHECK(t.host == "::1");
	CHECK(t.port == 9000);
	REQUIRE(parse_connect_target("[::1]", t));
	CHECK(t.port == 27015);
	REQUIRE(parse_connect_target("::1", t)); // bare v6 literal: no port
	CHECK(t.host == "::1");
	CHECK_FALSE(parse_connect_target("", t));
	CHECK_FALSE(parse_connect_target(":27015", t));
	CHECK_FALSE(parse_connect_target("host:0", t));
	CHECK_FALSE(parse_connect_target("host:70000", t));
	CHECK_FALSE(parse_connect_target("host:abc", t));
	CHECK_FALSE(parse_connect_target("[::1", t));
	CHECK_FALSE(parse_connect_target("[::1]x", t));

	CHECK(is_local_host("localhost"));
	CHECK(is_local_host("LocalHost"));
	CHECK(is_local_host("127.0.0.1"));
	CHECK(is_local_host("127.1.2.3"));
	CHECK(is_local_host("::1"));
	CHECK_FALSE(is_local_host("play.example.com"));
	CHECK_FALSE(is_local_host("10.0.0.5"));
	CHECK_FALSE(is_local_host("127example.com")); // only the 127.x.x.x range, not any 127-prefixed name
}

TEST_CASE("protocol parsing") {
	CHECK(parse_protocol("voxel_browser v1.2.3 (protocol 26, built 2026-10-04T08:04:27Z)") == 26);
	CHECK(parse_protocol("x (protocol 7)") == 7);
	CHECK_FALSE(parse_protocol("no such field"));
	CHECK_FALSE(parse_protocol("protocol x"));
	CHECK_FALSE(parse_protocol(""));
}

TEST_CASE("run_capture collects output and reports failures") {
#if !defined(_WIN32)
	const CaptureResult ok = run_capture("/bin/sh", { "-c", "echo out; echo err >&2; exit 3" });
	CHECK(ok.error.empty());
	CHECK(ok.exit_code == 3);
	CHECK(ok.output.find("out") != std::string::npos);
	CHECK(ok.output.find("err") != std::string::npos); // stderr is captured too

	const CaptureResult big = run_capture("/bin/sh", { "-c", "yes | head -c 100000" }, {}, 1000);
	CHECK(big.output.size() == 1000); // truncated, and the child is not left blocked

	const CaptureResult missing = run_capture("/definitely/not/here", {});
	CHECK_FALSE(missing.error.empty());
	CHECK(missing.exit_code == -1);
#endif
}

#if !defined(_WIN32)
namespace {
// A stand-in binary that answers `--version` like the real ones.
void fake_binary(const fs::path &p, int protocol) {
	write(p, "#!/bin/sh\necho \"voxel_browser test (protocol " + std::to_string(protocol) + ", built now)\"\n");
	chmod(p.c_str(), 0755);
}
} // namespace

TEST_CASE("entry_protocol prefers the install receipt, else asks the binary") {
	TempDir t;
	const Layout l = test_layout(t.path);
	fake_binary(l.version_dir("v0.1.0") / binary_file_name(Binary::Client), 25);
	fake_binary(l.version_dir("v0.1.0") / binary_file_name(Binary::Server), 25);
	Entry e;
	e.name = "v0.1.0";
	e.root = l.version_dir("v0.1.0");

	CHECK(entry_protocol(e, false) == 25); // no receipt: runs `--version`
	CHECK(entry_protocol(e, true) == 25);
	write(e.root / ".install.toml", "engine_protocol_version = 30\n");
	CHECK(entry_protocol(e, false) == 30); // the receipt wins (and costs no process)
	fs::remove(e.root / ".install.toml");
	fs::remove(e.root / binary_file_name(Binary::Client));
	CHECK_FALSE(entry_protocol(e, false)); // unknown, not a guess
}

TEST_CASE("protocol_warning compares against locally managed servers only") {
	TempDir t;
	const Layout l = test_layout(t.path);
	for (const auto &[tag, protocol] : { std::pair<const char *, int>{ "v0.1.0", 25 }, { "v0.2.0", 26 } }) {
		fake_binary(l.version_dir(tag) / binary_file_name(Binary::Client), protocol);
		fake_binary(l.version_dir(tag) / binary_file_name(Binary::Server), protocol);
	}
	REQUIRE(write_default_version(l, "v0.2.0"));
	const Entry client = *resolve_entry(l, "v0.2.0");

	// A "running" server on v0.1.0, port 27111: record it as the instance would.
	REQUIRE(create_instance(l, "old", "v0.1.0", "builtin:base", std::uint16_t{ 27111 }));
	const Instance inst = *load_instance(l, "old");
	const auto token = process_start_token(
#if defined(_WIN32)
			0
#else
			static_cast<Pid>(getpid())
#endif
	);
	REQUIRE(token);
	write(instance_run_dir(inst) / "server.pid",
			"pid = " + std::to_string(static_cast<long long>(getpid())) + "\nstart_token = " +
					std::to_string(*token) + "\nstarted = 1\nport = 27111\nversion = \"v0.1.0\"\n");

	ConnectTarget target;
	target.host = "localhost";
	target.port = 27111;
	const std::string warning = protocol_warning(l, client, target);
	CHECK(warning.find("'old'") != std::string::npos);
	CHECK(warning.find("v0.1.0") != std::string::npos);
	CHECK(warning.find("protocol 25") != std::string::npos);
	CHECK(warning.find("protocol 26") != std::string::npos);
	CHECK(warning.find("--version v0.1.0") != std::string::npos); // tells you how to fix it

	// Matching client: silent. Other port, remote host: silent (unknown).
	CHECK(protocol_warning(l, *resolve_entry(l, "v0.1.0"), target).empty());
	target.port = 27999;
	CHECK(protocol_warning(l, client, target).empty());
	target.port = 27111;
	target.host = "play.example.com";
	CHECK(protocol_warning(l, client, target).empty());
}

TEST_CASE("launch --connect passes the address and warns on a mismatch") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path record = t.path / "argv.txt";
	for (const auto &[tag, protocol] : { std::pair<const char *, int>{ "v0.1.0", 25 }, { "v0.2.0", 26 } }) {
		fake_binary(l.version_dir(tag) / binary_file_name(Binary::Server), protocol);
		// A client that answers --version and otherwise records its arguments.
		const fs::path client = l.version_dir(tag) / binary_file_name(Binary::Client);
		write(client, "#!/bin/sh\nif [ \"$1\" = --version ]; then echo \"x (protocol " + std::to_string(protocol) + ", built now)\"; exit 0; fi\nprintf '%s\\n' \"$@\" > '" + record.string() + "'\n");
		chmod(client.c_str(), 0755);
	}
	REQUIRE(write_default_version(l, "v0.2.0"));

	Run r = run_vb(l, { "launch", "--connect", "play.example.com:27020" });
	CHECK(r.code == 0);
	CHECK(r.err.empty());
	const std::string args = slurp(record);
	CHECK(args.find("--server\nplay.example.com\n--port\n27020") != std::string::npos);

	CHECK(run_vb(l, { "launch", "--connect=host" }).code == 0);
	CHECK(slurp(record).find("--server\nhost\n--port\n27015") != std::string::npos);

	CHECK(run_vb(l, { "launch", "--connect", "bad:port" }).code == kExitUsage);
	CHECK(run_vb(l, { "launch", "--connect" }).code == kExitUsage);
	CHECK(run_vb(l, { "launch", "--connect", "h:1", "--", "--port", "2" }).code == kExitUsage);
}
#endif

// ---- arm64 ---------------------------------------------------------------------------

TEST_CASE("arm64 platforms are selectable") {
	const std::string text =
			"schema = 1\nversion = \"v1.0.0\"\ncommit = \"c\"\ndate = \"d\"\nengine_protocol_version = 26\n\n"
			"[[artifact]]\nkind = \"game\"\nplatform = \"linux-arm64\"\nbuild = \"release\"\n"
			"file = \"a.zip\"\nsize = 1\nsha256 = \"" +
			std::string(64, 'a') + "\"\n\n"
								   "[[artifact]]\nkind = \"cli\"\nplatform = \"windows-arm64\"\nbuild = \"release\"\n"
								   "file = \"b.zip\"\nsize = 1\nsha256 = \"" +
			std::string(64, 'b') + "\"\n";
	ReleaseManifest m;
	REQUIRE(parse_manifest(text, m));
	CHECK(select_artifact(m, "game", "linux-arm64", "release") != nullptr);
	CHECK(select_artifact(m, "cli", "windows-arm64", "release") != nullptr);
	CHECK(select_artifact(m, "game", "linux-x86_64", "release") == nullptr);

	const std::string here = current_platform();
#if defined(__linux__) && defined(__aarch64__)
	CHECK(here == "linux-arm64");
#elif defined(__linux__) && defined(__x86_64__)
	CHECK(here == "linux-x86_64");
#endif
	(void)here;
}
