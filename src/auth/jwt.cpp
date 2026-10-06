#include "vb/auth/jwt.hpp"

#include <array>

namespace vb::auth {

namespace {

constexpr char kAlphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr std::array<std::int8_t, 256> make_decode_table() {
	std::array<std::int8_t, 256> t{};
	for (auto &v : t) {
		v = -1;
	}
	for (int i = 0; i < 64; ++i) {
		t[static_cast<unsigned char>(kAlphabet[i])] = static_cast<std::int8_t>(i);
	}
	return t;
}
constexpr auto kDecode = make_decode_table();

} // namespace

std::string base64url_encode(std::span<const std::uint8_t> data) {
	std::string out;
	out.reserve((data.size() * 4 + 2) / 3);
	std::size_t i = 0;
	for (; i + 3 <= data.size(); i += 3) {
		const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
				(static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
		out.push_back(kAlphabet[(v >> 18) & 63]);
		out.push_back(kAlphabet[(v >> 12) & 63]);
		out.push_back(kAlphabet[(v >> 6) & 63]);
		out.push_back(kAlphabet[v & 63]);
	}
	const std::size_t rest = data.size() - i;
	if (rest == 1) {
		const std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
		out.push_back(kAlphabet[(v >> 18) & 63]);
		out.push_back(kAlphabet[(v >> 12) & 63]);
	} else if (rest == 2) {
		const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
				(static_cast<std::uint32_t>(data[i + 1]) << 8);
		out.push_back(kAlphabet[(v >> 18) & 63]);
		out.push_back(kAlphabet[(v >> 12) & 63]);
		out.push_back(kAlphabet[(v >> 6) & 63]);
	}
	return out;
}

std::optional<std::vector<std::uint8_t>> base64url_decode(std::string_view in) {
	if (in.size() % 4 == 1) {
		return std::nullopt;
	}
	std::vector<std::uint8_t> out;
	out.reserve(in.size() * 3 / 4);
	std::uint32_t acc = 0;
	int bits = 0;
	for (const char ch : in) {
		const std::int8_t v = kDecode[static_cast<unsigned char>(ch)];
		if (v < 0) {
			return std::nullopt;
		}
		acc = (acc << 6) | static_cast<std::uint32_t>(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
		}
	}
	// Canonical encodings leave only zero padding bits.
	if (bits > 0 && (acc & ((1u << bits) - 1u)) != 0) {
		return std::nullopt;
	}
	return out;
}

JwsParse parse_jws(std::string_view token) {
	JwsParse out;
	if (token.empty() || token.size() > kMaxJwtBytes) {
		out.error = "token empty or too large";
		return out;
	}
	const std::size_t d1 = token.find('.');
	if (d1 == std::string_view::npos) {
		out.error = "token is not a compact JWS";
		return out;
	}
	const std::size_t d2 = token.find('.', d1 + 1);
	if (d2 == std::string_view::npos ||
			token.find('.', d2 + 1) != std::string_view::npos) {
		out.error = "token is not a compact JWS (need exactly 3 parts)";
		return out;
	}
	const std::string_view h = token.substr(0, d1);
	const std::string_view p = token.substr(d1 + 1, d2 - d1 - 1);
	const std::string_view s = token.substr(d2 + 1);
	if (h.empty() || p.empty() || s.empty()) {
		out.error = "token has an empty part";
		return out;
	}
	auto hb = base64url_decode(h);
	auto pb = base64url_decode(p);
	auto sb = base64url_decode(s);
	if (!hb || !pb || !sb) {
		out.error = "token part is not valid base64url";
		return out;
	}
	ParsedJws jws;
	jws.header_json.assign(hb->begin(), hb->end());
	jws.payload_json.assign(pb->begin(), pb->end());
	jws.signing_input = std::string(token.substr(0, d2));
	jws.signature = std::move(*sb);
	out.jws = std::move(jws);
	return out;
}

std::string redact_token(std::string_view token) {
	return "<jwt len=" + std::to_string(token.size()) + ">";
}

} // namespace vb::auth
