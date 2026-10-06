#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "vb/editor/structure_doc.hpp"

// Undoable edits to a StructureDoc (docs/structure-editor.md §I, S4). Every
// user-visible edit is one Command, so Ctrl+Z / Ctrl+Y step through whole
// operations (a box fill, a paste) rather than single voxels.

namespace vb::editor {

class Command {
public:
	virtual ~Command() = default;
	virtual void apply(StructureDoc &doc) = 0;
	virtual void revert(StructureDoc &doc) = 0;
	virtual std::string label() const = 0;
};

struct CellChange {
	std::size_t variant = 0;
	core::IVec3 pos{};
	Cell before = kKeepCell;
	Cell after = kKeepCell;
};

// Sets a list of cells. The generic building block: place, remove, paint,
// box fill, line, flood replace, paste and move are all one of these.
class CellEdits final : public Command {
public:
	CellEdits(std::string label, std::vector<CellChange> changes) : label_(std::move(label)), changes_(std::move(changes)) {}
	void apply(StructureDoc &doc) override;
	void revert(StructureDoc &doc) override;
	std::string label() const override { return label_; }
	const std::vector<CellChange> &changes() const { return changes_; }

private:
	std::string label_;
	std::vector<CellChange> changes_;
};

// Replaces every variant's volume and the anchor (resize, re-anchor). Holds
// full before/after snapshots, which is cheap at the 64^3 cap.
class VolumeReplace final : public Command {
public:
	VolumeReplace(std::string label, std::vector<Volume> before, core::IVec3 anchor_before, std::vector<Volume> after,
			core::IVec3 anchor_after) :
			label_(std::move(label)),
			before_(std::move(before)),
			after_(std::move(after)),
			anchor_before_(anchor_before),
			anchor_after_(anchor_after) {}
	void apply(StructureDoc &doc) override;
	void revert(StructureDoc &doc) override;
	std::string label() const override { return label_; }

private:
	static void set(StructureDoc &doc, const std::vector<Volume> &volumes, core::IVec3 anchor);

	std::string label_;
	std::vector<Volume> before_;
	std::vector<Volume> after_;
	core::IVec3 anchor_before_;
	core::IVec3 anchor_after_;
};

// Replaces the whole variant list (add, duplicate, delete, reorder, reweight,
// bake). Snapshots are cheap at the 64^3 cap, and the name table only ever
// grows, so undo leaves unused names behind that the writer skips.
class VariantsReplace final : public Command {
public:
	VariantsReplace(std::string label, std::vector<DocVariant> before, std::vector<DocVariant> after) :
			label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}
	void apply(StructureDoc &doc) override { doc.variants = after_; }
	void revert(StructureDoc &doc) override { doc.variants = before_; }
	std::string label() const override { return label_; }

private:
	std::string label_;
	std::vector<DocVariant> before_;
	std::vector<DocVariant> after_;
};

class UndoStack {
public:
	// Applies `command` to `doc` and records it, dropping any redo history.
	void push(std::unique_ptr<Command> command, StructureDoc &doc);
	bool can_undo() const { return next_ > 0; }
	bool can_redo() const { return next_ < commands_.size(); }
	// Return false when there is nothing to undo/redo.
	bool undo(StructureDoc &doc);
	bool redo(StructureDoc &doc);
	std::string undo_label() const { return can_undo() ? commands_[next_ - 1]->label() : std::string(); }
	std::string redo_label() const { return can_redo() ? commands_[next_]->label() : std::string(); }

	// Dirty tracking: the document is clean when the stack is at the point
	// mark_saved() was called.
	void mark_saved() { saved_ = next_; saved_valid_ = true; }
	bool dirty() const { return !saved_valid_ ? next_ != 0 : next_ != saved_; }
	void clear();

private:
	std::vector<std::unique_ptr<Command>> commands_;
	std::size_t next_ = 0; // commands_[0..next_) are applied
	std::size_t saved_ = 0;
	bool saved_valid_ = true;
};

} // namespace vb::editor
