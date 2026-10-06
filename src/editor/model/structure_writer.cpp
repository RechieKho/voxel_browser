#include "vb/editor/structure_writer.hpp"

#include <charconv>
#include <cstdio>

namespace vb::editor {

namespace {

std::string quote(const std::string &s) {
	std::string out = "\"";
	for (const char c : s) {
		const auto u = static_cast<unsigned char>(c);
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (u < 32 || u == 127) {
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\%03u", static_cast<unsigned>(u));
			out += buf;
		} else {
			out += c;
		}
	}
	out += '"';
	return out;
}

std::string number(double v) {
	char buf[32];
	const auto res = std::to_chars(buf, buf + sizeof buf, v);
	return std::string(buf, res.ptr);
}

std::string vec3(const char *name, core::IVec3 v) {
	return std::string(name) + " = { x = " + std::to_string(v.x) + ", y = " + std::to_string(v.y) +
			", z = " + std::to_string(v.z) + " },\n";
}

} // namespace

std::string structure_file_stem(const std::string &name) {
	const auto colon = name.rfind(':');
	std::string stem = colon == std::string::npos ? name : name.substr(colon + 1);
	for (char &c : stem) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				c == '_' || c == '-';
		if (!ok) {
			c = '_';
		}
	}
	return stem.empty() ? "structure" : stem;
}

std::string write_structure_lua(const worldgen::StructureSpec &spec) {
	std::string out;
	out += "-- " + spec.name + ". Written by vb_structure_editor.\n";
	out += "-- The layout is stable: hand edits that keep it valid load fine, but the editor\n";
	out += "-- rewrites the whole file when it saves.\n";
	out += "return {\n";
	out += "\tname = " + quote(spec.name) + ",\n";
	out += "\t" + vec3("size", spec.size);
	out += "\t" + vec3("anchor", spec.anchor);
	out += "\tpalette = {\n";
	for (const auto &[key, block] : spec.palette) {
		out += "\t\t[" + quote(std::string(1, key)) + "] = " + (block ? quote(*block) : std::string("false")) + ",\n";
	}
	out += "\t},\n";
	out += "\tvariants = {\n";
	for (const auto &variant : spec.variants) {
		out += "\t\t{\n\t\t\tweight = " + number(variant.weight) + ",\n\t\t\tlayers = {\n";
		for (std::size_t y = 0; y < variant.layers.size(); ++y) {
			out += "\t\t\t\t{ -- y = " + std::to_string(y) + "\n";
			for (const std::string &row : variant.layers[y]) {
				out += "\t\t\t\t\t" + quote(row) + ",\n";
			}
			out += "\t\t\t\t},\n";
		}
		out += "\t\t\t},\n\t\t},\n";
	}
	out += "\t},\n";

	const worldgen::PlacementSpec &p = spec.placement;
	if (!p.empty()) {
		out += "\tplacement = {\n";
		if (p.on) {
			out += "\t\ton = {";
			for (std::size_t i = 0; i < p.on->size(); ++i) {
				out += (i == 0 ? " " : ", ") + quote((*p.on)[i]);
			}
			out += p.on->empty() ? "},\n" : " },\n";
		}
		if (p.replace) {
			out += std::string("\t\treplace = ") + quote(worldgen::replace_policy_name(*p.replace)) + ",\n";
		}
		if (p.rotate) {
			out += std::string("\t\trotate = ") + (*p.rotate ? "true" : "false") + ",\n";
		}
		if (p.mirror) {
			out += std::string("\t\tmirror = ") + (*p.mirror ? "true" : "false") + ",\n";
		}
		if (p.min_spacing) {
			out += "\t\tmin_spacing = " + std::to_string(*p.min_spacing) + ",\n";
		}
		if (p.cluster) {
			out += "\t\tcluster = " + number(*p.cluster) + ",\n";
		}
		if (p.max_slope) {
			out += "\t\tmax_slope = " + std::to_string(*p.max_slope) + ",\n";
		}
		if (p.y_min) {
			out += "\t\ty_min = " + std::to_string(*p.y_min) + ",\n";
		}
		if (p.y_max) {
			out += "\t\ty_max = " + std::to_string(*p.y_max) + ",\n";
		}
		out += "\t},\n";
	}
	out += "}\n";
	return out;
}

std::string write_all_index(const std::vector<std::string> &stems) {
	std::string out;
	out += "-- Written by vb_structure_editor: every structure in this folder.\n";
	out += "-- Register them all from pack code with:\n";
	out += "--   for _, s in ipairs(require(\"structures.all\")) do vb.register_structure(s) end\n";
	out += "return {\n";
	for (const std::string &stem : stems) {
		out += "\trequire(" + quote("structures." + stem) + "),\n";
	}
	out += "}\n";
	return out;
}

} // namespace vb::editor
