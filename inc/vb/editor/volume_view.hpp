#pragma once

#include <optional>

#include "vb/core/math.hpp"
#include "vb/editor/block_catalog.hpp"
#include "vb/editor/structure_doc.hpp"
#include "vb/world/client_chunk_store.hpp"

// A structure variant laid out in a private ClientChunkStore so the client's
// ChunkRenderer meshes it unchanged (docs/structure-editor.md §J). Cell
// (x, y, z) sits at world voxel kVolumeOrigin + (x, y, z); keep and explicit-
// air cells are both empty voxels here (the viewport draws ghosts for the
// air ones).

namespace vb::editor {

// Keeps every chunk coordinate non-negative and leaves a margin around the
// largest structure (64 blocks).
inline constexpr core::IVec3 kVolumeOrigin{ 32, 32, 32 };

class VolumeView {
public:
	explicit VolumeView(const BlockCatalog &catalog);

	// Replaces the contents with variant `variant` of `doc`.
	void rebuild(const StructureDoc &doc, std::size_t variant);

	// Updates one cell in place (the single-voxel edit path, which relights
	// and re-meshes only what changed). `doc` supplies the name behind `cell`.
	void set_cell(const StructureDoc &doc, core::IVec3 cell_pos, Cell cell);

	const world::ClientChunkStore &store() const { return store_; }

	// World voxel for a cell position.
	static core::IVec3 to_world(core::IVec3 cell_pos) {
		return { cell_pos.x + kVolumeOrigin.x, cell_pos.y + kVolumeOrigin.y, cell_pos.z + kVolumeOrigin.z };
	}

private:
	core::BlockId id_for(const StructureDoc &doc, Cell cell) const;

	const BlockCatalog &catalog_;
	world::ClientChunkStore store_;
};

} // namespace vb::editor
