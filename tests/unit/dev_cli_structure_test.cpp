// Structure editor S7: `vb structure new | validate | edit`.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "vb/cli/commands.hpp"
#include "vb/cli/layout.hpp"
#include "vb/cli/store.hpp"
#include "vb/editor/structure_writer.hpp"

namespace fs = std::filesystem;
using namespace vb::cli;

namespace {

struct TempDir {
	fs::path path;
	TempDir() {
		std::random_device rd;
		path = fs::temp_directory_path() / ("vb_structure_cli_" + std::to_string(rd()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

Layout test_layout(const fs::path &root) { return Layout(root / "data", root / "config", root / "cache"); }

void write(const fs::path &p, const std::string &text) {
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

std::string slurp(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	std::ostringstream os;
	os << f.rdbuf();
	return os.str();
}

struct Run {
	int code;
	std::string out;
	std::string err;
};

Run run_vb(const Layout &layout, std::vector<std::string> args) {
	std::ostringstream out, err;
	const int code = run_cli(args, layout, out, err);
	return { code, out.str(), err.str() };
}

// A pack with a block data script and pack.toml.
fs::path make_pack(const fs::path &root) {
	const fs::path pack = root / "mypack";
	write(pack / "pack.toml", "name = \"mypack\"\n");
	write(pack / "data/blocks.lua", R"(return { { name = "mypack:marble" } })");
	return pack;
}

#if VB_WITH_LUA
std::string structure_text(const std::string &name, const std::string &block) {
	vb::worldgen::StructureSpec spec;
	spec.name = name;
	spec.size = { 2, 1, 1 };
	spec.palette['.'] = std::nullopt;
	spec.palette['M'] = block;
	spec.variants.push_back({ 1.0, { { "M." } } });
	return vb::editor::write_structure_lua(spec);
}
#endif

} // namespace

TEST_CASE("vb structure new writes an empty structure and the index") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path pack = make_pack(t.path);

	// A bare name takes the pack's name as its prefix.
	const Run r = run_vb(l, { "structure", "new", "oak", "--pack", pack.string(), "--size", "5x7x5" });
	REQUIRE_MESSAGE(r.code == 0, r.err);
	const fs::path file = pack / "structures/oak.lua";
	REQUIRE(fs::exists(file));
	const std::string text = slurp(file);
	CHECK(text.find("name = \"mypack:oak\"") != std::string::npos);
	CHECK(text.find("size = { x = 5, y = 7, z = 5 }") != std::string::npos);
	CHECK(text.find("anchor = { x = 2, y = 0, z = 2 }") != std::string::npos);
	CHECK(slurp(pack / "structures/all.lua").find("require(\"structures.oak\")") != std::string::npos);

	// A second structure joins the index; an existing file is refused.
	REQUIRE(run_vb(l, { "structure", "new", "mypack:birch", "--pack", pack.string() }).code == 0);
	CHECK(slurp(pack / "structures/all.lua").find("structures.birch") != std::string::npos);
	const Run again = run_vb(l, { "structure", "new", "oak", "--pack", pack.string() });
	CHECK(again.code != 0);
	CHECK(again.err.find("already exists") != std::string::npos);

	// Bad sizes and unknown options are usage errors.
	CHECK(run_vb(l, { "structure", "new", "x", "--pack", pack.string(), "--size", "5x7" }).code == kExitUsage);
	CHECK(run_vb(l, { "structure", "new", "x", "--pack", pack.string(), "--size", "99x1x1" }).code == kExitUsage);
	CHECK(run_vb(l, { "structure", "new", "--pack", pack.string() }).code == kExitUsage);
	CHECK(run_vb(l, { "structure", "new", "x", "--bogus" }).code == kExitUsage);
	CHECK(run_vb(l, { "structure", "bogus" }).code == kExitUsage);
	CHECK(run_vb(l, { "structure" }).code == kExitUsage);
}

#if VB_WITH_LUA

TEST_CASE("vb structure validate passes a clean pack") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path pack = make_pack(t.path);
	// One structure from `vb structure new` (which writes all.lua), one by hand.
	REQUIRE(run_vb(l, { "structure", "new", "oak", "--pack", pack.string() }).code == 0);
	write(pack / "structures/rock.lua", structure_text("mypack:rock", "mypack:marble"));
	write(pack / "structures/all.lua", "return { require(\"structures.oak\"), require(\"structures.rock\") }\n");

	const Run r = run_vb(l, { "structure", "validate", (pack / "data/blocks.lua").string() });
	CHECK_MESSAGE(r.code == 0, r.out << r.err);
	CHECK(r.out.find("ok: pack 'mypack'") != std::string::npos);
	CHECK(r.out.find("2 structure files") != std::string::npos);
}

TEST_CASE("vb structure validate reports each kind of problem") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path pack = make_pack(t.path);
	write(pack / "structures/unknown.lua", structure_text("mypack:unknown", "other:ghost"));
	write(pack / "structures/broken.lua", "return { name = 3 }");
	write(pack / "structures/dup_a.lua", structure_text("mypack:dup", "mypack:marble"));
	write(pack / "structures/dup_b.lua", structure_text("mypack:dup", "mypack:marble"));
	write(pack / "structures/left_out.lua", structure_text("mypack:left_out", "mypack:marble"));
	write(pack / "structures/all.lua",
			"return { require(\"structures.unknown\"), require(\"structures.dup_a\"), require(\"structures.dup_b\") }\n");

	const Run r = run_vb(l, { "structure", "validate", (pack / "data/blocks.lua").string() });
	CHECK(r.code == kExitFailure);
	CHECK(r.out.find("broken.lua") != std::string::npos);
	CHECK(r.out.find("block 'other:ghost' is not in the block data script") != std::string::npos);
	CHECK(r.out.find("structure 'mypack:dup' is also defined in") != std::string::npos);
	CHECK(r.out.find("structure 'mypack:left_out' is not listed in structures/all.lua") != std::string::npos);
	CHECK(r.err.find("problem(s) found") != std::string::npos);

	// A missing all.lua next to structure files is a problem too.
	fs::remove(pack / "structures/all.lua");
	const Run missing = run_vb(l, { "structure", "validate", (pack / "data/blocks.lua").string() });
	CHECK(missing.out.find("all.lua is missing") != std::string::npos);

	// A block script that doesn't load fails with its file name.
	write(pack / "data/bad.lua", "return {");
	const Run bad = run_vb(l, { "structure", "validate", (pack / "data/bad.lua").string() });
	CHECK(bad.code == kExitFailure);
	CHECK(bad.out.find("bad.lua") != std::string::npos);
}

TEST_CASE("vb structure validate with no argument finds data/blocks.lua of the current pack") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path pack = make_pack(t.path);
	const fs::path before = fs::current_path();
	fs::current_path(pack);
	const Run r = run_vb(l, { "structure", "validate" });
	fs::current_path(before);
	CHECK_MESSAGE(r.code == 0, r.out << r.err);
	CHECK(r.out.find("ok: pack 'mypack', 8 blocks, 0 structure files") != std::string::npos);
}

#endif // VB_WITH_LUA

#if !defined(_WIN32)
TEST_CASE("vb structure edit launches the editor of the chosen version with the absolute script path") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path pack = make_pack(t.path);
	write(l.version_dir("v0.2.0") / binary_file_name(Binary::Client), "x");
	write(l.version_dir("v0.2.0") / binary_file_name(Binary::Server), "x");
	REQUIRE(write_default_version(l, "v0.2.0"));

	// No editor in this install yet.
	const Run none = run_vb(l, { "structure", "edit", (pack / "data/blocks.lua").string() });
	CHECK(none.code == kExitFailure);
	CHECK(none.err.find("vb_structure_editor") != std::string::npos);

	const fs::path editor = l.version_dir("v0.2.0") / binary_file_name(Binary::Editor);
	write(editor, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$0.args\"\n");
	chmod(editor.c_str(), 0755);

	REQUIRE(run_vb(l, { "structure", "edit", (pack / "data/blocks.lua").string(), "--", "--open", "mypack:oak" }).code == 0);
	const std::string args = slurp(editor.string() + ".args");
	CHECK(args.find((pack / "data/blocks.lua").string()) != std::string::npos);
	CHECK(args.find("--open\nmypack:oak") != std::string::npos);

	// The default script is the current pack's data/blocks.lua.
	const fs::path before = fs::current_path();
	fs::current_path(pack);
	REQUIRE(run_vb(l, { "structure", "edit" }).code == 0);
	fs::current_path(before);
	CHECK(slurp(editor.string() + ".args").find("data/blocks.lua") != std::string::npos);

	// A missing script is reported, not passed on.
	const Run missing = run_vb(l, { "structure", "edit", (pack / "nope.lua").string() });
	CHECK(missing.code == kExitFailure);
	CHECK(missing.err.find("not found") != std::string::npos);

	// `vb which editor` finds it.
	const Run which = run_vb(l, { "which", "editor" });
	CHECK(which.code == 0);
	CHECK(which.out.find("vb_structure_editor") != std::string::npos);
}
#endif
