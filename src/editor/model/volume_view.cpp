#include "vb/editor/volume_view.hpp"

#include <algorithm>

#include "vb/protocol/world.hpp"
#include "vb/world/chunk_codec.hpp"

namespace vb::editor {

VolumeView::VolumeView(const BlockCatalog &catalog) : catalog_(catalog), store_(catalog.registry()) {}

core::BlockId VolumeView::id_for(const StructureDoc &doc, Cell cell) const {
	if (cell == kKeepCell) {
		return core::BlockId::kAir;
	}
	return catalog_.render_id(doc.names.name(cell));
}

void VolumeView::rebuild(const StructureDoc &doc, std::size_t variant) {
	store_ = world::ClientChunkStore(catalog_.registry());
	if (variant >= doc.variants.size()) {
		return;
	}
	const Volume &volume = doc.variants[variant].volume;
	const core::IVec3 size = volume.size();
	if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
		return;
	}

	const auto chunk_of = [](int v) { return v >= 0 ? v / world::kChunkDim : -((-v + world::kChunkDim - 1) / world::kChunkDim); };
	const core::IVec3 lo = to_world({ 0, 0, 0 });
	const core::IVec3 hi = to_world({ size.x - 1, size.y - 1, size.z - 1 });
	for (int cz = chunk_of(lo.z); cz <= chunk_of(hi.z); ++cz) {
		for (int cy = chunk_of(lo.y); cy <= chunk_of(hi.y); ++cy) {
			for (int cx = chunk_of(lo.x); cx <= chunk_of(hi.x); ++cx) {
				world::Chunk chunk({ cx, cy, cz });
				// Full-bright until the first edit relights the chunk: a
				// freestanding structure has open sky above it anyway.
				for (auto &light : chunk.light_volume()) {
					light.packed = 0xFF;
				}
				for (int lz = 0; lz < world::kChunkDim; ++lz) {
					for (int ly = 0; ly < world::kChunkDim; ++ly) {
						for (int lx = 0; lx < world::kChunkDim; ++lx) {
							const core::IVec3 cell_pos{ cx * world::kChunkDim + lx - kVolumeOrigin.x,
								cy * world::kChunkDim + ly - kVolumeOrigin.y,
								cz * world::kChunkDim + lz - kVolumeOrigin.z };
							if (!volume.in_bounds(cell_pos)) {
								continue;
							}
							const core::BlockId id = id_for(doc, volume.get(cell_pos));
							if (id != core::BlockId::kAir) {
								chunk.blocks().set(world::index_of(lx, ly, lz), id);
							}
						}
					}
				}
				protocol::S2CChunkAdd add;
				add.coord = chunk.coord();
				add.revision = 1;
				add.payload = world::encode_chunk_payload(chunk);
				(void)store_.apply_add(add);
			}
		}
	}
}

void VolumeView::set_cell(const StructureDoc &doc, core::IVec3 cell_pos, Cell cell) {
	(void)doc.variants; // only the name table is needed
	store_.edit_block(to_world(cell_pos), id_for(doc, cell));
}

} // namespace vb::editor
