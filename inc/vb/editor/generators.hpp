#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vb/core/math.hpp"
#include "vb/editor/structure_doc.hpp"
#include "vb/editor/volume.hpp"

// Parametric, seeded shape generators (docs/structure-editor.md §F, S5). The
// randomness of "procedural" decoration lives here, at authoring time: a
// generator fills the volume, "Bake xN" stores N variants, and the engine only
// picks one of them (plus a rotation and mirror) when it places the structure.
// Every generator is a pure function of (parameters, seed), built on DetRng
// and add/multiply/sqrt only -- no libm trigonometry -- so a seed gives the
// same shape on every platform.

namespace vb::editor {

enum class ParamKind { kInt, kFloat, kBlock, kChoice };

struct ParamDesc {
	std::string key;
	std::string label;
	ParamKind kind = ParamKind::kInt;
	double min = 0.0;
	double max = 1.0;
	double def = 0.0; // kInt, kFloat, and the default index of a kChoice
	std::string def_text; // kBlock: a block name
	std::vector<std::string> choices; // kChoice labels
};

struct ParamValue {
	double number = 0.0; // kInt, kFloat, kChoice (index)
	std::string text; // kBlock

	bool operator==(const ParamValue &) const = default;
};

using ParamValues = std::map<std::string, ParamValue>;

class Generator {
public:
	virtual ~Generator() = default;
	virtual std::string id() const = 0; // "tree"
	virtual std::string label() const = 0; // "Tree"
	virtual const std::vector<ParamDesc> &params() const = 0;

	// Replaces the contents of `volume` (it is cleared to keep first). The
	// shape is rooted at `anchor` (the trunk's foot, the blob's base...).
	// Block names are interned into `names`. Cells that fall outside the
	// volume are dropped.
	virtual void generate(Volume &volume, core::IVec3 anchor, NameTable &names, const ParamValues &values,
			std::uint64_t seed) const = 0;
};

// Tree, bush, boulder and fallen log, in that order.
const std::vector<std::unique_ptr<Generator>> &all_generators();
const Generator *find_generator(const std::string &id);

// Every descriptor's default.
ParamValues default_params(const Generator &generator);
// `values` clamped to each descriptor's range, with missing keys defaulted.
ParamValues normalized_params(const Generator &generator, const ParamValues &values);

// The seed for baked variant `index` of a base seed: stable, distinct per
// index, and index 0 differs from the base so "Generate" and the first bake
// of the same seed don't coincide by accident.
std::uint64_t variant_seed(std::uint64_t base_seed, int index);

} // namespace vb::editor
