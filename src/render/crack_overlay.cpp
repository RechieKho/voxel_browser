#include "vb/render/crack_overlay.hpp"

#include <cstring>
#include <type_traits>

#include <raymath.h>
#include <rlgl.h>

namespace vb::render {

namespace {
// Exactly one block: depth separation from the block's own faces comes from
// the eye pull below, so the silhouette can match the block (and the
// selection wireframe) instead of bulging past it.
constexpr float kSide = 1.0f;

// Fraction of the eye->overlay distance the cube is pulled toward the eye.
// Perspective depth precision falls off with distance, so the separation
// has to grow with it: 1% is hundreds of 24-bit depth steps at reach
// distance (still a few with a 16-bit buffer), and is invisible on screen
// because every vertex moves along its own view ray.
constexpr float kEyePull = 0.01f;

std::uint8_t face_of(float nx, float ny, float nz) {
	if (nx > 0.5f) {
		return CrackOverlay::kPosX;
	}
	if (nx < -0.5f) {
		return CrackOverlay::kNegX;
	}
	if (ny > 0.5f) {
		return CrackOverlay::kPosY;
	}
	if (ny < -0.5f) {
		return CrackOverlay::kNegY;
	}
	return nz > 0.0f ? CrackOverlay::kPosZ : CrackOverlay::kNegZ;
}
} // namespace

CrackOverlay::CrackOverlay() {
	// GenMeshCube uploads its mesh as *static*, so UploadMesh(&mesh, true)
	// on it is rejected ("Trying to re-load an already loaded mesh") and the
	// per-stage texcoord rewrites below would stream into a static buffer.
	// Copy its CPU-side arrays into a fresh, not-yet-uploaded Mesh, add a
	// vertex-color buffer for per-face visibility, and upload that as dynamic.
	const Mesh cube = GenMeshCube(kSide, kSide, kSide);
	const auto vertex_count = static_cast<std::size_t>(cube.vertexCount);
	const auto copy = [](const auto *src, std::size_t n) {
		using T = std::remove_cv_t<std::remove_pointer_t<decltype(src)>>;
		auto *dst = static_cast<T *>(MemAlloc(static_cast<unsigned int>(n * sizeof(T))));
		std::memcpy(dst, src, n * sizeof(T));
		return dst;
	};
	mesh_.vertexCount = cube.vertexCount;
	mesh_.triangleCount = cube.triangleCount;
	mesh_.vertices = copy(cube.vertices, vertex_count * 3);
	mesh_.texcoords = copy(cube.texcoords, vertex_count * 2);
	mesh_.normals = copy(cube.normals, vertex_count * 3);
	mesh_.indices = copy(cube.indices, static_cast<std::size_t>(cube.triangleCount) * 3);
	UnloadMesh(cube);
	mesh_.colors = static_cast<unsigned char *>(MemAlloc(
			static_cast<unsigned int>(vertex_count * 4)));
	std::memset(mesh_.colors, 255, vertex_count * 4);
	UploadMesh(&mesh_, true); // dynamic: texcoords/colors are rewritten per stage/block

	base_texcoords_.assign(mesh_.texcoords, mesh_.texcoords + vertex_count * 2);
	face_bits_.resize(vertex_count);
	for (std::size_t i = 0; i < vertex_count; ++i) {
		face_bits_[i] = face_of(mesh_.normals[i * 3 + 0], mesh_.normals[i * 3 + 1],
				mesh_.normals[i * 3 + 2]);
	}
	material_ = LoadMaterialDefault();
}

CrackOverlay::~CrackOverlay() {
	UnloadMesh(mesh_);
	UnloadMaterial(material_);
}

void CrackOverlay::set_texture(Texture2D atlas) {
	material_.maps[MATERIAL_MAP_DIFFUSE].texture = atlas;
}

void CrackOverlay::begin() {
	rlDrawRenderBatchActive(); // flush batched lines (selection box) first
	rlDisableDepthMask();
}

void CrackOverlay::end() {
	rlDrawRenderBatchActive();
	rlEnableDepthMask();
}

void CrackOverlay::draw(Vector3 center, Vector3 eye, AtlasRect rect,
		unsigned char alpha, std::uint8_t hidden_faces) {
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
	if (hidden_faces != last_hidden_) {
		for (int i = 0; i < mesh_.vertexCount; ++i) {
			const bool hidden = (face_bits_[static_cast<std::size_t>(i)] & hidden_faces) != 0;
			mesh_.colors[i * 4 + 3] = hidden ? 0 : 255;
		}
		UpdateMeshBuffer(mesh_, 3, mesh_.colors, mesh_.vertexCount * 4, 0);
		last_hidden_ = hidden_faces;
	}
	material_.maps[MATERIAL_MAP_DIFFUSE].color = Color{ 255, 255, 255, alpha };
	// Translate to the block, then scale about the eye: every vertex slides
	// toward the camera along its own view ray.
	const float k = 1.0f - kEyePull;
	const Matrix transform = MatrixMultiply(
			MatrixMultiply(MatrixTranslate(center.x - eye.x, center.y - eye.y,
								   center.z - eye.z),
					MatrixScale(k, k, k)),
			MatrixTranslate(eye.x, eye.y, eye.z));
	DrawMesh(mesh_, material_, transform);
}

} // namespace vb::render
