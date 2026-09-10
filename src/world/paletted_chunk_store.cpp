#include "vb/world/paletted_chunk_store.hpp"

#include <algorithm>

namespace vb::world {

namespace {
std::size_t words_for(std::uint8_t bits) {
	if (bits == 0) {
		return 0;
	}
	const std::size_t total_bits = kChunkVolume * bits;
	return (total_bits + 63) / 64;
}
} // namespace

PalettedChunkStore::PalettedChunkStore(core::BlockId fill_block) {
	palette_.push_back(fill_block);
}

std::uint8_t PalettedChunkStore::width_for(std::size_t palette_size) {
	if (palette_size <= 1) {
		return 0;
	}
	if (palette_size <= 2) {
		return 1;
	}
	if (palette_size <= 4) {
		return 2;
	}
	if (palette_size <= 16) {
		return 4;
	}
	if (palette_size <= 256) {
		return 8;
	}
	return 16;
}

std::uint64_t PalettedChunkStore::read_raw(std::size_t index) const {
	if (bits_per_index_ == 0) {
		return 0;
	}
	const std::size_t bit = index * bits_per_index_;
	const std::size_t word = bit / 64;
	const std::size_t shift = bit % 64;
	const std::uint64_t mask = (bits_per_index_ == 64)
			? ~std::uint64_t{ 0 }
			: ((std::uint64_t{ 1 } << bits_per_index_) - 1);
	return (data_[word] >> shift) & mask;
}

void PalettedChunkStore::write_raw(std::size_t index, std::uint64_t value) {
	const std::size_t bit = index * bits_per_index_;
	const std::size_t word = bit / 64;
	const std::size_t shift = bit % 64;
	const std::uint64_t mask = ((std::uint64_t{ 1 } << bits_per_index_) - 1) << shift;
	data_[word] = (data_[word] & ~mask) | ((value << shift) & mask);
}

void PalettedChunkStore::set_width(std::uint8_t new_bits) {
	if (new_bits == bits_per_index_) {
		return;
	}
	std::vector<std::uint64_t> old_data = std::move(data_);
	const std::uint8_t old_bits = bits_per_index_;

	data_.assign(words_for(new_bits), 0);
	bits_per_index_ = new_bits;

	if (old_bits == 0) {
		return; // was homogeneous: every index is 0, and data_ is already 0
	}

	// Repack each voxel's palette index at the new width.
	const std::uint64_t old_mask = (std::uint64_t{ 1 } << old_bits) - 1;
	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		const std::size_t bit = i * old_bits;
		const std::uint64_t v =
				(old_data[bit / 64] >> (bit % 64)) & old_mask;
		if (v != 0) {
			write_raw(i, v);
		}
	}
}

std::size_t PalettedChunkStore::palette_index_of(core::BlockId block) {
	for (std::size_t i = 0; i < palette_.size(); ++i) {
		if (palette_[i] == block) {
			return i;
		}
	}
	palette_.push_back(block);
	const std::uint8_t needed = width_for(palette_.size());
	if (needed > bits_per_index_) {
		set_width(needed);
	}
	return palette_.size() - 1;
}

core::BlockId PalettedChunkStore::get(std::size_t index) const {
	if (bits_per_index_ == 0) {
		return palette_[0];
	}
	const std::size_t idx = static_cast<std::size_t>(read_raw(index));
	return idx < palette_.size() ? palette_[idx] : palette_[0];
}

bool PalettedChunkStore::set(std::size_t index, core::BlockId block) {
	if (get(index) == block) {
		return false;
	}
	const std::size_t idx = palette_index_of(block);
	write_raw(index, static_cast<std::uint64_t>(idx));
	return true;
}

void PalettedChunkStore::fill(core::BlockId block) {
	palette_.assign(1, block);
	data_.clear();
	bits_per_index_ = 0;
}

bool PalettedChunkStore::uniform_value(core::BlockId *out) const {
	if (bits_per_index_ == 0) {
		if (out != nullptr) {
			*out = palette_[0];
		}
		return true;
	}
	const core::BlockId first = get(0);
	for (std::size_t i = 1; i < kChunkVolume; ++i) {
		if (get(i) != first) {
			return false;
		}
	}
	if (out != nullptr) {
		*out = first;
	}
	return true;
}

void PalettedChunkStore::compact() {
	if (bits_per_index_ == 0) {
		return;
	}

	core::BlockId uniform{};
	if (uniform_value(&uniform)) {
		fill(uniform);
		return;
	}

	// Which palette slots are actually used?
	std::vector<bool> used(palette_.size(), false);
	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		used[static_cast<std::size_t>(read_raw(i))] = true;
	}
	if (std::all_of(used.begin(), used.end(), [](bool b) { return b; })) {
		set_width(width_for(palette_.size())); // maybe the width can shrink
		return;
	}

	// Build a compacted palette + a remap old->new.
	std::vector<core::BlockId> new_palette;
	std::vector<std::size_t> remap(palette_.size(), 0);
	for (std::size_t i = 0; i < palette_.size(); ++i) {
		if (used[i]) {
			remap[i] = new_palette.size();
			new_palette.push_back(palette_[i]);
		}
	}

	std::vector<std::size_t> indices(kChunkVolume);
	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		indices[i] = remap[static_cast<std::size_t>(read_raw(i))];
	}

	palette_ = std::move(new_palette);
	bits_per_index_ = 0;
	data_.clear();
	set_width(width_for(palette_.size()));
	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		if (indices[i] != 0) {
			write_raw(i, static_cast<std::uint64_t>(indices[i]));
		}
	}
}

} // namespace vb::world
