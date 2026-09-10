#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// FNV-1a — a small, fast, allocation-free hash for content digests (determinism
// gates, cache keys, cheap checksums). NOT for asset content addressing — that
// uses xxHash3-128 (spec §9.1); this is for reproducibility checks.

namespace vb::core {

class Fnv1a {
public:
	static constexpr std::uint64_t kPrime = 0x100000001b3ULL;
	static constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;

	constexpr Fnv1a &update(std::uint8_t byte) {
		state_ ^= byte;
		state_ *= kPrime;
		return *this;
	}
	Fnv1a &update(std::span<const std::byte> bytes) {
		for (std::byte b : bytes) {
			update(static_cast<std::uint8_t>(b));
		}
		return *this;
	}
	Fnv1a &update(std::string_view s) {
		for (char c : s) {
			update(static_cast<std::uint8_t>(c));
		}
		return *this;
	}
	template <typename T>
	Fnv1a &update_u(T value) {
		static_assert(std::is_unsigned_v<T>);
		for (std::size_t i = 0; i < sizeof(T); ++i) {
			update(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
		}
		return *this;
	}

	constexpr std::uint64_t digest() const { return state_; }

private:
	std::uint64_t state_ = kOffsetBasis;
};

} // namespace vb::core
