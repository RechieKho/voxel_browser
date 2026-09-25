#include "vb/render/entity_renderer.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
	// Entity-management follow-up: per-NetId decoded override visuals, lazily
	// built the first time sync() sees a ClientSession::entity_visual_override
	// for that id (see decode_kind_visual()/sync() below). Takes priority
	// over kind_visuals in draw() when present.
	std::unordered_map<core::NetId, KindVisual> instance_visuals;
	// Ids sync() has already attempted an override decode for, whether or not
	// it succeeded -- an override is immutable for an entity's replicated
	// lifetime (same as `kind`), so there's never a reason to retry.
	std::unordered_set<core::NetId> instance_visual_attempted;
	VirtualFs vfs; // set once, right after join, by set_virtual_fs()
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
	for (auto &[id, visual] : impl_->instance_visuals) {
		(void)id;
		if (visual.texture.id != 0) {
			UnloadTexture(visual.texture);
		}
	}
}

namespace {

// Shared by set_kind_visual() (a kind's own default `visual`) and sync()'s
// lazy per-instance override decode -- both decode a real PNG out of `vfs`
// and validate it against a resolved protocol::EntityVisualDef the same way,
// only differing in where the def came from and what to log on failure.
// nullopt on any failure -- the caller keeps its existing fallback
// (placeholder or, for an override, the kind's own visual).
std::optional<KindVisual> decode_kind_visual(std::string_view context,
		const protocol::EntityVisualDef &def, const VirtualFs &vfs) {
	const auto it = vfs.find(def.texture);
	if (it == vfs.end()) {
		VB_WARN("render", context, ": texture '", def.texture,
				"' not found in the synced pack, keeping placeholder");
		return std::nullopt;
	}
	Image decoded = LoadImageFromMemory(".png",
			reinterpret_cast<const unsigned char *>(it->second.data()),
			static_cast<int>(it->second.size()));
	if (decoded.data == nullptr) {
		VB_WARN("render", context, ": failed to decode '", def.texture, "'");
		return std::nullopt;
	}
	const auto layout = build_entity_visual_layout(def, decoded.width, decoded.height);
	if (!layout) {
		VB_WARN("render", context, ": '", def.texture, "' (", decoded.width, "x",
				decoded.height, ") doesn't match its declared frame/facings/clip layout");
		UnloadImage(decoded);
		return std::nullopt;
	}
	KindVisual visual;
	visual.texture = LoadTextureFromImage(decoded);
	visual.layout = *layout;
	UnloadImage(decoded);
	return visual;
}

} // namespace

void EntityRenderer::set_kind_visual(core::EntityKindId id,
		const protocol::EntityVisualDef &def, const VirtualFs &vfs) {
	if (auto visual = decode_kind_visual("entity kind visual", def, vfs)) {
		impl_->kind_visuals.insert_or_assign(id, std::move(*visual));
	}
}

void EntityRenderer::set_virtual_fs(VirtualFs vfs) {
	impl_->vfs = std::move(vfs);
}

void EntityRenderer::sync(const net::ClientSession &client,
		const CameraView &camera, double dt_seconds) {
	const auto &remote = client.remote_entities();

	for (auto it = impl_->states.begin(); it != impl_->states.end();) {
		if (remote.find(it->first) == remote.end()) {
			if (const auto vis_it = impl_->instance_visuals.find(it->first);
					vis_it != impl_->instance_visuals.end()) {
				if (vis_it->second.texture.id != 0) {
					UnloadTexture(vis_it->second.texture);
				}
				impl_->instance_visuals.erase(vis_it);
			}
			impl_->instance_visual_attempted.erase(it->first);
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
		const protocol::EntityKindRegistryRecord *kind_record = client.entity_kind(rec.kind);
		if (kind_record != nullptr) {
			it->second.width = kind_record->width;
			it->second.height = kind_record->height;
		}
		// Entity-management follow-up: a per-instance visual_override is
		// resolved (merged over the kind's own default, if any, then decoded)
		// at most once per NetId -- see instance_visual_attempted's own
		// comment for why a retry is never useful.
		if (const protocol::EntityVisualOverride *override_def =
						client.entity_visual_override(id)) {
			if (impl_->instance_visual_attempted.insert(id).second) {
				protocol::EntityVisualDef base;
				if (kind_record != nullptr && kind_record->visual) {
					base = *kind_record->visual;
				}
				const protocol::EntityVisualDef merged =
						merge_visual_override(base, *override_def);
				if (auto visual = decode_kind_visual(
							"entity instance visual override", merged, impl_->vfs)) {
					impl_->instance_visuals.insert_or_assign(id, std::move(*visual));
				}
			}
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

		// A per-instance override (if decoded successfully) always wins over
		// the kind's own default visual -- see sync()'s lazy decode above.
		const KindVisual *visual = nullptr;
		if (const auto inst_it = impl_->instance_visuals.find(id);
				inst_it != impl_->instance_visuals.end()) {
			visual = &inst_it->second;
		} else if (const auto kind_it = impl_->kind_visuals.find(tracked.kind);
				kind_it != impl_->kind_visuals.end()) {
			visual = &kind_it->second;
		}
		const bool has_visual = visual != nullptr;

		Rectangle source;
		Texture2D texture;
		if (has_visual) {
			const EntityVisualLayout &layout = visual->layout;
			texture = visual->texture;
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
		const float origin_x = has_visual ? visual->layout.origin_x : 0.5f;
		const float origin_y = has_visual ? visual->layout.origin_y : 1.0f;
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
