#include "vb/automation/predicate.hpp"

#include <cmath>
#include <regex>

namespace vb::automation {

using nlohmann::json;

namespace {

constexpr int kMaxDepth = 16;

struct Eval {
	const PredicateContext &ctx;

	static EvalResult fail(std::string msg) {
		EvalResult r;
		r.error = std::move(msg);
		return r;
	}
	static EvalResult result(bool matched, json observed) {
		EvalResult r;
		r.matched = matched;
		r.observed = std::move(observed);
		return r;
	}

	const json *state_field(const char *name) const {
		auto it = ctx.state.find(name);
		return it == ctx.state.end() ? nullptr : &*it;
	}

	// `{"p": "x"}` shorthand: a bare scalar stands for the predicate's primary arg.
	static json normalize(const json &arg, const char *primary) {
		if (arg.is_object()) {
			return arg;
		}
		json o = json::object();
		o[primary] = arg;
		return o;
	}

	static bool read_pos(const json &j, double out[3]) {
		if (!j.is_array() || j.size() != 3) {
			return false;
		}
		for (int i = 0; i < 3; ++i) {
			if (!j[static_cast<std::size_t>(i)].is_number()) {
				return false;
			}
			out[i] = j[static_cast<std::size_t>(i)].get<double>();
		}
		return true;
	}

	static bool near(const json &have, const double want[3], double radius) {
		double p[3];
		if (!read_pos(have, p)) {
			return false;
		}
		const double dx = p[0] - want[0], dy = p[1] - want[1], dz = p[2] - want[2];
		return std::sqrt(dx * dx + dy * dy + dz * dz) <= radius;
	}

	static bool compare(const std::string &op, double a, double b, bool &known) {
		known = true;
		if (op == "<")
			return a < b;
		if (op == "<=")
			return a <= b;
		if (op == ">")
			return a > b;
		if (op == ">=")
			return a >= b;
		if (op == "==")
			return a == b;
		if (op == "!=")
			return a != b;
		known = false;
		return false;
	}

	EvalResult numeric_compare(const char *name, const char *field, const json &arg,
			const char *default_op) const {
		json a = arg.is_object() ? arg : json{ { "op", "==" }, { "value", arg } };
		if (!a.contains("value") || !a["value"].is_number()) {
			return fail(std::string(name) + ": needs numeric 'value'");
		}
		const std::string op = a.value("op", std::string(default_op));
		const json *f = state_field(field);
		const json observed = f != nullptr ? *f : json();
		bool known = false;
		const bool m = f != nullptr && f->is_number() &&
				compare(op, f->get<double>(), a["value"].get<double>(), known);
		if (f != nullptr && f->is_number() && !known) {
			return fail(std::string(name) + ": unknown op '" + op + "'");
		}
		return result(m, observed);
	}

	EvalResult named_near(const char *name, const char *list_field, const json &arg) const {
		if (!arg.is_object() || !arg.contains("name") || !arg["name"].is_string()) {
			return fail(std::string(name) + ": needs 'name'");
		}
		double want[3];
		if (!read_pos(arg.value("pos", json()), want) || !arg.contains("radius") ||
				!arg["radius"].is_number()) {
			return fail(std::string(name) + ": needs 'pos' [x,y,z] and numeric 'radius'");
		}
		const std::string who = arg["name"].get<std::string>();
		const double radius = arg["radius"].get<double>();
		json seen = json::array();
		bool matched = false;
		if (const json *list = state_field(list_field); list != nullptr && list->is_array()) {
			for (const json &e : *list) {
				if (e.value("name", std::string()) != who) {
					continue;
				}
				seen.push_back(e.value("pos", json()));
				matched = matched || near(e.value("pos", json()), want, radius);
			}
		}
		return result(matched, seen);
	}

	EvalResult leaf(const std::string &name, const json &arg) const {
		if (name == "joined") {
			const json *f = state_field("joined");
			return result(f != nullptr && f->is_boolean() && f->get<bool>(),
					f != nullptr ? *f : json(false));
		}
		if (name == "on_ground") {
			const json *f = state_field("on_ground");
			return result(f != nullptr && f->is_boolean() && f->get<bool>(),
					f != nullptr ? *f : json(false));
		}
		if (name == "app_state") {
			const json a = normalize(arg, "is");
			const json *f = state_field("app_state");
			if (!a.contains("is") || !a["is"].is_string()) {
				return fail("app_state: needs 'is'");
			}
			return result(f != nullptr && *f == a["is"], f != nullptr ? *f : json());
		}
		if (name == "chat_contains") {
			const json a = normalize(arg, "text");
			const bool is_regex = a.contains("regex");
			const char *key = is_regex ? "regex" : "text";
			if (!a.contains(key) || !a[key].is_string()) {
				return fail("chat_contains: needs 'text' or 'regex'");
			}
			const std::string needle = a[key].get<std::string>();
			std::regex re;
			if (is_regex) {
				try {
					re = std::regex(needle);
				} catch (const std::regex_error &) {
					return fail("chat_contains: invalid regex");
				}
			}
			json chat = json::array();
			bool matched = false;
			if (const json *f = state_field("chat"); f != nullptr && f->is_array()) {
				chat = *f;
				for (const json &line : *f) {
					if (!line.is_string()) {
						continue;
					}
					const std::string s = line.get<std::string>();
					matched = matched ||
							(is_regex ? std::regex_search(s, re)
									  : s.find(needle) != std::string::npos);
				}
			}
			return result(matched, json{ { "chat", chat } });
		}
		if (name == "entity_visible") {
			const json a = normalize(arg, "name");
			if (!a.contains("name") || !a["name"].is_string()) {
				return fail("entity_visible: needs 'name'");
			}
			json names = json::array();
			bool matched = false;
			if (const json *f = state_field("entities"); f != nullptr && f->is_array()) {
				for (const json &e : *f) {
					names.push_back(e.value("name", std::string()));
					matched = matched || json(e.value("name", std::string())) == a["name"];
				}
			}
			return result(matched, json{ { "entities", names } });
		}
		if (name == "entity_near")
			return named_near("entity_near", "entities", arg);
		if (name == "player_near")
			return named_near("player_near", "players", arg);
		if (name == "player_login") {
			// Server only (auth.md §9.7): a connected player `name` whose verified
			// login has this `subject` (and `provider`, if given). A player with
			// no login -- the server isn't authenticating -- never matches.
			if (!arg.is_object() || !arg.contains("subject") || !arg["subject"].is_string()) {
				return fail("player_login: needs 'subject' (and optionally 'name', 'provider')");
			}
			const json want_name = arg.value("name", json());
			json seen = json::array();
			bool matched = false;
			if (const json *list = state_field("players"); list != nullptr && list->is_array()) {
				for (const json &e : *list) {
					const json login = e.value("login", json());
					seen.push_back(login);
					if (!login.is_object() || login.value("subject", json()) != arg["subject"]) {
						continue;
					}
					if (arg.contains("provider") && login.value("provider", json()) != arg["provider"]) {
						continue;
					}
					if (!want_name.is_null() && e.value("name", json()) != want_name) {
						continue;
					}
					matched = true;
				}
			}
			return result(matched, json{ { "logins", seen } });
		}
		if (name == "block_is") {
			if (!arg.is_object() || !arg.contains("block") || !arg["block"].is_string()) {
				return fail("block_is: needs 'pos' and 'block'");
			}
			double p[3];
			if (!read_pos(arg.value("pos", json()), p)) {
				return fail("block_is: needs 'pos' [x,y,z]");
			}
			std::optional<std::string> have;
			if (ctx.block_name_at) {
				have = ctx.block_name_at(static_cast<int>(std::floor(p[0])),
						static_cast<int>(std::floor(p[1])), static_cast<int>(std::floor(p[2])));
			}
			return result(have && *have == arg["block"].get<std::string>(),
					json{ { "block", have ? json(*have) : json() } });
		}
		if (name == "chunk_loaded") {
			double p[3];
			if (!arg.is_object() || !read_pos(arg.value("pos", json()), p)) {
				return fail("chunk_loaded: needs 'pos' [x,y,z]");
			}
			const bool loaded = ctx.chunk_loaded &&
					ctx.chunk_loaded(static_cast<int>(std::floor(p[0])), static_cast<int>(std::floor(p[1])),
							static_cast<int>(std::floor(p[2])));
			return result(loaded, json{ { "loaded", loaded } });
		}
		if (name == "ui_open") {
			const json a = normalize(arg, "name");
			if (!a.contains("name") || !a["name"].is_string()) {
				return fail("ui_open: needs 'name'");
			}
			json have;
			if (const json *f = state_field("ui"); f != nullptr && f->is_object()) {
				have = f->value("name", json());
			}
			return result(have == a["name"], json{ { "ui", have } });
		}
		if (name == "widget" || name == "hud_widget") {
			if (!arg.is_object() || !arg.contains("id") || !arg["id"].is_string()) {
				return fail(name + ": needs 'id'");
			}
			json ids = json::array();
			bool matched = false;
			if (const json *f = state_field(name == "widget" ? "ui" : "hud");
					f != nullptr && f->is_object()) {
				for (const json &w : f->value("widgets", json::array())) {
					ids.push_back(w.value("id", std::string()));
					if (json(w.value("id", std::string())) == arg["id"]) {
						matched = !arg.contains("text") || w.value("text", json()) == arg["text"];
					}
				}
			}
			return result(matched, json{ { "widgets", ids } });
		}
		if (name == "pos_near") {
			double want[3];
			if (!arg.is_object() || !read_pos(arg.value("pos", json()), want) ||
					!arg.contains("radius") || !arg["radius"].is_number()) {
				return fail("pos_near: needs 'pos' [x,y,z] and numeric 'radius'");
			}
			const json *f = state_field("feet");
			return result(f != nullptr && near(*f, want, arg["radius"].get<double>()),
					json{ { "feet", f != nullptr ? *f : json() } });
		}
		if (name == "inventory_has") {
			if (!arg.is_object() || !arg.contains("item") || !arg["item"].is_string()) {
				return fail("inventory_has: needs 'item'");
			}
			const long long want = arg.value("count", 1LL);
			long long have = 0;
			if (const json *f = state_field("inventory"); f != nullptr && f->is_array()) {
				for (const json &s : *f) {
					if (json(s.value("item", std::string())) == arg["item"]) {
						have += s.value("count", 0LL);
					}
				}
			}
			return result(have >= want, json{ { "count", have } });
		}
		if (name == "health")
			return numeric_compare("health", "health", arg, "==");
		if (name == "rtt_ms")
			return numeric_compare("rtt_ms", "rtt_ms", arg, ">=");
		if (name == "player_count")
			return numeric_compare("player_count", "player_count", arg, "==");
		if (name == "chunks_loaded") {
			const json a = normalize(arg, "min");
			if (!a.contains("min") || !a["min"].is_number()) {
				return fail("chunks_loaded: needs numeric 'min'");
			}
			const json *f = state_field("chunks_loaded");
			return result(f != nullptr && f->is_number() && f->get<double>() >= a["min"].get<double>(),
					f != nullptr ? *f : json());
		}
		return fail("unknown predicate '" + name + "'");
	}

	EvalResult eval(const json &pred, int depth) const {
		if (depth > kMaxDepth) {
			return fail("predicate nested too deeply");
		}
		if (!pred.is_object() || pred.size() != 1) {
			return fail("a predicate is an object with exactly one key");
		}
		const auto entry = pred.begin(); // named: value() refers into `pred`, not the iterator
		const std::string name = entry.key();
		const json &arg = entry.value();
		if (name == "all" || name == "any") {
			if (!arg.is_array() || arg.empty()) {
				return fail(name + ": needs a non-empty array");
			}
			const bool is_all = name == "all";
			bool matched = is_all;
			json observed = json::array();
			for (const json &sub : arg) {
				EvalResult r = eval(sub, depth + 1);
				if (!r.error.empty()) {
					return r;
				}
				observed.push_back(r.observed);
				matched = is_all ? (matched && r.matched) : (matched || r.matched);
			}
			return result(matched, observed);
		}
		if (name == "not") {
			EvalResult r = eval(arg, depth + 1);
			if (!r.error.empty()) {
				return r;
			}
			return result(!r.matched, r.observed);
		}
		return leaf(name, arg);
	}
};

} // namespace

EvalResult evaluate_predicate(const json &pred, const PredicateContext &ctx) {
	return Eval{ ctx }.eval(pred, 0);
}

} // namespace vb::automation
