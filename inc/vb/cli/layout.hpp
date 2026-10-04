#pragma once

#include <filesystem>
#include <string_view>

// The per-user directory tree the developer CLI manages (dev-cli.md §5.2).

namespace vb::cli {

class Layout {
public:
	Layout(std::filesystem::path data, std::filesystem::path config,
			std::filesystem::path cache)
		: data_(std::move(data)), config_(std::move(config)), cache_(std::move(cache)) {}

	// Roots from vb::core::user_{data,config,cache}_dir() (honours VB_HOME).
	static Layout from_environment();

	const std::filesystem::path &data() const { return data_; }
	const std::filesystem::path &config() const { return config_; }
	const std::filesystem::path &cache() const { return cache_; }

	std::filesystem::path versions_dir() const { return data_ / "versions"; }
	std::filesystem::path version_dir(std::string_view tag) const {
		return versions_dir() / tag;
	}
	std::filesystem::path links_dir() const { return data_ / "links"; }
	std::filesystem::path link_file(std::string_view name) const {
		return links_dir() / (std::string(name) + ".toml");
	}
	std::filesystem::path servers_dir() const { return data_ / "servers"; }
	std::filesystem::path singleplayer_world_dir() const {
		return data_ / "worlds" / "singleplayer";
	}
	std::filesystem::path downloads_dir() const { return data_ / "downloads"; }
	std::filesystem::path lock_file() const { return data_ / "lock"; }
	std::filesystem::path cli_toml() const { return config_ / "cli.toml"; }
	std::filesystem::path client_toml() const { return config_ / "client.toml"; }

private:
	std::filesystem::path data_;
	std::filesystem::path config_;
	std::filesystem::path cache_;
};

} // namespace vb::cli
