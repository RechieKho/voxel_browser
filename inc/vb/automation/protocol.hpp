// Development-only automation protocol (docs/e2e-automation.md §5): JSON lines,
// one request/response object per line, correlated by `id`. Compiled only when
// VB_WITH_AUTOMATION is ON; production binaries contain none of this.
#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace vb::automation {

// Bump on any incompatible change to commands/predicates; checked by `hello`.
inline constexpr int kAutomationProtocolVersion = 1;

struct Request {
	nlohmann::json id; // echoed verbatim (number or string); null if absent
	std::string cmd;
	nlohmann::json args = nlohmann::json::object();
};

struct Reply {
	bool ok = true;
	nlohmann::json result = nlohmann::json::object();
	std::string code; // error only
	std::string message; // error only
	nlohmann::json extra = nlohmann::json::object(); // merged into the error object

	static Reply success(nlohmann::json result = nlohmann::json::object()) {
		Reply r;
		r.result = std::move(result);
		return r;
	}
	static Reply error(std::string code, std::string message,
			nlohmann::json extra = nlohmann::json::object()) {
		Reply r;
		r.ok = false;
		r.code = std::move(code);
		r.message = std::move(message);
		r.extra = std::move(extra);
		return r;
	}
};

// Parses one protocol line. On failure returns nullopt and sets `error`.
std::optional<Request> parse_request(const std::string &line, std::string &error);

// `{"id":..,"ok":true,"result":..}` / `{"id":..,"ok":false,"error":{code,message,..}}`
nlohmann::json make_response(const nlohmann::json &id, const Reply &reply);

// `{"event":<name>, ...fields}` (unsolicited, no id)
nlohmann::json make_event(const std::string &name, nlohmann::json fields);

} // namespace vb::automation
