// AutomationHost: the JSON-lines driver channel (docs/e2e-automation.md §5).
// A reader thread parses stdin lines into a mutex-guarded queue; every command
// then runs on the main thread inside pump(), between frames/ticks, so
// automation never races the mesh/worldgen worker pools.
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "vb/automation/predicate.hpp"
#include "vb/automation/protocol.hpp"

namespace vb::automation {

// Role-specific half of the host, implemented by the client and server mains.
class Endpoint {
public:
	virtual ~Endpoint() = default;
	virtual std::string role() const = 0; // "client" | "server"
	virtual nlohmann::json hello_info() const { return nlohmann::json::object(); }
	virtual nlohmann::json state() = 0; // snapshot (see predicate.hpp)
	virtual std::optional<std::string> block_name_at(int, int, int) { return std::nullopt; }
	// Role-specific commands. nullopt = not mine -> "unknown_command".
	virtual std::optional<Reply> command(const Request &) { return std::nullopt; }
};

class Host {
public:
	using Clock = std::chrono::steady_clock;

	// Reads `in` on a background thread (must outlive the host unless it hits
	// EOF) and writes frames to `out`.
	Host(std::istream &in, std::ostream &out);
	~Host();
	Host(const Host &) = delete;
	Host &operator=(const Host &) = delete;

	// Protocol on the process's real stdin/stdout. fd 1 is redirected to stderr
	// (std::cout, printf and Lua's print included) so game output can't corrupt
	// frames. nullptr if the descriptors couldn't be duplicated.
	static std::unique_ptr<Host> open_stdio();

	// `--automation-clock manual`: frames only advance via `step{frames}`.
	void set_manual_clock(bool manual) { manual_clock_ = manual; }
	bool manual_clock() const { return manual_clock_; }

	// Main thread, once per loop iteration. Dispatches queued commands and
	// evaluates pending waits. Returns false once the host wants the process
	// to exit (`quit` received or stdin closed).
	bool pump(Endpoint &endpoint, Clock::time_point now = Clock::now());

	// Manual clock: may the loop run a frame now? (Always true in real mode.)
	bool frame_allowed() const;
	// Call after each frame actually run; completes `step` requests.
	void frame_done();

	void emit_event(const std::string &name, nlohmann::json fields);
	bool quit_requested() const { return quit_; }

private:
	struct Wait {
		nlohmann::json id;
		nlohmann::json pred;
		Clock::time_point deadline;
	};
	struct Step {
		nlohmann::json id;
		int remaining = 0;
		int total = 0;
	};
	struct Shared {
		std::mutex mutex;
		std::deque<Request> queue; // cmd=="" -> unparseable line, args.error says why
		bool eof = false;
		std::atomic<bool> done{ false }; // reader thread has exited
	};

	void send(const nlohmann::json &frame);
	void dispatch(Endpoint &endpoint, const Request &req, Clock::time_point now);
	Reply handle_wait_for(const Request &req, Endpoint &endpoint, Clock::time_point now,
			bool &deferred);
	void evaluate_waits(Endpoint &endpoint, Clock::time_point now);

	std::shared_ptr<Shared> shared_;
	std::thread reader_;
	std::ostream *out_;
	std::shared_ptr<void> keepalive_; // owns the streams open_stdio() built
	std::mutex out_mutex_;
	bool manual_clock_ = false;
	bool quit_ = false;
	std::vector<Wait> waits_;
	std::deque<Step> steps_;
};

} // namespace vb::automation
