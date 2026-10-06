#include "vb/editor/structure_doc.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace vb::editor {

Cell NameTable::intern(const std::string &name) {
	if (auto existing = find(name)) {
		return *existing;
	}
	entries_.push_back({ name, 0, false });
	return static_cast<Cell>(entries_.size());
}

std::optional<Cell> NameTable::find(const std::string &name) const {
	for (std::size_t i = 0; i < entries_.size(); ++i) {
		if (entries_[i].name == name) {
			return static_cast<Cell>(i + 1);
		}
	}
	return std::nullopt;
}

StructureDoc StructureDoc::create(const std::string &name, core::IVec3 size, core::IVec3 anchor) {
	StructureDoc doc;
	doc.name = name;
	doc.anchor = anchor;
	doc.variants.push_back({ Volume(size), 1.0 });
	return doc;
}

std::optional<StructureDoc> StructureDoc::from_spec(const worldgen::StructureSpec &spec, std::string *error) {
	const std::string bad = worldgen::validate_structure_spec(spec);
	if (!bad.empty()) {
		if (error) {
			*error = bad;
		}
		return std::nullopt;
	}
	StructureDoc doc;
	doc.name = spec.name;
	doc.anchor = spec.anchor;
	doc.placement = spec.placement;

	// Palette entries first, in key order, so the table order is deterministic
	// and every loaded key is preserved.
	std::vector<Cell> by_key(256, kKeepCell);
	for (const auto &[key, block] : spec.palette) {
		if (!block) {
			doc.keep_key = key;
			continue;
		}
		const Cell cell = doc.names.intern(*block);
		auto &entry = doc.names.entries()[static_cast<std::size_t>(cell) - 1];
		entry.key = key;
		entry.keep_unused = true;
		by_key[static_cast<unsigned char>(key)] = cell;
	}
	for (const auto &vs : spec.variants) {
		DocVariant variant;
		variant.weight = vs.weight;
		variant.volume = Volume(spec.size);
		for (int y = 0; y < spec.size.y; ++y) {
			for (int z = 0; z < spec.size.z; ++z) {
				const std::string &row = vs.layers[static_cast<std::size_t>(y)][static_cast<std::size_t>(z)];
				for (int x = 0; x < spec.size.x; ++x) {
					variant.volume.set(x, y, z, by_key[static_cast<unsigned char>(row[static_cast<std::size_t>(x)])]);
				}
			}
		}
		doc.variants.push_back(std::move(variant));
	}
	return doc;
}

bool StructureDoc::uses(Cell cell) const {
	for (const DocVariant &v : variants) {
		if (std::find(v.volume.cells().begin(), v.volume.cells().end(), cell) != v.volume.cells().end()) {
			return true;
		}
	}
	return false;
}

namespace {

bool key_ok(char c) {
	const auto u = static_cast<unsigned char>(c);
	return u > 32 && u < 127 && c != '"' && c != '\\';
}

// Candidate palette characters for `name`, best first: air gets '_'; then the
// letters of the local name (upper, then lower), then the alphabet, digits and
// a few symbols.
std::string key_candidates(const std::string &name) {
	std::string out;
	if (name == "base:air") {
		out += '_';
	}
	const auto colon = name.rfind(':');
	const std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
	for (const char c : local) {
		if (std::isalnum(static_cast<unsigned char>(c))) {
			out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
	}
	for (const char c : local) {
		if (std::isalpha(static_cast<unsigned char>(c))) {
			out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
	}
	out += "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789#%&*+=@~^$";
	return out;
}

} // namespace

worldgen::StructureSpec StructureDoc::to_spec() {
	std::set<char> taken;
	taken.insert(keep_key);
	for (const auto &entry : names.entries()) {
		if (entry.key != 0) {
			taken.insert(entry.key);
		}
	}
	for (std::size_t i = 0; i < names.size(); ++i) {
		auto &entry = names.entries()[i];
		if (entry.key != 0 || !uses(static_cast<Cell>(i + 1))) {
			continue;
		}
		for (const char c : key_candidates(entry.name)) {
			if (key_ok(c) && taken.insert(c).second) {
				entry.key = c;
				break;
			}
		}
	}

	worldgen::StructureSpec spec;
	spec.name = name;
	spec.size = size();
	spec.anchor = anchor;
	spec.placement = placement;
	spec.palette[keep_key] = std::nullopt;
	std::vector<char> key_of(names.size() + 1, keep_key);
	for (std::size_t i = 0; i < names.size(); ++i) {
		const auto &entry = names.entries()[i];
		if (entry.key == 0) {
			continue;
		}
		if (entry.keep_unused || uses(static_cast<Cell>(i + 1))) {
			spec.palette[entry.key] = entry.name;
		}
		key_of[i + 1] = entry.key;
	}
	for (const DocVariant &v : variants) {
		worldgen::StructureVariantSpec out;
		out.weight = v.weight;
		const core::IVec3 s = v.volume.size();
		for (int y = 0; y < s.y; ++y) {
			std::vector<std::string> rows;
			for (int z = 0; z < s.z; ++z) {
				std::string row;
				for (int x = 0; x < s.x; ++x) {
					row += key_of[v.volume.get(x, y, z)];
				}
				rows.push_back(std::move(row));
			}
			out.layers.push_back(std::move(rows));
		}
		spec.variants.push_back(std::move(out));
	}
	return spec;
}

} // namespace vb::editor
