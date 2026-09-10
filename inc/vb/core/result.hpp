#pragma once

#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// Minimal `expected`-style result type for fallible operations on hot paths
// (decode, transport, worldgen). No exceptions, no allocation. Superseded by
// std::expected once the project moves to C++23.
//
//   Result<Chunk, ProtocolError> decode(ByteReader &r);
//   auto res = decode(r);
//   if (!res) return res.error();
//   use(*res);

namespace vb::core {

template <typename E>
class Err {
public:
	explicit Err(E value) : value_(std::move(value)) {}
	const E &value() const & { return value_; }
	E &&value() && { return std::move(value_); }

private:
	E value_;
};

template <typename E>
Err(E) -> Err<E>;

template <typename T, typename E>
class Result {
	static_assert(!std::is_reference_v<T>, "Result<T&> is not supported");

public:
	using value_type = T;
	using error_type = E;

	Result(T value) : has_value_(true) { // NOLINT(google-explicit-constructor)
		::new (&storage_.value) T(std::move(value));
	}
	Result(Err<E> err) : has_value_(false) { // NOLINT(google-explicit-constructor)
		::new (&storage_.error) E(std::move(err).value());
	}

	Result(const Result &other) : has_value_(other.has_value_) {
		if (has_value_) {
			::new (&storage_.value) T(other.storage_.value);
		} else {
			::new (&storage_.error) E(other.storage_.error);
		}
	}
	Result(Result &&other) noexcept : has_value_(other.has_value_) {
		if (has_value_) {
			::new (&storage_.value) T(std::move(other.storage_.value));
		} else {
			::new (&storage_.error) E(std::move(other.storage_.error));
		}
	}

	Result &operator=(const Result &other) {
		if (this != &other) {
			destroy();
			has_value_ = other.has_value_;
			if (has_value_) {
				::new (&storage_.value) T(other.storage_.value);
			} else {
				::new (&storage_.error) E(other.storage_.error);
			}
		}
		return *this;
	}
	Result &operator=(Result &&other) noexcept {
		if (this != &other) {
			destroy();
			has_value_ = other.has_value_;
			if (has_value_) {
				::new (&storage_.value) T(std::move(other.storage_.value));
			} else {
				::new (&storage_.error) E(std::move(other.storage_.error));
			}
		}
		return *this;
	}

	~Result() { destroy(); }

	bool has_value() const { return has_value_; }
	explicit operator bool() const { return has_value_; }

	T &value() & { return storage_.value; }
	const T &value() const & { return storage_.value; }
	T &&value() && { return std::move(storage_.value); }

	T &operator*() & { return storage_.value; }
	const T &operator*() const & { return storage_.value; }
	T &&operator*() && { return std::move(storage_.value); }
	T *operator->() { return &storage_.value; }
	const T *operator->() const { return &storage_.value; }

	const E &error() const & { return storage_.error; }
	E &&error() && { return std::move(storage_.error); }

	template <typename U>
	T value_or(U &&fallback) const & {
		return has_value_ ? storage_.value : static_cast<T>(std::forward<U>(fallback));
	}

private:
	void destroy() {
		if (has_value_) {
			storage_.value.~T();
		} else {
			storage_.error.~E();
		}
	}

	union Storage {
		Storage() {}
		~Storage() {}
		T value;
		E error;
	} storage_;
	bool has_value_;
};

// Result<void, E> — success carries nothing.
template <typename E>
class Result<void, E> {
public:
	using value_type = void;
	using error_type = E;

	Result() : has_value_(true) {}
	Result(Err<E> err) // NOLINT(google-explicit-constructor)
			: error_(std::move(err).value()),
			  has_value_(false) {}

	bool has_value() const { return has_value_; }
	explicit operator bool() const { return has_value_; }

	const E &error() const & { return error_; }
	E &&error() && { return std::move(error_); }

private:
	E error_{};
	bool has_value_;
};

template <typename E>
using Status = Result<void, E>;

} // namespace vb::core
