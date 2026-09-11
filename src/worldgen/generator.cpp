#include "vb/worldgen/generator.hpp"

#include <cmath>

#include "vb/core/math.hpp"
#include "vb/world/paletted_chunk_store.hpp"

namespace vb::worldgen {

using world::kChunkDim;

WorldGenerator::WorldGenerator(WorldGenParams params,
		const world::BlockRegistry &registry) : params_(params),
												air_(core::BlockId::kAir),
												stone_(registry.find("base:stone")),
												dirt_(registry.find("base:dirt")),
												grass_(registry.find("base:grass")),
												sand_(registry.find("base:sand")),
												water_(registry.find("base:water")) {}

int WorldGenerator::surface_height(int world_x, int world_z) const {
	const double n = core::noise::fbm2(params_.seed,
			static_cast<double>(world_x), static_cast<double>(world_z),
			params_.height_noise);
	// n in [0,1) -> centred [-1,1) -> scaled around base_height.
	const double h = params_.base_height + (n * 2.0 - 1.0) * params_.amplitude;
	return static_cast<int>(std::floor(h));
}

void WorldGenerator::generate(world::Chunk &chunk) const {
	const core::IVec3 origin = core::chunk_origin(chunk.coord());
	world::PalettedChunkStore &blocks = chunk.blocks();

	for (int lz = 0; lz < kChunkDim; ++lz) {
		for (int lx = 0; lx < kChunkDim; ++lx) {
			const int wx = origin.x + lx;
			const int wz = origin.z + lz;
			const int height = surface_height(wx, wz);
			const bool beach = height <= params_.sea_level + 1;

			for (int ly = 0; ly < kChunkDim; ++ly) {
				const int wy = origin.y + ly;
				core::BlockId block = air_;

				if (wy > height) {
					block = (wy <= params_.sea_level) ? water_ : air_;
				} else if (wy == height) {
					block = beach ? sand_ : grass_;
				} else if (wy >= height - params_.soil_depth) {
					block = beach ? sand_ : dirt_;
				} else {
					block = stone_;
				}

				if (block != air_) {
					blocks.set(world::index_of(lx, ly, lz), block);
				}
			}
		}
	}

	chunk.dirty().terrain = true;
	chunk.dirty().light = true;
	chunk.dirty().mesh = true;
	chunk.set_gen_state(world::GenState::kGenerated);
}

core::Vec3d default_spawn_position(const WorldGenerator &gen, int spawn_x,
		int spawn_z) {
	const int surface = gen.surface_height(spawn_x, spawn_z);
	// Feet one voxel above the topmost solid block (occupies [surface,
	// surface+1)), centred in the column.
	return { static_cast<double>(spawn_x) + 0.5,
		static_cast<double>(surface) + 1.0, static_cast<double>(spawn_z) + 0.5 };
}

} // namespace vb::worldgen
