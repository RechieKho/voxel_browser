#pragma once

#include <cstdint>
#include <vector>

#include <raylib.h>

#include "vb/render/texture_atlas.hpp" // AtlasRect

// GPU-side draw helper for the crack-stage overlay cube (REMAINING_TASKS.md
// 6.5's last piece). Owns one unit cube Mesh/Material built once at
// construction; draw() remaps the mesh's texcoords into the given
// CrackAtlas rect and fades out faces that sit against an opaque neighbour
// (both re-uploaded to the GPU only when they actually change, not every
// frame) so the same geometry can show any block's crack stage without
// rebuilding it. Not unit tested -- GL-context-requiring, same posture as
// ChunkRenderer/EntityRenderer's own draw paths.

namespace vb::render {

class CrackOverlay {
public:
	// Bit per cube face for draw()'s `hidden_faces`.
	enum Face : std::uint8_t {
		kPosX = 1u << 0,
		kNegX = 1u << 1,
		kPosY = 1u << 2,
		kNegY = 1u << 3,
		kPosZ = 1u << 4,
		kNegZ = 1u << 5,
	};

	CrackOverlay();
	~CrackOverlay();

	CrackOverlay(const CrackOverlay &) = delete;
	CrackOverlay &operator=(const CrackOverlay &) = delete;

	// Hands over the already-uploaded CrackAtlas texture (see
	// CrackAtlas::upload()). Call once per session before the first draw().
	void set_texture(Texture2D atlas);

	// Call once before a run of draw() calls, inside BeginMode3D; end() after.
	// The overlay is translucent, so it never writes depth.
	void begin();
	void end();

	// Draws the overlay over the block centered at `center`, sampling `rect`
	// from the atlas set via set_texture(), tinted with `alpha` (0 =
	// invisible). `eye` is the camera position: the cube is pulled toward it
	// by a fixed fraction of its distance, which leaves its on-screen
	// footprint unchanged but keeps it in front of the block's own faces at
	// any distance and depth-buffer precision (a fixed world-space offset
	// z-fights once the block is far enough away or the depth buffer is
	// coarse enough). `hidden_faces` (Face bits) are faces against an opaque
	// neighbour -- the mesher culls those, and pulling them toward the eye
	// would otherwise let a sliver poke out in front of the neighbour.
	void draw(Vector3 center, Vector3 eye, AtlasRect rect, unsigned char alpha,
			std::uint8_t hidden_faces);

private:
	Mesh mesh_{};
	Material material_{};
	std::vector<float> base_texcoords_;
	std::vector<std::uint8_t> face_bits_; // per vertex: which Face it belongs to
	AtlasRect last_rect_{ -1.0f, -1.0f, -1.0f, -1.0f };
	int last_hidden_ = -1;
};

} // namespace vb::render
