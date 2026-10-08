#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vb/cli/layout.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/store.hpp"

// Named, persistent server instances under <data>/servers/<name>/
// (dev-cli.md §6): each has its own server.toml, world, logs and pinned
// version, and can run in the foreground or detached.

namespace vb::cli {

constexpr const char *kDefaultInstanceName = "default";

struct Instance {
	std::string name;
	std::filesystem::path dir;
	std::string version = "default"; // "default" (follows `vb use`), a tag, or a link name
	std::string pack = "builtin:base"; // "builtin:<name>" or an absolute pack directory
};

// Instance directory layout.
std::filesystem::path instance_toml(const Instance &i);
std::filesystem::path instance_server_toml(const Instance &i);
std::filesystem::path instance_world_dir(const Instance &i);
// A folder name for one pack's own world: the pack directory's name plus a
// hash of its absolute path ("my_pack-1a2b3c4d"), so two packs never share a
// world save (saved chunks are block ids, only meaningful to their own pack).
std::string pack_world_name(const std::filesystem::path &pack);
// Where `vb host --pack <pack>` keeps that pack's world when it is not the
// instance's own pack: <instance>/worlds/<pack_world_name>.
std::filesystem::path instance_pack_world_dir(const Instance &i, const std::filesystem::path &pack);
std::filesystem::path instance_log_file(const Instance &i);
std::filesystem::path instance_run_dir(const Instance &i);
std::filesystem::path instance_status_file(const Instance &i);

// Empty when `name` is acceptable: [A-Za-z0-9][A-Za-z0-9._-]{0,63}.
std::string validate_instance_name(const std::string &name);

// Creates <servers>/<name>/{instance.toml,server.toml}. `port` (optional) is
// written into server.toml. Fails if the instance already exists.
Status create_instance(const Layout &layout, const std::string &name,
		const std::string &version, const std::string &pack, std::optional<std::uint16_t> port);

std::optional<Instance> load_instance(const Layout &layout, const std::string &name,
		std::string *why = nullptr);
std::vector<Instance> list_instances(const Layout &layout); // sorted by name

// Deletes the instance. `keep_world` leaves <dir>/world in place (and the
// instance dir with it). Refuses while the instance is running.
Status remove_instance(const Layout &layout, const std::string &name, bool keep_world);

// Where `pack` points for `entry_root` ("builtin:<n>" is looked up in
// <root>/content/<n>, then <root>/../content/<n> so a linked in-tree build
// directory finds the source tree's content). nullopt + why when missing.
std::optional<std::filesystem::path> resolve_pack(const std::string &pack,
		const std::filesystem::path &entry_root, std::string *why = nullptr);

// A running (or foreground-hosted) instance, as recorded in run/server.pid.
struct RunRecord {
	Pid pid = 0;
	std::uint64_t start_token = 0;
	std::int64_t started_unix = 0;
	std::uint16_t port = 0; // UDP port it was started on (0 = unknown, older record)
	std::string version; // resolved entry name the server runs from
};

// The live record, or nullopt. A record whose process is gone, or whose pid
// now belongs to a different process (start token mismatch), is stale: it is
// deleted (with the stop file) and nullopt returned.
std::optional<RunRecord> running_record(const Instance &inst);

// Per-run overrides (`vb host --pack/--version/--port`, never persisted).
struct Overrides {
	std::string version; // empty = the instance's
	std::string pack; // empty = the instance's
	std::optional<std::uint16_t> port;
	std::vector<std::string> extra_args; // appended verbatim to the server's command line
};

struct LaunchPlan {
	std::filesystem::path pack; // resolved content pack directory
	std::filesystem::path exe;
	std::filesystem::path cwd;
	std::vector<std::string> args;
	std::string version_name; // resolved entry
	std::uint16_t port = 0; // effective UDP port
};

// Resolves version + binary + pack, validates server.toml with the engine's own
// loader, and works out the effective port. Does not touch the filesystem
// beyond reading.
Status plan_launch(const Layout &layout, const Instance &inst, const Overrides &ov,
		LaunchPlan &out);

struct StartResult {
	Status status;
	int exit_code = 0; // foreground: the server's exit code
};

// Starts `inst`: not already running, plan_launch, UDP port free, stale stop
// file removed. Foreground blocks until the server exits (and returns its exit
// code); otherwise the server is detached and a short startup check reports an
// immediate crash with the tail of its log.
StartResult start_instance(const Layout &layout, const Instance &inst, const Overrides &ov,
		bool foreground, std::ostream &out);

// Graceful stop via the stop file (+ SIGTERM on POSIX), waiting up to
// `timeout_seconds`; `kill` escalates to a hard kill afterwards. Stopping an
// instance that isn't running succeeds ("not running" is printed).
Status stop_instance(const Instance &inst, int timeout_seconds, bool kill, std::ostream &out);

// Version name -> instance names that need it: instances pinning it explicitly
// plus running ones whose server executable comes from it. Used to refuse
// `uninstall`/`prune` of a version something depends on.
std::map<std::string, std::vector<std::string>> instance_version_users(const Layout &layout);

// Last `lines` lines of the log (empty when there is none).
std::string tail_log(const Instance &inst, int lines);

} // namespace vb::cli

namespace vb::cli {

// What the server last wrote to --status-file (dev-cli.md §6.5).
struct ServerStatus {
	bool running = false; // the server's own claim; false after a clean shutdown
	std::int64_t updated_unix = 0;
	std::int64_t uptime_seconds = 0;
	std::int64_t tick = 0;
	int target_tick_rate = 0;
	double tick_rate = 0.0; // achieved, over the last interval
	int max_players = 0;
	std::string seed;
	std::string motd;
	std::vector<std::string> players;
};

// nullopt when there is no (parseable) status file.
std::optional<ServerStatus> read_server_status(const Instance &inst);

// A status is "fresh" if it was written within `max_age_seconds` of `now_unix`;
// an older one belongs to a hung or pre-status-file server and is not shown.
bool status_is_fresh(const ServerStatus &s, std::int64_t now_unix, std::int64_t max_age_seconds = 20);

// Rotates logs/server.log -> server.log.1 -> ... -> server.log.<keep> when the
// log is larger than `max_bytes` (so a long-lived server cannot fill the disk).
// Called before each detached start; never while the server holds it open.
void rotate_log(const Instance &inst, std::uintmax_t max_bytes = 10u << 20, int keep = 3);

} // namespace vb::cli
