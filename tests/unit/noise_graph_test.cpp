#include <doctest/doctest.h>

#include <memory>

#include "vb/worldgen/noise_graph.hpp"

// Pure C++ evaluator tests for the Phase 6.14 noise-graph IR
// (vb/worldgen/noise_graph.hpp) -- no Lua/PackRuntime involved, since the IR
// itself has no sol2 dependency (see that header's own comment). Parsing a
// vb.noise.*-built Lua table into this IR is covered separately by
// pack_runtime_test.cpp/pack_runtime_integration_test.cpp.

using vb::worldgen::NoiseCombineOp;
using vb::worldgen::NoiseNode;
using vb::worldgen::NoiseNodePtr;
using vb::worldgen::NoiseNodeType;

namespace {

NoiseNodePtr make_value(std::uint64_t salt = 0, double frequency = 1.0) {
	auto n = std::make_shared<NoiseNode>();
	n->type = NoiseNodeType::kValue;
	n->salt = salt;
	n->frequency = frequency;
	return n;
}

} // namespace

TEST_CASE("kConstant always returns its literal value") {
	auto n = std::make_shared<NoiseNode>();
	n->type = NoiseNodeType::kConstant;
	n->constant_value = 3.5;
	CHECK(n->eval2(1, 10.0, 20.0) == doctest::Approx(3.5));
	CHECK(n->eval3(1, 10.0, 20.0, 30.0) == doctest::Approx(3.5));
}

TEST_CASE("kValue is pure and stays in [0, 1)") {
	NoiseNodePtr n = make_value();
	const double a = n->eval2(42, 1.25, -3.5);
	const double b = n->eval2(42, 1.25, -3.5);
	CHECK(a == doctest::Approx(b));
	CHECK(a >= 0.0);
	CHECK(a < 1.0);
}

TEST_CASE("two structurally-identical kValue nodes with different salts "
		"decorrelate") {
	NoiseNodePtr a = make_value(0);
	NoiseNodePtr b = make_value(1);
	// Not a mathematical guarantee for every input, but true almost
	// everywhere for hash noise -- sample a handful of points and require at
	// least one divergence.
	bool any_diff = false;
	for (int i = 0; i < 8; ++i) {
		const double x = static_cast<double>(i) * 3.7;
		const double y = static_cast<double>(i) * -1.9;
		if (a->eval2(7, x, y) != b->eval2(7, x, y)) {
			any_diff = true;
			break;
		}
	}
	CHECK(any_diff);
}

TEST_CASE("kCellular stays in [0, 1)") {
	auto n = std::make_shared<NoiseNode>();
	n->type = NoiseNodeType::kCellular;
	n->frequency = 0.1;
	for (int i = -5; i <= 5; ++i) {
		const double v = n->eval2(3, static_cast<double>(i) * 4.1, static_cast<double>(i) * -2.2);
		CHECK(v >= 0.0);
		CHECK(v < 1.0);
	}
}

TEST_CASE("kFbm wrapping a kValue source stays normalized and is pure") {
	auto fbm = std::make_shared<NoiseNode>();
	fbm->type = NoiseNodeType::kFbm;
	fbm->octaves = 5;
	fbm->lacunarity = 2.0;
	fbm->gain = 0.5;
	fbm->source = make_value(9);

	const double a = fbm->eval2(11, 4.0, -2.0);
	const double b = fbm->eval2(11, 4.0, -2.0);
	CHECK(a == doctest::Approx(b));
	CHECK(a >= 0.0);
	CHECK(a < 1.0);
}

TEST_CASE("kRemap maps its source's [in_min,in_max) onto [out_min,out_max)") {
	auto constant = std::make_shared<NoiseNode>();
	constant->type = NoiseNodeType::kConstant;
	constant->constant_value = 0.5; // midpoint of the default [0,1) input range

	auto remap = std::make_shared<NoiseNode>();
	remap->type = NoiseNodeType::kRemap;
	remap->source = constant;
	remap->in_min = 0.0;
	remap->in_max = 1.0;
	remap->out_min = 10.0;
	remap->out_max = 20.0;

	CHECK(remap->eval2(0, 0.0, 0.0) == doctest::Approx(15.0));
}

TEST_CASE("kCombine implements add/multiply/min/max over two constants") {
	auto ca = std::make_shared<NoiseNode>();
	ca->type = NoiseNodeType::kConstant;
	ca->constant_value = 2.0;
	auto cb = std::make_shared<NoiseNode>();
	cb->type = NoiseNodeType::kConstant;
	cb->constant_value = 5.0;

	auto combine = [&](NoiseCombineOp op) {
		auto n = std::make_shared<NoiseNode>();
		n->type = NoiseNodeType::kCombine;
		n->a = ca;
		n->b = cb;
		n->op = op;
		return n->eval2(0, 0.0, 0.0);
	};

	CHECK(combine(NoiseCombineOp::kAdd) == doctest::Approx(7.0));
	CHECK(combine(NoiseCombineOp::kMultiply) == doctest::Approx(10.0));
	CHECK(combine(NoiseCombineOp::kMin) == doctest::Approx(2.0));
	CHECK(combine(NoiseCombineOp::kMax) == doctest::Approx(5.0));
}

TEST_CASE("eval3 is pure and stays in [0, 1) for kValue/kFbm") {
	NoiseNodePtr value = make_value(3);
	const double v1 = value->eval3(5, 1.0, 2.0, 3.0);
	const double v2 = value->eval3(5, 1.0, 2.0, 3.0);
	CHECK(v1 == doctest::Approx(v2));
	CHECK(v1 >= 0.0);
	CHECK(v1 < 1.0);

	auto fbm = std::make_shared<NoiseNode>();
	fbm->type = NoiseNodeType::kFbm;
	fbm->octaves = 3;
	fbm->source = make_value(4);
	const double f = fbm->eval3(5, 1.0, 2.0, 3.0);
	CHECK(f >= 0.0);
	CHECK(f < 1.0);
}
