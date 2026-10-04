// vb -- developer CLI: manage installed copies of Voxel Browser and run them.
// Design: architecture_spec/dev-cli.md.

#include <iostream>
#include <string>
#include <vector>

#include "vb/cli/commands.hpp"
#include "vb/cli/layout.hpp"

int main(int argc, char **argv) {
	std::vector<std::string> args;
	for (int i = 1; i < argc; ++i) {
		args.emplace_back(argv[i] != nullptr ? argv[i] : "");
	}
	return vb::cli::run_cli(args, vb::cli::Layout::from_environment(), std::cout, std::cerr);
}
