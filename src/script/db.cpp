#include "vb/script/db.hpp"

#include <fstream>
#include <system_error>

#include "vb/core/log.hpp"
#include "vb/core/sha256.hpp"

namespace vb::script {

namespace {

std::string read_whole_file(const std::filesystem::path &p) {
	std::ifstream f(p, std::ios::binary | std::ios::ate);
	if (!f) {
		return {};
	}
	const auto size = f.tellg();
	f.seekg(0);
	std::string out(static_cast<std::size_t>(size), '\0');
	if (size > 0) {
		f.read(out.data(), size);
	}
	return out;
}

} // namespace

ScriptDb::ScriptDb(std::filesystem::path root) : root_(std::move(root)) {
	std::error_code ec;
	std::filesystem::create_directories(root_, ec);
}

std::filesystem::path ScriptDb::entry_path(std::string_view key) const {
	const std::string hash = core::sha256_hex(key);
	return root_ / hash.substr(0, 2) / hash;
}

std::optional<std::string> ScriptDb::get(std::string_view key) const {
	const std::filesystem::path p = entry_path(key);
	std::error_code ec;
	if (!std::filesystem::exists(p, ec)) {
		return std::nullopt;
	}
	return read_whole_file(p);
}

void ScriptDb::set(std::string_view key, std::string_view value) {
	const std::filesystem::path p = entry_path(key);
	std::error_code ec;
	std::filesystem::create_directories(p.parent_path(), ec);

	std::filesystem::path tmp = p;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		f.write(value.data(), static_cast<std::streamsize>(value.size()));
	}
	std::filesystem::rename(tmp, p, ec);
	if (ec) {
		VB_WARN("script", "vb.db: failed to commit key (write error): ",
				ec.message());
	}
}

void ScriptDb::erase(std::string_view key) {
	std::error_code ec;
	std::filesystem::remove(entry_path(key), ec);
}

} // namespace vb::script
