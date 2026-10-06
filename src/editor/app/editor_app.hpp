#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include "vb/editor/edit_session.hpp"
#include "vb/editor/orbit_camera.hpp"
#include "vb/editor/pick.hpp"
#include "vb/editor/preview.hpp"
#include "vb/editor/volume_view.hpp"
#include "vb/editor/workspace.hpp"
#include "vb/render/camera.hpp"
#include "vb/render/chunk_renderer.hpp"
#include "vb/render/window.hpp"

// The structure editor window (docs/structure-editor.md §I). All state that
// matters lives in the headless model (vb_editor_model); this class owns the
// window, the renderer and the panels and turns input into EditSession calls.

namespace vb::editor {

struct EditorOptions {
	std::filesystem::path block_script;
	std::string open_name; // --open <name>
	// Dev/testing: render this many frames, save a screenshot to this path
	// and exit (needs a display; CI uses Xvfb).
	std::filesystem::path screenshot;
	int frames = 0;
	int width = 1360;
	int height = 800;
	// Dev/testing: run these tool actions before the screenshot, e.g.
	// "brush base:stone;box 0,0,0 4,0,4". See EditorApp::run_script.
	std::string script;
};

enum class Tool { kPlace,
	kRemove,
	kPaint,
	kPick,
	kBox,
	kLine,
	kFlood,
	kSelect,
	kAnchor };

enum class SideTab { kBlocks,
	kGenerate,
	kVariants,
	kPlacement,
	kPreview };

enum class Dialog { kNone,
	kNew,
	kSaveAs,
	kResize,
	kUnsaved };

// One palette row: a block the brush can use.
struct PaletteEntry {
	std::string name; // "" for keep
	bool keep = false;
	core::BlockId id = core::BlockId::kAir; // for the atlas icon
};

class EditorApp {
public:
	EditorApp(Workspace workspace, EditorOptions options);
	~EditorApp();

	EditorApp(const EditorApp &) = delete;
	EditorApp &operator=(const EditorApp &) = delete;

	int run();

private:
	// --- loading / documents ---
	void rebuild_renderer(); // atlas + ChunkRenderer from the current catalog
	void rebuild_palette();
	void open_structure(const std::string &name);
	void new_structure(const std::string &name, core::IVec3 size, core::IVec3 anchor);
	void reload();
	void refresh_view();
	void frame_camera();
	bool save();
	bool save_as(const std::string &name);
	// Runs `action` now, or after the unsaved-changes prompt if the document
	// is dirty.
	void guard_unsaved(std::function<void()> action);
	void set_status(const std::string &text) { status_ = text; }

	// --- per frame: input ---
	void handle_input();
	void handle_shortcuts();
	void handle_preview_input();
	// --- preview ---
	bool preview_mode() const { return side_tab_ == SideTab::kPreview && session_ != nullptr; }
	void load_other_structures();
	void mark_preview_dirty();
	void update_preview();
	void draw_preview_world();
	void handle_viewport_click();
	PickResult current_pick() const;
	void apply_tool(const PickResult &pick, bool shift, bool ctrl, bool alt);
	core::IVec3 target_cell(const PickResult &pick) const;
	bool mouse_over_ui() const;
	void cancel_drag();

	// --- per frame: drawing ---
	void draw_world();
	void draw_overlays();
	void draw_ui();
	void draw_top_bar();
	void draw_tool_bar();
	void draw_side_panel();
	void draw_palette(Rectangle area);
	void draw_generate_tab(Rectangle area);
	void draw_variants_tab(Rectangle area);
	void draw_placement_tab(Rectangle area);
	void draw_preview_tab(Rectangle area);
	void draw_errors_panel();
	void draw_open_dialog();
	void draw_dialogs();
	void draw_block_icon(Rectangle where, const PaletteEntry &entry) const;
	void register_ui(Rectangle r) { ui_rects_.push_back(r); }

	// Dev/testing hook: `script` is "cmd;cmd;...", see the .cpp.
	void run_script(const std::string &script);

	Workspace workspace_;
	EditorOptions options_;
	std::unique_ptr<render::Window> window_;
	std::unique_ptr<render::ChunkRenderer> renderer_;
	std::unique_ptr<VolumeView> view_;
	OrbitCamera camera_;

	std::unique_ptr<EditSession> session_;
	std::uint64_t seen_revision_ = 0;
	int slice_ = VolumeView::kNoSlice; // highest visible layer
	std::vector<core::IVec3> ghosts_; // explicit-air cells, drawn faintly
	std::string status_;

	Tool tool_ = Tool::kPlace;
	std::optional<core::IVec3> drag_start_; // first corner of a box/line/select
	std::vector<PaletteEntry> palette_;
	std::string search_;
	bool search_edit_ = false;
	float palette_scroll_ = 0.0f;

	SideTab side_tab_ = SideTab::kBlocks;
	int gen_index_ = 0; // into all_generators()
	std::map<std::string, ParamValues> gen_params_; // per generator id
	int gen_seed_ = 1;
	int bake_count_ = 4;
	bool gen_value_edit_[2] = {};
	int weight_edit_ = -1; // variant whose weight is being typed
	std::string weight_text_;

	// Placement tab: which integer fields are being typed into.
	bool placement_edit_[4] = {};
	float cluster_ui_ = 0.0f;
	bool cluster_dragging_ = false;

	// Preview tab (S6).
	PreviewInput preview_input_;
	std::vector<worldgen::StructureSpec> other_specs_; // the folder's other structures
	std::unique_ptr<TerrainPreview> preview_;
	std::unique_ptr<render::ChunkRenderer> preview_renderer_;
	render::FirstPersonController fly_;
	bool preview_pending_ = true;
	double preview_changed_at_ = 0.0;
	std::uint64_t preview_seen_revision_ = 0;
	bool preview_value_edit_[2] = {};
	int preview_seed_box_ = 1;

	bool show_open_ = false;
	bool show_errors_ = false;
	int open_scroll_ = 0;
	int open_active_ = -1;
	int errors_scroll_ = 0;
	int errors_active_ = -1;

	Dialog dialog_ = Dialog::kNone;
	std::function<void()> after_unsaved_;
	std::string dialog_name_;
	bool dialog_name_edit_ = false;
	int dialog_size_[3] = { 5, 6, 5 };
	int dialog_anchor_[3] = { 2, 0, 2 };
	bool dialog_value_edit_[6] = {};
	int dialog_side_[3] = { 0, 0, 0 };
	bool quit_ = false;

	std::vector<Rectangle> ui_rects_; // panels drawn this frame, for hit-testing
};

} // namespace vb::editor
