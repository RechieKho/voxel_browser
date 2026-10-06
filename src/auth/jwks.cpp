#include "vb/auth/jwks.hpp"

#include <nlohmann/json.hpp>

#include "vb/auth/jwt.hpp"

namespace vb::auth {

namespace {

using nlohmann::json;

std::optional<std::string> str(const json &o, const char *key) {
	auto it = o.find(key);
	if (it == o.end() || !it->is_string()) {
		return std::nullopt;
	}
	return it->get<std::string>();
}

std::size_t bit_length(const std::vector<std::uint8_t> &be) {
	std::size_t i = 0;
	while (i < be.size() && be[i] == 0) {
		++i;
	}
	if (i == be.size()) {
		return 0;
	}
	std::size_t bits = (be.size() - i) * 8;
	std::uint8_t top = be[i];
	while ((top & 0x80) == 0) {
		top = static_cast<std::uint8_t>(top << 1);
		--bits;
	}
	return bits;
}

} // namespace

JwksParse parse_jwks(std::string_view text) {
	JwksParse out;
	const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
	if (doc.is_discarded() || !doc.is_object()) {
		out.error = "JWKS is not a JSON object";
		return out;
	}
	auto keys = doc.find("keys");
	if (keys == doc.end() || !keys->is_array()) {
		out.error = "JWKS has no 'keys' array";
		return out;
	}
	for (const json &k : *keys) {
		if (!k.is_object()) {
			++out.skipped;
			continue;
		}
		const auto kid = str(k, "kid");
		const auto kty = str(k, "kty");
		if (!kid || kid->empty() || !kty) {
			++out.skipped;
			continue;
		}
		if (const auto use = str(k, "use"); use && *use != "sig") {
			++out.skipped;
			continue;
		}
		PublicKey key;
		if (*kty == "RSA") {
			const auto n = str(k, "n");
			const auto e = str(k, "e");
			if (!n || !e) {
				++out.skipped;
				continue;
			}
			auto nb = base64url_decode(*n);
			auto eb = base64url_decode(*e);
			if (!nb || !eb || eb->empty() || eb->size() > 8) {
				++out.skipped;
				continue;
			}
			const std::size_t bits = bit_length(*nb);
			if (bits < kMinRsaBits || bits > kMaxRsaBits) {
				++out.skipped;
				continue;
			}
			key.alg = JwsAlg::kRS256;
			key.rsa_n = std::move(*nb);
			key.rsa_e = std::move(*eb);
		} else if (*kty == "EC") {
			const auto crv = str(k, "crv");
			const auto x = str(k, "x");
			const auto y = str(k, "y");
			if (!crv || *crv != "P-256" || !x || !y) {
				++out.skipped;
				continue;
			}
			auto xb = base64url_decode(*x);
			auto yb = base64url_decode(*y);
			if (!xb || !yb || xb->empty() || yb->empty() || xb->size() > 32 ||
					yb->size() > 32) {
				++out.skipped;
				continue;
			}
			key.alg = JwsAlg::kES256;
			key.ec_x = std::move(*xb);
			key.ec_y = std::move(*yb);
		} else {
			++out.skipped;
			continue;
		}
		out.keys[*kid] = std::move(key);
	}
	if (out.keys.empty()) {
		out.error = "JWKS contains no usable signing keys";
	}
	return out;
}

} // namespace vb::auth
