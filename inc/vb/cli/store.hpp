#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vb/cli/layout.hpp"

// Installed copies of Voxel Browser: release versions under <data>/versions/
// and "links" (pseudo-versions pointing at a local build directory), plus the
// `default_version` setting in cli.toml (dev-cli.md §5).

namespace vb::cli {

enum class EntryKind { Release, Link };
enum class Binary { Client, Server, Editor };

struct Entry {
	std::string name; // "v0.2.0" or a link name
	EntryKind kind = EntryKind::Release;
	std::filesystem::path root; // directory that holds the binaries
	bool is_default = false;
	bool root_exists = true; // false for a link whose build dir vanished
};

// Plain failure value: empty `error` means success.
struct Status {
	std::string error;
	explicit operator bool() const { return error.empty(); }
};

// Quotes `s` as a TOML basic string.
std::string toml_quote(const std::string &s);
// Writes `text` to `path` (creating parent dirs) via write-then-rename, so a
// crash never leaves a truncated file behind.
Status write_text_atomic(const std::filesystem::path &path, const std::string &text);

// File name of a binary for this platform ("voxel_browser_server[.exe]"). The
// editor (vb_structure_editor) is optional: a release or build may not ship it.
std::string binary_file_name(Binary which);

// cli.toml `default_version`; nullopt when unset/unreadable.
std::optional<std::string> read_default_version(const Layout &layout);
Status write_default_version(const Layout &layout, const std::string &name);

// Releases newest-first, then links alphabetically.
std::vector<Entry> list_entries(const Layout &layout);

// Finds `name`, or the default version when `name` is empty. nullopt + `why`
// explains (nothing installed, unknown name, no default set...).
std::optional<Entry> resolve_entry(const Layout &layout, std::string_view name,
		std::string *why = nullptr);

// <entry.root>/<binary>; nullopt when the file is missing.
std::optional<std::filesystem::path> find_binary(const Entry &entry, Binary which);

// `vb link`: register `build_dir` (made absolute) under `name`.
Status add_link(const Layout &layout, std::string_view name,
		const std::filesystem::path &build_dir);
// `vb unlink`.
Status remove_link(const Layout &layout, std::string_view name);
// `vb uninstall`: deletes an installed release dir, or just the link file for
// a link (never touches the linked build directory). Clears default_version if
// it pointed at the removed entry.
Status uninstall(const Layout &layout, std::string_view name);

} // namespace vb::cli
