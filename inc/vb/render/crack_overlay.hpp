#pragma once

#include <vector>

#include <raylib.h>

#include "vb/render/texture_atlas.hpp" // AtlasRect

// GPU-side draw helper for the crack-stage overlay cube (REMAINING_TASKS.md
// 6.5's last piece). Owns one unit-ish cube Mesh/Material built once at
// construction; draw() remaps the mesh's texcoords into the given
// CrackAtlas rect (re-uploaded to the GPU only when the rect actually
// changes, not every frame) so the same geometry can show any block's crack
// stage without rebuilding it. Not unit tested -- GL-context-requiring, same
// posture as ChunkRenderer/EntityRenderer's own draw paths.

namespace vb::render {

class CrackOverlay {
public:
	CrackOverlay();
	~CrackOverlay();

	CrackOverlay(const CrackOverlay &) = delete;
	CrackOverlay &operator=(const CrackOverlay &) = delete;

	// Hands over the already-uploaded CrackAtlas texture (see
	// CrackAtlas::upload()). Call once per session before the first draw().
	void set_texture(Texture2D atlas);

	// Draws the overlay cube centered at `center`, sampling `rect` from the
	// atlas set via set_texture(), tinted with `alpha` (0 = invisible).
	void draw(Vector3 center, AtlasRect rect, unsigned char alpha);

private:
	Mesh mesh_{};
	Material material_{};
	std::vector<float> base_texcoords_;
	AtlasRect last_rect_{ -1.0f, -1.0f, -1.0f, -1.0f };
};

} // namespace vb::render
