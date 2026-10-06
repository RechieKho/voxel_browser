#pragma once

#include <charconv>
#include <string_view>

// Strict "N<sep>N<sep>N" integer parsing for CLI/editor text fields. Replaces
// sscanf, which MSVC flags (C4996, an error under /WX) and which silently
// accepts trailing junk.

namespace vb::core {

// Parses exactly `count` ints separated by `sep` into `out` and requires the
// whole string to be consumed (no spaces, no '+', no trailing text).
inline bool parse_int_list(std::string_view text, char sep, int *out, int count) {
	const char *p = text.data();
	const char *const end = p + text.size();
	for (int i = 0; i < count; ++i) {
		if (i > 0) {
			if (p == end || *p != sep) {
				return false;
			}
			++p;
		}
		const auto [next, ec] = std::from_chars(p, end, out[i]);
		if (ec != std::errc{}) {
			return false;
		}
		p = next;
	}
	return p == end;
}

} // namespace vb::core
