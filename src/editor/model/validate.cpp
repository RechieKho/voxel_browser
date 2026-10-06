#include "vb/editor/validate.hpp"

#include <algorithm>
#include <set>

#include "vb/editor/structure_writer.hpp"
#include "vb/editor/workspace.hpp"
#include "vb/script/data_script.hpp"
#include "vb/script/structure_def.hpp"

namespace vb::editor {

namespace fs = std::filesystem;

ValidationReport validate_pack(const fs::path &block_data_script) {
	ValidationReport report;
	std::string error;
	auto workspace = Workspace::open(block_data_script, &error);
	if (!workspace) {
		report.issues.push_back({ ValidationIssue::Kind::kBlockScript, block_data_script.generic_string(), error });
		return report;
	}
	report.pack_name = workspace->pack_name();
	report.blocks = workspace->catalog().block_names().size();
	report.structure_files = workspace->structures().size();

	// Files that failed to load or parse.
	std::set<fs::path> broken;
	for (const EditorError &e : workspace->errors()) {
		report.issues.push_back({ ValidationIssue::Kind::kStructureFile, e.file, e.message });
		broken.insert(fs::path(e.file).lexically_normal());
	}

	std::vector<std::pair<std::string, fs::path>> names; // structure name -> file
	for (const StructureEntry &entry : workspace->structures()) {
		if (!entry.ok) {
			continue;
		}
		std::string load_error;
		const auto doc = workspace->open_structure(entry.name, &load_error);
		if (!doc) {
			report.issues.push_back({ ValidationIssue::Kind::kStructureFile, entry.path.generic_string(), load_error });
			continue;
		}
		for (const auto &prior : names) {
			if (prior.first == entry.name) {
				report.issues.push_back({ ValidationIssue::Kind::kDuplicateName, entry.path.generic_string(),
						"structure '" + entry.name + "' is also defined in " + prior.second.generic_string() });
			}
		}
		names.emplace_back(entry.name, entry.path);

		// Block names must exist in the block data script (or be a built-in).
		std::set<std::string> reported;
		for (const auto &e : doc->names.entries()) {
			if (!workspace->catalog().known(e.name) && reported.insert(e.name).second) {
				report.issues.push_back({ ValidationIssue::Kind::kMissingBlock, entry.path.generic_string(),
						"block '" + e.name + "' is not in the block data script (a block registered only by pack code won't show in the editor)" });
			}
		}
	}

	// Structure files left out of all.lua. (Without Lua the workspace never
	// opens, so this only runs in Lua builds.)
#if VB_WITH_LUA
	if (!workspace->structures().empty()) {
		const fs::path index = workspace->structures_dir() / (std::string(kAllIndexStem) + ".lua");
		std::set<std::string> indexed;
		std::error_code ec;
		if (!fs::is_regular_file(index, ec)) {
			report.issues.push_back({ ValidationIssue::Kind::kNotInIndex, index.generic_string(),
					"structures/all.lua is missing (saving in the editor or `vb structure new` writes it)" });
		} else {
			auto script = script::eval_data_script(index, workspace->pack_root());
			if (!script.ok) {
				report.issues.push_back({ ValidationIssue::Kind::kStructureFile, index.generic_string(), script.error });
			} else {
				for (const auto &kv : script.value) {
					if (kv.second.get_type() == sol::type::table) {
						try {
							indexed.insert(script::parse_structure(kv.second.as<sol::table>()).name);
						} catch (const sol::error &) {
							// The failing file is reported on its own.
						}
					}
				}
				for (const auto &[name, file] : names) {
					if (indexed.count(name) == 0) {
						report.issues.push_back({ ValidationIssue::Kind::kNotInIndex, file.generic_string(),
								"structure '" + name + "' is not listed in structures/all.lua" });
					}
				}
			}
		}
	}
#endif
	return report;
}

} // namespace vb::editor
