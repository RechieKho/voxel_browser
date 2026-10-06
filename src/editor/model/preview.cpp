#include "vb/editor/preview.hpp"

#include <algorithm>
#include <cmath>

#include "vb/core/noise.hpp"
#include "vb/protocol/world.hpp"
#include "vb/world/chunk_codec.hpp"
#include "vb/worldgen/biome_selector.hpp"

namespace vb::editor {

namespace {

int floor_div(int a, int b) {
	int q = a / b;
	if (a % b != 0 && ((a < 0) != (b < 0))) {
		--q;
	}
	return q;
}

core::BlockId find_block(const world::BlockRegistry &registry, const std::string &name, std::string &error) {
	const core::BlockId id = registry.find(name);
	if (id == core::BlockId::kAir && registry.get(core::BlockId::kAir).name != name) {
		error = "test terrain block '" + name + "' is not in the block data script";
	}
	return id;
}

} // namespace

std::shared_ptr<const worldgen::PackWorldGenPipeline> build_preview_pipeline(
		const BlockCatalog &catalog, const PreviewInput &input, std::string &error) {
	const world::BlockRegistry &registry = catalog.registry();
	const TerrainConfig &t = input.terrain;
	if (input.structures.empty()) {
		error = "no structure to preview";
		return nullptr;
	}

	auto pipeline = std::make_shared<worldgen::PackWorldGenPipeline>();
	const core::noise::FbmParams fbm{ 4, 1.0 / std::max(4.0, t.scale), 2.0, 0.5 };
	const std::uint64_t seed = t.seed;
	const double base = static_cast<double>(t.base_height);
	const double amplitude = t.amplitude;
	pipeline->height_field = [seed, base, amplitude, fbm](double x, double z) {
		const double n = core::noise::fbm2(seed, x, z, fbm);
		return base + (n * 2.0 - 1.0) * amplitude;
	};
	pipeline->sea_level = t.sea_level;
	pipeline->soil_depth = t.soil_depth;

	worldgen::BiomeEntry biome;
	biome.name = "preview:terrain";
	biome.probability = 1.0;
	biome.surface = find_block(registry, t.surface, error);
	biome.filler = find_block(registry, t.filler, error);
	biome.stone = find_block(registry, t.stone, error);
	biome.adjacency = { 1.0 };
	if (!error.empty()) {
		return nullptr;
	}
	pipeline->biomes = worldgen::BiomeSelector(seed, 4096.0, { biome });

	pipeline->replaceable.assign(registry.size(), 0);
	for (std::size_t i = 0; i < registry.size(); ++i) {
		pipeline->replaceable[i] = registry.get(static_cast<core::BlockId>(i)).replaceable ? 1 : 0;
	}

	pipeline->decoration.assign(1, {});
	const std::size_t count = input.include_others ? input.structures.size() : 1;
	for (std::size_t i = 0; i < count; ++i) {
		worldgen::StructureDef def;
		if (!worldgen::resolve_structure(input.structures[i], registry, def, error)) {
			return nullptr;
		}
		worldgen::PlacementRule rule;
		if (!worldgen::resolve_placement(static_cast<std::uint32_t>(pipeline->structures.size()), input.spawn_rate,
					input.structures[i].placement, registry, "structure '" + input.structures[i].name + "'", rule, error)) {
			return nullptr;
		}
		pipeline->structures.push_back(std::move(def));
		pipeline->decoration[0].push_back(std::move(rule));
	}
	return pipeline;
}

TerrainPreview::TerrainPreview(const BlockCatalog &catalog) : catalog_(catalog), store_(catalog.registry()) {}

TerrainPreview::~TerrainPreview() = default;

void TerrainPreview::start(const PreviewInput &input) {
	// Stop pending work first: the pool joins its threads on destruction and
	// drops anything still queued.
	pool_.reset();
	generator_.reset();
	store_ = world::ClientChunkStore(catalog_.registry());
	stats_ = {};
	expected_ = received_ = 0;
	started_ = true;

	const TerrainConfig &t = input.terrain;
	stats_.patch = std::clamp(t.patch, 1, 32);
	std::string error;
	const auto pipeline = build_preview_pipeline(catalog_, input, error);
	if (!pipeline) {
		stats_.error = error;
		return;
	}

	worldgen::WorldGenParams params;
	params.seed = t.seed;
	params.sea_level = t.sea_level;
	generator_ = std::make_unique<worldgen::WorldGenerator>(params, catalog_.registry(), pipeline);

	const int extent = stats_.patch * world::kChunkDim;
	// Terrain height range, sampled on a coarse grid.
	stats_.surface_min = 1 << 20;
	stats_.surface_max = -(1 << 20);
	for (int z = 0; z < extent; z += 8) {
		for (int x = 0; x < extent; x += 8) {
			const int h = generator_->surface_height(x, z);
			stats_.surface_min = std::min(stats_.surface_min, h);
			stats_.surface_max = std::max(stats_.surface_max, h);
		}
	}

	// Placement counts over the whole patch, from the same call the chunks
	// use to stamp.
	for (const auto &p : generator_->structure_placements(0, 0, extent - 1, extent - 1)) {
		(p.rule == 0 ? stats_.edited_placements : stats_.other_placements)++;
	}

	// Warnings about the edited structure's rule.
	const worldgen::PlacementRule &rule = pipeline->decoration[0][0];
	const auto surface = catalog_.registry().find(t.surface);
	if (!rule.on.empty() && std::find(rule.on.begin(), rule.on.end(), surface) == rule.on.end()) {
		stats_.warnings.push_back("'on' doesn't list the test terrain's surface block (" + t.surface +
				"), so nothing can anchor here");
	}
	if (rule.y_max < stats_.surface_min || rule.y_min > stats_.surface_max) {
		stats_.warnings.push_back("y_min..y_max (" + std::to_string(rule.y_min) + ".." + std::to_string(rule.y_max) +
				") doesn't cover the terrain heights (" + std::to_string(stats_.surface_min) + ".." +
				std::to_string(stats_.surface_max) + ")");
	}
	if (stats_.surface_max < t.sea_level) {
		stats_.warnings.push_back("the test terrain is entirely under water (sea level " + std::to_string(t.sea_level) +
				"); structures are never placed underwater");
	}
	if (stats_.edited_placements == 0) {
		stats_.warnings.push_back("no valid anchor in the preview patch (check on, slope, y range, spawn rate and spacing)");
	}

	// Chunk range: from just under the lowest surface to above the highest
	// surface (or water) plus the tallest structure.
	int tallest = 0;
	for (const worldgen::StructureDef &def : pipeline->structures) {
		for (const worldgen::StructureVariant &v : def.variants) {
			tallest = std::max(tallest, v.size.y);
		}
	}
	const int top = std::max(stats_.surface_max, t.sea_level) + tallest + 2;
	const int bottom = std::max(0, stats_.surface_min - t.soil_depth - 1);
	const int cy_lo = floor_div(bottom, world::kChunkDim);
	const int cy_hi = floor_div(top, world::kChunkDim);

	pool_ = std::make_unique<worldgen::WorldGenWorkerPool>(*generator_);
	for (int cy = cy_lo; cy <= cy_hi; ++cy) {
		for (int cz = 0; cz < stats_.patch; ++cz) {
			for (int cx = 0; cx < stats_.patch; ++cx) {
				if (pool_->submit({ cx, cy, cz })) {
					++expected_;
				}
			}
		}
	}

	const int mid = extent / 2;
	spawn_ = { static_cast<double>(mid), static_cast<double>(generator_->surface_height(mid, mid)) + 28.0,
		static_cast<double>(mid) + static_cast<double>(extent) * 0.45 };
}

bool TerrainPreview::poll() {
	if (!pool_) {
		return false;
	}
	bool any = false;
	for (auto &chunk : pool_->poll_completed()) {
		// Full-bright: the preview is about shapes, not lighting.
		for (auto &light : chunk->light_volume()) {
			light.packed = 0xFF;
		}
		protocol::S2CChunkAdd add;
		add.coord = chunk->coord();
		add.revision = ++epoch_;
		add.payload = world::encode_chunk_payload(*chunk);
		(void)store_.apply_add(add);
		++received_;
		any = true;
	}
	return any;
}

} // namespace vb::editor
