#pragma once

#include <filesystem>
#include <string>
#include <vector>

// `vb structure validate` (docs/structure-editor.md S7): headless checks of a
// pack's block data script and structure files, using the same data-script
// evaluation and parsing the engine and the editor use.

namespace vb::editor {

struct ValidationIssue {
	enum class Kind {
		kBlockScript, // the block data script didn't load
		kStructureFile, // a structure file didn't load or parse
		kMissingBlock, // a structure names a block the block data script doesn't define
		kNotInIndex, // a structure file isn't listed in structures/all.lua
		kDuplicateName, // two files define the same structure name
	};
	Kind kind = Kind::kStructureFile;
	std::string file;
	std::string message;
};

struct ValidationReport {
	std::string pack_name;
	std::size_t blocks = 0;
	std::size_t structure_files = 0;
	std::vector<ValidationIssue> issues;

	bool ok() const { return issues.empty(); }
};

ValidationReport validate_pack(const std::filesystem::path &block_data_script);

} // namespace vb::editor
