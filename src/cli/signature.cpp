#include "vb/cli/signature.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

#include <toml++/toml.hpp>

extern "C" {
#include "ed25519.h"
}

#ifndef VB_RELEASE_PUBLIC_KEYS
#define VB_RELEASE_PUBLIC_KEYS ""
#endif

namespace vb::cli {

namespace {

int hex_value(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int base64_value(char c) {
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

} // namespace

Status parse_public_key(std::string_view hex, PublicKey &out) {
	if (hex.size() != 64) {
		return { "a public key is 64 hex characters" };
	}
	for (std::size_t i = 0; i < 32; ++i) {
		const int hi = hex_value(hex[2 * i]);
		const int lo = hex_value(hex[2 * i + 1]);
		if (hi < 0 || lo < 0) {
			return { "a public key is 64 hex characters" };
		}
		out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
	}
	return {};
}

Status decode_signature(std::string_view text, SignatureBytes &out) {
	std::vector<std::uint8_t> bytes;
	unsigned buffer = 0;
	int bits = 0;
	int padding = 0;
	for (const char c : text) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			continue;
		}
		if (c == '=') {
			++padding;
			continue;
		}
		const int v = base64_value(c);
		if (v < 0 || padding > 0) {
			return { "the signature is not valid base64" };
		}
		buffer = (buffer << 6) | static_cast<unsigned>(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			bytes.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
		}
	}
	if (bytes.size() != out.size()) {
		return { "the signature must be 64 bytes (an Ed25519 signature)" };
	}
	std::copy(bytes.begin(), bytes.end(), out.begin());
	return {};
}

bool verify_ed25519(const SignatureBytes &sig, std::string_view message, const PublicKey &key) {
	return ed25519_verify(sig.data(), reinterpret_cast<const unsigned char *>(message.data()),
				   message.size(), key.data()) != 0;
}

Status verify_manifest_signature(std::string_view manifest_text, std::string_view signature_text,
		const TrustPolicy &policy) {
	if (policy.keys.empty()) {
		return {}; // no trusted key configured: signatures are not enforced
	}
	const bool has_signature = std::any_of(signature_text.begin(), signature_text.end(),
			[](char c) { return !std::isspace(static_cast<unsigned char>(c)); });
	if (!has_signature) {
		if (policy.require) {
			return { "this release is not signed (no release.toml.sig), and vb is configured "
					 "to require signatures; set require_signature = false in cli.toml to "
					 "allow unsigned releases" };
		}
		return {};
	}
	SignatureBytes sig{};
	if (const Status s = decode_signature(signature_text, sig); !s) {
		return { "release.toml.sig: " + s.error };
	}
	for (const PublicKey &key : policy.keys) {
		if (verify_ed25519(sig, manifest_text, key)) {
			return {};
		}
	}
	return { "release.toml.sig does not match any trusted key; refusing this release "
			 "(it may have been tampered with, or signed with a key vb does not trust)" };
}

std::vector<PublicKey> embedded_public_keys() {
	std::vector<PublicKey> keys;
	const std::string_view all = VB_RELEASE_PUBLIC_KEYS; // comma-separated hex
	std::size_t pos = 0;
	while (pos < all.size()) {
		std::size_t end = all.find(',', pos);
		if (end == std::string_view::npos) {
			end = all.size();
		}
		PublicKey key{};
		if (parse_public_key(all.substr(pos, end - pos), key)) {
			keys.push_back(key);
		}
		pos = end + 1;
	}
	return keys;
}

TrustPolicy load_trust_policy(const Layout &layout, std::string *warning) {
	TrustPolicy policy;
	policy.keys = embedded_public_keys();
	std::optional<bool> require_override;
	try {
		const toml::table tbl = toml::parse_file(layout.cli_toml().string());
		if (const toml::array *arr = tbl["trusted_keys"].as_array()) {
			for (const toml::node &n : *arr) {
				PublicKey key{};
				const auto hex = n.value<std::string>();
				if (hex && parse_public_key(*hex, key)) {
					if (std::find(policy.keys.begin(), policy.keys.end(), key) == policy.keys.end()) {
						policy.keys.push_back(key);
					}
				} else if (warning != nullptr) {
					*warning = "ignoring a malformed entry in trusted_keys (cli.toml)";
				}
			}
		}
		require_override = tbl["require_signature"].value<bool>();
	} catch (const toml::parse_error &) {
	}
	policy.require = require_override.value_or(!policy.keys.empty());
	return policy;
}

} // namespace vb::cli
