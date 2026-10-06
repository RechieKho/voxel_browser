#include "vb/editor/workspace.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "vb/editor/structure_writer.hpp"
#include "vb/script/data_script.hpp"
#include "vb/script/structure_def.hpp"

namespace vb::editor {

namespace fs = std::filesystem;

namespace {

// Evaluates one structure file and parses it. On failure returns nullopt and
// fills `error` with a message that names the file.
#if VB_WITH_LUA
std::optional<worldgen::StructureSpec> load_spec(
		const fs::path &file, const fs::path &pack_root, std::string *error) {
	auto script = script::eval_data_script(file, pack_root);
	if (!script.ok) {
		*error = script.error;
		return std::nullopt;
	}
	try {
		return script::parse_structure(script.value);
	} catch (const sol::error &e) {
		*error = file.generic_string() + ": " + e.what();
		return std::nullopt;
	}
}
#else
std::optional<worldgen::StructureSpec> load_spec(
		const fs::path &file, const fs::path &, std::string *error) {
	*error = file.generic_string() + ": built without VB_WITH_LUA";
	return std::nullopt;
}
#endif

bool write_file(const fs::path &path, const std::string &text, std::string *error) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << text;
	out.flush();
	if (!out) {
		if (error) {
			*error = "could not write " + path.generic_string();
		}
		return false;
	}
	return true;
}

} // namespace

std::optional<Workspace> Workspace::open(const fs::path &block_data_script, std::string *error) {
	CatalogResult loaded = load_block_catalog(block_data_script);
	if (!loaded.ok) {
		if (error) {
			*error = loaded.error;
		}
		return std::nullopt;
	}
	Workspace ws;
	ws.block_script_ = block_data_script;
	ws.pack_root_ = loaded.pack_root;
	ws.pack_name_ = loaded.pack_name;
	ws.catalog_ = std::move(loaded.catalog);
	ws.scan();
	return ws;
}

std::vector<fs::path> list_structure_files(const fs::path &structures_dir) {
	std::vector<fs::path> files;
	std::error_code ec;
	if (fs::is_directory(structures_dir, ec)) {
		for (const auto &entry : fs::directory_iterator(structures_dir, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == ".lua" && entry.path().stem() != kAllIndexStem) {
				files.push_back(entry.path());
			}
		}
	}
	std::sort(files.begin(), files.end());
	return files;
}

bool write_structures_index(const fs::path &structures_dir, std::string *error) {
	std::vector<std::string> stems;
	for (const fs::path &file : list_structure_files(structures_dir)) {
		stems.push_back(file.stem().string());
	}
	return write_file(structures_dir / (std::string(kAllIndexStem) + ".lua"), write_all_index(stems), error);
}

bool create_structure_file(const fs::path &pack_root, const std::string &name, core::IVec3 size, fs::path *out_path, std::string *error) {
	const auto fail = [&](const std::string &msg) {
		if (error) {
			*error = msg;
		}
		return false;
	};
	if (name.empty() || name.back() == ':') {
		return fail("structure name '" + name + "' is empty");
	}
	const fs::path dir = pack_root / "structures";
	const fs::path file = dir / (structure_file_stem(name) + ".lua");
	std::error_code ec;
	if (fs::exists(file, ec)) {
		return fail(file.generic_string() + " already exists");
	}
	StructureDoc doc = StructureDoc::create(name, size, { size.x / 2, 0, size.z / 2 });
	const worldgen::StructureSpec spec = doc.to_spec();
	const std::string bad = worldgen::validate_structure_spec(spec);
	if (!bad.empty()) {
		return fail(bad);
	}
	if (!write_file(file, write_structure_lua(spec), error) || !write_structures_index(dir, error)) {
		return false;
	}
	if (out_path) {
		*out_path = file;
	}
	return true;
}

void Workspace::scan() {
	structures_.clear();
	for (const fs::path &file : list_structure_files(structures_dir())) {
		StructureEntry entry;
		entry.path = file;
		std::string error;
		if (const auto spec = load_spec(file, pack_root_, &error)) {
			entry.name = spec->name;
			entry.ok = true;
		} else {
			entry.name = file.stem().string();
			errors_.push_back({ file.generic_string(), error });
		}
		structures_.push_back(std::move(entry));
	}
}

bool Workspace::reload() {
	errors_.clear();
	CatalogResult loaded = load_block_catalog(block_script_);
	bool ok = loaded.ok;
	if (loaded.ok) {
		pack_root_ = loaded.pack_root;
		pack_name_ = loaded.pack_name;
		catalog_ = std::move(loaded.catalog);
	} else {
		errors_.push_back({ block_script_.generic_string(), loaded.error });
	}
	scan();
	return ok;
}

std::optional<StructureDoc> Workspace::open_structure(
		const std::string &name_or_stem, std::string *error) const {
	for (const StructureEntry &entry : structures_) {
		if (entry.name != name_or_stem && entry.path.stem() != name_or_stem) {
			continue;
		}
		std::string load_error;
		const auto spec = load_spec(entry.path, pack_root_, &load_error);
		if (!spec) {
			if (error) {
				*error = load_error;
			}
			return std::nullopt;
		}
		return StructureDoc::from_spec(*spec, error);
	}
	if (error) {
		*error = "no structure named '" + name_or_stem + "' in " + structures_dir().generic_string();
	}
	return std::nullopt;
}

fs::path Workspace::path_for(const std::string &doc_name) const {
	return structures_dir() / (structure_file_stem(doc_name) + ".lua");
}

bool Workspace::write_index(std::string *error) const { return write_structures_index(structures_dir(), error); }

bool Workspace::save(StructureDoc &doc, std::string *error) {
	const worldgen::StructureSpec spec = doc.to_spec();
	const std::string bad = worldgen::validate_structure_spec(spec);
	if (!bad.empty()) {
		if (error) {
			*error = bad;
		}
		return false;
	}
	if (!write_file(path_for(doc.name), write_structure_lua(spec), error)) {
		return false;
	}
	if (!write_index(error)) {
		return false;
	}
	errors_.clear();
	scan();
	return true;
}

} // namespace vb::editor
