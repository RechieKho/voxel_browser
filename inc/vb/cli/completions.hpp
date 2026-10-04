#pragma once

#include <string>
#include <utility>
#include <vector>

// Shell completion scripts, generated from the dispatcher's own command table
// so they cannot drift from the commands that exist.

namespace vb::cli {

struct CompletionSpec {
	std::vector<std::pair<std::string, std::string>> commands; // name, summary
	std::vector<std::string> server_subcommands;
	std::vector<std::string> config_subcommands;
};

// "bash" | "zsh" | "fish" | "powershell". Empty string for an unknown shell.
std::string generate_completions(const std::string &shell, const CompletionSpec &spec);

} // namespace vb::cli
