#include "vb/cli/release_manifest.hpp"

#include <cctype>

#include <toml++/toml.hpp>

#include "vb/cli/version.hpp"

namespace vb::cli {

namespace {

bool is_hex64(const std::string &s) {
	if (s.size() != 64) {
		return false;
	}
	for (const char c : s) {
		if (!std::isxdigit(static_cast<unsigned char>(c)) || std::isupper(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
}

bool is_plain_file_name(const std::string &s) {
	return !s.empty() && s != "." && s != ".." &&
			s.find_first_of("/\\:") == std::string::npos;
}

} // namespace

Status parse_manifest(std::string_view text, ReleaseManifest &out) {
	toml::table tbl;
	try {
		tbl = toml::parse(text);
	} catch (const toml::parse_error &e) {
		return { std::string("release.toml: ") + std::string(e.description()) };
	}
	ReleaseManifest m;
	m.schema = tbl["schema"].value_or(0);
	if (m.schema != 1) {
		return { "release.toml: unsupported schema " + std::to_string(m.schema) +
			" (this vb understands 1; update vb)" };
	}
	m.version = tbl["version"].value_or(std::string());
	const auto v = parse_version(m.version);
	if (!v || to_tag(*v) != m.version) {
		return { "release.toml: bad version '" + m.version + "'" };
	}
	m.commit = tbl["commit"].value_or(std::string());
	m.date = tbl["date"].value_or(std::string());
	m.engine_protocol_version = tbl["engine_protocol_version"].value_or(0);

	const toml::array *arr = tbl["artifact"].as_array();
	if (arr == nullptr) {
		return { "release.toml: no [[artifact]] entries" };
	}
	for (const toml::node &node : *arr) {
		const toml::table *t = node.as_table();
		if (t == nullptr) {
			return { "release.toml: malformed [[artifact]]" };
		}
		ArtifactInfo a;
		a.kind = (*t)["kind"].value_or(std::string());
		a.platform = (*t)["platform"].value_or(std::string());
		a.build = (*t)["build"].value_or(std::string());
		a.file = (*t)["file"].value_or(std::string());
		const std::int64_t size = (*t)["size"].value_or<std::int64_t>(-1);
		a.sha256 = (*t)["sha256"].value_or(std::string());
		if ((a.kind != "game" && a.kind != "cli") ||
				(a.build != "release" && a.build != "debug") || a.platform.empty() ||
				!is_plain_file_name(a.file) || size < 0 || !is_hex64(a.sha256)) {
			return { "release.toml: invalid artifact entry '" + a.file + "'" };
		}
		a.size = static_cast<std::uint64_t>(size);
		m.artifacts.push_back(std::move(a));
	}
	out = std::move(m);
	return {};
}

std::string current_platform() {
#if defined(_WIN32)
#if defined(_M_X64) || defined(__x86_64__)
	return "windows-x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
	return "windows-arm64";
#else
	return {};
#endif
#elif defined(__APPLE__)
	return "macos-universal";
#elif defined(__linux__)
#if defined(__x86_64__)
	return "linux-x86_64";
#elif defined(__aarch64__)
	return "linux-arm64";
#else
	return {};
#endif
#else
	return {};
#endif
}

const ArtifactInfo *select_artifact(const ReleaseManifest &m, std::string_view kind,
		std::string_view platform, std::string_view build) {
	for (const ArtifactInfo &a : m.artifacts) {
		if (a.kind == kind && a.platform == platform && a.build == build) {
			return &a;
		}
	}
	return nullptr;
}

} // namespace vb::cli
