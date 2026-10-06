#include "vb/editor/block_catalog.hpp"

#include <toml++/toml.hpp>

#include "vb/script/block_def.hpp"
#include "vb/script/data_script.hpp"

namespace vb::editor {

BlockCatalog::BlockCatalog() : registry_(world::BlockRegistry::base()) {}

void BlockCatalog::add_missing_marker() {
	if (missing_id_ == core::BlockId::kAir) {
		world::BlockType missing;
		missing.name = kMissingBlockName;
		missing.texture = kMissingTexturePath;
		missing_id_ = registry_.add(missing);
	}
}

std::vector<std::string> BlockCatalog::block_names() const {
	std::vector<std::string> out;
	for (std::size_t i = 1; i < registry_.size(); ++i) {
		const auto id = static_cast<core::BlockId>(i);
		if (missing_id_ == core::BlockId::kAir || id != missing_id_) {
			out.push_back(registry_.get(id).name);
		}
	}
	return out;
}

bool BlockCatalog::known(const std::string &name) const {
	if (name == registry_.get(core::BlockId::kAir).name) {
		return true;
	}
	const core::BlockId id = registry_.find(name);
	return id != core::BlockId::kAir && (missing_id_ == core::BlockId::kAir || id != missing_id_);
}

core::BlockId BlockCatalog::render_id(const std::string &name) const {
	return known(name) ? registry_.find(name) : missing_id_;
}

std::filesystem::path find_pack_root(const std::filesystem::path &script) {
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path abs = fs::absolute(script, ec);
	fs::path dir = abs.parent_path();
	for (fs::path d = dir; !d.empty(); d = d.parent_path()) {
		if (fs::is_regular_file(d / "pack.toml", ec)) {
			return d;
		}
		if (d == d.parent_path()) {
			break;
		}
	}
	return dir;
}

std::string read_pack_name(const std::filesystem::path &pack_root) {
	try {
		const auto tbl = toml::parse_file((pack_root / "pack.toml").string());
		if (auto name = tbl["name"].value<std::string>(); name && !name->empty()) {
			return *name;
		}
	} catch (const toml::parse_error &) {
		// Fall through to the folder name.
	}
	return pack_root.filename().string();
}

#if VB_WITH_LUA

CatalogResult load_block_catalog(const std::filesystem::path &block_data_script) {
	CatalogResult result;
	result.pack_root = find_pack_root(block_data_script);
	result.pack_name = read_pack_name(result.pack_root);

	auto script = script::eval_data_script(block_data_script, result.pack_root);
	if (!script.ok) {
		result.error = script.error;
		return result;
	}
	const std::string where = block_data_script.generic_string();
	std::size_t index = 0;
	for (const auto &kv : script.value) {
		++index;
		if (kv.second.get_type() != sol::type::table) {
			result.error = where + ": entry " + std::to_string(index) + " is not a block table";
			return result;
		}
		try {
			const world::BlockType type = script::parse_block_type(kv.second.as<sol::table>());
			script::register_block_type(result.catalog.registry(), type);
		} catch (const sol::error &e) {
			result.error = where + ": entry " + std::to_string(index) + ": " + e.what();
			return result;
		}
	}
	result.catalog.add_missing_marker();
	result.ok = true;
	return result;
}

#else

CatalogResult load_block_catalog(const std::filesystem::path &block_data_script) {
	CatalogResult result;
	result.pack_root = find_pack_root(block_data_script);
	result.pack_name = read_pack_name(result.pack_root);
	result.error = block_data_script.generic_string() + ": built without VB_WITH_LUA";
	return result;
}

#endif

} // namespace vb::editor
