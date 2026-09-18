#include "vb/worldgen/fastnoise2_compile.hpp"

#if VB_WITH_WORLDGEN

#include <FastNoise/FastNoise.h>

namespace vb::worldgen {

namespace {

// Value/Perlin/Cellular's native output is roughly [-1, 1] (FastNoise2's own
// docs: "Output is bounded -1 : 1"); the IR's contract for a leaf/kFbm node
// is ~[0, 1), matching NoiseNode::eval2/eval3's hand-rolled behavior -- wrap
// with a Remap so both backends honor the same contract for the same node
// types. kRemap/kCombine/kConstant are deliberately NOT auto-normalized here,
// same as the hand-rolled evaluator: their range is whatever the pack author
// asked for.
FastNoise::SmartNode<> normalize_to_unit(FastNoise::SmartNode<> src) {
	auto r = FastNoise::New<FastNoise::Remap>();
	r->SetSource(src);
	r->SetRemap(-1.0f, 1.0f, 0.0f, 1.0f);
	return r;
}

FastNoise::SmartNode<> domain_scale(FastNoise::SmartNode<> src, double frequency) {
	auto s = FastNoise::New<FastNoise::DomainScale>();
	s->SetSource(src);
	s->SetScale(static_cast<float>(frequency));
	return s;
}

FastNoise::SmartNode<> compile_node(const NoiseNode &node);

// MSVC rejects the equivalent ternary here as an ambiguous common-type
// conversion between two different SmartNode<T> instantiations -- plain
// if/else sidesteps that.
FastNoise::SmartNode<> compile_or_default(const NoiseNodePtr &n) {
	if (n) {
		return compile_node(*n);
	}
	return FastNoise::New<FastNoise::Constant>();
}

FastNoise::SmartNode<> compile_node(const NoiseNode &node) {
	switch (node.type) {
		case NoiseNodeType::kConstant: {
			auto n = FastNoise::New<FastNoise::Constant>();
			n->SetValue(static_cast<float>(node.constant_value));
			return n;
		}
		case NoiseNodeType::kValue: {
			auto v = FastNoise::New<FastNoise::Value>();
			return normalize_to_unit(domain_scale(v, node.frequency));
		}
		case NoiseNodeType::kCellular: {
			auto c = FastNoise::New<FastNoise::CellularValue>();
			return normalize_to_unit(domain_scale(c, node.frequency));
		}
		case NoiseNodeType::kFbm: {
			const FastNoise::SmartNode<> source = compile_or_default(node.source);
			auto frac = FastNoise::New<FastNoise::FractalFBm>();
			frac->SetSource(domain_scale(source, node.frequency));
			frac->SetOctaveCount(node.octaves);
			frac->SetGain(static_cast<float>(node.gain));
			frac->SetLacunarity(static_cast<float>(node.lacunarity));
			return normalize_to_unit(frac);
		}
		case NoiseNodeType::kRemap: {
			const FastNoise::SmartNode<> source = compile_or_default(node.source);
			auto r = FastNoise::New<FastNoise::Remap>();
			r->SetSource(source);
			r->SetRemap(static_cast<float>(node.in_min), static_cast<float>(node.in_max),
					static_cast<float>(node.out_min), static_cast<float>(node.out_max));
			return r;
		}
		case NoiseNodeType::kCombine: {
			const FastNoise::SmartNode<> a = compile_or_default(node.a);
			const FastNoise::SmartNode<> b = compile_or_default(node.b);
			switch (node.op) {
				case NoiseCombineOp::kAdd: {
					auto n = FastNoise::New<FastNoise::Add>();
					n->SetLHS(a);
					n->SetRHS(b);
					return n;
				}
				case NoiseCombineOp::kMultiply: {
					auto n = FastNoise::New<FastNoise::Multiply>();
					n->SetLHS(a);
					n->SetRHS(b);
					return n;
				}
				case NoiseCombineOp::kMin: {
					auto n = FastNoise::New<FastNoise::Min>();
					n->SetLHS(a);
					n->SetRHS(b);
					return n;
				}
				case NoiseCombineOp::kMax: {
					auto n = FastNoise::New<FastNoise::Max>();
					n->SetLHS(a);
					n->SetRHS(b);
					return n;
				}
			}
			return a;
		}
	}
	return FastNoise::New<FastNoise::Constant>();
}

} // namespace

std::function<double(double, double)> compile_fastnoise2_2d(
		const NoiseNodePtr &node, std::uint64_t seed) {
	FastNoise::SmartNode<> compiled = compile_or_default(node);
	const int seed_i = static_cast<int>(seed);
	return [compiled, seed_i](double x, double y) {
		return static_cast<double>(
				compiled->GenSingle2D(static_cast<float>(x), static_cast<float>(y), seed_i));
	};
}

std::function<double(double, double, double)> compile_fastnoise2_3d(
		const NoiseNodePtr &node, std::uint64_t seed) {
	FastNoise::SmartNode<> compiled = compile_or_default(node);
	const int seed_i = static_cast<int>(seed);
	return [compiled, seed_i](double x, double y, double z) {
		return static_cast<double>(compiled->GenSingle3D(
				static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), seed_i));
	};
}

} // namespace vb::worldgen

#endif // VB_WITH_WORLDGEN
