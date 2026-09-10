#pragma once

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>

// Levelled, thread-safe, sink-based logging.
//
//   VB_INFO("net", "connected to ", addr, ':', port);
//   vb::core::log::set_level(vb::core::LogLevel::kWarn);
//   vb::core::log::add_sink([](const vb::core::LogRecord &r){ ... });
//
// The default sink writes to stderr. Sinks are invoked under a lock, so they may
// be called from any thread but must not re-enter the logger.

namespace vb::core {

enum class LogLevel : std::uint8_t {
	kTrace = 0,
	kDebug,
	kInfo,
	kWarn,
	kError,
	kOff,
};

std::string_view to_string(LogLevel level);

struct LogRecord {
	LogLevel level;
	std::string_view category;
	std::string message;
	std::int64_t unix_millis;
};

namespace log {

using Sink = std::function<void(const LogRecord &)>;

void set_level(LogLevel level);
LogLevel level();
bool enabled(LogLevel level);

// Replaces all sinks with a single stderr writer (the startup default).
void reset_to_default_sink();
// Adds a sink; returns an id usable with remove_sink().
std::uint64_t add_sink(Sink sink);
void remove_sink(std::uint64_t id);
void clear_sinks();

void write(LogLevel level, std::string_view category, std::string message);

} // namespace log

namespace detail {

template <typename... Args>
void log_fmt(LogLevel level, std::string_view category, Args &&...args) {
	if (!log::enabled(level)) {
		return;
	}
	std::ostringstream oss;
	(oss << ... << std::forward<Args>(args));
	log::write(level, category, oss.str());
}

} // namespace detail

} // namespace vb::core

#define VB_TRACE(cat, ...) ::vb::core::detail::log_fmt(::vb::core::LogLevel::kTrace, cat, __VA_ARGS__)
#define VB_DEBUG(cat, ...) ::vb::core::detail::log_fmt(::vb::core::LogLevel::kDebug, cat, __VA_ARGS__)
#define VB_INFO(cat, ...) ::vb::core::detail::log_fmt(::vb::core::LogLevel::kInfo, cat, __VA_ARGS__)
#define VB_WARN(cat, ...) ::vb::core::detail::log_fmt(::vb::core::LogLevel::kWarn, cat, __VA_ARGS__)
#define VB_ERROR(cat, ...) ::vb::core::detail::log_fmt(::vb::core::LogLevel::kError, cat, __VA_ARGS__)
