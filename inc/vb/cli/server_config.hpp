#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vb/cli/store.hpp" // Status

// `vb server config`: reading and editing an instance's server.toml. Edits are
// textual (one `key = value` line replaced, appended or removed) so the
// user's comments and layout survive, and are validated against the engine's
// own loader before they replace the file.

namespace vb::cli {

enum class ConfigType { String,
	Integer,
	Number,
	Bool };

struct ConfigKey {
	std::string name;
	ConfigType type;
};

// Every key voxel_browser_server reads from server.toml.
const std::vector<ConfigKey> &server_config_keys();

// Effective value of `key` (file overlaid on the engine's defaults) as text;
// nullopt for an unknown key or an unreadable file (`why` says which).
std::optional<std::string> get_server_config_value(const std::filesystem::path &file,
		const std::string &key, std::string *why = nullptr);

// "key = value" for every known key, effective values.
std::optional<std::string> dump_server_config(const std::filesystem::path &file,
		std::string *why = nullptr);

// Sets `key` to `value` (type-checked: "true"/"false", digits, a number, or
// free text for strings). Unknown keys and values the engine would not accept
// are rejected and the file is left untouched.
Status set_server_config_value(const std::filesystem::path &file, const std::string &key,
		const std::string &value);

// Removes `key`'s line so the engine default applies again.
Status unset_server_config_key(const std::filesystem::path &file, const std::string &key);

} // namespace vb::cli
