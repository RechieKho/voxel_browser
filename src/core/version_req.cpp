#include "vb/core/version_req.hpp"

#include <charconv>

#include "vb/core/version.hpp"

namespace vb::core {

namespace {

bool parse_part(std::string_view s, int &out) {
	if (s.empty() || s.size() > 6) {
		return false;
	}
	const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
	return ec == std::errc{} && ptr == s.data() + s.size();
}

std::string_view trim(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.remove_prefix(1);
	}
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
		s.remove_suffix(1);
	}
	return s;
}

// Number of components written ("0.5" = 2), so `^`/`~` can tell `^0` from `^0.5`.
struct Partial {
	SemVer v;
	int parts = 0;
};

std::optional<Partial> parse_partial(std::string_view text) {
	text = trim(text);
	if (!text.empty() && text.front() == 'v') {
		text.remove_prefix(1);
	}
	if (text.empty()) {
		return std::nullopt;
	}
	Partial p;
	int *slots[3] = { &p.v.major, &p.v.minor, &p.v.patch };
	std::size_t pos = 0;
	while (true) {
		if (p.parts == 3) {
			return std::nullopt;
		}
		const std::size_t dot = text.find('.', pos);
		const std::string_view part =
				text.substr(pos, dot == std::string_view::npos ? std::string_view::npos : dot - pos);
		if (!parse_part(part, *slots[p.parts])) {
			return std::nullopt;
		}
		++p.parts;
		if (dot == std::string_view::npos) {
			break;
		}
		pos = dot + 1;
	}
	return p;
}

} // namespace

std::optional<SemVer> parse_semver(std::string_view text) {
	const auto p = parse_partial(text);
	if (!p) {
		return std::nullopt;
	}
	return p->v;
}

std::string to_string(const SemVer &v) {
	return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

VersionReq VersionReq::any() {
	return VersionReq{};
}

std::optional<VersionReq> VersionReq::parse(std::string_view text, std::string *error) {
	const auto fail = [&](std::string msg) -> std::optional<VersionReq> {
		if (error != nullptr) {
			*error = std::move(msg);
		}
		return std::nullopt;
	};
	text = trim(text);
	if (text.empty()) {
		return fail("empty version requirement (use \"*\" for any version)");
	}
	if (text == "*") {
		return any();
	}
	VersionReq req;
	std::size_t pos = 0;
	while (pos <= text.size()) {
		const std::size_t comma = text.find(',', pos);
		std::string_view item =
				trim(text.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos));
		if (item.empty()) {
			return fail("empty comparator in \"" + std::string(text) + "\"");
		}
		Op op = Op::kEq;
		bool caret = false;
		bool tilde = false;
		if (item.substr(0, 2) == ">=") {
			op = Op::kGe;
			item.remove_prefix(2);
		} else if (item.substr(0, 2) == "<=") {
			op = Op::kLe;
			item.remove_prefix(2);
		} else if (item.front() == '>') {
			op = Op::kGt;
			item.remove_prefix(1);
		} else if (item.front() == '<') {
			op = Op::kLt;
			item.remove_prefix(1);
		} else if (item.front() == '=') {
			item.remove_prefix(1);
		} else if (item.front() == '^') {
			caret = true;
			item.remove_prefix(1);
		} else if (item.front() == '~') {
			tilde = true;
			item.remove_prefix(1);
		} else {
			return fail("expected a comparator (>=, >, <=, <, =, ^, ~) in \"" + std::string(item) + "\"");
		}
		const auto partial = parse_partial(item);
		if (!partial) {
			return fail("invalid version \"" + std::string(trim(item)) + "\" (expected MAJOR[.MINOR[.PATCH]])");
		}
		const SemVer v = partial->v;
		if (caret || tilde) {
			// ^1.2.3 = >=1.2.3,<2.0.0; ^0.5.1 = >=0.5.1,<0.6.0; ^0.0.3 = >=0.0.3,<0.0.4;
			// ~1.2.3 = >=1.2.3,<1.3.0; ~1 = >=1.0.0,<2.0.0.
			SemVer upper;
			if (tilde) {
				upper = partial->parts == 1 ? SemVer{ v.major + 1, 0, 0 } : SemVer{ v.major, v.minor + 1, 0 };
			} else if (v.major > 0 || partial->parts == 1) {
				upper = { v.major + 1, 0, 0 };
			} else if (v.minor > 0 || partial->parts == 2) {
				upper = { 0, v.minor + 1, 0 };
			} else {
				upper = { 0, 0, v.patch + 1 };
			}
			req.comparators_.push_back({ Op::kGe, v });
			req.comparators_.push_back({ Op::kLt, upper });
		} else {
			req.comparators_.push_back({ op, v });
		}
		if (comma == std::string_view::npos) {
			break;
		}
		pos = comma + 1;
	}
	return req;
}

bool VersionReq::matches(const SemVer &v) const {
	for (const Comparator &c : comparators_) {
		bool ok = false;
		switch (c.op) {
			case Op::kGe:
				ok = v >= c.version;
				break;
			case Op::kGt:
				ok = v > c.version;
				break;
			case Op::kLe:
				ok = v <= c.version;
				break;
			case Op::kLt:
				ok = v < c.version;
				break;
			case Op::kEq:
				ok = v == c.version;
				break;
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}

std::string VersionReq::to_string() const {
	if (comparators_.empty()) {
		return "*";
	}
	std::string out;
	for (const Comparator &c : comparators_) {
		if (!out.empty()) {
			out += ", ";
		}
		switch (c.op) {
			case Op::kGe:
				out += ">=";
				break;
			case Op::kGt:
				out += ">";
				break;
			case Op::kLe:
				out += "<=";
				break;
			case Op::kLt:
				out += "<";
				break;
			case Op::kEq:
				out += "=";
				break;
		}
		out += vb::core::to_string(c.version);
	}
	return out;
}

std::optional<SemVer> VersionReq::lower_bound() const {
	for (const Comparator &c : comparators_) {
		if (c.op == Op::kGe || c.op == Op::kEq || c.op == Op::kGt) {
			return c.version;
		}
	}
	return std::nullopt;
}

SemVer engine_version() {
	return parse_semver(kVersionNumeric).value_or(SemVer{});
}

EngineReqCheck check_engine_req(const std::string &pack_name, const std::optional<std::string> &req_text,
		const SemVer &engine) {
	EngineReqCheck out;
	if (!req_text) {
		out.warning = true;
		out.message = "pack '" + pack_name +
				"' has no engine_version_req in pack.toml; treating it as \"*\" (add e.g. engine_version_req = \">=" +
				vb::core::to_string(engine) + "\")";
		return out;
	}
	std::string err;
	const auto req = VersionReq::parse(*req_text, &err);
	if (!req) {
		out.ok = false;
		out.message = "pack '" + pack_name + "': invalid engine_version_req \"" + *req_text + "\" in pack.toml: " + err;
		return out;
	}
	if (!req->matches(engine)) {
		out.ok = false;
		out.message = "pack '" + pack_name + "' requires engine " + req->to_string() +
				" (pack.toml engine_version_req); this build is " + vb::core::to_string(engine);
	}
	return out;
}

} // namespace vb::core
