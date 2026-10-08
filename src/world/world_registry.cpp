#include "vb/world/world_registry.hpp"

#include <fstream>
#include <system_error>

namespace vb::world {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kPackPrefix = "# pack: ";

struct SavedRegistry {
	std::string pack;
	std::vector<std::string> names;
};

bool read_saved(const fs::path &file, SavedRegistry &out) {
	std::ifstream in(file);
	if (!in) {
		return false;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.rfind(kPackPrefix, 0) == 0) {
			out.pack = line.substr(kPackPrefix.size());
		} else if (!line.empty() && line[0] != '#') {
			out.names.push_back(line);
		}
	}
	return true;
}

bool has_region_files(const fs::path &dir) {
	std::error_code ec;
	for (const auto &e : fs::directory_iterator(dir, ec)) {
		if (e.path().extension() == ".vbr") {
			return true;
		}
	}
	return false;
}

void write_record(const fs::path &dir, const std::vector<std::string> &names,
		std::string_view pack) {
	std::error_code ec;
	fs::create_directories(dir, ec);
	const fs::path tmp = dir / (std::string(kWorldRegistryFile) + ".tmp");
	{
		std::ofstream out(tmp, std::ios::trunc);
		out << "# Block ids of this world save (line N = id N-1). Written by the server;\n"
			   "# a pack whose blocks don't match refuses to load this world.\n"
			<< kPackPrefix << pack << '\n';
		for (const auto &n : names) {
			out << n << '\n';
		}
	}
	fs::rename(tmp, dir / kWorldRegistryFile, ec);
}

} // namespace

WorldRegistryCheck check_world_registry(const fs::path &world_dir,
		const BlockRegistry &registry, std::string_view pack_name) {
	std::vector<std::string> names;
	names.reserve(registry.size());
	for (std::size_t i = 0; i < registry.size(); ++i) {
		names.push_back(registry.get(static_cast<core::BlockId>(i)).name);
	}

	WorldRegistryCheck result;
	SavedRegistry saved;
	if (!read_saved(world_dir / kWorldRegistryFile, saved)) {
		if (has_region_files(world_dir)) {
			result.message = "world " + world_dir.string() +
					" has no block record (saved by an older version); recording pack '" +
					std::string(pack_name) + "' -- if it was made with another pack, its blocks will be wrong";
		}
		write_record(world_dir, names, pack_name);
		return result;
	}

	for (std::size_t i = 0; i < saved.names.size(); ++i) {
		const std::string here = i < names.size() ? names[i] : std::string("(none)");
		if (saved.names[i] != here) {
			result.ok = false;
			result.message = "world " + world_dir.string() + " was saved by pack '" +
					(saved.pack.empty() ? std::string("?") : saved.pack) +
					"' with different blocks (block id " + std::to_string(i) + " is '" +
					saved.names[i] + "' there, '" + here + "' in pack '" + std::string(pack_name) +
					"'). Use a different world directory, or move this one away to start a new world.";
			return result;
		}
	}
	if (saved.names.size() != names.size() || saved.pack != pack_name) {
		write_record(world_dir, names, pack_name); // blocks were added: keep the record current
	}
	return result;
}

} // namespace vb::world
