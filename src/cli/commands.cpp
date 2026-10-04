#include "vb/cli/commands.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>

#include "vb/cli/process.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/version.hpp"
#include "vb/core/build_info.hpp"

namespace vb::cli {

namespace {

struct Ctx {
	const Layout &layout;
	std::ostream &out;
	std::ostream &err;
};

using Handler = std::function<int(const Ctx &, const std::vector<std::string> &)>;

struct Command {
	const char *usage;
	const char *summary;
	Handler run;
};

int usage_error(const Ctx &c, const std::string &msg) {
	c.err << "vb: " << msg << "\nTry `vb --help`.\n";
	return kExitUsage;
}

int failure(const Ctx &c, const std::string &msg) {
	c.err << "vb: " << msg << "\n";
	return kExitFailure;
}

// Splits `args` at the first "--": {before, after}.
std::pair<std::vector<std::string>, std::vector<std::string>> split_passthrough(
		const std::vector<std::string> &args) {
	const auto it = std::find(args.begin(), args.end(), "--");
	if (it == args.end()) {
		return { args, {} };
	}
	return { { args.begin(), it }, { it + 1, args.end() } };
}

// Extracts `--version <v>` / `--version=<v>` from `args` (removing it).
// Returns false on a dangling `--version`.
bool take_version_flag(std::vector<std::string> &args, std::string &version) {
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--version") {
			if (i + 1 >= args.size()) {
				return false;
			}
			version = args[i + 1];
			args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
					args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
			return true;
		}
		if (args[i].rfind("--version=", 0) == 0) {
			version = args[i].substr(10);
			args.erase(args.begin() + static_cast<std::ptrdiff_t>(i));
			return true;
		}
	}
	return true;
}

int cmd_paths(const Ctx &c, const std::vector<std::string> &args) {
	if (!args.empty()) {
		return usage_error(c, "`paths` takes no arguments");
	}
	c.out << "data:   " << c.layout.data().string() << "\n"
		  << "config: " << c.layout.config().string() << "\n"
		  << "cache:  " << c.layout.cache().string() << "\n";
	return kExitOk;
}

int cmd_list(const Ctx &c, const std::vector<std::string> &args) {
	if (!args.empty()) {
		return usage_error(c, "`list` takes no arguments yet (`--remote` arrives with 8.5)");
	}
	const auto entries = list_entries(c.layout);
	if (entries.empty()) {
		c.out << "nothing installed. Register a local build with "
				 "`vb link dev <build-dir>`.\n";
		return kExitOk;
	}
	for (const Entry &e : entries) {
		c.out << (e.is_default ? "* " : "  ") << e.name;
		if (e.kind == EntryKind::Link) {
			c.out << "  -> " << e.root.string() << (e.root_exists ? "" : "  (missing)");
		}
		c.out << "\n";
	}
	return kExitOk;
}

int cmd_use(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 1) {
		return usage_error(c, "usage: vb use <version>");
	}
	std::string why;
	const auto e = resolve_entry(c.layout, args[0], &why);
	if (!e) {
		return failure(c, why);
	}
	if (const Status s = write_default_version(c.layout, e->name); !s) {
		return failure(c, s.error);
	}
	c.out << "default version is now " << e->name << "\n";
	return kExitOk;
}

int cmd_which(const Ctx &c, std::vector<std::string> args) {
	std::string version;
	if (!take_version_flag(args, version)) {
		return usage_error(c, "--version needs a value");
	}
	Binary which = Binary::Client;
	if (args.size() > 1) {
		return usage_error(c, "usage: vb which [client|server] [--version <v>]");
	}
	if (args.size() == 1) {
		if (args[0] == "server") {
			which = Binary::Server;
		} else if (args[0] != "client") {
			return usage_error(c, "usage: vb which [client|server] [--version <v>]");
		}
	}
	std::string why;
	const auto e = resolve_entry(c.layout, version, &why);
	if (!e) {
		return failure(c, why);
	}
	const auto bin = find_binary(*e, which);
	if (!bin) {
		return failure(c, e->name + " has no " + binary_file_name(which) + " in " +
				e->root.string());
	}
	c.out << bin->string() << "\n";
	return kExitOk;
}

int cmd_uninstall(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 1) {
		return usage_error(c, "usage: vb uninstall <version>");
	}
	if (const Status s = uninstall(c.layout, args[0]); !s) {
		return failure(c, s.error);
	}
	c.out << "removed " << args[0] << "\n";
	return kExitOk;
}

int cmd_link(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 2) {
		return usage_error(c, "usage: vb link <name> <build-dir>");
	}
	if (const Status s = add_link(c.layout, args[0], args[1]); !s) {
		return failure(c, s.error);
	}
	c.out << "linked " << args[0] << " -> " << args[1] << "\n";
	return kExitOk;
}

int cmd_unlink(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 1) {
		return usage_error(c, "usage: vb unlink <name>");
	}
	if (const Status s = remove_link(c.layout, args[0]); !s) {
		return failure(c, s.error);
	}
	c.out << "unlinked " << args[0] << "\n";
	return kExitOk;
}

int cmd_launch(const Ctx &c, const std::vector<std::string> &raw) {
	auto [opts, passthrough] = split_passthrough(raw);
	std::string version;
	if (!take_version_flag(opts, version)) {
		return usage_error(c, "--version needs a value");
	}
	if (!opts.empty()) {
		return usage_error(c, "unexpected argument '" + opts[0] +
				"' (pass client arguments after `--`)");
	}
	std::string why;
	const auto e = resolve_entry(c.layout, version, &why);
	if (!e) {
		return failure(c, why);
	}
	const auto bin = find_binary(*e, Binary::Client);
	if (!bin) {
		return failure(c, e->name + " has no " + binary_file_name(Binary::Client) + " in " +
				e->root.string());
	}
	// Settings live in the user config dir so every installed version shares
	// them; an explicit --config from the user wins.
	const bool has_config = std::any_of(passthrough.begin(), passthrough.end(),
			[](const std::string &a) { return a == "--config" || a.rfind("--config=", 0) == 0; });
	std::vector<std::string> args;
	if (!has_config) {
		args = { "--config", c.layout.client_toml().string() };
	}
	args.insert(args.end(), passthrough.begin(), passthrough.end());
	const RunResult r = run_foreground(*bin, args);
	if (!r.error.empty()) {
		return failure(c, r.error);
	}
	return r.exit_code;
}

const std::map<std::string, Command> &commands() {
	static const std::map<std::string, Command> table = {
		{ "list", { "list", "show installed versions and links (* = default)", cmd_list } },
		{ "use", { "use <version>", "set the default version", cmd_use } },
		{ "which",
				{ "which [client|server] [--version <v>]", "print a binary's path",
						[](const Ctx &c, const std::vector<std::string> &a) {
							return cmd_which(c, a);
						} } },
		{ "uninstall",
				{ "uninstall <version>", "remove an installed version (or a link)",
						cmd_uninstall } },
		{ "link",
				{ "link <name> <build-dir>", "register a local build as a pseudo-version",
						cmd_link } },
		{ "unlink", { "unlink <name>", "remove a link", cmd_unlink } },
		{ "launch",
				{ "launch [--version <v>] [-- client args...]", "start the client",
						cmd_launch } },
		{ "paths", { "paths", "print the data/config/cache directories", cmd_paths } },
	};
	return table;
}

void print_help(std::ostream &out) {
	out << "vb -- manage and run Voxel Browser (user-scoped, no admin rights)\n\n"
		   "usage: vb <command> [args]\n\ncommands:\n";
	for (const auto &[name, cmd] : commands()) {
		out << "  " << cmd.usage;
		const std::size_t pad = std::string(cmd.usage).size();
		out << std::string(pad < 44 ? 44 - pad : 2, ' ') << cmd.summary << "\n";
	}
	out << "\noptions:\n  -h, --help     show this help\n  -V, --version  print the vb build\n"
		   "\nenvironment:\n  VB_HOME=<dir>  keep everything under <dir> (portable/CI)\n";
}

} // namespace

int run_cli(const std::vector<std::string> &args, const Layout &layout, std::ostream &out,
		std::ostream &err) {
	const Ctx ctx{ layout, out, err };
	if (args.empty()) {
		print_help(err);
		return kExitUsage;
	}
	const std::string &first = args.front();
	if (first == "-h" || first == "--help" || first == "help") {
		print_help(out);
		return kExitOk;
	}
	if (first == "-V" || first == "--version") {
		out << vb::core::describe_build() << "\n";
		return kExitOk;
	}
	const auto it = commands().find(first);
	if (it == commands().end()) {
		return usage_error(ctx, "unknown command '" + first + "'");
	}
	return it->second.run(ctx, std::vector<std::string>(args.begin() + 1, args.end()));
}

} // namespace vb::cli
