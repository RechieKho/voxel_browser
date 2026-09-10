#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "vb/core/error.hpp"

// Hand-written little-endian wire codec primitives (spec §14). No RTTI, no
// exceptions. ByteReader accumulates the first error and short-circuits every
// subsequent read, so a decoder can do a run of reads and check once at the end:
//
//   ByteReader r(bytes);
//   const auto version = r.u16();
//   const auto name = r.string();
//   if (r.failed()) return vb::core::Err{r.error()};

namespace vb::protocol {

using core::ProtocolError;

// Guard rail for length-prefixed fields decoded from untrusted input.
inline constexpr std::size_t kMaxDecodedLength = 64u * 1024u * 1024u;

class ByteWriter {
public:
	explicit ByteWriter(std::vector<std::byte> &out) : out_(out) {}

	void u8(std::uint8_t v) { out_.push_back(static_cast<std::byte>(v)); }
	void u16(std::uint16_t v) { put_le(v); }
	void u32(std::uint32_t v) { put_le(v); }
	void u64(std::uint64_t v) { put_le(v); }
	void i8(std::int8_t v) { u8(static_cast<std::uint8_t>(v)); }
	void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
	void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
	void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
	void boolean(bool v) { u8(v ? 1u : 0u); }

	void f32(float v) {
		std::uint32_t bits{};
		std::memcpy(&bits, &v, sizeof(bits));
		u32(bits);
	}
	void f64(double v) {
		std::uint64_t bits{};
		std::memcpy(&bits, &v, sizeof(bits));
		u64(bits);
	}

	// Unsigned LEB128.
	void varint(std::uint64_t v) {
		while (v >= 0x80) {
			out_.push_back(static_cast<std::byte>((v & 0x7F) | 0x80));
			v >>= 7;
		}
		out_.push_back(static_cast<std::byte>(v));
	}
	// Zig-zag + LEB128 for signed.
	void svarint(std::int64_t v) {
		varint((static_cast<std::uint64_t>(v) << 1) ^
				static_cast<std::uint64_t>(v >> 63));
	}

	void bytes(std::span<const std::byte> b) {
		out_.insert(out_.end(), b.begin(), b.end());
	}
	void string(std::string_view s) {
		varint(s.size());
		out_.insert(out_.end(), reinterpret_cast<const std::byte *>(s.data()),
				reinterpret_cast<const std::byte *>(s.data()) + s.size());
	}

	std::size_t size() const { return out_.size(); }

private:
	template <typename T>
	void put_le(T v) {
		static_assert(std::is_unsigned_v<T>);
		for (std::size_t i = 0; i < sizeof(T); ++i) {
			out_.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
		}
	}

	std::vector<std::byte> &out_;
};

class ByteReader {
public:
	explicit ByteReader(std::span<const std::byte> data) : data_(data) {}

	bool failed() const { return err_ != ProtocolError::kNone; }
	ProtocolError error() const { return err_; }
	std::size_t remaining() const { return data_.size() - pos_; }
	bool at_end() const { return pos_ >= data_.size(); }

	void fail(ProtocolError e) {
		if (err_ == ProtocolError::kNone) {
			err_ = e;
		}
	}
	// Call after decoding a message that must consume its whole payload.
	void expect_consumed() {
		if (!failed() && !at_end()) {
			err_ = ProtocolError::kTrailingBytes;
		}
	}

	std::uint8_t u8() {
		if (!ensure(1)) {
			return 0;
		}
		return static_cast<std::uint8_t>(data_[pos_++]);
	}
	std::uint16_t u16() { return get_le<std::uint16_t>(); }
	std::uint32_t u32() { return get_le<std::uint32_t>(); }
	std::uint64_t u64() { return get_le<std::uint64_t>(); }
	std::int8_t i8() { return static_cast<std::int8_t>(u8()); }
	std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
	std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
	std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
	bool boolean() {
		const std::uint8_t v = u8();
		if (v > 1) {
			fail(ProtocolError::kMalformed);
		}
		return v != 0;
	}

	float f32() {
		const std::uint32_t bits = u32();
		float v{};
		std::memcpy(&v, &bits, sizeof(v));
		return v;
	}
	double f64() {
		const std::uint64_t bits = u64();
		double v{};
		std::memcpy(&v, &bits, sizeof(v));
		return v;
	}

	std::uint64_t varint() {
		std::uint64_t result = 0;
		for (int shift = 0; shift < 64; shift += 7) {
			if (!ensure(1)) {
				return 0;
			}
			const std::uint8_t byte = static_cast<std::uint8_t>(data_[pos_++]);
			result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
			if ((byte & 0x80) == 0) {
				return result;
			}
		}
		fail(ProtocolError::kOverlongVarint);
		return 0;
	}
	std::int64_t svarint() {
		const std::uint64_t v = varint();
		return static_cast<std::int64_t>((v >> 1) ^ (~(v & 1) + 1));
	}

	std::span<const std::byte> bytes(std::size_t n) {
		if (!ensure(n)) {
			return {};
		}
		const auto out = data_.subspan(pos_, n);
		pos_ += n;
		return out;
	}
	std::string string() {
		const std::uint64_t len = varint();
		if (failed()) {
			return {};
		}
		if (len > kMaxDecodedLength) {
			fail(ProtocolError::kLengthExceeded);
			return {};
		}
		const auto b = bytes(static_cast<std::size_t>(len));
		if (failed()) {
			return {};
		}
		return std::string(reinterpret_cast<const char *>(b.data()), b.size());
	}

private:
	bool ensure(std::size_t n) {
		if (failed()) {
			return false;
		}
		if (remaining() < n) {
			fail(ProtocolError::kShortBuffer);
			return false;
		}
		return true;
	}
	template <typename T>
	T get_le() {
		static_assert(std::is_unsigned_v<T>);
		if (!ensure(sizeof(T))) {
			return 0;
		}
		std::uint64_t acc = 0;
		for (std::size_t i = 0; i < sizeof(T); ++i) {
			acc |= static_cast<std::uint64_t>(
						   static_cast<std::uint8_t>(data_[pos_ + i]))
					<< (8 * i);
		}
		pos_ += sizeof(T);
		return static_cast<T>(acc);
	}

	std::span<const std::byte> data_;
	std::size_t pos_ = 0;
	ProtocolError err_ = ProtocolError::kNone;
};

} // namespace vb::protocol
