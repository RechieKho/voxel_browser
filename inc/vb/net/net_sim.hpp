// Development-only fake network conditions (docs/e2e-automation.md §5.4):
//   --net-sim lag_ms=120,jitter_ms=30,loss_pct=5[,reorder_pct=..][,dup_pct=..]
// Header-only and compiled only with VB_WITH_AUTOMATION; production binaries reject the
// flag. Applied to GameNetworkingSockets' fake-packet settings (GnsTransport::set_net_sim),
// which act on packets *this process sends*: give both binaries the same spec for a
// symmetric link (so lag_ms shows up as roughly 2 x lag_ms of round-trip time).
#pragma once

#if defined(VB_WITH_AUTOMATION)

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace vb::net {

struct NetSimParams {
	int lag_ms = 0; // fixed delay on every sent packet
	int jitter_ms = 0; // extra random delay, average jitter_ms, capped at 2 x jitter_ms
	double loss_pct = 0.0; // percent of sent packets dropped
	double reorder_pct = 0.0; // percent of sent packets delayed so later ones overtake them
	double dup_pct = 0.0; // percent of sent packets delivered twice

	bool active() const {
		return lag_ms > 0 || jitter_ms > 0 || loss_pct > 0.0 || reorder_pct > 0.0 || dup_pct > 0.0;
	}
};

// Parses "key=value,key=value". On failure returns nullopt and sets `error`.
inline std::optional<NetSimParams> parse_net_sim(std::string_view spec, std::string &error) {
	NetSimParams p;
	if (spec.empty()) {
		error = "empty --net-sim spec (e.g. lag_ms=120,jitter_ms=30,loss_pct=5)";
		return std::nullopt;
	}
	while (!spec.empty()) {
		const std::size_t comma = spec.find(',');
		const std::string_view item = spec.substr(0, comma);
		spec = comma == std::string_view::npos ? std::string_view{} : spec.substr(comma + 1);
		if (comma != std::string_view::npos && spec.empty()) {
			error = "trailing ',' in --net-sim spec";
			return std::nullopt;
		}
		const std::size_t eq = item.find('=');
		if (eq == std::string_view::npos || eq == 0 || eq + 1 >= item.size()) {
			error = "bad --net-sim item '" + std::string(item) + "' (expected key=value)";
			return std::nullopt;
		}
		const std::string key(item.substr(0, eq));
		const std::string text(item.substr(eq + 1));
		char *end = nullptr;
		const double v = std::strtod(text.c_str(), &end);
		if (end == text.c_str() || *end != '\0' || !(v >= 0.0)) {
			error = "bad value '" + text + "' for --net-sim " + key + " (a non-negative number)";
			return std::nullopt;
		}
		auto pct = [&](double &out) {
			if (v > 100.0) {
				error = "--net-sim " + key + " must be 0..100";
				return false;
			}
			out = v;
			return true;
		};
		auto ms = [&](int &out) {
			if (v > 5000.0) {
				error = "--net-sim " + key + " must be 0..5000";
				return false;
			}
			out = static_cast<int>(v);
			return true;
		};
		bool ok = false;
		if (key == "lag_ms") ok = ms(p.lag_ms);
		else if (key == "jitter_ms") ok = ms(p.jitter_ms);
		else if (key == "loss_pct") ok = pct(p.loss_pct);
		else if (key == "reorder_pct") ok = pct(p.reorder_pct);
		else if (key == "dup_pct") ok = pct(p.dup_pct);
		else error = "unknown --net-sim key '" + key + "' (lag_ms, jitter_ms, loss_pct, reorder_pct, dup_pct)";
		if (!ok) {
			return std::nullopt;
		}
	}
	return p;
}

} // namespace vb::net

#endif // VB_WITH_AUTOMATION
