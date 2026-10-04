#include "vb/cli/version.hpp"

#include <charconv>

namespace vb::cli {

namespace {

bool parse_part(std::string_view s, int &out) {
	if (s.empty() || s.size() > 6) {
		return false;
	}
	const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
	return ec == std::errc{} && ptr == s.data() + s.size();
}

} // namespace

std::optional<Version> parse_version(std::string_view text) {
	if (!text.empty() && text.front() == 'v') {
		text.remove_prefix(1);
	}
	Version v;
	const std::size_t a = text.find('.');
	if (a == std::string_view::npos) {
		return std::nullopt;
	}
	const std::size_t b = text.find('.', a + 1);
	if (b == std::string_view::npos) {
		return std::nullopt;
	}
	if (!parse_part(text.substr(0, a), v.major) ||
			!parse_part(text.substr(a + 1, b - a - 1), v.minor) ||
			!parse_part(text.substr(b + 1), v.patch)) {
		return std::nullopt;
	}
	return v;
}

std::string to_tag(const Version &v) {
	return "v" + std::to_string(v.major) + "." + std::to_string(v.minor) + "." +
			std::to_string(v.patch);
}

bool is_valid_link_name(std::string_view name) {
	if (name.empty() || name.size() > 32 || name == "latest" || name == "default") {
		return false;
	}
	if (name.front() < 'a' || name.front() > 'z') {
		return false;
	}
	for (const char c : name) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!ok) {
			return false;
		}
	}
	return !parse_version(name).has_value();
}

} // namespace vb::cli
