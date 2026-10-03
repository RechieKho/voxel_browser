#include "vb/automation/host.hpp"

#include <cstdio>
#include <iostream>
#include <streambuf>

#if defined(_WIN32)
#include <io.h>
#define VB_DUP _dup
#define VB_DUP2 _dup2
#else
#include <unistd.h>
#define VB_DUP ::dup
#define VB_DUP2 ::dup2
#endif

#include "vb/core/build_info.hpp"
#include "vb/core/version.hpp"

namespace vb::automation {

using nlohmann::json;

namespace {

std::string dump(const json &j) {
	// Chat/player names can carry invalid UTF-8; never let that throw.
	return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

// Reads fd 0 with a raw read(2). std::cin would hold the stdio lock of stdin
// while the reader thread blocks, and exit() then deadlocks flushing stdio
// when the process quits on `quit` with stdin still open.
class FdStreambuf final : public std::streambuf {
public:
	explicit FdStreambuf(int fd) :
			fd_(fd) {}

protected:
	int_type underflow() override {
		if (gptr() < egptr()) {
			return traits_type::to_int_type(*gptr());
		}
#if defined(_WIN32)
		const int n = _read(fd_, buf_, static_cast<unsigned>(sizeof buf_));
#else
		const auto n = ::read(fd_, buf_, sizeof buf_);
#endif
		if (n <= 0) {
			return traits_type::eof();
		}
		setg(buf_, buf_, buf_ + n);
		return traits_type::to_int_type(*gptr());
	}

private:
	int fd_;
	char buf_[4096];
};

// Writes the protocol to a private duplicate of the original stdout, so that
// stray writes to fd 1 (Lua's print(), printf, std::cout) can be redirected
// to stderr without ever interleaving with protocol frames.
class FdOutStreambuf final : public std::streambuf {
public:
	explicit FdOutStreambuf(int fd) :
			fd_(fd) {}

protected:
	std::streamsize xsputn(const char *s, std::streamsize n) override {
		std::streamsize done = 0;
		while (done < n) {
#if defined(_WIN32)
			const int w = _write(fd_, s + done, static_cast<unsigned>(n - done));
#else
			const auto w = ::write(fd_, s + done, static_cast<std::size_t>(n - done));
#endif
			if (w <= 0) {
				break;
			}
			done += w;
		}
		return done;
	}
	int_type overflow(int_type ch) override {
		if (traits_type::eq_int_type(ch, traits_type::eof())) {
			return traits_type::not_eof(ch);
		}
		const char c = traits_type::to_char_type(ch);
		return xsputn(&c, 1) == 1 ? ch : traits_type::eof();
	}

private:
	int fd_;
};

struct StdoutHolder {
	explicit StdoutHolder(int fd) :
			buf(fd), out(&buf) {}
	FdOutStreambuf buf;
	std::ostream out;
};

// Owns the stdin streambuf/istream pair for open_stdio().
struct StdinHolder {
	FdStreambuf buf{ 0 };
	std::istream in{ &buf };
};

} // namespace

Host::Host(std::istream &in, std::ostream &out) :
		shared_(std::make_shared<Shared>()), out_(&out) {
	std::istream *input = &in;
	// The thread owns only `shared` and `input` (never `this`), so it may be
	// detached safely if stdin is still blocked when the host is destroyed.
	reader_ = std::thread([shared = shared_, input] {
		std::string line;
		while (std::getline(*input, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty()) {
				continue;
			}
			Request req;
			std::string error;
			if (auto parsed = parse_request(line, error)) {
				req = std::move(*parsed);
			} else {
				req.args = json{ { "error", error } }; // cmd stays "" -> bad_request
			}
			std::lock_guard<std::mutex> lock(shared->mutex);
			shared->queue.push_back(std::move(req));
		}
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->eof = true;
		shared->done = true;
	});
}

Host::~Host() {
	if (reader_.joinable()) {
		if (shared_->done) {
			reader_.join();
		} else {
			reader_.detach();
		}
	}
}

std::unique_ptr<Host> Host::open_stdio() {
	// Protocol frames own a private copy of the real stdout; fd 1 itself (and
	// so std::cout, printf and Lua's print) is pointed at stderr.
	std::cout.flush();
	std::fflush(stdout);
	const int protocol_fd = VB_DUP(1);
	if (protocol_fd < 0 || VB_DUP2(2, 1) < 0) {
		return nullptr;
	}
	// Intentionally leaked: the reader thread may still be blocked in read(2)
	// when the process exits, so the buffer it uses must outlive the host.
	static StdinHolder *const stdin_holder = new StdinHolder();
	auto holder = std::make_unique<StdoutHolder>(protocol_fd);
	std::ostream *out = &holder->out;
	auto host = std::make_unique<Host>(stdin_holder->in, *out);
	host->keepalive_ = std::shared_ptr<void>(holder.release(),
			[](void *p) { delete static_cast<StdoutHolder *>(p); });
	return host;
}

void Host::send(const json &frame) {
	std::lock_guard<std::mutex> lock(out_mutex_);
	*out_ << dump(frame) << '\n';
	out_->flush();
}

void Host::emit_event(const std::string &name, json fields) {
	send(make_event(name, std::move(fields)));
}

bool Host::frame_allowed() const {
	return !manual_clock_ || !steps_.empty();
}

void Host::frame_done() {
	if (steps_.empty()) {
		return;
	}
	Step &s = steps_.front();
	if (--s.remaining <= 0) {
		send(make_response(s.id, Reply::success(json{ { "frames", s.total } })));
		steps_.pop_front();
	}
}

bool Host::pump(Endpoint &endpoint, Clock::time_point now) {
	std::deque<Request> batch;
	bool eof = false;
	{
		std::lock_guard<std::mutex> lock(shared_->mutex);
		batch.swap(shared_->queue);
		eof = shared_->eof;
	}
	for (const Request &req : batch) {
		dispatch(endpoint, req, now);
	}
	evaluate_waits(endpoint, now);
	if (eof && batch.empty()) {
		quit_ = true; // harness died / closed the pipe -> exit, never orphan
	}
	return !quit_;
}

void Host::dispatch(Endpoint &endpoint, const Request &req, Clock::time_point now) {
	if (req.cmd.empty()) {
		send(make_response(req.id,
				Reply::error("bad_request", req.args.value("error", std::string("bad request")))));
		return;
	}
	if (req.cmd == "hello") {
		const json &p = req.args.contains("proto") ? req.args["proto"] : json();
		const int proto = p.is_number_integer() ? p.get<int>() : kAutomationProtocolVersion;
		if (proto != kAutomationProtocolVersion) {
			send(make_response(req.id,
					Reply::error("proto_mismatch",
							"automation protocol " + std::to_string(kAutomationProtocolVersion) +
									" expected, got " + std::to_string(proto))));
			return;
		}
		json info = endpoint.hello_info();
		info["role"] = endpoint.role();
		info["proto"] = kAutomationProtocolVersion;
		info["engine"] = vb::kVersionString;
		info["build"] = vb::core::describe_build();
		info["clock"] = manual_clock_ ? "manual" : "real";
		send(make_response(req.id, Reply::success(std::move(info))));
		return;
	}
	if (req.cmd == "state") {
		send(make_response(req.id, Reply::success(endpoint.state())));
		return;
	}
	if (req.cmd == "quit") {
		quit_ = true;
		send(make_response(req.id, Reply::success()));
		return;
	}
	if (req.cmd == "step") {
		if (!manual_clock_) {
			send(make_response(req.id,
					Reply::error("unsupported", "step requires --automation-clock manual")));
			return;
		}
		const long long n = req.args.value("frames", 1LL);
		if (n < 1 || n > 1000000) {
			send(make_response(req.id, Reply::error("bad_request", "frames must be 1..1000000")));
			return;
		}
		steps_.push_back(Step{ req.id, static_cast<int>(n), static_cast<int>(n) });
		return; // answered by frame_done() once the frames ran
	}
	if (req.cmd == "wait_for") {
		bool deferred = false;
		Reply r = handle_wait_for(req, endpoint, now, deferred);
		if (!deferred) {
			send(make_response(req.id, r));
		}
		return;
	}
	if (auto reply = endpoint.command(req)) {
		send(make_response(req.id, *reply));
		return;
	}
	send(make_response(req.id, Reply::error("unknown_command", "unknown command '" + req.cmd + "'")));
}

Reply Host::handle_wait_for(const Request &req, Endpoint &endpoint, Clock::time_point now,
		bool &deferred) {
	if (!req.args.contains("pred")) {
		return Reply::error("bad_request", "wait_for needs 'pred'");
	}
	const long long timeout_ms = req.args.value("timeout_ms", 5000LL);
	if (timeout_ms < 0) {
		return Reply::error("bad_request", "timeout_ms must be >= 0");
	}
	const json state = endpoint.state();
	const PredicateContext ctx{ state,
		[&endpoint](int x, int y, int z) { return endpoint.block_name_at(x, y, z); } };
	EvalResult r = evaluate_predicate(req.args["pred"], ctx);
	if (!r.error.empty()) {
		return Reply::error("bad_request", r.error);
	}
	if (r.matched) {
		return Reply::success(json{ { "observed", r.observed } });
	}
	waits_.push_back(Wait{ req.id, req.args["pred"], now + std::chrono::milliseconds(timeout_ms) });
	deferred = true;
	return Reply::success();
}

void Host::evaluate_waits(Endpoint &endpoint, Clock::time_point now) {
	if (waits_.empty()) {
		return;
	}
	const json state = endpoint.state();
	const PredicateContext ctx{ state,
		[&endpoint](int x, int y, int z) { return endpoint.block_name_at(x, y, z); } };
	for (auto it = waits_.begin(); it != waits_.end();) {
		EvalResult r = evaluate_predicate(it->pred, ctx);
		if (r.matched) {
			send(make_response(it->id, Reply::success(json{ { "observed", r.observed } })));
			it = waits_.erase(it);
		} else if (now >= it->deadline) {
			send(make_response(it->id,
					Reply::error("timeout", "wait_for timed out", json{ { "last", r.observed } })));
			it = waits_.erase(it);
		} else {
			++it;
		}
	}
}

} // namespace vb::automation
