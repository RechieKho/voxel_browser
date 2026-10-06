#include "vb/editor/commands.hpp"

namespace vb::editor {

void CellEdits::apply(StructureDoc &doc) {
	for (const CellChange &c : changes_) {
		if (c.variant < doc.variants.size()) {
			doc.variants[c.variant].volume.set(c.pos, c.after);
		}
	}
}

void CellEdits::revert(StructureDoc &doc) {
	// Reverse order so a position touched twice returns to its first value.
	for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) {
		if (it->variant < doc.variants.size()) {
			doc.variants[it->variant].volume.set(it->pos, it->before);
		}
	}
}

void VolumeReplace::set(StructureDoc &doc, const std::vector<Volume> &volumes, core::IVec3 anchor) {
	for (std::size_t i = 0; i < volumes.size() && i < doc.variants.size(); ++i) {
		doc.variants[i].volume = volumes[i];
	}
	doc.anchor = anchor;
}

void VolumeReplace::apply(StructureDoc &doc) { set(doc, after_, anchor_after_); }
void VolumeReplace::revert(StructureDoc &doc) { set(doc, before_, anchor_before_); }

void UndoStack::push(std::unique_ptr<Command> command, StructureDoc &doc) {
	command->apply(doc);
	commands_.resize(next_);
	// Redo history is gone; a saved point inside it can no longer be reached.
	if (saved_valid_ && saved_ > next_) {
		saved_valid_ = false;
	}
	commands_.push_back(std::move(command));
	++next_;
}

bool UndoStack::undo(StructureDoc &doc) {
	if (!can_undo()) {
		return false;
	}
	commands_[--next_]->revert(doc);
	return true;
}

bool UndoStack::redo(StructureDoc &doc) {
	if (!can_redo()) {
		return false;
	}
	commands_[next_++]->apply(doc);
	return true;
}

void UndoStack::clear() {
	commands_.clear();
	next_ = 0;
	saved_ = 0;
	saved_valid_ = true;
}

} // namespace vb::editor
