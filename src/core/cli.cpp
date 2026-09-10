#include "vb/core/cli.hpp"

#include <charconv>
#include <cstddef>

namespace vb::core {

namespace {

bool is_option(std::string_view s) {
	return s.size() >= 2 && s[0] == '-';
}

// "--key", "-k", "--key=value" -> "key" / "k" (key part only).
std::string_view option_key(std::string_view s) {
	while (!s.empty() && s.front() == '-') {
		s.remove_prefix(1);
	}
	const std::size_t eq = s.find('=');
	return eq == std::string_view::npos ? s : s.substr(0, eq);
}

} // namespace

Args::Args(int argc, char **argv) {
	if (argc > 0 && argv[0] != nullptr) {
		program_ = argv[0];
	}
	for (int i = 1; i < argc; ++i) {
		tokens_.emplace_back(argv[i] != nullptr ? argv[i] : "");
	}
}

bool Args::has(std::string_view name, char short_name) const {
	for (const std::string &tok : tokens_) {
		if (!is_option(tok)) {
			continue;
		}
		const std::string_view key = option_key(tok);
		if (key == name) {
			return true;
		}
		if (short_name != '\0' && key.size() == 1 && key.front() == short_name) {
			return true;
		}
	}
	return false;
}

std::optional<std::string> Args::value(std::string_view name) const {
	for (std::size_t i = 0; i < tokens_.size(); ++i) {
		const std::string &tok = tokens_[i];
		if (!is_option(tok) || option_key(tok) != name) {
			continue;
		}
		const std::size_t eq = tok.find('=');
		if (eq != std::string::npos) {
			return tok.substr(eq + 1);
		}
		if (i + 1 < tokens_.size() && !is_option(tokens_[i + 1])) {
			return tokens_[i + 1];
		}
		return std::string{};
	}
	return std::nullopt;
}

std::string Args::value_or(std::string_view name, std::string fallback) const {
	if (auto v = value(name); v && !v->empty()) {
		return *v;
	}
	return fallback;
}

int Args::int_or(std::string_view name, int fallback) const {
	const auto v = value(name);
	if (!v || v->empty()) {
		return fallback;
	}
	int out = fallback;
	const char *begin = v->data();
	const char *end = begin + v->size();
	const auto [ptr, ec] = std::from_chars(begin, end, out);
	if (ec != std::errc{} || ptr != end) {
		return fallback;
	}
	return out;
}

std::vector<std::string> Args::positional() const {
	std::vector<std::string> out;
	for (std::size_t i = 0; i < tokens_.size(); ++i) {
		const std::string &tok = tokens_[i];
		if (is_option(tok)) {
			continue;
		}
		// A bare value belonging to the preceding `--key` (no `=`) is consumed.
		if (i > 0 && is_option(tokens_[i - 1]) &&
				tokens_[i - 1].find('=') == std::string::npos) {
			continue;
		}
		out.push_back(tok);
	}
	return out;
}

} // namespace vb::core
