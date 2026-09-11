#include "vb/world/chunk_mesher.hpp"

#include <array>

#include "vb/core/math.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/paletted_chunk_store.hpp"

namespace vb::world {

namespace {

using core::IVec3;

// Face order: +X -X +Y -Y +Z -Z.
constexpr std::array<IVec3, 6> kFaceNormal{ { { 1, 0, 0 }, { -1, 0, 0 },
		{ 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } } };

// Quad corners per face, CCW when viewed from outside. Each corner is a unit
// offset in chunk-local space added to the voxel's min corner.
constexpr std::array<std::array<IVec3, 4>, 6> kFaceCorners{ {
		{ { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } } }, // +X
		{ { { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 } } }, // -X
		{ { { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 0 } } }, // +Y
		{ { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } } }, // -Y
		{ { { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 } } }, // +Z
		{ { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } } }, // -Z
} };

// The two in-plane tangent axes for each face (u, v), used for AO neighbour
// sampling.
constexpr std::array<std::array<IVec3, 2>, 6> kFaceTangents{ {
		{ { { 0, 0, 1 }, { 0, 1, 0 } } }, // +X
		{ { { 0, 0, 1 }, { 0, 1, 0 } } }, // -X
		{ { { 1, 0, 0 }, { 0, 0, 1 } } }, // +Y
		{ { { 1, 0, 0 }, { 0, 0, 1 } } }, // -Y
		{ { { 1, 0, 0 }, { 0, 1, 0 } } }, // +Z
		{ { { 1, 0, 0 }, { 0, 1, 0 } } }, // -Z
} };

// Which (u,v) sign each of the 4 corners sits at, matching kFaceCorners order.
constexpr std::array<std::array<int, 2>, 4> kCornerUV{ { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } } };

// raylib's Mesh.indices is `unsigned short*` — a hard 16-bit vertex cap per
// chunk mesh. Water is non-opaque, so without this a fully submerged region
// meshes every internal face of every water voxel (nothing culls water against
// water) and can blow past 65535 vertices, silently wrapping the index and
// corrupting the whole chunk's geometry. Stop early rather than emit garbage;
// this should only ever bite pathological cases now that liquids cull too.
inline constexpr std::size_t kMaxMeshVertices = 65532; // multiple of 4, < 65536

float ao_level(bool side1, bool side2, bool corner) {
	if (side1 && side2) {
		return 0.0f;
	}
	return static_cast<float>(3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner)));
}

} // namespace

MeshData mesh_chunk(const ClientChunkStore &store, core::ChunkCoord coord) {
	MeshData mesh;
	const Chunk *chunk = store.find(coord);
	if (chunk == nullptr) {
		return mesh;
	}
	const BlockRegistry &reg = store.registry();
	const IVec3 origin = core::chunk_origin(coord);

	// Faces cull against anything that visually seals the gap. Liquids aren't
	// opaque (light passes through, physics doesn't collide with them) but they
	// still need to cull mesh faces against each other and against solids —
	// see the kMaxMeshVertices comment above. Water renders as a solid-looking
	// box until the Phase 4.3 transparent pass.
	const auto blocks_face = [&](IVec3 world_voxel) {
		const core::BlockId id = store.block_at(world_voxel);
		return reg.is_opaque(id) || reg.is_liquid(id);
	};

	for (int ly = 0; ly < kChunkDim; ++ly) {
		for (int lz = 0; lz < kChunkDim; ++lz) {
			for (int lx = 0; lx < kChunkDim; ++lx) {
				const core::BlockId block = chunk->get(lx, ly, lz);
				if (block == core::BlockId::kAir) {
					continue;
				}
				const IVec3 wv{ origin.x + lx, origin.y + ly, origin.z + lz };

				for (int f = 0; f < 6; ++f) {
					if (mesh.vertices.size() + 4 > kMaxMeshVertices) {
						return mesh; // hit the 16-bit index cap; see the comment above
					}
					const IVec3 n = kFaceNormal[static_cast<std::size_t>(f)];
					const IVec3 outside{ wv.x + n.x, wv.y + n.y, wv.z + n.z };
					if (blocks_face(outside)) {
						continue; // culled
					}

					const auto fu = static_cast<std::size_t>(f);
					const IVec3 tu = kFaceTangents[fu][0];
					const IVec3 tv = kFaceTangents[fu][1];
					const Light lgt = store.light_at(outside);
					const float base_light =
							static_cast<float>(lgt.max()) / 15.0f * 0.85f + 0.15f;

					const std::uint32_t first =
							static_cast<std::uint32_t>(mesh.vertices.size());
					for (int c = 0; c < 4; ++c) {
						const IVec3 corner =
								kFaceCorners[fu][static_cast<std::size_t>(c)];
						const int su = kCornerUV[static_cast<std::size_t>(c)][0];
						const int sv = kCornerUV[static_cast<std::size_t>(c)][1];

						// AO from the 3 voxels around this corner, in the face's
						// outward plane.
						const IVec3 a{ outside.x + tu.x * su, outside.y + tu.y * su,
							outside.z + tu.z * su };
						const IVec3 bpt{ outside.x + tv.x * sv,
							outside.y + tv.y * sv, outside.z + tv.z * sv };
						const IVec3 d{ outside.x + tu.x * su + tv.x * sv,
							outside.y + tu.y * su + tv.y * sv,
							outside.z + tu.z * su + tv.z * sv };
						const float ao = ao_level(blocks_face(a), blocks_face(bpt),
								blocks_face(d));
						const float vlight = base_light * (0.55f + 0.15f * ao);

						MeshVertex vert;
						vert.px = static_cast<float>(lx + corner.x);
						vert.py = static_cast<float>(ly + corner.y);
						vert.pz = static_cast<float>(lz + corner.z);
						vert.nx = static_cast<float>(n.x);
						vert.ny = static_cast<float>(n.y);
						vert.nz = static_cast<float>(n.z);
						vert.u = (c == 1 || c == 2) ? 1.0f : 0.0f;
						vert.v = (c >= 2) ? 1.0f : 0.0f;
						vert.light = vlight;
						vert.block_id = static_cast<std::uint32_t>(block);
						mesh.vertices.push_back(vert);
					}

					mesh.indices.push_back(first + 0);
					mesh.indices.push_back(first + 1);
					mesh.indices.push_back(first + 2);
					mesh.indices.push_back(first + 0);
					mesh.indices.push_back(first + 2);
					mesh.indices.push_back(first + 3);
				}
			}
		}
	}

	return mesh;
}

} // namespace vb::world
