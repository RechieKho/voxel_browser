#include "vb/cli/server_config.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#include "vb/core/config.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

bool is_auth_key(const std::string &key) { return key.rfind("auth.", 0) == 0; }

const ConfigKey *find_key(const std::string &name) {
	for (const ConfigKey &k : server_config_keys()) {
		if (k.name == name) {
			return &k;
		}
	}
	return nullptr;
}

std::string known_keys_hint() {
	std::string out;
	for (const ConfigKey &k : server_config_keys()) {
		out += (out.empty() ? "" : ", ") + k.name;
	}
	return out;
}

std::string number_text(double v) {
	std::ostringstream os;
	os << v;
	return os.str();
}

std::string effective_value(const vb::core::ServerConfig &c, const std::string &key) {
	if (key == "auth.issuer")
		return c.auth.issuer;
	if (key == "auth.client_id")
		return c.auth.client_id;
	if (key == "auth.project_id")
		return c.auth.project_id;
	if (key == "auth.api_key")
		return c.auth.api_key.empty() ? std::string() : std::string("(set)"); // never echo the key
	if (key == "bind_address")
		return c.bind_address;
	if (key == "port")
		return std::to_string(c.port);
	if (key == "content_pack")
		return c.content_pack;
	if (key == "max_players")
		return std::to_string(c.max_players);
	if (key == "view_distance")
		return std::to_string(c.view_distance);
	if (key == "tick_rate")
		return std::to_string(c.tick_rate);
	if (key == "world_seed")
		return std::to_string(c.world_seed);
	if (key == "gravity")
		return number_text(c.gravity);
	if (key == "void_kill_y")
		return number_text(c.void_kill_y);
	if (key == "day_length_seconds")
		return number_text(c.day_length_seconds);
	if (key == "asset_max_file_mb")
		return std::to_string(c.asset_max_file_mb);
	if (key == "asset_max_total_mb")
		return std::to_string(c.asset_max_total_mb);
	if (key == "max_connections_per_ip")
		return std::to_string(c.max_connections_per_ip);
	if (key == "max_messages_per_second")
		return number_text(c.max_messages_per_second);
	if (key == "auth_mode")
		return c.auth_mode == vb::core::ConfigAuthMode::kToken ? "token" : "none";
	if (key == "motd")
		return c.motd;
	if (key == "persist_world")
		return c.persist_world ? "true" : "false";
	if (key == "world_dir")
		return c.world_dir;
	if (key == "autosave_interval_seconds")
		return number_text(c.autosave_interval_seconds);
	if (key == "chunk_send_budget_bytes_per_tick") {
		return std::to_string(c.chunk_send_budget_bytes_per_tick);
	}
	return {};
}

std::string quote(const std::string &s) {
	std::string out = "\"";
	for (const char c : s) {
		switch (c) {
			case '\\':
				out += "\\\\";
				break;
			case '"':
				out += "\\\"";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				out += c;
				break;
		}
	}
	return out + "\"";
}

// TOML literal for `value` as `type`, or an error.
Status literal_for(const ConfigKey &key, const std::string &value, std::string &out) {
	switch (key.type) {
		case ConfigType::String:
			if (key.name == "auth_mode" && value != "none" && value != "token") {
				return { "auth_mode must be \"none\" or \"token\"" };
			}
			out = quote(value);
			return {};
		case ConfigType::Bool:
			if (value != "true" && value != "false") {
				return { key.name + " must be true or false" };
			}
			out = value;
			return {};
		case ConfigType::Integer: {
			if (value.empty() || value.size() > 18 ||
					value.find_first_not_of("0123456789") != std::string::npos) {
				return { key.name + " must be a non-negative whole number" };
			}
			out = value;
			return {};
		}
		case ConfigType::Number: {
			char *end = nullptr;
			errno = 0;
			const double v = std::strtod(value.c_str(), &end);
			if (value.empty() || end != value.c_str() + value.size() || errno != 0 || !std::isfinite(v)) {
				return { key.name + " must be a number" };
			}
			out = value;
			return {};
		}
	}
	return { "unsupported type" };
}

// Does `line` assign `key`? (`key = ...`, tolerant of leading whitespace.)
bool assigns(const std::string &line, const std::string &key) {
	std::size_t i = line.find_first_not_of(" \t");
	if (i == std::string::npos || line.compare(i, key.size(), key) != 0) {
		return false;
	}
	i = line.find_first_not_of(" \t", i + key.size());
	return i != std::string::npos && line[i] == '=';
}

Status read_lines(const fs::path &file, std::vector<std::string> &lines) {
	std::ifstream in(file, std::ios::binary);
	if (!in) {
		return {};
	}
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lines.push_back(line);
	}
	return {};
}

// Writes `lines` to `file` only if the engine's loader accepts the result.
Status commit_validated(const fs::path &file, const std::vector<std::string> &lines) {
	std::string text;
	for (const std::string &l : lines) {
		text += l + "\n";
	}
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	fs::path probe = file;
	probe += ".check";
	{
		std::ofstream out(probe, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out) {
			return { "cannot write " + probe.string() };
		}
	}
	auto loaded = vb::core::load_server_config(probe.string());
	fs::remove(probe, ec);
	if (!loaded) {
		return { "not saved: " + std::string(vb::core::message(loaded.error())) };
	}
	fs::path tmp = file;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out) {
			return { "cannot write " + tmp.string() };
		}
	}
	fs::rename(tmp, file, ec);
	if (ec) {
		return { "cannot replace " + file.string() + ": " + ec.message() };
	}
	return {};
}

} // namespace

const std::vector<ConfigKey> &server_config_keys() {
	static const std::vector<ConfigKey> keys = {
		{ "bind_address", ConfigType::String },
		{ "port", ConfigType::Integer },
		{ "content_pack", ConfigType::String },
		{ "max_players", ConfigType::Integer },
		{ "view_distance", ConfigType::Integer },
		{ "tick_rate", ConfigType::Integer },
		{ "world_seed", ConfigType::Integer },
		{ "gravity", ConfigType::Number },
		{ "void_kill_y", ConfigType::Number },
		{ "day_length_seconds", ConfigType::Number },
		{ "asset_max_file_mb", ConfigType::Integer },
		{ "asset_max_total_mb", ConfigType::Integer },
		{ "max_connections_per_ip", ConfigType::Integer },
		{ "max_messages_per_second", ConfigType::Number },
		{ "auth_mode", ConfigType::String },
		{ "motd", ConfigType::String },
		{ "persist_world", ConfigType::Bool },
		{ "world_dir", ConfigType::String },
		{ "autosave_interval_seconds", ConfigType::Number },
		{ "chunk_send_budget_bytes_per_tick", ConfigType::Integer },
		// Deployment overrides for the pack's auth.lua (architecture_spec/auth.md
		// §4): they live in the [auth] table. They can change where identity is
		// checked, never whether.
		{ "auth.issuer", ConfigType::String },
		{ "auth.client_id", ConfigType::String },
		{ "auth.project_id", ConfigType::String },
		{ "auth.api_key", ConfigType::String },
	};
	return keys;
}

std::optional<std::string> get_server_config_value(const fs::path &file, const std::string &key,
		std::string *why) {
	if (find_key(key) == nullptr) {
		if (why != nullptr) {
			*why = "unknown key '" + key + "' (known: " + known_keys_hint() + ")";
		}
		return std::nullopt;
	}
	auto loaded = vb::core::load_server_config(file.string());
	if (!loaded) {
		if (why != nullptr) {
			*why = file.string() + ": " + std::string(vb::core::message(loaded.error()));
		}
		return std::nullopt;
	}
	return effective_value(*loaded, key);
}

std::optional<std::string> dump_server_config(const fs::path &file, std::string *why) {
	auto loaded = vb::core::load_server_config(file.string());
	if (!loaded) {
		if (why != nullptr) {
			*why = file.string() + ": " + std::string(vb::core::message(loaded.error()));
		}
		return std::nullopt;
	}
	std::string out;
	for (const ConfigKey &k : server_config_keys()) {
		const std::string v = effective_value(*loaded, k.name);
		if (is_auth_key(k.name) && v.empty()) {
			continue; // unset deployment overrides are noise
		}
		out += k.name + " = " + (k.type == ConfigType::String ? quote(v) : v) + "\n";
	}
	return out;
}

namespace {

bool is_table_header(const std::string &line) {
	const std::size_t i = line.find_first_not_of(" \t");
	return i != std::string::npos && line[i] == '[';
}

// Index of the first `[table]` header line, or lines.size() if there is none.
// Root keys must stay above it, or TOML would read them as part of the table.
std::size_t first_table(const std::vector<std::string> &lines) {
	for (std::size_t i = 0; i < lines.size(); ++i) {
		if (is_table_header(lines[i])) {
			return i;
		}
	}
	return lines.size();
}

// [begin, end) of the lines inside `[auth]` (header excluded), or false.
bool auth_section(const std::vector<std::string> &lines, std::size_t &begin, std::size_t &end) {
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const std::size_t p = lines[i].find_first_not_of(" \t");
		if (p != std::string::npos && lines[i].compare(p, 6, "[auth]") == 0) {
			begin = i + 1;
			end = begin;
			while (end < lines.size() && !is_table_header(lines[end])) {
				++end;
			}
			return true;
		}
	}
	return false;
}

} // namespace

Status set_server_config_value(const fs::path &file, const std::string &key,
		const std::string &value) {
	const ConfigKey *spec = find_key(key);
	if (spec == nullptr) {
		return { "unknown key '" + key + "' (known: " + known_keys_hint() + ")" };
	}
	std::string literal;
	if (const Status s = literal_for(*spec, value, literal); !s) {
		return s;
	}
	std::vector<std::string> lines;
	read_lines(file, lines);
	if (is_auth_key(key)) {
		const std::string leaf = key.substr(5);
		const std::string assignment = leaf + " = " + literal;
		std::size_t begin = 0;
		std::size_t end = 0;
		if (auth_section(lines, begin, end)) {
			bool replaced = false;
			for (std::size_t i = begin; i < end; ++i) {
				if (assigns(lines[i], leaf)) {
					lines[i] = assignment;
					replaced = true;
					break;
				}
			}
			if (!replaced) {
				lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(begin), assignment);
			}
		} else {
			if (!lines.empty() && !lines.back().empty()) {
				lines.push_back("");
			}
			lines.push_back("[auth]");
			lines.push_back(assignment);
		}
		return commit_validated(file, lines);
	}
	const std::string assignment = key + " = " + literal;
	const std::size_t root_end = first_table(lines);
	bool replaced = false;
	for (std::size_t i = 0; i < root_end; ++i) {
		if (assigns(lines[i], key)) {
			lines[i] = assignment; // inline comments on this one line are not kept
			replaced = true;
			break;
		}
	}
	if (!replaced) {
		// Root keys go above the first [table], never after it.
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(root_end), assignment);
	}
	return commit_validated(file, lines);
}

Status unset_server_config_key(const fs::path &file, const std::string &key) {
	if (find_key(key) == nullptr) {
		return { "unknown key '" + key + "' (known: " + known_keys_hint() + ")" };
	}
	std::vector<std::string> lines;
	read_lines(file, lines);
	if (is_auth_key(key)) {
		const std::string leaf = key.substr(5);
		std::size_t begin = 0;
		std::size_t end = 0;
		if (auth_section(lines, begin, end)) {
			for (std::size_t i = end; i > begin; --i) {
				if (assigns(lines[i - 1], leaf)) {
					lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i - 1));
				}
			}
		}
		return commit_validated(file, lines);
	}
	const std::size_t root_end = first_table(lines);
	for (std::size_t i = root_end; i > 0; --i) {
		if (assigns(lines[i - 1], key)) {
			lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i - 1));
		}
	}
	return commit_validated(file, lines);
}

} // namespace vb::cli
