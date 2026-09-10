#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Tiny, dependency-free command-line parsing shared by the client and server
// entry points. This is deliberately minimal for Phase 0; the full config
// loader (TOML + layered overrides) arrives in Phase 1.1.
//
// Grammar: `--flag`, `--key value`, `--key=value`, `-k` short aliases, and bare
// positional arguments. A token that follows a bare `--key` (no `=`) and does
// not itself start with `-` is that key's value, not a positional.

namespace vb::core {

class Args {
public:
	Args(int argc, char **argv);

	// True if `--name` (or `-n` when short_name is given) is present.
	bool has(std::string_view name, char short_name = '\0') const;

	// Value of `--name <value>` / `--name=<value>`, if present.
	std::optional<std::string> value(std::string_view name) const;

	// Value of `--name`, or `fallback` when absent/empty.
	std::string value_or(std::string_view name, std::string fallback) const;

	// Integer value of `--name`, or `fallback` when absent or unparseable.
	int int_or(std::string_view name, int fallback) const;

	const std::string &program() const { return program_; }
	std::vector<std::string> positional() const;

private:
	std::string program_;
	std::vector<std::string> tokens_;
};

} // namespace vb::core
