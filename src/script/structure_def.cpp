#include "vb/script/structure_def.hpp"

#if VB_WITH_LUA

#include <algorithm>
#include <cmath>
#include <string>

namespace vb::script {

namespace {

using worldgen::PlacementSpec;
using worldgen::StructureSpec;

[[noreturn]] void fail(const std::string &context, const std::string &msg) {
	throw sol::error(context + ": " + msg);
}

std::string key_name(const sol::object &key) {
	if (key.get_type() == sol::type::string) {
		return key.as<std::string>();
	}
	return "<" + std::string(key.get_type() == sol::type::number ? "number" : "non-string") +
			" key>";
}

bool is_integer(const sol::object &o) {
	if (o.get_type() != sol::type::number) {
		return false;
	}
	const double d = o.as<double>();
	return std::isfinite(d) && d == std::floor(d);
}

int read_int(const sol::object &o, const std::string &context, const char *field) {
	if (!is_integer(o)) {
		fail(context, std::string("'") + field + "' must be an integer");
	}
	return static_cast<int>(o.as<double>());
}

void check_keys(const sol::table &tbl, const std::string &context,
		std::initializer_list<const char *> allowed) {
	for (const auto &kv : tbl) {
		const std::string key = key_name(kv.first);
		const bool ok = kv.first.get_type() == sol::type::string &&
				std::any_of(allowed.begin(), allowed.end(),
						[&](const char *a) { return key == a; });
		if (!ok) {
			fail(context, "unknown key '" + key + "'");
		}
	}
}

core::IVec3 read_vec3(const sol::object &o, const std::string &context, const char *field) {
	if (o.get_type() != sol::type::table) {
		fail(context, std::string("'") + field + "' must be a table {x=, y=, z=}");
	}
	const sol::table t = o.as<sol::table>();
	check_keys(t, context + " '" + field + "'", { "x", "y", "z" });
	core::IVec3 v;
	const auto axis = [&](const char *name) {
		const sol::object a = t.get<sol::object>(name);
		if (a.get_type() == sol::type::lua_nil) {
			fail(context, std::string("'") + field + "." + name + "' is required");
		}
		return read_int(a, context, name);
	};
	v.x = axis("x");
	v.y = axis("y");
	v.z = axis("z");
	return v;
}

} // namespace

PlacementSpec parse_placement(const sol::table &tbl, const std::string &context,
		std::initializer_list<const char *> extra_allowed) {
	PlacementSpec p;
	for (const auto &kv : tbl) {
		const std::string key = key_name(kv.first);
		if (kv.first.get_type() != sol::type::string) {
			fail(context, "unknown key '" + key + "'");
		}
		const sol::object &v = kv.second;
		if (key == "on") {
			if (v.get_type() != sol::type::table) {
				fail(context, "'on' must be a list of block names");
			}
			std::vector<std::string> names;
			for (const auto &item : v.as<sol::table>()) {
				if (item.second.get_type() != sol::type::string) {
					fail(context, "'on' must be a list of block names");
				}
				names.push_back(item.second.as<std::string>());
			}
			p.on = std::move(names);
		} else if (key == "replace") {
			const auto policy = v.get_type() == sol::type::string
					? worldgen::parse_replace_policy(v.as<std::string>())
					: std::nullopt;
			if (!policy) {
				fail(context, "'replace' must be \"air\", \"air_and_plants\" or \"all\"");
			}
			p.replace = policy;
		} else if (key == "rotate" || key == "mirror") {
			if (v.get_type() != sol::type::boolean) {
				fail(context, "'" + key + "' must be a boolean");
			}
			(key == "rotate" ? p.rotate : p.mirror) = v.as<bool>();
		} else if (key == "min_spacing") {
			p.min_spacing = read_int(v, context, "min_spacing");
		} else if (key == "max_slope") {
			p.max_slope = read_int(v, context, "max_slope");
		} else if (key == "y_min") {
			p.y_min = read_int(v, context, "y_min");
		} else if (key == "y_max") {
			p.y_max = read_int(v, context, "y_max");
		} else if (key == "cluster") {
			if (v.get_type() != sol::type::number) {
				fail(context, "'cluster' must be a number");
			}
			p.cluster = v.as<double>();
		} else if (std::none_of(extra_allowed.begin(), extra_allowed.end(),
						   [&](const char *a) { return key == a; })) {
			fail(context, "unknown key '" + key + "'");
		}
	}
	const std::string bad = worldgen::validate_placement_spec(p, context);
	if (!bad.empty()) {
		throw sol::error(bad);
	}
	return p;
}

StructureSpec parse_structure(const sol::table &def) {
	StructureSpec spec;
	const sol::object name_obj = def.get<sol::object>("name");
	if (name_obj.get_type() != sol::type::string || name_obj.as<std::string>().empty()) {
		throw sol::error("structure: 'name' is required");
	}
	spec.name = name_obj.as<std::string>();
	const std::string ctx = "structure '" + spec.name + "'";

	check_keys(def, ctx, { "name", "size", "anchor", "palette", "variants", "placement" });

	spec.size = read_vec3(def.get<sol::object>("size"), ctx, "size");
	if (def.get<sol::object>("anchor").get_type() != sol::type::lua_nil) {
		spec.anchor = read_vec3(def.get<sol::object>("anchor"), ctx, "anchor");
	}

	const sol::object palette_obj = def.get<sol::object>("palette");
	if (palette_obj.get_type() != sol::type::table) {
		fail(ctx, "'palette' must be a table");
	}
	for (const auto &kv : palette_obj.as<sol::table>()) {
		if (kv.first.get_type() != sol::type::string || kv.first.as<std::string>().size() != 1) {
			fail(ctx, "palette keys must be single characters (got " + key_name(kv.first) + ")");
		}
		const char key = kv.first.as<std::string>()[0];
		if (kv.second.get_type() == sol::type::boolean && !kv.second.as<bool>()) {
			spec.palette[key] = std::nullopt;
		} else if (kv.second.get_type() == sol::type::string) {
			spec.palette[key] = kv.second.as<std::string>();
		} else {
			fail(ctx, std::string("palette '") + key + "' must be a block name or false");
		}
	}

	const sol::object variants_obj = def.get<sol::object>("variants");
	if (variants_obj.get_type() != sol::type::table) {
		fail(ctx, "'variants' must be a list");
	}
	for (const auto &vkv : variants_obj.as<sol::table>()) {
		if (vkv.second.get_type() != sol::type::table) {
			fail(ctx, "each variant must be a table {weight=, layers=}");
		}
		const sol::table vt = vkv.second.as<sol::table>();
		check_keys(vt, ctx + " variant", { "weight", "layers" });
		worldgen::StructureVariantSpec variant;
		const sol::object weight = vt.get<sol::object>("weight");
		if (weight.get_type() != sol::type::lua_nil) {
			if (weight.get_type() != sol::type::number) {
				fail(ctx, "variant 'weight' must be a number");
			}
			variant.weight = weight.as<double>();
		}
		const sol::object layers = vt.get<sol::object>("layers");
		if (layers.get_type() != sol::type::table) {
			fail(ctx, "variant 'layers' must be a list of layers");
		}
		for (const auto &lkv : layers.as<sol::table>()) {
			if (lkv.second.get_type() != sol::type::table) {
				fail(ctx, "each layer must be a list of row strings");
			}
			std::vector<std::string> rows;
			for (const auto &rkv : lkv.second.as<sol::table>()) {
				if (rkv.second.get_type() != sol::type::string) {
					fail(ctx, "each row must be a string");
				}
				rows.push_back(rkv.second.as<std::string>());
			}
			variant.layers.push_back(std::move(rows));
		}
		spec.variants.push_back(std::move(variant));
	}

	const sol::object placement_obj = def.get<sol::object>("placement");
	if (placement_obj.get_type() != sol::type::lua_nil) {
		if (placement_obj.get_type() != sol::type::table) {
			fail(ctx, "'placement' must be a table");
		}
		spec.placement = parse_placement(placement_obj.as<sol::table>(), ctx);
	}

	const std::string bad = worldgen::validate_structure_spec(spec);
	if (!bad.empty()) {
		throw sol::error(bad);
	}
	return spec;
}

} // namespace vb::script

#endif // VB_WITH_LUA
