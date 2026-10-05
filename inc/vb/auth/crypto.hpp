#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Signature primitives for the token verifier (auth.md §5.4), backed by
// Mbed TLS. Only the two JWS algorithms the allowlist admits exist here:
// RS256 (RSASSA-PKCS1-v1_5 + SHA-256, key >= 2048 bits) and ES256 (ECDSA
// P-256 + SHA-256, raw r||s signature).

namespace vb::auth {

enum class JwsAlg : std::uint8_t {
	kRS256,
	kES256,
};

struct PublicKey {
	JwsAlg alg = JwsAlg::kRS256;
	std::vector<std::uint8_t> rsa_n, rsa_e; // big-endian, no leading zeros required
	std::vector<std::uint8_t> ec_x, ec_y; // big-endian, <= 32 bytes each
};

inline constexpr std::size_t kMinRsaBits = 2048;
inline constexpr std::size_t kMaxRsaBits = 8192;

// True iff `signature` is a valid signature of `signing_input` under `key`.
// Any malformed key/signature is simply "false"; never throws.
bool verify_signature(const PublicKey &key, std::string_view signing_input,
		std::span<const std::uint8_t> signature);

std::array<std::uint8_t, 32> sha256(std::string_view data);

// `n` bytes from the OS entropy source (empty vector if it fails).
std::vector<std::uint8_t> random_bytes(std::size_t n);

namespace testing {

// Test-only: generates a key and signs with it so unit tests can mint tokens
// without checked-in key material. Not used by any production path.
class TestSigner {
public:
	static TestSigner rsa(std::size_t bits = 2048);
	static TestSigner p256();
	~TestSigner();
	TestSigner(TestSigner &&) noexcept;
	TestSigner &operator=(TestSigner &&) noexcept;
	TestSigner(const TestSigner &) = delete;
	TestSigner &operator=(const TestSigner &) = delete;

	JwsAlg alg() const;
	const PublicKey &public_key() const;
	// RS256: PKCS#1 v1.5 signature; ES256: raw 64-byte r||s.
	std::vector<std::uint8_t> sign(std::string_view signing_input) const;

private:
	TestSigner();
	struct Impl;
	Impl *impl_ = nullptr;
};

} // namespace testing
} // namespace vb::auth
