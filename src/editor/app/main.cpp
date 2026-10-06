// vb_structure_editor <block-data-script> [--open <name>]
//
// Interactive voxel tool for building biome decorations
// (docs/structure-editor.md). Reads the pack's block data script and
// structures/*.lua, writes only structures/<name>.lua and structures/all.lua.

#include <cstdlib>
#include <iostream>
#include <string>

#include "editor_app.hpp"
#include "vb/editor/workspace.hpp"

namespace {

void usage(const char *argv0) {
	std::cerr << "usage: " << argv0 << " <block-data-script> [--open <structure>]\n"
			  << "         [--check] [--screenshot <png> [--frames <n>]]\n"
			  << "  <block-data-script>  a Lua file returning a list of block tables, e.g.\n"
			  << "                       content/base/data/blocks.lua\n"
			  << "  --open <structure>   open a structure by name or file name on start\n"
			  << "  --check              load the pack, print a summary and exit (no window)\n"
			  << "  --screenshot <png>   dev: render a few frames, save a screenshot, exit\n";
}

} // namespace

int main(int argc, char **argv) {
	vb::editor::EditorOptions options;
	bool check_only = false;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const auto value = [&](const char *flag) -> const char * {
			if (i + 1 >= argc) {
				std::cerr << "vb_structure_editor: " << flag << " needs a value\n";
				usage(argv[0]);
				std::exit(EXIT_FAILURE);
			}
			return argv[++i];
		};
		if (arg == "--open") {
			options.open_name = value("--open");
		} else if (arg == "--screenshot") {
			options.screenshot = value("--screenshot");
		} else if (arg == "--frames") {
			options.frames = std::atoi(value("--frames"));
		} else if (arg == "--check") {
			check_only = true;
		} else if (arg == "-h" || arg == "--help") {
			usage(argv[0]);
			return EXIT_SUCCESS;
		} else if (!arg.empty() && arg[0] == '-') {
			std::cerr << "vb_structure_editor: unknown option '" << arg << "'\n";
			usage(argv[0]);
			return EXIT_FAILURE;
		} else if (options.block_script.empty()) {
			options.block_script = arg;
		} else {
			std::cerr << "vb_structure_editor: unexpected argument '" << arg << "'\n";
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (options.block_script.empty()) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	std::string error;
	auto workspace = vb::editor::Workspace::open(options.block_script, &error);
	if (!workspace) {
		std::cerr << "vb_structure_editor: " << error << "\n";
		return EXIT_FAILURE;
	}

	if (check_only) {
		std::cout << "pack '" << workspace->pack_name() << "' at " << workspace->pack_root().generic_string()
				  << ": " << workspace->catalog().block_names().size() << " blocks, "
				  << workspace->structures().size() << " structure files, " << workspace->errors().size()
				  << " errors\n";
		for (const auto &e : workspace->errors()) {
			std::cout << "  error: " << e.message << "\n";
		}
		return workspace->errors().empty() ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	vb::editor::EditorApp app(std::move(*workspace), std::move(options));
	return app.run();
}
