#include "vb/core/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <utility>
#include <vector>

namespace vb::core {

std::string_view to_string(LogLevel level) {
	switch (level) {
		case LogLevel::kTrace:
			return "TRACE";
		case LogLevel::kDebug:
			return "DEBUG";
		case LogLevel::kInfo:
			return "INFO";
		case LogLevel::kWarn:
			return "WARN";
		case LogLevel::kError:
			return "ERROR";
		case LogLevel::kOff:
			return "OFF";
	}
	return "?";
}

namespace log {
namespace {

struct Registry {
	std::atomic<LogLevel> level{ LogLevel::kInfo };
	std::mutex mutex;
	std::vector<std::pair<std::uint64_t, Sink>> sinks;
	std::uint64_t next_id{ 1 };
};

Registry &registry() {
	static Registry r;
	return r;
}

void stderr_sink(const LogRecord &record) {
	const std::time_t secs = static_cast<std::time_t>(record.unix_millis / 1000);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &secs);
#else
	localtime_r(&secs, &tm);
#endif
	char stamp[16];
	std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min,
			tm.tm_sec, static_cast<int>(record.unix_millis % 1000));
	std::fprintf(stderr, "%s %-5s [%.*s] %s\n", stamp,
			std::string(to_string(record.level)).c_str(),
			static_cast<int>(record.category.size()), record.category.data(),
			record.message.c_str());
}

struct DefaultInstaller {
	DefaultInstaller() { reset_to_default_sink(); }
};
DefaultInstaller g_default_installer;

} // namespace

void set_level(LogLevel level) {
	registry().level.store(level, std::memory_order_relaxed);
}
LogLevel level() { return registry().level.load(std::memory_order_relaxed); }
bool enabled(LogLevel lvl) {
	return lvl != LogLevel::kOff && lvl >= level();
}

void reset_to_default_sink() {
	Registry &r = registry();
	std::lock_guard lock(r.mutex);
	r.sinks.clear();
	r.sinks.emplace_back(r.next_id++, &stderr_sink);
}

std::uint64_t add_sink(Sink sink) {
	Registry &r = registry();
	std::lock_guard lock(r.mutex);
	const std::uint64_t id = r.next_id++;
	r.sinks.emplace_back(id, std::move(sink));
	return id;
}

void remove_sink(std::uint64_t id) {
	Registry &r = registry();
	std::lock_guard lock(r.mutex);
	for (auto it = r.sinks.begin(); it != r.sinks.end(); ++it) {
		if (it->first == id) {
			r.sinks.erase(it);
			return;
		}
	}
}

void clear_sinks() {
	Registry &r = registry();
	std::lock_guard lock(r.mutex);
	r.sinks.clear();
}

void write(LogLevel lvl, std::string_view category, std::string message) {
	if (!enabled(lvl)) {
		return;
	}
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	LogRecord record{ lvl, category, std::move(message),
		std::chrono::duration_cast<std::chrono::milliseconds>(now).count() };

	Registry &r = registry();
	std::lock_guard lock(r.mutex);
	for (const auto &[id, sink] : r.sinks) {
		(void)id;
		sink(record);
	}
}

} // namespace log
} // namespace vb::core
