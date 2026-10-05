#include <doctest/doctest.h>

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

#include "vb/core/hash.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/paletted_chunk_store.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/structure.hpp"

using namespace vb;
using namespace vb::worldgen;

namespace {

// A flat-ish, single-biome pipeline with one structure and one rule, built in
// C++ the way worldgen_test.cpp's make_test_pipeline is.
struct Fixture {
	world::BlockRegistry registry = world::BlockRegistry::base();
	std::shared_ptr<PackWorldGenPipeline> pipeline = std::make_shared<PackWorldGenPipeline>();
	WorldGenParams params;

	explicit Fixture(std::function<double(double, double)> height = [](double, double) { return 64.5; }) {
		params.seed = 0xABCDEF;
		pipeline->height_field = std::move(height);
		pipeline->sea_level = 62;
		pipeline->soil_depth = 3;
		std::vector<BiomeEntry> entries(1);
		entries[0].name = "test:plains";
		entries[0].surface = registry.find("base:grass");
		entries[0].filler = registry.find("base:dirt");
		entries[0].stone = registry.find("base:stone");
		entries[0].adjacency = { 1.0 };
		pipeline->biomes = BiomeSelector(params.seed, 128.0, entries);
		pipeline->decoration.assign(1, {});
		pipeline->replaceable.assign(registry.size(), 0);
		pipeline->replaceable[static_cast<std::size_t>(registry.find("base:leaves"))] = 1;
	}

	core::BlockId id(const char *name) const { return registry.find(name); }

	// Adds a structure and a rule using it; returns the rule for tweaking.
	PlacementRule &add(StructureDef def, PlacementRule rule) {
		rule.structure = static_cast<std::uint32_t>(pipeline->structures.size());
		pipeline->structures.push_back(std::move(def));
		pipeline->decoration[0].push_back(std::move(rule));
		return pipeline->decoration[0].back();
	}

	WorldGenerator generator() const { return WorldGenerator(params, registry, pipeline); }
};

// 3x2x3, anchor (1,0,1): wood trunk on the anchor, leaves one block +x of it
// on the second layer, sand one block +z of it. Asymmetric on purpose.
StructureDef arm_structure(const Fixture &f) {
	StructureDef def;
	def.name = "test:arm";
	def.anchor = { 1, 0, 1 };
	def.radius_xz = 1;
	StructureVariant v;
	v.size = { 3, 2, 3 };
	v.cells.assign(18, kKeepCell);
	const auto at = [&](int x, int y, int z) -> core::BlockId & {
		return v.cells[static_cast<std::size_t>((y * 3 + z) * 3 + x)];
	};
	at(1, 0, 1) = f.id("base:wood");
	at(2, 1, 1) = f.id("base:leaves");
	at(1, 1, 2) = f.id("base:sand");
	def.variants.push_back(std::move(v));
	return def;
}

PlacementRule base_rule() {
	PlacementRule r;
	r.spawn_rate = 2.0;
	r.min_spacing = 4;
	r.max_slope = 8;
	return r;
}

// Lazily generated world for point queries.
class World {
public:
	explicit World(const WorldGenerator &gen) : gen_(gen) {}
	core::BlockId at(int x, int y, int z) {
		const core::ChunkCoord c{ floor_div(x), floor_div(y), floor_div(z) };
		auto key = std::make_tuple(c.x, c.y, c.z);
		auto it = chunks_.find(key);
		if (it == chunks_.end()) {
			auto chunk = std::make_unique<world::Chunk>(c);
			gen_.generate(*chunk);
			it = chunks_.emplace(key, std::move(chunk)).first;
		}
		return it->second->blocks().get(
				world::index_of(x - c.x * world::kChunkDim, y - c.y * world::kChunkDim,
						z - c.z * world::kChunkDim));
	}
	std::size_t count(const world::Chunk &chunk, core::BlockId block) const {
		std::size_t n = 0;
		for (std::size_t i = 0; i < world::kChunkVolume; ++i) {
			n += chunk.blocks().get(i) == block ? 1u : 0u;
		}
		return n;
	}

private:
	static int floor_div(int v) { return static_cast<int>(std::floor(v / 32.0)); }
	const WorldGenerator &gen_;
	std::map<std::tuple<int, int, int>, std::unique_ptr<world::Chunk>> chunks_;
};

std::uint64_t digest_chunk(const WorldGenerator &gen, core::ChunkCoord c) {
	world::Chunk chunk(c);
	gen.generate(chunk);
	core::Fnv1a h;
	for (std::size_t i = 0; i < world::kChunkVolume; ++i) {
		h.update_u<std::uint16_t>(static_cast<std::uint16_t>(chunk.blocks().get(i)));
	}
	return h.digest();
}

} // namespace

TEST_CASE("structures that straddle chunk borders are stamped whole, with no extras") {
	Fixture f;
	f.add(arm_structure(f), base_rule());
	const WorldGenerator gen = f.generator();
	World w(gen);

	const auto placements = gen.structure_placements(-32, -32, 127, 127);
	REQUIRE(placements.size() > 50);

	bool crossed_border = false;
	for (const StructurePlacement &p : placements) {
		CHECK(p.ground_y == 64);
		CHECK(w.at(p.x, 65, p.z) == f.id("base:wood"));
		// Footprint straddles a chunk border when the +/-1 neighbors fall in
		// a different chunk column.
		crossed_border = crossed_border || (p.x % 32 == 0 || p.x % 32 == 31 || p.z % 32 == 0 ||
												p.z % 32 == 31);
	}
	CHECK(crossed_border);

	// No stray wood: every wood block in the region is an anchor.
	std::size_t wood = 0;
	for (int cz = -1; cz <= 3; ++cz) {
		for (int cx = -1; cx <= 3; ++cx) {
			world::Chunk chunk({ cx, 2, cz });
			gen.generate(chunk);
			wood += w.count(chunk, f.id("base:wood"));
		}
	}
	std::size_t anchors = 0;
	for (const auto &p : gen.structure_placements(-32, -32, 127, 127)) {
		(void)p;
		++anchors;
	}
	CHECK(wood == anchors);
}

TEST_CASE("every vertical chunk of a column agrees on placements, including across a vertical border") {
	// Ground at y=94: the anchor cell is y=95 (chunk y=2), the next layer y=96 (chunk y=3).
	Fixture f([](double, double) { return 94.5; });
	PlacementRule rule = base_rule();
	rule.spawn_rate = 6.0;
	f.add(arm_structure(f), rule);
	const WorldGenerator gen = f.generator();

	const auto placements = gen.structure_placements(0, 0, 31, 31);
	REQUIRE(placements.size() > 3);
	world::Chunk low({ 0, 2, 0 });
	world::Chunk high({ 0, 3, 0 });
	world::Chunk below({ 0, 1, 0 });
	gen.generate(low);
	gen.generate(high);
	gen.generate(below);
	World w(gen);
	CHECK(w.count(low, f.id("base:wood")) == placements.size());
	// Sand/leaves sit on the second layer, which crosses into chunk y=3. A
	// placement near the +x/+z edge can leak its leaves/sand into the next
	// column, hence >=.
	CHECK(w.count(high, f.id("base:leaves")) >= placements.size() - 1);
	CHECK(w.count(below, f.id("base:wood")) == 0);
}

TEST_CASE("chunk generation order and threads do not change the result") {
	Fixture f;
	f.add(arm_structure(f), base_rule());
	const WorldGenerator gen = f.generator();

	std::vector<core::ChunkCoord> coords;
	for (int cz = -1; cz <= 1; ++cz) {
		for (int cx = -1; cx <= 1; ++cx) {
			coords.push_back({ cx, 2, cz });
		}
	}
	std::vector<std::uint64_t> forward;
	for (const auto &c : coords) {
		forward.push_back(digest_chunk(gen, c));
	}
	std::vector<std::uint64_t> reverse(coords.size());
	for (std::size_t i = coords.size(); i-- > 0;) {
		reverse[i] = digest_chunk(gen, coords[i]);
	}
	CHECK(forward == reverse);

	std::vector<std::uint64_t> threaded(coords.size());
	std::vector<std::thread> threads;
	for (std::size_t i = 0; i < coords.size(); ++i) {
		threads.emplace_back([&, i] { threaded[i] = digest_chunk(gen, coords[i]); });
	}
	for (auto &t : threads) {
		t.join();
	}
	CHECK(forward == threaded);

	// A different seed moves the placements.
	Fixture g;
	g.params.seed = 1;
	g.add(arm_structure(g), base_rule());
	CHECK(digest_chunk(g.generator(), coords[4]) != forward[4]);
}

TEST_CASE("rotation and mirror land asymmetric cells in the right place") {
	Fixture f;
	PlacementRule rule = base_rule();
	rule.rotate = true;
	rule.mirror = true;
	f.add(arm_structure(f), rule);
	const WorldGenerator gen = f.generator();
	World w(gen);

	// Where the +x leaves arm and the +z sand nub should land, per
	// (mirror, rotation). Mirror negates x first, then rotation turns the
	// offset about +y.
	const int leaves_dx[2][4] = { { 1, 0, -1, 0 }, { -1, 0, 1, 0 } };
	const int leaves_dz[2][4] = { { 0, 1, 0, -1 }, { 0, -1, 0, 1 } };
	const int sand_dx[4] = { 0, -1, 0, 1 };
	const int sand_dz[4] = { 1, 0, -1, 0 };

	std::set<std::pair<int, bool>> seen;
	for (const StructurePlacement &p : gen.structure_placements(0, 0, 255, 255)) {
		const int m = p.mirror ? 1 : 0;
		CHECK(w.at(p.x + leaves_dx[m][p.rotation], 66, p.z + leaves_dz[m][p.rotation]) ==
				f.id("base:leaves"));
		CHECK(w.at(p.x + sand_dx[p.rotation], 66, p.z + sand_dz[p.rotation]) == f.id("base:sand"));
		seen.insert({ p.rotation, p.mirror });
	}
	CHECK(seen.size() == 8);
}

TEST_CASE("rules without rotate or mirror never rotate") {
	Fixture f;
	f.add(arm_structure(f), base_rule());
	for (const auto &p : f.generator().structure_placements(0, 0, 127, 127)) {
		CHECK(p.rotation == 0);
		CHECK_FALSE(p.mirror);
	}
}

TEST_CASE("the `on` block and the slope limit reject anchors") {
	{
		Fixture f;
		PlacementRule rule = base_rule();
		rule.on = { f.id("base:sand") };
		f.add(arm_structure(f), rule);
		CHECK(f.generator().structure_placements(0, 0, 255, 255).empty());
	}
	{
		Fixture f;
		PlacementRule rule = base_rule();
		rule.on = { f.id("base:grass") };
		f.add(arm_structure(f), rule);
		CHECK_FALSE(f.generator().structure_placements(0, 0, 255, 255).empty());
	}
	{
		// The fixture's footprint columns span 2 blocks in x (the trunk and the
		// +x leaves cell) on terrain that rises 1 block per block in x, so
		// max_slope 0 rejects everything and 1 accepts.
		auto slope = [](double x, double) { return 64.5 + x; };
		Fixture steep(slope);
		PlacementRule rule = base_rule();
		rule.max_slope = 0;
		steep.add(arm_structure(steep), rule);
		CHECK(steep.generator().structure_placements(0, 0, 255, 255).empty());

		Fixture ok(slope);
		rule.max_slope = 1;
		ok.add(arm_structure(ok), rule);
		CHECK_FALSE(ok.generator().structure_placements(0, 0, 255, 255).empty());
	}
	{
		// Underwater ground is never an anchor.
		Fixture f([](double, double) { return 50.5; });
		f.add(arm_structure(f), base_rule());
		CHECK(f.generator().structure_placements(0, 0, 255, 255).empty());
	}
	{
		// y range.
		Fixture f;
		PlacementRule rule = base_rule();
		rule.y_min = 70;
		f.add(arm_structure(f), rule);
		CHECK(f.generator().structure_placements(0, 0, 255, 255).empty());
	}
}

TEST_CASE("replace policy and explicit air cells") {
	// 1x2x1, anchor on layer 1: layer 0 replaces the ground block, layer 1 is
	// the cell above it.
	const auto make = [](const Fixture &f, core::BlockId bottom) {
		StructureDef def;
		def.name = "test:post";
		def.anchor = { 0, 1, 0 };
		StructureVariant v;
		v.size = { 1, 2, 1 };
		v.cells = { bottom, f.id("base:wood") };
		def.variants.push_back(std::move(v));
		return def;
	};
	{
		Fixture f;
		PlacementRule rule = base_rule();
		rule.replace = ReplacePolicy::kAir;
		f.add(make(f, f.id("base:stone")), rule);
		const WorldGenerator gen = f.generator();
		World w(gen);
		const auto p = gen.structure_placements(0, 0, 63, 63);
		REQUIRE_FALSE(p.empty());
		CHECK(w.at(p[0].x, 64, p[0].z) == f.id("base:grass")); // not overwritten
		CHECK(w.at(p[0].x, 65, p[0].z) == f.id("base:wood"));
	}
	{
		Fixture f;
		PlacementRule rule = base_rule();
		rule.replace = ReplacePolicy::kAll;
		f.add(make(f, f.id("base:stone")), rule);
		const WorldGenerator gen = f.generator();
		World w(gen);
		const auto p = gen.structure_placements(0, 0, 63, 63);
		REQUIRE_FALSE(p.empty());
		CHECK(w.at(p[0].x, 64, p[0].z) == f.id("base:stone"));
	}
	{
		// Explicit air carves even under the default "air" policy.
		Fixture f;
		f.add(make(f, core::BlockId::kAir), base_rule());
		const WorldGenerator gen = f.generator();
		World w(gen);
		const auto p = gen.structure_placements(0, 0, 63, 63);
		REQUIRE_FALSE(p.empty());
		CHECK(w.at(p[0].x, 64, p[0].z) == core::BlockId::kAir);
	}
}

TEST_CASE("spawn_rate sets the expected placements per 32x32 column") {
	Fixture f;
	PlacementRule rule = base_rule();
	rule.spawn_rate = 2.0;
	f.add(arm_structure(f), rule);
	// 8x8 columns -> about 128 placements.
	const std::size_t n = f.generator().structure_placements(0, 0, 255, 255).size();
	CHECK(n > 80);
	CHECK(n < 190);

	Fixture none;
	rule.spawn_rate = 0.0;
	none.add(arm_structure(none), rule);
	CHECK(none.generator().structure_placements(0, 0, 255, 255).empty());
}

TEST_CASE("min_spacing keeps anchors apart") {
	Fixture f;
	PlacementRule rule = base_rule();
	rule.min_spacing = 6;
	rule.spawn_rate = 30.0; // saturate the grid
	f.add(arm_structure(f), rule);
	const auto p = f.generator().structure_placements(0, 0, 127, 127);
	REQUIRE(p.size() > 100);
	for (std::size_t i = 0; i < p.size(); ++i) {
		for (std::size_t j = i + 1; j < p.size(); ++j) {
			const int dx = std::abs(p[i].x - p[j].x);
			const int dz = std::abs(p[i].z - p[j].z);
			CHECK(std::max(dx, dz) >= 6);
		}
	}
}

TEST_CASE("cluster gates placement with low-frequency noise but keeps the mean") {
	Fixture even;
	PlacementRule rule = base_rule();
	rule.spawn_rate = 1.0;
	even.add(arm_structure(even), rule);
	Fixture clumped;
	rule.cluster = 1.0;
	clumped.add(arm_structure(clumped), rule);
	const auto a = even.generator().structure_placements(0, 0, 511, 511);
	const auto b = clumped.generator().structure_placements(0, 0, 511, 511);
	CHECK(a != b);
	CHECK(b.size() > a.size() / 2);
	CHECK(b.size() < a.size() * 2);
}

TEST_CASE("structure placement golden hash") {
	Fixture f([](double x, double z) { return 66.0 + std::sin(x * 0.05) * 3.0 + std::cos(z * 0.07) * 3.0; });
	PlacementRule rule = base_rule();
	rule.rotate = true;
	rule.mirror = true;
	rule.cluster = 0.5;
	rule.max_slope = 4;
	f.add(arm_structure(f), rule);
	const WorldGenerator gen = f.generator();

	core::Fnv1a h;
	for (int cz = -1; cz <= 1; ++cz) {
		for (int cx = -1; cx <= 1; ++cx) {
			for (int cy = 1; cy <= 3; ++cy) {
				h.update_u<std::uint64_t>(digest_chunk(gen, { cx, cy, cz }));
			}
		}
	}
	// Golden: if this changes, structure placement output changed -- bump the
	// pack version and note why.
	CHECK(h.digest() == 0x6D288955A8FE5541ULL);
}
