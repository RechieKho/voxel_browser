#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vb/core/version_req.hpp"

// Headless content-pack validation: `voxel_browser_server --check-pack <dir>`
// (architecture_spec/dev-experience.md §3.4). Loads the pack the way a real
// server start does -- loader order, `require`, `auth.lua`, freeze(), worldgen
// pipeline construction plus one generated chunk, `ui/*.lua` compiled in a
// UiRuntime -- without opening a socket, and adds a static per-environment
// global-access check (§3.4.1). Everything is reported as `file:line`
// diagnostics.

namespace vb::server {

struct PackDiagnostic {
	enum class Severity { kError,
		kWarning };
	Severity severity = Severity::kError;
	std::string file; // pack-relative, forward slashes; empty when not tied to a file
	int line = 0; // 1-based, 0 = unknown
	std::string message;
};

struct CheckOptions {
	bool strict = false; // warnings fail the check
	bool ignore_engine_req = false;
	core::SemVer engine = core::engine_version(); // overridable for tests
};

struct CheckReport {
	std::string pack_dir;
	std::string pack_name;
	std::string engine_req; // raw engine_version_req, empty if absent
	std::string engine_version; // the engine the check ran against
	std::vector<PackDiagnostic> diagnostics;

	int errors() const;
	int warnings() const;
	bool ok(bool strict) const { return errors() == 0 && (!strict || warnings() == 0); }
};

CheckReport check_pack(const std::filesystem::path &dir, const CheckOptions &options = {});

// "blocks/ruby.lua:12: error: message" lines plus a summary line.
std::string format_human(const CheckReport &report, bool strict);

// {"ok":bool,"pack":..,"engine":{..},"errors":n,"warnings":n,"diagnostics":[{severity,file,line,message}]}
std::string format_json(const CheckReport &report, bool strict);

// Which Lua environment a pack file runs in (§3.4.1), from its pack-relative path.
enum class LuaEnv { kServer,
	kUi,
	kData,
	kAuth };
LuaEnv environment_of(const std::string &rel_path);

} // namespace vb::server
