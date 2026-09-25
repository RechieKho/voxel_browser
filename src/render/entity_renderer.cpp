#include "vb/render/entity_renderer.hpp"

#include <cstdint>
#include <unordered_map>
#include <utility>

#include <raylib.h>

#include "vb/core/ids.hpp"
#include "vb/core/log.hpp"
#include "vb/net/session.hpp"
#include "vb/render/entity_visual.hpp"
#include "vb/render/entity_visual_layout.hpp"

namespace vb::render {

namespace {

constexpr int kDefaultFacings = 8;
constexpr float kPlaceholderWidth = 0.8f; // metres
constexpr float kPlaceholderHeight = 1.8f; // matches physics::MoveParams::height

Camera3D to_raylib_camera(const CameraView &v) {
	Camera3D cam{};
	cam.position = { static_cast<float>(v.position.x),
		static_cast<float>(v.position.y), static_cast<float>(v.position.z) };
	cam.target = { static_cast<float>(v.target.x),
		static_cast<float>(v.target.y), static_cast<float>(v.target.z) };
	cam.up = { 0.0f, 1.0f, 0.0f };
	cam.fovy = 60.0f; // unused by DrawBillboardPro's view-matrix math
	cam.projection = CAMERA_PERSPECTIVE;
	return cam;
}

// A cheap, deterministic hash -> hue so distinct entities are visually
// distinguishable even as flat placeholder quads (real art replaces this).
Color tint_for_entity(core::NetId id) {
	std::uint32_t h = static_cast<std::uint32_t>(id) * 2654435761u;
	h ^= h >> 16;
	const float hue = static_cast<float>(h % 360u);
	return ColorFromHSV(hue, 0.55f, 0.85f);
}

} // namespace

// Entity-management follow-up to Phase 6.1: a script entity's registered
// vb.register_entity{width=, height=} (protocol::EntityKindRegistryRecord,
// looked up via ClientSession::entity_kind()) replaces the flat
// kPlaceholderWidth/kPlaceholderHeight for its billboard -- players
// (EntityRecord::kind == kInvalid) and any kind with no registry entry (host
// never opted in) keep the placeholder defaults, same "missing = default"
// posture as every other opt-in registry in this codebase.
struct TrackedEntity {
	EntityPresentationState state;
	core::EntityKindId kind = core::EntityKindId::kInvalid;
	float width = kPlaceholderWidth;
	float height = kPlaceholderHeight;

	explicit TrackedEntity(int facings) : state(facings) {}
};

// A kind's real spritesheet, uploaded once per session by set_kind_visual().
struct KindVisual {
	Texture2D texture{};
	EntityVisualLayout layout;
};

struct EntityRenderer::Impl {
	Texture2D placeholder{};
	std::unordered_map<core::NetId, TrackedEntity> states;
	std::unordered_map<core::EntityKindId, KindVisual> kind_visuals;
};

EntityRenderer::EntityRenderer() : impl_(std::make_unique<Impl>()) {
	Image img = GenImageColor(1, 1, WHITE);
	impl_->placeholder = LoadTextureFromImage(img);
	UnloadImage(img);
}

EntityRenderer::~EntityRenderer() {
	if (impl_->placeholder.id != 0) {
		UnloadTexture(impl_->placeholder);
	}
	for (auto &[id, visual] : impl_->kind_visuals) {
		(void)id;
		if (visual.texture.id != 0) {
			UnloadTexture(visual.texture);
		}
	}
}

void EntityRenderer::set_kind_visual(core::EntityKindId id,
		const protocol::EntityVisualDef &def, const VirtualFs &vfs) {
	const auto it = vfs.find(def.texture);
	if (it == vfs.end()) {
		VB_WARN("render", "entity kind visual: texture '", def.texture,
				"' not found in the synced pack, keeping placeholder");
		return;
	}
	Image decoded = LoadImageFromMemory(".png",
			reinterpret_cast<const unsigned char *>(it->second.data()),
			static_cast<int>(it->second.size()));
	if (decoded.data == nullptr) {
		VB_WARN("render", "entity kind visual: failed to decode '", def.texture, "'");
		return;
	}
	const auto layout = build_entity_visual_layout(def, decoded.width, decoded.height);
	if (!layout) {
		VB_WARN("render", "entity kind visual: '", def.texture,
				"' (", decoded.width, "x", decoded.height,
				") doesn't match its declared frame/facings/clip layout");
		UnloadImage(decoded);
		return;
	}
	KindVisual visual;
	visual.texture = LoadTextureFromImage(decoded);
	visual.layout = *layout;
	UnloadImage(decoded);
	impl_->kind_visuals.insert_or_assign(id, std::move(visual));
}

void EntityRenderer::sync(const net::ClientSession &client,
		const CameraView &camera, double dt_seconds) {
	const auto &remote = client.remote_entities();

	for (auto it = impl_->states.begin(); it != impl_->states.end();) {
		if (remote.find(it->first) == remote.end()) {
			it = impl_->states.erase(it);
		} else {
			++it;
		}
	}

	for (const auto &[id, rec] : remote) {
		auto [it, inserted] =
				impl_->states.try_emplace(id, kDefaultFacings);
		(void)inserted;
		// Re-checked every sync (cheap: one map lookup) rather than only on
		// insert, so a registry that arrives just after this entity's first
		// snapshot still takes effect -- frame arrival order across the
		// S2C_EntityKindRegistry/S2C_EntitySnapshot messages isn't guaranteed.
		it->second.kind = rec.kind;
		if (const auto *kind = client.entity_kind(rec.kind)) {
			it->second.width = kind->width;
			it->second.height = kind->height;
		}
		const core::Vec3d pos = client.interpolated_pos(id);
		it->second.state.update(pos, static_cast<double>(rec.rot.x), rec.vel,
				rec.flags, camera.position, dt_seconds);
	}
}

void EntityRenderer::draw(const CameraView &camera_view) const {
	const Camera3D camera = to_raylib_camera(camera_view);
	for (const auto &[id, tracked] : impl_->states) {
		const EntityPresentationState::Frame &frame = tracked.state.frame();
		const core::Vec3d pos = tracked.state.position();
		const Vector3 feet{ static_cast<float>(pos.x),
			static_cast<float>(pos.y), static_cast<float>(pos.z) };

		const auto visual_it = impl_->kind_visuals.find(tracked.kind);
		const bool has_visual = visual_it != impl_->kind_visuals.end();

		Rectangle source;
		Texture2D texture;
		if (has_visual) {
			const EntityVisualLayout &layout = visual_it->second.layout;
			texture = visual_it->second.texture;
			const EntityClipLayout &clip =
					resolve_clip(layout, anim_clip_name(frame.clip));
			const int frame_in_clip = clip.frames > 0
					? static_cast<int>(frame.clip_time * clip.fps) % clip.frames
					: 0;
			const int column = clip.start_frame + frame_in_clip;
			const int row = frame.pose.pose_index;
			source = Rectangle{
				static_cast<float>(column * layout.frame_width),
				static_cast<float>(row * layout.frame_height),
				static_cast<float>(layout.frame_width),
				static_cast<float>(layout.frame_height),
			};
		} else {
			texture = impl_->placeholder;
			source = Rectangle{ 0.0f, 0.0f,
				static_cast<float>(impl_->placeholder.width),
				static_cast<float>(impl_->placeholder.height) };
		}

		Vector2 size{ tracked.width, tracked.height };
		if (frame.pose.mirrored) {
			size.x = -size.x; // DrawBillboardPro flips the source horizontally
		}
		// Anchor conversion: the spec's origin_x/origin_y are normalized,
		// image-space (0,0 = top-left, y grows downward, default {0.5, 1.0}
		// = bottom-centre feet point). raylib's `origin` for
		// DrawBillboardPro is a world-size offset measured from `position`
		// along the quad's own right/up axes (up = +Y), so origin.y=0 means
		// `position` sits at the quad's bottom edge -- the inverse sense of
		// image-space y. origin.x uses the same left-right sense in both
		// (the mirrored `size.x` flip above keeps a mirrored pose centred
		// symmetrically either way).
		const float origin_x = has_visual ? visual_it->second.layout.origin_x : 0.5f;
		const float origin_y = has_visual ? visual_it->second.layout.origin_y : 1.0f;
		const Vector2 origin{ size.x * origin_x, size.y * (1.0f - origin_y) };

		DrawBillboardPro(camera, texture, source, feet,
				Vector3{ 0.0f, 1.0f, 0.0f }, size, origin, 0.0f,
				has_visual ? WHITE : tint_for_entity(id));
	}
}

std::size_t EntityRenderer::tracked_count() const {
	return impl_->states.size();
}

} // namespace vb::render
