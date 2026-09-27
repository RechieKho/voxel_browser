#include "vb/render/crack_overlay.hpp"

#include <raymath.h>

namespace vb::render {

namespace {
// Slightly larger than a unit block to avoid z-fighting against the block's
// own mesh faces -- same 1.004x fudge the old flat-cube overlay used.
constexpr float kSide = 1.004f;
} // namespace

CrackOverlay::CrackOverlay() {
	mesh_ = GenMeshCube(kSide, kSide, kSide);
	UploadMesh(&mesh_, true); // dynamic: texcoords are rewritten per stage/block
	base_texcoords_.assign(mesh_.texcoords,
			mesh_.texcoords + static_cast<std::size_t>(mesh_.vertexCount) * 2);
	material_ = LoadMaterialDefault();
}

CrackOverlay::~CrackOverlay() {
	UnloadMesh(mesh_);
	UnloadMaterial(material_);
}

void CrackOverlay::set_texture(Texture2D atlas) {
	material_.maps[MATERIAL_MAP_DIFFUSE].texture = atlas;
}

void CrackOverlay::draw(Vector3 center, AtlasRect rect, unsigned char alpha) {
	if (rect.u0 != last_rect_.u0 || rect.v0 != last_rect_.v0 ||
			rect.u1 != last_rect_.u1 || rect.v1 != last_rect_.v1) {
		for (int i = 0; i < mesh_.vertexCount; ++i) {
			const float u = base_texcoords_[static_cast<std::size_t>(i) * 2 + 0];
			const float v = base_texcoords_[static_cast<std::size_t>(i) * 2 + 1];
			mesh_.texcoords[i * 2 + 0] = rect.u0 + u * (rect.u1 - rect.u0);
			mesh_.texcoords[i * 2 + 1] = rect.v0 + v * (rect.v1 - rect.v0);
		}
		UpdateMeshBuffer(mesh_, 1, mesh_.texcoords,
				mesh_.vertexCount * 2 * static_cast<int>(sizeof(float)), 0);
		last_rect_ = rect;
	}
	material_.maps[MATERIAL_MAP_DIFFUSE].color = Color{ 255, 255, 255, alpha };
	const Matrix transform = MatrixTranslate(center.x, center.y, center.z);
	DrawMesh(mesh_, material_, transform);
}

} // namespace vb::render
