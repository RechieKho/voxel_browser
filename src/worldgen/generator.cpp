#include "vb/worldgen/generator.hpp"

#include <algorithm>
#include <cmath>

#include "vb/core/math.hpp"
#include "vb/world/paletted_chunk_store.hpp"

namespace vb::worldgen {

using world::kChunkDim;

namespace {

// Small deterministic counter-based PRNG for vein/decoration scatter -- built
// on the same dependency-free integer hashing as vb/core/noise.hpp (no
// <random>, so results stay bit-identical across platforms/compilers, same
// determinism requirement that file documents).
struct DetRng {
	std::uint64_t state;

	double next01() {
		state = core::noise::mix64(state);
		return core::noise::to_unit(state);
	}

	// Uniform in [0, n). `n` must be > 0.
	std::int64_t next_index(std::int64_t n) {
		state = core::noise::mix64(state);
		return static_cast<std::int64_t>(state % static_cast<std::uint64_t>(n));
	}
};

} // namespace

WorldGenerator::WorldGenerator(WorldGenParams params,
		const world::BlockRegistry &registry,
		std::shared_ptr<const PackWorldGenPipeline> pipeline) : params_(params),
																air_(core::BlockId::kAir),
																stone_(registry.find("base:stone")),
																dirt_(registry.find("base:dirt")),
																grass_(registry.find("base:grass")),
																sand_(registry.find("base:sand")),
																water_(registry.find("base:water")),
																pipeline_(std::move(pipeline)) {}

int WorldGenerator::surface_height(int world_x, int world_z) const {
	if (pipeline_) {
		return static_cast<int>(std::floor(
				pipeline_->height_field(static_cast<double>(world_x),
						static_cast<double>(world_z))));
	}
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

	if (!pipeline_) {
		// Fixed default path -- byte-identical to every pre-6.14 build, so
		// the worldgen_test.cpp golden hash stays valid unless a pack
		// actually opts into vb.worldgen.set_pipeline.
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
	} else {
		const PackWorldGenPipeline &pipe = *pipeline_;
		const int sea_level = pipe.sea_level;
		const int soil_depth = pipe.soil_depth;

		for (int lz = 0; lz < kChunkDim; ++lz) {
			for (int lx = 0; lx < kChunkDim; ++lx) {
				const int wx = origin.x + lx;
				const int wz = origin.z + lz;
				const int height = static_cast<int>(
						std::floor(pipe.height_field(wx, wz)));

				core::BlockId surface = grass_;
				core::BlockId filler = dirt_;
				core::BlockId stone = stone_;
				if (!pipe.biomes.empty()) {
					const auto &biome = pipe.biomes.biome(pipe.biomes.resolve(wx, wz));
					surface = biome.surface != core::BlockId::kAir ? biome.surface : surface;
					filler = biome.filler != core::BlockId::kAir ? biome.filler : filler;
					stone = biome.stone != core::BlockId::kAir ? biome.stone : stone;
				}

				for (int ly = 0; ly < kChunkDim; ++ly) {
					const int wy = origin.y + ly;
					core::BlockId block = air_;

					if (wy > height) {
						block = (wy <= sea_level) ? water_ : air_;
					} else if (wy == height) {
						block = surface;
					} else if (wy >= height - soil_depth) {
						block = filler;
					} else {
						block = stone;
					}

					// Carvers: any carver's density at/above its threshold in
					// this voxel's y-range carves solid, non-water rock/soil
					// back to air (caves never carve through the water table
					// or the biome's own surface crust look).
					if (block != air_ && block != water_) {
						for (const CarverDef &carver : pipe.carvers) {
							if (wy < carver.y_min || wy > carver.y_max) {
								continue;
							}
							if (carver.density(static_cast<double>(wx),
										static_cast<double>(wy), static_cast<double>(wz)) >=
									carver.threshold) {
								block = air_;
								break;
							}
						}
					}

					if (block != air_) {
						blocks.set(world::index_of(lx, ly, lz), block);
					}
				}
			}
		}

		// Vein/scatter pass (spec §6 stage 5) -- deterministic per-chunk RNG,
		// a small random-walk "blob" per vein instance that only replaces
		// `target_rock`.
		const std::uint64_t chunk_seed = core::noise::hash3(
				params_.seed, chunk.coord().x, chunk.coord().y, chunk.coord().z);
		for (std::size_t vi = 0; vi < pipe.veins.size(); ++vi) {
			const VeinDef &vein = pipe.veins[vi];
			DetRng rng{ chunk_seed ^ (0xF00D000000000000ULL + vi) };
			int count = static_cast<int>(vein.spawn_rate);
			if (rng.next01() < vein.spawn_rate - static_cast<double>(count)) {
				++count;
			}
			for (int n = 0; n < count; ++n) {
				const int ax = static_cast<int>(rng.next_index(kChunkDim));
				const int az = static_cast<int>(rng.next_index(kChunkDim));
				const int height_span =
						std::max(1, vein.height_max - vein.height_min + 1);
				int ay = vein.height_min + static_cast<int>(rng.next_index(height_span)) -
						origin.y;
				int cx = ax;
				int cy = ay;
				int cz = az;
				for (int step = 0; step < vein.vein_size; ++step) {
					if (cx >= 0 && cx < kChunkDim && cy >= 0 && cy < kChunkDim &&
							cz >= 0 && cz < kChunkDim) {
						const auto idx = world::index_of(cx, cy, cz);
						if (blocks.get(idx) == vein.target_rock) {
							blocks.set(idx, vein.block);
						}
					}
					cx += static_cast<int>(rng.next_index(3)) - 1;
					cy += static_cast<int>(rng.next_index(3)) - 1;
					cz += static_cast<int>(rng.next_index(3)) - 1;
				}
			}
		}

		// Decoration pass (spec §6 stage 6) -- schematic-only, see
		// vb/worldgen/pipeline.hpp's DecorationEntry comment for the scope
		// note (no cross-chunk placement yet: offsets landing outside this
		// chunk are simply skipped).
		if (!pipe.biomes.empty()) {
			constexpr int kHalfChunk = kChunkDim / 2;
			const auto biome_index = pipe.biomes.resolve(
					static_cast<double>(origin.x + kHalfChunk),
					static_cast<double>(origin.z + kHalfChunk));
			const auto &entries = pipe.decoration_for(biome_index);
			for (std::size_t ei = 0; ei < entries.size(); ++ei) {
				const DecorationEntry &entry = entries[ei];
				DetRng rng{ chunk_seed ^ (0xDEC0000000000000ULL + ei) };
				int count = static_cast<int>(entry.spawn_rate);
				if (rng.next01() < entry.spawn_rate - static_cast<double>(count)) {
					++count;
				}
				for (int n = 0; n < count; ++n) {
					const int ax = static_cast<int>(rng.next_index(kChunkDim));
					const int az = static_cast<int>(rng.next_index(kChunkDim));
					const int surface_wy = surface_height(origin.x + ax, origin.z + az);
					const int ay = surface_wy + 1 - origin.y;
					for (const auto &bo : entry.blocks) {
						const int lx = ax + bo.offset.x;
						const int ly = ay + bo.offset.y;
						const int lz = az + bo.offset.z;
						if (lx >= 0 && lx < kChunkDim && ly >= 0 && ly < kChunkDim &&
								lz >= 0 && lz < kChunkDim) {
							blocks.set(world::index_of(lx, ly, lz), bo.block);
						}
					}
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
