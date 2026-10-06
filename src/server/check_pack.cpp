#include "check_pack.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "vb/auth/config.hpp"
#include "vb/net/loopback.hpp"
#include "vb/script/global_scan.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_manifest.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/script/ui_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/worldgen/generator.hpp"

namespace vb::server {

namespace fs = std::filesystem;
using Severity = PackDiagnostic::Severity;

int CheckReport::errors() const {
	return static_cast<int>(std::count_if(diagnostics.begin(), diagnostics.end(),
			[](const PackDiagnostic &d) { return d.severity == Severity::kError; }));
}

int CheckReport::warnings() const {
	return static_cast<int>(diagnostics.size()) - errors();
}

LuaEnv environment_of(const std::string &rel) {
	if (rel == "auth.lua") {
		return LuaEnv::kAuth;
	}
	if (rel.rfind("ui/", 0) == 0) {
		return LuaEnv::kUi;
	}
	if (rel.rfind("data/", 0) == 0) {
		return LuaEnv::kData;
	}
	return LuaEnv::kServer;
}

namespace {

// What the sandbox leaves in every pack Lua state (Vm::strip_sandbox + the reinstated `require`).
const std::set<std::string> kBuiltins = { "_G", "_ENV", "_VERSION", "assert", "error", "getmetatable", "ipairs",
	"next", "pairs", "pcall", "print", "rawequal", "rawget", "rawlen", "rawset", "select", "setmetatable",
	"tonumber", "tostring", "type", "warn", "xpcall", "require", "string", "table", "math", "coroutine",
	"utf8", "debug" };

// Present in stock Lua, removed by the sandbox.
const std::set<std::string> kSandboxRemoved = { "os", "io", "package", "dofile", "loadfile", "load", "loadstring",
	"collectgarbage" };

const std::set<std::string> kServerGlobals = { "vb" };
const std::set<std::string> kUiGlobals = { "ui", "client" };

const char *env_name(LuaEnv e) {
	switch (e) {
		case LuaEnv::kServer:
			return "the server pack VM";
		case LuaEnv::kUi:
			return "the client UI VM";
		case LuaEnv::kData:
			return "a data script";
		case LuaEnv::kAuth:
			return "auth.lua";
	}
	return "";
}

std::string read_file(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::vector<std::string> list_lua_files(const fs::path &dir) {
	std::vector<std::string> out;
	std::error_code ec;
	for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator();
			it.increment(ec)) {
		if (ec) {
			break;
		}
		const fs::path rel = fs::relative(it->path(), dir, ec);
		if (!rel.empty() && rel.begin()->generic_string().rfind('.', 0) == 0) {
			if (it->is_directory(ec)) {
				it.disable_recursion_pending();
			}
			continue;
		}
		if (it->is_regular_file(ec) && it->path().extension() == ".lua") {
			out.push_back(rel.generic_string());
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

void add(CheckReport &r, Severity s, std::string file, int line, std::string message) {
	r.diagnostics.push_back({ s, std::move(file), line, std::move(message) });
}

// Turns a Lua error ("blocks/a.lua:5: boom\nstack traceback:\n\t[C]: in function 'vb.register_block'\n\t
// blocks/a.lua:5: in main chunk") into a diagnostic with the best file:line available. Errors raised
// from C++ bindings carry no position of their own, so the traceback's first pack-file frame is used.
void add_lua_error(CheckReport &r, const std::set<std::string> &known_files, const std::string &raw,
		const std::string &fallback_file) {
	// Chunks are named by their pack-relative path, which Lua prints as `[string "blocks/a.lua"]:5:`
	// (the loader passes a plain name) or `blocks/a.lua:5:` (compile errors of an `@name`).
	static const std::regex head(R"x(^(?:\[string "([^"]+)"\]|([^:\n]+\.lua)):(\d+): (.*)$)x");
	static const std::regex frame(R"x(\n\s+(?:\[string "([^"]+)"\]|([^:\s]+\.lua)):(\d+):)x");
	const std::string first = raw.substr(0, raw.find('\n'));
	std::smatch m;
	if (std::regex_match(first, m, head)) {
		const std::string file = m[1].matched ? m[1].str() : m[2].str();
		if (known_files.count(file) != 0) {
			add(r, Severity::kError, file, std::stoi(m[3].str()), m[4].str());
			return;
		}
	}
	// Errors raised from C++ bindings carry no position: use the first pack-file frame of the traceback.
	for (auto it = std::sregex_iterator(raw.begin(), raw.end(), frame); it != std::sregex_iterator(); ++it) {
		const std::string file = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
		if (known_files.count(file) != 0) {
			add(r, Severity::kError, file, std::stoi((*it)[3].str()), first);
			return;
		}
	}
	add(r, Severity::kError, fallback_file, 0, first);
}

// §3.4.1: the static per-environment global check.
struct Scanned {
	std::string rel;
	LuaEnv env;
	std::vector<script::GlobalAccess> accesses;
};

// Compiles every file (nothing runs) and collects its global accesses. Syntax errors are reported
// here; returns true when any file failed to compile.
bool scan_files(CheckReport &r, const fs::path &dir, const std::vector<std::string> &files,
		std::vector<Scanned> &scanned, std::map<LuaEnv, std::set<std::string>> &assigned) {
	bool syntax_error = false;
	for (const std::string &rel : files) {
		const script::GlobalScan scan = script::scan_globals(read_file(dir / rel), rel);
		if (!scan.ok) {
			syntax_error = true;
			static const std::regex head(R"(^[^:\n]+:(\d+): (.*)$)");
			std::smatch m;
			const std::string first = scan.error.substr(0, scan.error.find('\n'));
			if (std::regex_match(first, m, head)) {
				add(r, Severity::kError, rel, std::stoi(m[1].str()), m[2].str());
			} else {
				add(r, Severity::kError, rel, 0, first);
			}
			continue;
		}
		const LuaEnv env = environment_of(rel);
		for (const auto &a : scan.accesses) {
			if (a.is_write) {
				assigned[env].insert(a.name);
			}
		}
		scanned.push_back({ rel, env, scan.accesses });
	}
	return syntax_error;
}

// §3.4.1: judge each read against the file's environment. `assigned` holds the globals defined by
// any file of an environment, statically (`x = 1`) and at run time (`_G[name] = ...`, as content/base's
// blocks/register.lua does). `judge_unknown` is false for an environment whose load aborted, where an
// "unknown global" would only be a consequence of the abort.
void report_globals(CheckReport &r, const std::vector<Scanned> &scanned,
		std::map<LuaEnv, std::set<std::string>> &assigned, const std::map<LuaEnv, bool> &judge_unknown) {
	for (const Scanned &s : scanned) {
		std::set<std::pair<int, std::string>> seen;
		for (const auto &a : s.accesses) {
			if (a.is_write || kBuiltins.count(a.name) != 0 || !seen.insert({ a.line, a.name }).second) {
				continue;
			}
			const std::set<std::string> &mine = s.env == LuaEnv::kServer ? kServerGlobals
					: s.env == LuaEnv::kUi								 ? kUiGlobals
																		 : std::set<std::string>{};
			if (mine.count(a.name) != 0) {
				continue;
			}
			if (s.env == LuaEnv::kData && a.name == "vb") {
				add(r, Severity::kError, s.rel, a.line,
						"'vb' is not usable in a data script (data scripts must only return data; register from pack code)");
				continue;
			}
			if (kSandboxRemoved.count(a.name) != 0) {
				add(r, Severity::kError, s.rel, a.line,
						"'" + a.name + "' is not available: the Lua sandbox removes it (see sdk/lua/library/sandbox.lua)");
				continue;
			}
			const bool other_server = s.env != LuaEnv::kServer && kServerGlobals.count(a.name) != 0;
			const bool other_ui = s.env != LuaEnv::kUi && kUiGlobals.count(a.name) != 0;
			if (other_server || other_ui) {
				add(r, Severity::kError, s.rel, a.line,
						"'" + a.name + "' is not available in " + env_name(s.env) + " (" +
								(other_server ? "server-only" : "client-UI-only") + ")");
				continue;
			}
			if (assigned[s.env].count(a.name) != 0) {
				continue; // a global some file of this environment defines (e.g. base_ui in ui/_style.lua)
			}
			if (!judge_unknown.at(s.env)) {
				continue;
			}
			add(r, Severity::kWarning, s.rel, a.line, "unknown global '" + a.name + "' (typo? it is never assigned in " + env_name(s.env) + ")");
		}
	}
}

// A static environment error ("'vb' is not available in the client UI VM") and the run-time error it
// causes ("attempt to index a nil value (global 'vb')") land on the same line; keep the static one.
void dedupe_errors(CheckReport &r) {
	const auto is_static = [](const PackDiagnostic &d) {
		return d.message.find("is not available") != std::string::npos ||
				d.message.find("is not usable") != std::string::npos;
	};
	std::vector<PackDiagnostic> out;
	for (const PackDiagnostic &d : r.diagnostics) {
		bool dropped = false;
		if (d.severity == Severity::kError && !d.file.empty() && d.line > 0) {
			for (PackDiagnostic &kept : out) {
				if (kept.severity == Severity::kError && kept.file == d.file && kept.line == d.line) {
					if (is_static(d) && !is_static(kept)) {
						kept = d;
					}
					dropped = true;
					break;
				}
			}
		}
		if (!dropped) {
			out.push_back(d);
		}
	}
	r.diagnostics = std::move(out);
}

} // namespace

CheckReport check_pack(const fs::path &dir, const CheckOptions &options) {
	CheckReport r;
	r.pack_dir = dir.generic_string();
	r.engine_version = core::to_string(options.engine);
	std::error_code ec;
	if (!fs::is_directory(dir, ec)) {
		add(r, Severity::kError, "", 0, "not a directory: " + dir.string());
		return r;
	}

	// 1. pack.toml + engine_version_req.
	const script::PackManifest manifest = script::read_pack_manifest(dir);
	r.pack_name = manifest.name.empty() ? dir.filename().string() : manifest.name;
	if (!manifest.found) {
		add(r, Severity::kError, "pack.toml", 0, "no pack.toml in " + dir.string());
	} else if (!manifest.error.empty()) {
		add(r, Severity::kError, "pack.toml", 0, manifest.error);
	} else {
		r.engine_req = manifest.engine_version_req.value_or("");
		const core::EngineReqCheck req = script::check_pack_engine_req(manifest, options.engine);
		if (!req.ok && !options.ignore_engine_req) {
			add(r, Severity::kError, "pack.toml", manifest.engine_version_req_line, req.message);
		} else if (req.warning) {
			add(r, Severity::kWarning, "pack.toml", 0, req.message);
		}
	}

	const std::vector<std::string> files = list_lua_files(dir);
	const std::set<std::string> known(files.begin(), files.end());

	// 2. Syntax + static global-access check (nothing is executed). A syntax error stops the
	// run-it phase below: it would only repeat the same error.
	std::vector<Scanned> scanned;
	std::map<LuaEnv, std::set<std::string>> assigned;
	const bool has_syntax_error = scan_files(r, dir, files, scanned, assigned);

	// 3. auth.lua.
	const auth::AuthLoad auth_load = auth::load_auth_lua(dir);
	if (auth_load.present && !auth_load.error.empty()) {
		add(r, Severity::kError, "auth.lua", 0, auth_load.error);
	}

	if (has_syntax_error) {
		report_globals(r, scanned, assigned, { { LuaEnv::kServer, false }, { LuaEnv::kUi, false }, { LuaEnv::kData, false }, { LuaEnv::kAuth, false } });
		return r;
	}
	std::map<LuaEnv, bool> judge = { { LuaEnv::kServer, true }, { LuaEnv::kUi, true }, { LuaEnv::kData, true }, { LuaEnv::kAuth, true } };

	// 4. Load the server-side pack like a real start.
	const fs::path scratch = fs::temp_directory_path() / ("vb_check_pack_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(scratch, ec);
	{
		net::LoopbackNetwork net;
		world::BlockRegistry registry = world::BlockRegistry::base();
		script::PackRuntime rt(net.server(), registry, scratch / "storage.json");
		rt.set_pack_modules(script::collect_requirable_modules(dir));
		rt.set_auth_required(auth_load.present && auth_load.error.empty());

		std::vector<std::string> order;
		const auto in_dir = [&](const char *sub) {
			const std::string prefix = std::string(sub) + "/";
			for (const auto &f : files) {
				if (f.rfind(prefix, 0) == 0 && f.find('/', prefix.size()) == std::string::npos) {
					order.push_back(f);
				}
			}
		};
		for (const char *sub : { "blocks", "entities", "biomes" }) {
			in_dir(sub);
		}
		for (const auto &f : files) {
			if (f.find('/') == std::string::npos && f != "init.lua" && f != "auth.lua") {
				order.push_back(f);
			}
		}
		if (known.count("init.lua") != 0) {
			order.push_back("init.lua");
		}

		// Pack `print` output would otherwise corrupt --json on stdout.
		(void)rt.load_pack_file("print = function(...) end", "=check-pack");
		bool load_failed = false;
		for (const auto &rel : order) {
			const script::ScriptResult res = rt.load_pack_file(read_file(dir / rel), rel);
			if (!res.ok) {
				add_lua_error(r, known, res.message, rel);
				load_failed = true;
				break; // later files depend on this one; the cascade would be noise
			}
		}
		judge[LuaEnv::kServer] = !load_failed;
		for (const auto &name : rt.global_names()) {
			assigned[LuaEnv::kServer].insert(name);
		}

		if (!load_failed) {
			rt.freeze();
			if (const auto worldgen = rt.validate_worldgen(); !worldgen) {
				add(r, Severity::kError, "", 0, "invalid worldgen data: " + worldgen.message);
			} else {
				try {
					worldgen::WorldGenParams params;
					params.seed = 1;
					const auto pipeline = rt.build_worldgen_pipeline(params);
					const worldgen::WorldGenerator generator(params, registry, pipeline);
					world::Chunk chunk(core::ChunkCoord{ 0, 0, 0 });
					generator.generate(chunk);
				} catch (const std::exception &e) {
					add(r, Severity::kError, "", 0, std::string("worldgen failed on the first chunk: ") + e.what());
				}
			}
		}
	}

	// 5. Client UI scripts, in the client's own (sorted) order.
	{
		script::UiRuntime ui;
		(void)ui.load_pack_file("print = function(...) end", "=check-pack");
		for (const auto &rel : files) {
			if (environment_of(rel) != LuaEnv::kUi) {
				continue;
			}
			const script::ScriptResult res = ui.load_pack_file(read_file(dir / rel), rel);
			if (!res.ok) {
				add_lua_error(r, known, res.message, rel);
				judge[LuaEnv::kUi] = false;
			}
		}
		for (const auto &name : ui.global_names()) {
			assigned[LuaEnv::kUi].insert(name);
		}
	}
	report_globals(r, scanned, assigned, judge);
	dedupe_errors(r);
	fs::remove_all(scratch, ec);
	return r;
}

std::string format_human(const CheckReport &r, bool strict) {
	std::ostringstream out;
	for (const auto &d : r.diagnostics) {
		if (!d.file.empty()) {
			out << d.file;
			if (d.line > 0) {
				out << ':' << d.line;
			}
			out << ": ";
		}
		out << (d.severity == Severity::kError ? "error: " : "warning: ") << d.message << '\n';
	}
	out << (r.ok(strict) ? "ok" : "FAILED") << ": " << r.pack_name << " -- " << r.errors() << " error(s), "
		<< r.warnings() << " warning(s)" << (strict && r.warnings() > 0 ? " (--strict)" : "") << '\n';
	return out.str();
}

std::string format_json(const CheckReport &r, bool strict) {
	nlohmann::json j;
	j["ok"] = r.ok(strict);
	j["pack"] = { { "dir", r.pack_dir }, { "name", r.pack_name }, { "engine_version_req", r.engine_req } };
	j["engine"] = r.engine_version;
	j["errors"] = r.errors();
	j["warnings"] = r.warnings();
	j["diagnostics"] = nlohmann::json::array();
	for (const auto &d : r.diagnostics) {
		j["diagnostics"].push_back({ { "severity", d.severity == Severity::kError ? "error" : "warning" },
				{ "file", d.file }, { "line", d.line }, { "message", d.message } });
	}
	return j.dump(2);
}

} // namespace vb::server
