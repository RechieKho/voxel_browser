#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "vb/editor/block_catalog.hpp"
#include "vb/editor/orbit_camera.hpp"
#include "vb/editor/structure_doc.hpp"
#include "vb/editor/structure_writer.hpp"
#include "vb/editor/volume.hpp"
#include "vb/editor/volume_view.hpp"
#include "vb/editor/workspace.hpp"

#if VB_WITH_LUA
#include "vb/net/loopback.hpp"
#include "vb/script/data_script.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/script/structure_def.hpp"
#endif

namespace {

using namespace vb;
namespace fs = std::filesystem;

fs::path scratch(const char *name) {
	auto dir = fs::temp_directory_path() / (std::string("vb_editor_") + name);
	fs::remove_all(dir);
	fs::create_directories(dir);
	return dir;
}

void write(const fs::path &p, const std::string &text) {
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

worldgen::StructureSpec sample_spec() {
	worldgen::StructureSpec spec;
	spec.name = "test:bush";
	spec.size = { 3, 2, 2 };
	spec.anchor = { 1, 0, 0 };
	spec.palette['.'] = std::nullopt;
	spec.palette['L'] = std::string("base:leaves");
	spec.palette['W'] = std::string("base:wood");
	spec.palette['_'] = std::string("base:air");
	spec.variants.push_back({ 2.0, { { "...", ".W." }, { "LLL", "L_L" } } });
	spec.variants.push_back({ 0.25, { { "...", ".W." }, { ".L.", "LLL" } } });
	spec.placement.on = std::vector<std::string>{ "base:grass", "base:dirt" };
	spec.placement.replace = worldgen::ReplacePolicy::kAirAndPlants;
	spec.placement.rotate = true;
	spec.placement.mirror = false;
	spec.placement.min_spacing = 5;
	spec.placement.cluster = 0.375;
	spec.placement.max_slope = 3;
	spec.placement.y_min = 10;
	spec.placement.y_max = 200;
	return spec;
}

const char *kBlocksLua = R"(return {
	{ name = "test:marble", solid = true, opaque = true },
	{ name = "test:glass", solid = true, opaque = false, texture = "textures/glass.png" },
})";

fs::path make_pack(const char *name) {
	const auto root = scratch(name);
	write(root / "pack.toml", "name = \"demo\"\nversion = \"0.1.0\"\n");
	write(root / "data/blocks.lua", kBlocksLua);
	return root;
}

} // namespace

TEST_CASE("Volume resize keeps content at the shift and fills the rest with keep") {
	editor::Volume v({ 2, 2, 2 });
	v.set(0, 0, 0, 1);
	v.set(1, 1, 1, 2);
	CHECK(v.get(5, 5, 5) == editor::kKeepCell);
	CHECK_FALSE(v.set(2, 0, 0, 3));

	const editor::Volume grown = v.resized({ 4, 3, 3 }, { 2, 1, 0 });
	CHECK(grown.size() == core::IVec3{ 4, 3, 3 });
	CHECK(grown.get(2, 1, 0) == 1);
	CHECK(grown.get(3, 2, 1) == 2);
	CHECK(grown.get(0, 0, 0) == editor::kKeepCell);

	// Shrinking drops what no longer fits.
	const editor::Volume shrunk = v.resized({ 1, 1, 1 }, { 0, 0, 0 });
	CHECK(shrunk.get(0, 0, 0) == 1);
	CHECK(shrunk.volume() == 1);
}

TEST_CASE("StructureDoc round trips a spec and assigns stable palette keys") {
	const worldgen::StructureSpec spec = sample_spec();
	std::string error;
	auto doc = editor::StructureDoc::from_spec(spec, &error);
	REQUIRE_MESSAGE(doc, error);
	CHECK(doc->variants.size() == 2);
	CHECK(doc->size() == core::IVec3{ 3, 2, 2 });
	CHECK(doc->to_spec() == spec);

	// A fresh doc assigns keys from the block names, air gets '_', and the
	// assignment doesn't change when more blocks are added later.
	auto fresh = editor::StructureDoc::create("test:fresh", { 2, 1, 1 }, { 0, 0, 0 });
	const editor::Cell stone = fresh.names.intern("base:stone");
	const editor::Cell air = fresh.names.intern("base:air");
	fresh.variants[0].volume.set(0, 0, 0, stone);
	fresh.variants[0].volume.set(1, 0, 0, air);
	auto first = fresh.to_spec();
	CHECK(first.palette.at('S') == std::optional<std::string>("base:stone"));
	CHECK(first.palette.at('_') == std::optional<std::string>("base:air"));
	CHECK(first.variants[0].layers[0][0] == "S_");
	const editor::Cell sand = fresh.names.intern("base:sand");
	fresh.variants[0].volume.set(0, 0, 0, sand);
	fresh.variants[0].volume.set(1, 0, 0, stone);
	auto second = fresh.to_spec();
	CHECK(second.palette.at('S') == std::optional<std::string>("base:stone")); // not renamed
	CHECK(second.palette.size() == 3); // keep, stone, sand: air is no longer used, so it is not written
}

TEST_CASE("StructureDoc::from_spec rejects an invalid spec with a message naming the structure") {
	auto spec = sample_spec();
	spec.variants[0].layers[0][0] = "..";
	std::string error;
	CHECK_FALSE(editor::StructureDoc::from_spec(spec, &error));
	CHECK(error.find("test:bush") != std::string::npos);
}

TEST_CASE("structure_file_stem takes the local name and sanitizes it") {
	CHECK(editor::structure_file_stem("base:oak_tree") == "oak_tree");
	CHECK(editor::structure_file_stem("plain") == "plain");
	CHECK(editor::structure_file_stem("a:b c/d") == "b_c_d");
	CHECK(editor::structure_file_stem("a:") == "structure");
}

TEST_CASE("write_all_index lists the structures in the order given") {
	const std::string text = editor::write_all_index({ "a", "b" });
	CHECK(text.find("require(\"structures.a\"),\n\trequire(\"structures.b\")") != std::string::npos);
	CHECK(editor::write_all_index({}).find("return {\n}") != std::string::npos);
}

#if VB_WITH_LUA

TEST_CASE("write_structure_lua output reads back to an equal spec and re-saves byte-identical") {
	std::vector<worldgen::StructureSpec> fixtures;
	fixtures.push_back(sample_spec());
	{
		auto bare = sample_spec();
		bare.name = "test:bare";
		bare.placement = {};
		fixtures.push_back(bare);
	}
	{
		auto odd = sample_spec();
		odd.name = "test:odd \"quoted\" \\ name";
		odd.palette['L'] = std::string("pack:with\"quote");
		odd.placement.on = std::vector<std::string>{};
		fixtures.push_back(odd);
	}

	const auto root = scratch("roundtrip");
	for (const auto &spec : fixtures) {
		const std::string text = editor::write_structure_lua(spec);
		write(root / "s.lua", text);
		auto script = script::eval_data_script(root / "s.lua", root);
		REQUIRE_MESSAGE(script.ok, script.error);
		const worldgen::StructureSpec back = script::parse_structure(script.value);
		CHECK(back == spec);

		std::string error;
		auto doc = editor::StructureDoc::from_spec(back, &error);
		REQUIRE_MESSAGE(doc, error);
		CHECK(editor::write_structure_lua(doc->to_spec()) == text);
	}
}

TEST_CASE("the catalog for content/base matches the registry the server builds from the full pack") {
	const fs::path base = fs::path(VB_PROJECT_SOURCE_DIR) / "content" / "base";
	const auto loaded = editor::load_block_catalog(base / "data" / "blocks.lua");
	REQUIRE_MESSAGE(loaded.ok, loaded.error);
	CHECK(loaded.pack_name == "base");
	CHECK(fs::equivalent(loaded.pack_root, base));

	net::LoopbackNetwork network;
	world::BlockRegistry server_registry = world::BlockRegistry::base();
	script::PackRuntime rt(network.server(), server_registry,
			fs::temp_directory_path() / "vb_editor_catalog_storage.json");
	REQUIRE(script::load_content_pack(rt, base));
	rt.freeze();

	const world::BlockRegistry &editor_registry = loaded.catalog.registry();
	// Every server block, same id, name, texture and flags; the editor adds
	// only its missing marker after them.
	REQUIRE(editor_registry.size() == server_registry.size() + 1);
	for (std::size_t i = 0; i < server_registry.size(); ++i) {
		const auto id = static_cast<core::BlockId>(i);
		const world::BlockType &a = editor_registry.get(id);
		const world::BlockType &b = server_registry.get(id);
		CHECK(a.name == b.name);
		CHECK(a.texture == b.texture);
		CHECK(a.crack_texture == b.crack_texture);
		CHECK(a.solid == b.solid);
		CHECK(a.opaque == b.opaque);
		CHECK(a.liquid == b.liquid);
		CHECK(a.light_emission == b.light_emission);
		CHECK(a.max_damage == b.max_damage);
		CHECK(a.max_stack == b.max_stack);
		CHECK(a.replaceable == b.replaceable);
	}
	CHECK(editor_registry.get(loaded.catalog.missing_id()).name == editor::kMissingBlockName);
	CHECK(loaded.catalog.known("base:leaves"));
	CHECK(loaded.catalog.known("base:air"));
	CHECK_FALSE(loaded.catalog.known("nope:nothing"));
	CHECK(loaded.catalog.render_id("nope:nothing") == loaded.catalog.missing_id());
	CHECK(loaded.catalog.render_id("base:air") == core::BlockId::kAir);
	const auto names = loaded.catalog.block_names();
	CHECK(std::find(names.begin(), names.end(), "base:air") == names.end());
	CHECK(std::find(names.begin(), names.end(), editor::kMissingBlockName) == names.end());
	CHECK(std::find(names.begin(), names.end(), "base:planks") != names.end());
}

TEST_CASE("the catalog reports script errors naming the file") {
	const auto root = make_pack("catalog_errors");
	write(root / "data/broken.lua", "return {");
	auto result = editor::load_block_catalog(root / "data/broken.lua");
	CHECK_FALSE(result.ok);
	CHECK(result.error.find("broken.lua") != std::string::npos);

	write(root / "data/noname.lua", "return { { solid = true } }");
	result = editor::load_block_catalog(root / "data/noname.lua");
	CHECK_FALSE(result.ok);
	CHECK(result.error.find("noname.lua") != std::string::npos);
	CHECK(result.error.find("entry 1") != std::string::npos);

	write(root / "data/calls_vb.lua", R"(vb.register_block({ name = "x:y" }) return {})");
	result = editor::load_block_catalog(root / "data/calls_vb.lua");
	CHECK_FALSE(result.ok);
	CHECK(result.error.find("must only return data") != std::string::npos);
}

TEST_CASE("pack root discovery walks up to pack.toml, else uses the script's folder") {
	const auto root = make_pack("packroot");
	CHECK(fs::equivalent(editor::find_pack_root(root / "data/blocks.lua"), root));
	CHECK(editor::read_pack_name(root) == "demo");

	const auto loose = scratch("loose_pack");
	write(loose / "blocks.lua", "return {}");
	CHECK(fs::equivalent(editor::find_pack_root(loose / "blocks.lua"), loose));
	CHECK(editor::read_pack_name(loose) == loose.filename().string());
}

TEST_CASE("Workspace lists structures, skips all.lua, shows broken files as errors, and saves") {
	const auto root = make_pack("workspace");
	write(root / "structures/rock.lua", editor::write_structure_lua(sample_spec()));
	write(root / "structures/broken.lua", "return { name = 3 }");
	write(root / "structures/all.lua", "return {}");

	std::string error;
	auto ws = editor::Workspace::open(root / "data/blocks.lua", &error);
	REQUIRE_MESSAGE(ws, error);
	CHECK(ws->name_prefix() == "demo:");
	CHECK(ws->catalog().known("test:marble"));

	REQUIRE(ws->structures().size() == 2);
	CHECK(ws->structures()[0].path.filename() == "broken.lua");
	CHECK_FALSE(ws->structures()[0].ok);
	CHECK(ws->structures()[1].name == "test:bush");
	CHECK(ws->structures()[1].ok);
	REQUIRE(ws->errors().size() == 1);
	CHECK(ws->errors()[0].file.find("broken.lua") != std::string::npos);

	auto doc = ws->open_structure("test:bush", &error);
	REQUIRE_MESSAGE(doc, error);
	CHECK(ws->open_structure("rock", &error)); // by file stem
	CHECK_FALSE(ws->open_structure("missing", &error));
	CHECK(error.find("missing") != std::string::npos);

	// Saving a new structure writes its file and regenerates all.lua.
	auto fresh = editor::StructureDoc::create("demo:pebble", { 1, 1, 1 }, { 0, 0, 0 });
	fresh.variants[0].volume.set(0, 0, 0, fresh.names.intern("test:marble"));
	REQUIRE_MESSAGE(ws->save(fresh, &error), error);
	CHECK(fs::exists(root / "structures/pebble.lua"));
	const std::string index = read(root / "structures/all.lua");
	CHECK(index.find("structures.broken") != std::string::npos);
	CHECK(index.find("structures.pebble") != std::string::npos);
	CHECK(index.find("structures.rock") != std::string::npos);
	CHECK(index.find("\trequire(\"structures.all\")") == std::string::npos); // never lists itself
	CHECK(ws->structures().size() == 3);

	// Removing the broken file and re-reading all.lua through a data script
	// gives the structures in file-name order.
	fs::remove(root / "structures/broken.lua");
	REQUIRE(ws->write_index(&error));
	auto script = script::eval_data_script(root / "structures/all.lua", root);
	REQUIRE_MESSAGE(script.ok, script.error);
	CHECK(script.value.size() == 2);
	CHECK(script::parse_structure(script.value[1]).name == "demo:pebble");
	CHECK(script::parse_structure(script.value[2]).name == "test:bush");
}

TEST_CASE("Workspace::open fails when the block script cannot load; reload keeps the old catalog") {
	const auto root = make_pack("reload");
	write(root / "data/bad.lua", "return {");
	std::string error;
	CHECK_FALSE(editor::Workspace::open(root / "data/bad.lua", &error));
	CHECK(error.find("bad.lua") != std::string::npos);

	auto ws = editor::Workspace::open(root / "data/blocks.lua", &error);
	REQUIRE_MESSAGE(ws, error);
	CHECK(ws->catalog().known("test:marble"));

	// Edit the script elsewhere: reload picks the new block up.
	write(root / "data/blocks.lua", R"(return { { name = "test:marble" }, { name = "test:basalt" } })");
	CHECK(ws->reload());
	CHECK(ws->catalog().known("test:basalt"));

	// Break it: the old catalog stays and the failure is listed.
	write(root / "data/blocks.lua", "return {");
	CHECK_FALSE(ws->reload());
	CHECK(ws->catalog().known("test:basalt"));
	REQUIRE_FALSE(ws->errors().empty());
	CHECK(ws->errors()[0].file.find("blocks.lua") != std::string::npos);
}

TEST_CASE("a block name the catalog doesn't know is kept on load and save and drawn as the missing marker") {
	const auto root = make_pack("missing");
	auto spec = sample_spec();
	spec.palette['W'] = std::string("pack:registered_elsewhere");
	write(root / "structures/bush.lua", editor::write_structure_lua(spec));

	std::string error;
	auto ws = editor::Workspace::open(root / "data/blocks.lua", &error);
	REQUIRE_MESSAGE(ws, error);
	auto doc = ws->open_structure("test:bush", &error);
	REQUIRE_MESSAGE(doc, error);

	CHECK_FALSE(ws->catalog().known("pack:registered_elsewhere"));
	editor::VolumeView view(ws->catalog());
	view.rebuild(*doc, 0);
	// The wood cell of the fixture (x=1, y=0, z=1) is the unknown block.
	const auto id = view.store().block_at(editor::VolumeView::to_world({ 1, 0, 1 }));
	CHECK(id == ws->catalog().missing_id());
	// Leaves resolve normally, keep cells are empty.
	CHECK(view.store().block_at(editor::VolumeView::to_world({ 0, 1, 0 })) ==
			ws->catalog().registry().find("base:leaves"));
	CHECK(view.store().block_at(editor::VolumeView::to_world({ 0, 0, 0 })) == core::BlockId::kAir);

	// Saving keeps the unknown name.
	REQUIRE_MESSAGE(ws->save(*doc, &error), error);
	CHECK(read(root / "structures/bush.lua").find("pack:registered_elsewhere") != std::string::npos);
	auto again = ws->open_structure("test:bush", &error);
	REQUIRE(again);
	CHECK(again->to_spec() == spec);
}

TEST_CASE("VolumeView set_cell edits one voxel in place") {
	const auto root = make_pack("view_edit");
	std::string error;
	auto ws = editor::Workspace::open(root / "data/blocks.lua", &error);
	REQUIRE_MESSAGE(ws, error);
	auto doc = editor::StructureDoc::create("demo:t", { 3, 3, 3 }, { 1, 0, 1 });
	editor::VolumeView view(ws->catalog());
	view.rebuild(doc, 0);
	CHECK(view.store().size() >= 1);
	const core::IVec3 at = editor::VolumeView::to_world({ 2, 1, 0 });
	CHECK(view.store().block_at(at) == core::BlockId::kAir);

	const editor::Cell marble = doc.names.intern("test:marble");
	view.set_cell(doc, { 2, 1, 0 }, marble);
	CHECK(view.store().block_at(at) == ws->catalog().registry().find("test:marble"));
	view.set_cell(doc, { 2, 1, 0 }, editor::kKeepCell);
	CHECK(view.store().block_at(at) == core::BlockId::kAir);
}

#endif // VB_WITH_LUA

TEST_CASE("OrbitCamera orbits, pans, zooms and frames a structure") {
	editor::OrbitCamera cam;
	cam.fit({ 5.0, 3.0, 5.0 }, { 10.0, 6.0, 10.0 });
	CHECK(cam.target().x == doctest::Approx(5.0));
	CHECK(cam.distance() == doctest::Approx(22.0));

	const auto eye = cam.eye();
	const double dist = std::sqrt((eye.x - 5.0) * (eye.x - 5.0) + (eye.y - 3.0) * (eye.y - 3.0) +
			(eye.z - 5.0) * (eye.z - 5.0));
	CHECK(dist == doctest::Approx(cam.distance()));

	const double before = cam.yaw();
	cam.orbit(100.0, 0.0);
	CHECK(cam.yaw() != before);
	for (int i = 0; i < 100; ++i) {
		cam.orbit(0.0, 100.0);
	}
	CHECK(cam.pitch() <= 89.0); // clamped, never flips over the pole

	const double d = cam.distance();
	cam.zoom(1.0);
	CHECK(cam.distance() < d);
	for (int i = 0; i < 200; ++i) {
		cam.zoom(1.0);
	}
	CHECK(cam.distance() == doctest::Approx(editor::OrbitCamera::kMinDistance));

	const auto t = cam.target();
	cam.pan(50.0, 0.0);
	CHECK((cam.target().x != t.x || cam.target().z != t.z));
}

// ---------------------------------------------------------------------------
// S4: shapes, picking, commands and the edit session.

#include "vb/editor/commands.hpp"
#include "vb/editor/edit_session.hpp"
#include "vb/editor/pick.hpp"
#include "vb/editor/shapes.hpp"

namespace {

editor::EditSession make_session(core::IVec3 size = { 5, 4, 5 }, core::IVec3 anchor = { 2, 0, 2 }) {
	editor::EditSession s(editor::StructureDoc::create("test:s", size, anchor));
	s.set_brush("base:stone");
	return s;
}

std::string name_at(const editor::EditSession &s, int x, int y, int z) {
	const auto n = s.eyedrop({ x, y, z });
	return n ? *n : std::string("<keep>");
}

} // namespace

TEST_CASE("shapes: boxes, lines, mirrors and flood regions") {
	const editor::Box b = editor::box_between({ 3, 1, 2 }, { 1, 2, 4 });
	CHECK(b.min == core::IVec3{ 1, 1, 2 });
	CHECK(b.max == core::IVec3{ 3, 2, 4 });
	CHECK(b.size() == core::IVec3{ 3, 2, 3 });
	CHECK(editor::box_positions(b).size() == 18);

	editor::Box clipped = editor::box_between({ -2, -2, -2 }, { 9, 9, 9 });
	CHECK(editor::clip_box(clipped, { 4, 4, 4 }));
	CHECK(clipped.max == core::IVec3{ 3, 3, 3 });
	editor::Box outside = editor::box_between({ 10, 10, 10 }, { 12, 12, 12 });
	CHECK_FALSE(editor::clip_box(outside, { 4, 4, 4 }));

	const auto line = editor::line_positions({ 0, 0, 0 }, { 4, 2, 0 });
	CHECK(line.size() == 5);
	CHECK(line.front() == core::IVec3{ 0, 0, 0 });
	CHECK(line.back() == core::IVec3{ 4, 2, 0 });
	CHECK(editor::line_positions({ 1, 1, 1 }, { 1, 1, 1 }).size() == 1);
	// Direction doesn't matter for coverage.
	CHECK(editor::line_positions({ 4, 2, 0 }, { 0, 0, 0 }).size() == 5);

	CHECK(editor::mirror_positions({ 1, 0, 1 }, { 5, 2, 5 }, {}).size() == 1);
	CHECK(editor::mirror_positions({ 1, 0, 1 }, { 5, 2, 5 }, { true, false }).size() == 2);
	const auto four = editor::mirror_positions({ 1, 0, 1 }, { 5, 2, 5 }, { true, true });
	CHECK(four.size() == 4);
	CHECK(std::find(four.begin(), four.end(), core::IVec3{ 3, 0, 3 }) != four.end());
	// A cell on the axis maps to itself.
	CHECK(editor::mirror_positions({ 2, 0, 2 }, { 5, 2, 5 }, { true, true }).size() == 1);

	editor::Volume v({ 4, 1, 4 });
	v.set(0, 0, 0, 1);
	v.set(1, 0, 0, 1);
	v.set(3, 0, 3, 1);
	CHECK(editor::flood_region(v, { 0, 0, 0 }).size() == 2);
	// Keep cells form one connected region too.
	CHECK(editor::flood_region(v, { 2, 0, 2 }).size() == 13);
	CHECK(editor::flood_region(v, { 9, 0, 0 }).empty());
}

TEST_CASE("pick_cell finds the first cell, the face hit, and the ground plane") {
	editor::Volume v({ 4, 4, 4 });
	v.set(1, 1, 1, 1);
	editor::PickOptions opt;

	// Straight down onto (1,1,1) from above: the top face, place above it.
	auto hit = editor::pick_cell(v, { 1.5, 10.0, 1.5 }, { 0.0, -1.0, 0.0 }, opt);
	CHECK(hit.hit);
	CHECK(hit.on_cell);
	CHECK(hit.cell == core::IVec3{ 1, 1, 1 });
	CHECK(hit.normal == core::IVec3{ 0, 1, 0 });
	CHECK(hit.place == core::IVec3{ 1, 2, 1 });

	// From the -x side: the -x face.
	hit = editor::pick_cell(v, { -5.0, 1.5, 1.5 }, { 1.0, 0.0, 0.0 }, opt);
	CHECK(hit.on_cell);
	CHECK(hit.normal == core::IVec3{ -1, 0, 0 });
	CHECK(hit.place == core::IVec3{ 0, 1, 1 });

	// From inside the volume's own bounding box too (the camera never is, but
	// a ray starting at a cell must still work).
	hit = editor::pick_cell(v, { 1.5, 3.5, 1.5 }, { 0.0, -1.0, 0.0 }, opt);
	CHECK(hit.on_cell);
	CHECK(hit.cell == core::IVec3{ 1, 1, 1 });

	// A ray that misses every cell lands on the ground plane.
	hit = editor::pick_cell(v, { 3.5, 10.0, 3.5 }, { 0.0, -1.0, 0.0 }, opt);
	CHECK(hit.hit);
	CHECK_FALSE(hit.on_cell);
	CHECK(hit.cell == core::IVec3{ 3, 0, 3 });
	CHECK(hit.place == core::IVec3{ 3, 0, 3 });

	// Past the ground padding there is nothing to hit.
	hit = editor::pick_cell(v, { 40.0, 10.0, 40.0 }, { 0.0, -1.0, 0.0 }, opt);
	CHECK_FALSE(hit.hit);

	// The layer slice hides cells above it, so the ray goes through them.
	opt.max_y = 0;
	hit = editor::pick_cell(v, { 1.5, 10.0, 1.5 }, { 0.0, -1.0, 0.0 }, opt);
	CHECK_FALSE(hit.on_cell);

	// A raised ground plane (the anchor above the bottom) is what an empty
	// column hits; a cell whose top face lies in that plane is hit first.
	opt = {};
	opt.ground_y = 2;
	hit = editor::pick_cell(v, { 3.5, 10.0, 3.5 }, { 0.0, -1.0, 0.0 }, opt);
	CHECK_FALSE(hit.on_cell);
	CHECK(hit.cell == core::IVec3{ 3, 2, 3 });
	hit = editor::pick_cell(v, { 1.5, 10.0, 1.5 }, { 0.0, -1.0, 0.0 }, opt);
	CHECK(hit.on_cell);
	CHECK(hit.place == core::IVec3{ 1, 2, 1 });
}

TEST_CASE("UndoStack applies, undoes, redoes and tracks dirtiness") {
	auto doc = editor::StructureDoc::create("t:s", { 2, 2, 2 }, { 0, 0, 0 });
	editor::UndoStack stack;
	CHECK_FALSE(stack.dirty());
	CHECK_FALSE(stack.undo(doc));

	stack.push(std::make_unique<editor::CellEdits>("a", std::vector<editor::CellChange>{ { 0, { 0, 0, 0 }, 0, 5 } }), doc);
	stack.push(std::make_unique<editor::CellEdits>("b", std::vector<editor::CellChange>{ { 0, { 1, 1, 1 }, 0, 6 } }), doc);
	CHECK(doc.variants[0].volume.get(0, 0, 0) == 5);
	CHECK(stack.undo_label() == "b");
	CHECK(stack.dirty());

	stack.mark_saved();
	CHECK_FALSE(stack.dirty());
	CHECK(stack.undo(doc));
	CHECK(doc.variants[0].volume.get(1, 1, 1) == 0);
	CHECK(stack.dirty());
	CHECK(stack.redo_label() == "b");
	CHECK(stack.redo(doc));
	CHECK(doc.variants[0].volume.get(1, 1, 1) == 6);
	CHECK_FALSE(stack.dirty()); // back at the saved point

	// A new edit after an undo drops the redo history.
	CHECK(stack.undo(doc));
	stack.push(std::make_unique<editor::CellEdits>("c", std::vector<editor::CellChange>{ { 0, { 0, 1, 0 }, 0, 7 } }), doc);
	CHECK_FALSE(stack.can_redo());
	CHECK(stack.dirty());
	// The saved point lived in the dropped history: undoing past it stays dirty.
	CHECK(stack.undo(doc));
	CHECK(stack.dirty());
}

TEST_CASE("CellEdits reverts a position touched twice to its first value") {
	auto doc = editor::StructureDoc::create("t:s", { 1, 1, 1 }, { 0, 0, 0 });
	editor::CellEdits edits("twice", { { 0, { 0, 0, 0 }, 0, 1 }, { 0, { 0, 0, 0 }, 1, 2 } });
	edits.apply(doc);
	CHECK(doc.variants[0].volume.get(0, 0, 0) == 2);
	edits.revert(doc);
	CHECK(doc.variants[0].volume.get(0, 0, 0) == 0);
}

TEST_CASE("EditSession: place, remove, paint and eyedrop, each one undo step") {
	auto s = make_session();
	CHECK(s.place({ 1, 0, 1 }));
	CHECK(name_at(s, 1, 0, 1) == "base:stone");
	CHECK_FALSE(s.place({ 1, 0, 1 })); // already stone: no change, no undo step
	CHECK_FALSE(s.place({ 9, 0, 1 })); // out of bounds

	s.set_brush("base:wood");
	CHECK_FALSE(s.paint({ 0, 0, 0 })); // paint leaves keep cells alone
	CHECK(s.paint({ 1, 0, 1 }));
	CHECK(name_at(s, 1, 0, 1) == "base:wood");

	CHECK(s.remove({ 1, 0, 1 }));
	CHECK(name_at(s, 1, 0, 1) == "<keep>");
	CHECK_FALSE(s.remove({ 1, 0, 1 }));

	CHECK(s.undo()); // undo remove
	CHECK(name_at(s, 1, 0, 1) == "base:wood");
	CHECK(s.undo()); // undo paint
	CHECK(name_at(s, 1, 0, 1) == "base:stone");
	CHECK(s.undo()); // undo place
	CHECK(name_at(s, 1, 0, 1) == "<keep>");
	CHECK_FALSE(s.undo());
	CHECK(s.redo());
	CHECK(name_at(s, 1, 0, 1) == "base:stone");

	// The "keep" brush clears; air is a real block that carves.
	s.set_brush(std::nullopt);
	CHECK(s.place({ 1, 0, 1 }));
	CHECK(name_at(s, 1, 0, 1) == "<keep>");
	s.set_brush("base:air");
	CHECK(s.place({ 1, 0, 1 }));
	CHECK(name_at(s, 1, 0, 1) == "base:air");
}

TEST_CASE("EditSession: box fill, line and flood replace") {
	auto s = make_session();
	CHECK(s.box_fill({ 0, 0, 0 }, { 2, 1, 1 }));
	CHECK(s.doc().variants[0].volume.get(2, 1, 1) != editor::kKeepCell);
	std::size_t filled = 0;
	for (const auto c : s.volume().cells()) {
		filled += c != editor::kKeepCell ? 1u : 0u;
	}
	CHECK(filled == 12);
	CHECK(s.undo());
	CHECK(s.volume().get(0, 0, 0) == editor::kKeepCell);

	// A box partly outside only fills the inside.
	CHECK(s.box_fill({ 3, 3, 3 }, { 9, 9, 9 }));
	CHECK(name_at(s, 4, 3, 4) == "base:stone");

	s.set_brush("base:sand");
	CHECK(s.line({ 0, 0, 0 }, { 4, 0, 0 }));
	CHECK(name_at(s, 0, 0, 0) == "base:sand");
	CHECK(name_at(s, 4, 0, 0) == "base:sand");

	// Flood replace the connected sand line with wood; stone is untouched.
	s.set_brush("base:wood");
	CHECK(s.flood_replace({ 2, 0, 0 }));
	CHECK(name_at(s, 0, 0, 0) == "base:wood");
	CHECK(name_at(s, 4, 3, 4) == "base:stone");
	// Flooding keep with a block fills the whole connected empty space.
	s.set_brush("base:dirt");
	CHECK(s.flood_replace({ 0, 3, 0 }));
	CHECK(name_at(s, 0, 3, 0) == "base:dirt");
	CHECK(s.undo());
	CHECK(name_at(s, 0, 3, 0) == "<keep>");
}

TEST_CASE("EditSession: mirror symmetry applies every tool to the mirror images") {
	auto s = make_session({ 5, 2, 5 });
	s.symmetry = { true, true };
	CHECK(s.place({ 0, 0, 1 }));
	CHECK(name_at(s, 0, 0, 1) == "base:stone");
	CHECK(name_at(s, 4, 0, 1) == "base:stone");
	CHECK(name_at(s, 0, 0, 3) == "base:stone");
	CHECK(name_at(s, 4, 0, 3) == "base:stone");
	CHECK(s.undo()); // one step for all four
	CHECK(name_at(s, 4, 0, 3) == "<keep>");

	s.symmetry = { true, false };
	CHECK(s.line({ 0, 1, 0 }, { 2, 1, 0 }));
	CHECK(name_at(s, 4, 1, 0) == "base:stone");
	CHECK(name_at(s, 2, 1, 0) == "base:stone"); // the center cell is its own mirror
	CHECK(name_at(s, 0, 1, 4) == "<keep>");
}

TEST_CASE("EditSession: selection, copy, cut, paste and move") {
	auto s = make_session({ 6, 3, 6 });
	s.box_fill({ 0, 0, 0 }, { 1, 0, 1 });
	s.set_brush("base:wood");
	s.place({ 0, 1, 0 });

	CHECK_FALSE(s.copy_selection());
	CHECK_FALSE(s.paste({ 3, 0, 3 })); // empty clipboard
	s.select(editor::box_between({ 0, 0, 0 }, { 1, 1, 1 }));
	CHECK(s.copy_selection());
	CHECK(s.paste({ 3, 0, 3 }));
	CHECK(name_at(s, 3, 0, 3) == "base:stone");
	CHECK(name_at(s, 4, 0, 4) == "base:stone");
	CHECK(name_at(s, 3, 1, 3) == "base:wood");
	CHECK(name_at(s, 4, 1, 4) == "<keep>"); // keep cells in the clipboard overwrite nothing
	CHECK(s.undo());
	CHECK(name_at(s, 3, 0, 3) == "<keep>");

	// Cut clears the selection and keeps the copy for pasting elsewhere.
	CHECK(s.cut_selection());
	CHECK(name_at(s, 0, 0, 0) == "<keep>");
	CHECK(s.paste({ 4, 0, 4 })); // partly outside: clipped
	CHECK(name_at(s, 4, 0, 4) == "base:stone");
	CHECK(name_at(s, 5, 0, 5) == "base:stone");

	// Move shifts the selected cells and the selection itself.
	s.select(editor::box_between({ 4, 0, 4 }, { 5, 0, 5 }));
	CHECK(s.move_selection({ -2, 1, -2 }));
	CHECK(name_at(s, 4, 0, 4) == "<keep>");
	CHECK(name_at(s, 2, 1, 2) == "base:stone");
	REQUIRE(s.selection());
	CHECK(s.selection()->min == core::IVec3{ 2, 1, 2 });
	CHECK(s.undo());
	CHECK(name_at(s, 4, 0, 4) == "base:stone");
	CHECK_FALSE(s.move_selection({ 0, 0, 0 }));

	// Overlapping move: shifting a row by one keeps every cell.
	auto row = make_session({ 5, 1, 1 });
	row.box_fill({ 0, 0, 0 }, { 2, 0, 0 });
	row.select(editor::box_between({ 0, 0, 0 }, { 2, 0, 0 }));
	CHECK(row.move_selection({ 1, 0, 0 }));
	CHECK(name_at(row, 0, 0, 0) == "<keep>");
	CHECK(name_at(row, 1, 0, 0) == "base:stone");
	CHECK(name_at(row, 3, 0, 0) == "base:stone");
}

TEST_CASE("EditSession: the clipboard survives a variant switch") {
	auto s = make_session();
	s.doc().variants.push_back({ editor::Volume({ 5, 4, 5 }), 1.0 });
	s.place({ 0, 0, 0 });
	s.select(editor::box_between({ 0, 0, 0 }, { 0, 0, 0 }));
	CHECK(s.copy_selection());
	s.set_variant(1);
	CHECK(s.variant() == 1);
	CHECK(s.paste({ 2, 2, 2 }));
	CHECK(name_at(s, 2, 2, 2) == "base:stone");
	s.set_variant(0);
	CHECK(name_at(s, 2, 2, 2) == "<keep>");
}

TEST_CASE("EditSession: resize keeps content on the chosen side and moves the anchor with it") {
	auto s = make_session({ 3, 3, 3 }, { 1, 0, 1 });
	s.place({ 0, 0, 0 });
	s.place({ 2, 2, 2 });

	// Grow +2 in x anchored at the max side: content shifts +2.
	CHECK(s.resize({ 5, 3, 3 }, { editor::ResizeSide::kMax, editor::ResizeSide::kMin, editor::ResizeSide::kMin }));
	CHECK(s.doc().size() == core::IVec3{ 5, 3, 3 });
	CHECK(name_at(s, 2, 0, 0) == "base:stone");
	CHECK(name_at(s, 4, 2, 2) == "base:stone");
	CHECK(s.doc().anchor == core::IVec3{ 3, 0, 1 });

	CHECK(s.undo());
	CHECK(s.doc().size() == core::IVec3{ 3, 3, 3 });
	CHECK(s.doc().anchor == core::IVec3{ 1, 0, 1 });
	CHECK(name_at(s, 0, 0, 0) == "base:stone");

	// Shrinking clamps the anchor and drops what no longer fits.
	CHECK(s.resize({ 1, 1, 1 }, {}));
	CHECK(s.doc().anchor == core::IVec3{ 0, 0, 0 });
	CHECK(name_at(s, 0, 0, 0) == "base:stone");

	CHECK_FALSE(s.resize({ 0, 1, 1 }, {}));
	CHECK_FALSE(s.resize({ 65, 1, 1 }, {}));
	CHECK_FALSE(s.resize({ 1, 1, 1 }, {})); // unchanged
}

TEST_CASE("EditSession: resize applies to every variant; center keeps the middle") {
	auto s = make_session({ 3, 1, 3 }, { 1, 0, 1 });
	s.doc().variants.push_back({ editor::Volume({ 3, 1, 3 }), 1.0 });
	s.place({ 1, 0, 1 });
	s.set_variant(1);
	s.place({ 0, 0, 0 });
	CHECK(s.resize({ 5, 1, 5 }, { editor::ResizeSide::kCenter, editor::ResizeSide::kMin, editor::ResizeSide::kCenter }));
	CHECK(s.doc().variants[1].volume.size() == core::IVec3{ 5, 1, 5 });
	CHECK(name_at(s, 1, 0, 1) == "base:stone"); // variant 1's corner moved by +1,+1
	s.set_variant(0);
	CHECK(name_at(s, 2, 0, 2) == "base:stone");
	CHECK(s.doc().anchor == core::IVec3{ 2, 0, 2 });
}

TEST_CASE("EditSession: set_anchor is validated and undoable; dirty tracks saves") {
	auto s = make_session();
	CHECK_FALSE(s.dirty());
	CHECK(s.set_anchor({ 0, 1, 0 }));
	CHECK(s.doc().anchor == core::IVec3{ 0, 1, 0 });
	CHECK(s.dirty());
	CHECK_FALSE(s.set_anchor({ 0, 1, 0 }));
	CHECK_FALSE(s.set_anchor({ 5, 0, 0 }));
	CHECK_FALSE(s.set_anchor({ -1, 0, 0 }));
	s.mark_saved();
	CHECK_FALSE(s.dirty());
	CHECK(s.undo());
	CHECK(s.doc().anchor == core::IVec3{ 2, 0, 2 });
	CHECK(s.dirty());
	CHECK(s.redo());
	CHECK_FALSE(s.dirty());
}

TEST_CASE("EditSession::revision moves on every change") {
	auto s = make_session();
	const auto r0 = s.revision();
	s.place({ 0, 0, 0 });
	const auto r1 = s.revision();
	CHECK(r1 > r0);
	s.place({ 0, 0, 0 }); // no-op
	CHECK(s.revision() == r1);
	s.undo();
	CHECK(s.revision() > r1);
}
