#include "vb/editor/edit_session.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace vb::editor {

EditSession::EditSession(StructureDoc doc) : doc_(std::move(doc)) {}

void EditSession::set_variant(std::size_t index) {
	if (index < doc_.variants.size() && index != variant_) {
		variant_ = index;
		++revision_;
	}
}

Cell EditSession::brush_cell() { return brush_ ? doc_.names.intern(*brush_) : kKeepCell; }

bool EditSession::push_changes(const std::string &label, std::vector<CellChange> changes) {
	if (changes.empty()) {
		return false;
	}
	undo_.push(std::make_unique<CellEdits>(label, std::move(changes)), doc_);
	++revision_;
	return true;
}

template <typename Filter>
bool EditSession::apply_cells(const std::string &label, const std::vector<core::IVec3> &positions, Cell value, Filter only_if) {
	const Volume &vol = volume();
	std::vector<CellChange> changes;
	std::set<std::tuple<int, int, int>> done;
	for (const core::IVec3 base : positions) {
		for (const core::IVec3 p : mirror_positions(base, vol.size(), symmetry)) {
			if (!vol.in_bounds(p) || !done.insert({ p.x, p.y, p.z }).second) {
				continue;
			}
			const Cell before = vol.get(p);
			if (before != value && only_if(before)) {
				changes.push_back({ variant_, p, before, value });
			}
		}
	}
	return push_changes(label, std::move(changes));
}

bool EditSession::place(core::IVec3 cell) {
	const Cell value = brush_cell();
	return apply_cells("place", { cell }, value, [](Cell) { return true; });
}

bool EditSession::remove(core::IVec3 cell) {
	return apply_cells("remove", { cell }, kKeepCell, [](Cell) { return true; });
}

bool EditSession::paint(core::IVec3 cell) {
	const Cell value = brush_cell();
	return apply_cells("paint", { cell }, value, [](Cell before) { return before != kKeepCell; });
}

std::optional<std::string> EditSession::eyedrop(core::IVec3 cell) const {
	const Cell c = volume().get(cell);
	if (c == kKeepCell) {
		return std::nullopt;
	}
	return doc_.names.name(c);
}

bool EditSession::box_fill(core::IVec3 a, core::IVec3 b) {
	Box box = box_between(a, b);
	if (!clip_box(box, volume().size())) {
		return false;
	}
	const Cell value = brush_cell();
	return apply_cells("box fill", box_positions(box), value, [](Cell) { return true; });
}

bool EditSession::line(core::IVec3 a, core::IVec3 b) {
	const Cell value = brush_cell();
	return apply_cells("line", line_positions(a, b), value, [](Cell) { return true; });
}

bool EditSession::flood_replace(core::IVec3 cell) {
	const Cell value = brush_cell();
	std::vector<core::IVec3> region;
	// With symmetry, flood each mirrored start; regions that coincide are
	// merged by apply_cells.
	for (const core::IVec3 start : mirror_positions(cell, volume().size(), symmetry)) {
		for (const core::IVec3 p : flood_region(volume(), start)) {
			region.push_back(p);
		}
	}
	return apply_cells("flood replace", region, value, [](Cell) { return true; });
}

void EditSession::select(std::optional<Box> box) {
	if (box && !clip_box(*box, volume().size())) {
		box.reset();
	}
	selection_ = box;
}

bool EditSession::copy_selection() {
	if (!selection_) {
		return false;
	}
	Clipboard clip;
	clip.size = selection_->size();
	for (int y = selection_->min.y; y <= selection_->max.y; ++y) {
		for (int z = selection_->min.z; z <= selection_->max.z; ++z) {
			for (int x = selection_->min.x; x <= selection_->max.x; ++x) {
				const Cell c = volume().get(x, y, z);
				clip.cells.push_back(c == kKeepCell ? std::nullopt : std::optional<std::string>(doc_.names.name(c)));
			}
		}
	}
	clipboard_ = std::move(clip);
	return true;
}

bool EditSession::cut_selection() {
	if (!copy_selection()) {
		return false;
	}
	std::vector<CellChange> changes;
	for (const core::IVec3 p : box_positions(*selection_)) {
		const Cell before = volume().get(p);
		if (before != kKeepCell) {
			changes.push_back({ variant_, p, before, kKeepCell });
		}
	}
	// The copy already succeeded; an all-keep selection simply cuts nothing.
	push_changes("cut", std::move(changes));
	return true;
}

bool EditSession::paste(core::IVec3 at) {
	if (clipboard_.empty()) {
		return false;
	}
	std::vector<CellChange> changes;
	std::size_t i = 0;
	for (int y = 0; y < clipboard_.size.y; ++y) {
		for (int z = 0; z < clipboard_.size.z; ++z) {
			for (int x = 0; x < clipboard_.size.x; ++x, ++i) {
				const auto &name = clipboard_.cells[i];
				if (!name) {
					continue;
				}
				const core::IVec3 p{ at.x + x, at.y + y, at.z + z };
				if (!volume().in_bounds(p)) {
					continue;
				}
				const Cell value = doc_.names.intern(*name);
				const Cell before = volume().get(p);
				if (before != value) {
					changes.push_back({ variant_, p, before, value });
				}
			}
		}
	}
	return push_changes("paste", std::move(changes));
}

bool EditSession::move_selection(core::IVec3 delta) {
	if (!selection_ || (delta.x == 0 && delta.y == 0 && delta.z == 0)) {
		return false;
	}
	const Volume &vol = volume();
	// Read every source cell first, clear them all, then write the shifted
	// copies, so overlapping source and destination behave.
	struct Moved {
		core::IVec3 to;
		Cell value;
	};
	std::vector<Moved> moved;
	std::vector<CellChange> changes;
	std::vector<std::pair<core::IVec3, Cell>> after; // final value per touched position
	const auto touch = [&](core::IVec3 p, Cell value) {
		for (auto &entry : after) {
			if (entry.first == p) {
				entry.second = value;
				return;
			}
		}
		after.emplace_back(p, value);
	};
	for (const core::IVec3 p : box_positions(*selection_)) {
		const Cell c = vol.get(p);
		if (c != kKeepCell) {
			touch(p, kKeepCell);
			moved.push_back({ { p.x + delta.x, p.y + delta.y, p.z + delta.z }, c });
		}
	}
	for (const Moved &m : moved) {
		if (vol.in_bounds(m.to)) {
			touch(m.to, m.value);
		}
	}
	for (const auto &[p, value] : after) {
		const Cell before = vol.get(p);
		if (before != value) {
			changes.push_back({ variant_, p, before, value });
		}
	}
	const Box old = *selection_;
	if (!push_changes("move", std::move(changes))) {
		return false;
	}
	selection_ = Box{ { old.min.x + delta.x, old.min.y + delta.y, old.min.z + delta.z },
		{ old.max.x + delta.x, old.max.y + delta.y, old.max.z + delta.z } };
	clamp_selection();
	return true;
}

void EditSession::clamp_selection() {
	if (selection_ && !clip_box(*selection_, volume().size())) {
		selection_.reset();
	}
}

bool EditSession::resize(core::IVec3 new_size, ResizeAnchor side) {
	const auto dim_ok = [](int v) { return v >= 1 && v <= worldgen::kMaxStructureDim; };
	if (!dim_ok(new_size.x) || !dim_ok(new_size.y) || !dim_ok(new_size.z)) {
		return false;
	}
	const core::IVec3 old_size = doc_.size();
	if (new_size == old_size) {
		return false;
	}
	const auto shift_of = [](int old_dim, int new_dim, ResizeSide s) {
		switch (s) {
			case ResizeSide::kMin:
				return 0;
			case ResizeSide::kCenter:
				return (new_dim - old_dim) / 2;
			case ResizeSide::kMax:
				return new_dim - old_dim;
		}
		return 0;
	};
	const core::IVec3 shift{ shift_of(old_size.x, new_size.x, side.x), shift_of(old_size.y, new_size.y, side.y),
		shift_of(old_size.z, new_size.z, side.z) };

	std::vector<Volume> before;
	std::vector<Volume> after;
	for (const DocVariant &v : doc_.variants) {
		before.push_back(v.volume);
		after.push_back(v.volume.resized(new_size, shift));
	}
	const core::IVec3 anchor_after{ std::clamp(doc_.anchor.x + shift.x, 0, new_size.x - 1),
		std::clamp(doc_.anchor.y + shift.y, 0, new_size.y - 1), std::clamp(doc_.anchor.z + shift.z, 0, new_size.z - 1) };
	undo_.push(std::make_unique<VolumeReplace>("resize", std::move(before), doc_.anchor, std::move(after), anchor_after), doc_);
	selection_.reset();
	++revision_;
	return true;
}

bool EditSession::set_anchor(core::IVec3 anchor) {
	const core::IVec3 size = doc_.size();
	if (anchor.x < 0 || anchor.y < 0 || anchor.z < 0 || anchor.x >= size.x || anchor.y >= size.y || anchor.z >= size.z ||
			anchor == doc_.anchor) {
		return false;
	}
	std::vector<Volume> same;
	for (const DocVariant &v : doc_.variants) {
		same.push_back(v.volume);
	}
	std::vector<Volume> same_after = same;
	undo_.push(std::make_unique<VolumeReplace>("move anchor", std::move(same), doc_.anchor, std::move(same_after), anchor), doc_);
	++revision_;
	return true;
}

bool EditSession::push_variants(const std::string &label, std::vector<DocVariant> after, std::size_t select) {
	undo_.push(std::make_unique<VariantsReplace>(label, doc_.variants, std::move(after)), doc_);
	variant_ = std::min(select, doc_.variants.size() - 1);
	selection_.reset();
	++revision_;
	return true;
}

bool EditSession::generate(const Generator &generator, const ParamValues &params, std::uint64_t seed) {
	Volume fresh = volume();
	generator.generate(fresh, doc_.anchor, doc_.names, normalized_params(generator, params), seed);
	std::vector<CellChange> changes;
	const Volume &old = volume();
	for (int y = 0; y < fresh.size().y; ++y) {
		for (int z = 0; z < fresh.size().z; ++z) {
			for (int x = 0; x < fresh.size().x; ++x) {
				if (old.get(x, y, z) != fresh.get(x, y, z)) {
					changes.push_back({ variant_, { x, y, z }, old.get(x, y, z), fresh.get(x, y, z) });
				}
			}
		}
	}
	return push_changes("generate " + generator.id(), std::move(changes));
}

bool EditSession::bake(const Generator &generator, const ParamValues &params, std::uint64_t base_seed, int count) {
	if (count < 1) {
		return false;
	}
	const ParamValues values = normalized_params(generator, params);
	std::vector<DocVariant> after = doc_.variants;
	const std::size_t first = after.size();
	for (int i = 0; i < count; ++i) {
		DocVariant v;
		v.volume = Volume(doc_.size());
		generator.generate(v.volume, doc_.anchor, doc_.names, values, variant_seed(base_seed, i));
		after.push_back(std::move(v));
	}
	return push_variants("bake " + generator.id() + " x" + std::to_string(count), std::move(after), first);
}

bool EditSession::add_variant() {
	std::vector<DocVariant> after = doc_.variants;
	after.insert(after.begin() + static_cast<std::ptrdiff_t>(variant_) + 1, DocVariant{ Volume(doc_.size()), 1.0 });
	return push_variants("add variant", std::move(after), variant_ + 1);
}

bool EditSession::duplicate_variant(std::size_t index) {
	if (index >= doc_.variants.size()) {
		return false;
	}
	std::vector<DocVariant> after = doc_.variants;
	after.insert(after.begin() + static_cast<std::ptrdiff_t>(index) + 1, doc_.variants[index]);
	return push_variants("duplicate variant", std::move(after), index + 1);
}

bool EditSession::delete_variant(std::size_t index) {
	if (index >= doc_.variants.size() || doc_.variants.size() < 2) {
		return false;
	}
	std::vector<DocVariant> after = doc_.variants;
	after.erase(after.begin() + static_cast<std::ptrdiff_t>(index));
	return push_variants("delete variant", std::move(after), index == 0 ? 0 : index - 1);
}

bool EditSession::move_variant(std::size_t index, int delta) {
	if (index >= doc_.variants.size()) {
		return false;
	}
	const auto target = static_cast<long>(index) + delta;
	if (target < 0 || target >= static_cast<long>(doc_.variants.size()) || target == static_cast<long>(index)) {
		return false;
	}
	std::vector<DocVariant> after = doc_.variants;
	std::swap(after[index], after[static_cast<std::size_t>(target)]);
	return push_variants("move variant", std::move(after), static_cast<std::size_t>(target));
}

bool EditSession::set_weight(std::size_t index, double weight) {
	if (index >= doc_.variants.size() || !(weight > 0.0) || !std::isfinite(weight) || doc_.variants[index].weight == weight) {
		return false;
	}
	std::vector<DocVariant> after = doc_.variants;
	after[index].weight = weight;
	// Keep the selection where it is.
	undo_.push(std::make_unique<VariantsReplace>("set weight", doc_.variants, std::move(after)), doc_);
	++revision_;
	return true;
}

bool EditSession::set_placement(const worldgen::PlacementSpec &placement) {
	if (placement == doc_.placement || !worldgen::validate_placement_spec(placement, "structure '" + doc_.name + "'").empty()) {
		return false;
	}
	undo_.push(std::make_unique<PlacementReplace>(doc_.placement, placement), doc_);
	++revision_;
	return true;
}

bool EditSession::undo() {
	if (!undo_.undo(doc_)) {
		return false;
	}
	variant_ = std::min(variant_, doc_.variants.size() - 1);
	clamp_selection();
	++revision_;
	return true;
}

bool EditSession::redo() {
	if (!undo_.redo(doc_)) {
		return false;
	}
	variant_ = std::min(variant_, doc_.variants.size() - 1);
	clamp_selection();
	++revision_;
	return true;
}

} // namespace vb::editor
