#pragma once

#include <iosfwd>
#include <string>
#include <vector>

// Long-form documentation of every `vb` command: the source of `vb help <command>`,
// `vb <command> --help`, `vb help --markdown` (the committed docs/cli.md) and
// `vb help --json` (architecture_spec/dev-experience.md §3.6). The one-line usage and
// summary stay in the command table in commands.cpp; this adds what a human or an agent
// needs to use the command without guessing.

namespace vb::cli {

struct DocExample {
	std::string command;
	std::string explanation;
};

struct DocFlag {
	std::string flag; // e.g. "--json"
	std::string text;
};

struct DocSub {
	std::string usage; // e.g. "pack init [dir] [--template ...]"
	std::string summary;
};

struct CommandDoc {
	std::string name;
	std::string details; // paragraphs separated by a blank line
	std::vector<DocFlag> flags;
	std::vector<DocExample> examples;
	std::vector<DocSub> subs;
	bool json = false; // supports --json (every subcommand, where subs exist)
};

const std::vector<CommandDoc> &command_docs();
const CommandDoc *find_command_doc(const std::string &name);

} // namespace vb::cli
