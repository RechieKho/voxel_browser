#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vb/core/error.hpp"
#include "vb/core/result.hpp"

// Layered configuration (spec §15): built-in defaults <- TOML file <- CLI
// overrides. A missing file is not an error (defaults are used); a malformed
// file is. Unknown keys are ignored.

namespace vb::core {

class Args; // fwd (vb/core/cli.hpp)

enum class ConfigAuthMode : std::uint8_t { kNone = 0,
	kToken = 1 };

struct ServerConfig {
	std::string bind_address = "0.0.0.0";
	std::uint16_t port = 27015;
	std::string content_pack = "content/base";
	std::uint32_t max_players = 16;
	std::uint32_t view_distance = 8; // chunks
	std::uint32_t tick_rate = 20;
	std::uint64_t world_seed = 0; // 0 = random at startup
	double gravity = 24.0;
	std::uint32_t asset_max_file_mb = 32;
	std::uint32_t asset_max_total_mb = 512;
	ConfigAuthMode auth_mode = ConfigAuthMode::kNone;
	std::string motd;
};

struct ClientConfig {
	std::uint32_t window_width = 1280;
	std::uint32_t window_height = 720;
	bool vsync = true;
	double fov = 70.0;
	std::uint32_t render_distance = 8; // chunks, clamped to server view_distance
	double mouse_sensitivity = 0.12;
	std::uint32_t asset_cache_mb = 512;
	std::string player_name = "Player";
	std::vector<std::string> recent_servers;
};

// Parse from an in-memory TOML document (used by tests and callers that already
// have the text).
Result<ServerConfig, CoreError> parse_server_config(std::string_view toml_text);
Result<ClientConfig, CoreError> parse_client_config(std::string_view toml_text);

// Load from a file path. A non-existent path yields defaults; a parse failure
// yields CoreError::kParseError.
Result<ServerConfig, CoreError> load_server_config(const std::string &path);
Result<ClientConfig, CoreError> load_client_config(const std::string &path);

// Apply recognised CLI flags on top of a config (mutates in place).
//   server: --bind --port --content-pack --tick-rate --max-players --seed --motd
//   client: --name --width --height --fov --render-distance
void apply_cli_overrides(ServerConfig &config, const Args &args);
void apply_cli_overrides(ClientConfig &config, const Args &args);

} // namespace vb::core
