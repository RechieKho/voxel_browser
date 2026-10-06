#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vb/editor/commands.hpp"
#include "vb/editor/generators.hpp"
#include "vb/editor/shapes.hpp"
#include "vb/editor/structure_doc.hpp"

// The document being edited plus everything that changes it (docs/structure-
// editor.md §I, S4): the brush, symmetry, selection and clipboard, and one
// undo step per user-visible operation. Headless -- the app only forwards
// mouse and keyboard events here -- so every tool is unit-tested.

namespace vb::editor {

// Which side of an axis stays put when the structure is resized.
enum class ResizeSide { kMin, kCenter, kMax };

struct ResizeAnchor {
	ResizeSide x = ResizeSide::kMin;
	ResizeSide y = ResizeSide::kMin;
	ResizeSide z = ResizeSide::kMin;
};

// Copied cells are held by block *name*, so a clipboard survives switching
// variants and a name table that grew meanwhile.
struct Clipboard {
	core::IVec3 size{ 0, 0, 0 };
	// size.x*size.y*size.z entries, Volume layout; nullopt is keep.
	std::vector<std::optional<std::string>> cells;

	bool empty() const { return size.x <= 0 || size.y <= 0 || size.z <= 0; }
};

class EditSession {
public:
	explicit EditSession(StructureDoc doc);

	StructureDoc &doc() { return doc_; }
	const StructureDoc &doc() const { return doc_; }
	std::size_t variant() const { return variant_; }
	void set_variant(std::size_t index);
	const Volume &volume() const { return doc_.variants[variant_].volume; }

	// --- brush / symmetry ---
	// The block the place, paint, fill, line and flood tools write; nullopt
	// is "keep" (it clears cells). "base:air" is a normal block name that
	// carves.
	void set_brush(std::optional<std::string> block) { brush_ = std::move(block); }
	const std::optional<std::string> &brush() const { return brush_; }
	Symmetry symmetry;

	// --- single-cell tools (each returns false when nothing changed) ---
	bool place(core::IVec3 cell);
	bool remove(core::IVec3 cell);
	// Replaces a non-keep cell with the brush, leaving keep cells alone.
	bool paint(core::IVec3 cell);
	// The block name at `cell`; nullopt for keep or out of bounds.
	std::optional<std::string> eyedrop(core::IVec3 cell) const;

	// --- shape tools ---
	bool box_fill(core::IVec3 a, core::IVec3 b);
	bool line(core::IVec3 a, core::IVec3 b);
	// Replaces the connected region at `cell` with the brush.
	bool flood_replace(core::IVec3 cell);

	// --- selection / clipboard ---
	void select(std::optional<Box> box);
	const std::optional<Box> &selection() const { return selection_; }
	bool copy_selection();
	bool cut_selection();
	// Pastes with the clipboard's minimum corner at `at`; keep cells in the
	// clipboard don't overwrite anything.
	bool paste(core::IVec3 at);
	// Moves the selected cells by `delta` (and the selection with them).
	bool move_selection(core::IVec3 delta);
	const Clipboard &clipboard() const { return clipboard_; }

	// --- structure ---
	// Resizes every variant, keeping content on the side(s) `side` names and
	// moving the anchor with it (clamped into the new bounds).
	bool resize(core::IVec3 new_size, ResizeAnchor side);
	bool set_anchor(core::IVec3 anchor);

	// --- generators and variants ---
	// Replaces the current variant's cells with the generator's output, rooted
	// at the structure's anchor. One undo step. False when nothing changed.
	bool generate(const Generator &generator, const ParamValues &params, std::uint64_t seed);
	// Appends `count` variants generated with seeds derived from `base_seed`
	// (variant_seed), each weight 1. Selects the first new one.
	bool bake(const Generator &generator, const ParamValues &params, std::uint64_t base_seed, int count);
	// A blank variant after the current one; selects it.
	bool add_variant();
	bool duplicate_variant(std::size_t index);
	// The last remaining variant can't be deleted.
	bool delete_variant(std::size_t index);
	// Moves variant `index` by `delta` places (negative = earlier).
	bool move_variant(std::size_t index, int delta);
	// Weights must be positive (the writer and validator require it).
	bool set_weight(std::size_t index, double weight);

	// --- history ---
	bool undo();
	bool redo();
	bool can_undo() const { return undo_.can_undo(); }
	bool can_redo() const { return undo_.can_redo(); }
	std::string undo_label() const { return undo_.undo_label(); }
	std::string redo_label() const { return undo_.redo_label(); }
	bool dirty() const { return undo_.dirty(); }
	void mark_saved() { undo_.mark_saved(); }

	// Bumped on every change to the document (edit, undo, redo, variant
	// switch); the app rebuilds its view when it moves.
	std::uint64_t revision() const { return revision_; }

private:
	Cell brush_cell();
	// Builds and pushes a CellEdits that sets each position (and its mirror
	// images) to `value`; `only_if` filters which existing cells may change.
	template <typename Filter>
	bool apply_cells(const std::string &label, const std::vector<core::IVec3> &positions, Cell value, Filter only_if);
	bool push_changes(const std::string &label, std::vector<CellChange> changes);
	bool push_variants(const std::string &label, std::vector<DocVariant> after, std::size_t select);
	void clamp_selection();

	StructureDoc doc_;
	std::size_t variant_ = 0;
	std::optional<std::string> brush_;
	std::optional<Box> selection_;
	Clipboard clipboard_;
	UndoStack undo_;
	std::uint64_t revision_ = 1;
};

} // namespace vb::editor
