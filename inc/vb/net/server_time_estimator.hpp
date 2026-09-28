#pragma once

#include <cmath>

// Wall-clock estimate of "what tick the server is at right now," smoothed
// across snapshot arrivals rather than snapped -- REMAINING_TASKS.md Phase
// 3's "wall-clock server_time_est + smoothing on the client" gap. Same
// posture as vb::render::EyeHeightSmoother (header-only pure math,
// unit-tested without a live session/GL context): advance() keeps the
// estimate progressing every client tick even between snapshots, fixing
// the old interpolation staircase (ClientSession::interpolated_pos() used
// to only move when last_server_tick_ itself jumped on a new packet,
// freezing solid in between); each arriving snapshot then nudges the
// estimate toward what it implies via exponential smoothing rather than
// overwriting it outright, so jitter in real packet arrival timing (GNS
// over real UDP -- LoopbackTransport has no such jitter or latency at all)
// doesn't visibly kick the estimate around frame to frame.

namespace vb::net {

class ServerTimeEstimator {
public:
	// Real elapsed time since the last call (or construction), seconds --
	// call once per ClientSession::tick(), unconditionally, so the estimate
	// keeps moving smoothly between packets instead of only jumping when
	// one lands.
	void advance(double dt_seconds) { estimate_seconds_ += dt_seconds; }

	// A fresh snapshot: `server_seconds` is that snapshot's own tick
	// converted to seconds (tick / tick_rate_hz); `one_way_latency_seconds`
	// is the best available estimate of how long it took to arrive (half
	// the transport's round-trip time, or 0 if unknown/unavailable). The
	// implied "true" server time as of *now* is `server_seconds +
	// one_way_latency_seconds`. The very first sample snaps outright --
	// nothing to smooth against yet, and a fresh join/reconnect should
	// never ease in from whatever this estimator happened to be
	// initialized to, same "snap on the first/a big jump" posture
	// vb::render::EyeHeightSmoother's own kSnapThreshold has. A gap far
	// larger than any real jitter (a long stall, a reused estimator after
	// a reconnect) also snaps rather than a slow multi-second ease that
	// would visibly desync interpolation in the meantime.
	void on_snapshot(double server_seconds, double one_way_latency_seconds) {
		const double implied = server_seconds + one_way_latency_seconds;
		if (!primed_ || std::fabs(implied - estimate_seconds_) > kSnapThresholdSeconds) {
			estimate_seconds_ = implied;
			primed_ = true;
			return;
		}
		estimate_seconds_ += (implied - estimate_seconds_) * kSmoothing;
	}

	double estimate_seconds() const { return estimate_seconds_; }
	bool primed() const { return primed_; }

private:
	static constexpr double kSnapThresholdSeconds = 1.0;
	// Fraction of the gap between the running estimate and each new
	// sample's implied value closed per sample -- low enough that one
	// jittery/late packet can't yank the estimate around, high enough that
	// a real, sustained latency change (a route change mid-session) still
	// converges within a couple of seconds at a typical server tick rate.
	static constexpr double kSmoothing = 0.15;

	double estimate_seconds_ = 0.0;
	bool primed_ = false;
};

} // namespace vb::net
