#include "vb/automation/protocol.hpp"

namespace vb::automation {

using nlohmann::json;

std::optional<Request> parse_request(const std::string &line, std::string &error) {
	json j = json::parse(line, nullptr, /*allow_exceptions*/ false);
	if (j.is_discarded() || !j.is_object()) {
		error = "request must be a JSON object";
		return std::nullopt;
	}
	Request req;
	if (auto it = j.find("id"); it != j.end()) {
		if (!it->is_number() && !it->is_string()) {
			error = "id must be a number or string";
			return std::nullopt;
		}
		req.id = *it;
	}
	auto cmd = j.find("cmd");
	if (cmd == j.end() || !cmd->is_string() || cmd->get<std::string>().empty()) {
		error = "missing string field 'cmd'";
		return std::nullopt;
	}
	req.cmd = cmd->get<std::string>();
	if (auto it = j.find("args"); it != j.end()) {
		if (!it->is_object()) {
			error = "args must be an object";
			return std::nullopt;
		}
		req.args = *it;
	}
	// `hello` carries `proto` at the top level in the doc's example; fold it
	// into args so handlers have a single place to look.
	if (auto it = j.find("proto"); it != j.end() && !req.args.contains("proto")) {
		req.args["proto"] = *it;
	}
	return req;
}

json make_response(const json &id, const Reply &reply) {
	json out = json::object();
	out["id"] = id;
	out["ok"] = reply.ok;
	if (reply.ok) {
		out["result"] = reply.result;
	} else {
		json err = reply.extra.is_object() ? reply.extra : json::object();
		err["code"] = reply.code;
		err["message"] = reply.message;
		out["error"] = std::move(err);
	}
	return out;
}

json make_event(const std::string &name, json fields) {
	json out = fields.is_object() ? std::move(fields) : json::object();
	out["event"] = name;
	return out;
}

} // namespace vb::automation
