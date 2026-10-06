#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include "vb/editor/edit_session.hpp"
#include "vb/editor/orbit_camera.hpp"
#include "vb/editor/pick.hpp"
#include "vb/editor/volume_view.hpp"
#include "vb/editor/workspace.hpp"
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
