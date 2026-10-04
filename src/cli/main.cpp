// vb -- developer CLI: manage installed copies of Voxel Browser and run them.
// Design: architecture_spec/dev-cli.md.

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "vb/cli/commands.hpp"
#include "vb/cli/layout.hpp"
#include "vb/cli/process.hpp"

int main(int argc, char **argv) {
#if defined(_WIN32)
	// `vb self update` renames the running vb.exe to vb.exe.old (a running
	// executable can be renamed but not deleted); this is the next run, so the
	// old file is no longer in use.
	{
		std::filesystem::path old = vb::cli::current_executable_path();
		if (!old.empty()) {
			old += ".old";
			std::error_code ec;
			std::filesystem::remove(old, ec);
		}
	}
#endif
	std::vector<std::string> args;
	for (int i = 1; i < argc; ++i) {
		args.emplace_back(argv[i] != nullptr ? argv[i] : "");
	}
	return vb::cli::run_cli(args, vb::cli::Layout::from_environment(), std::cout, std::cerr);
}
