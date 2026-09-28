#include <doctest/doctest.h>

#include "vb/net/server_time_estimator.hpp"

using vb::net::ServerTimeEstimator;

TEST_CASE("server time estimator is unprimed before the first snapshot") {
	ServerTimeEstimator e;
	CHECK_FALSE(e.primed());
	CHECK(e.estimate_seconds() == doctest::Approx(0.0));
}

TEST_CASE("server time estimator snaps outright on the first snapshot") {
	ServerTimeEstimator e;
	e.on_snapshot(12.5, 0.02);
	CHECK(e.primed());
	CHECK(e.estimate_seconds() == doctest::Approx(12.52));
}

TEST_CASE("server time estimator advances smoothly between snapshots") {
	ServerTimeEstimator e;
	e.on_snapshot(10.0, 0.0);
	e.advance(0.05);
	e.advance(0.05);
	CHECK(e.estimate_seconds() == doctest::Approx(10.1));
}

TEST_CASE("server time estimator eases toward a later sample instead of "
		"snapping to it") {
	ServerTimeEstimator e;
	e.on_snapshot(10.0, 0.0);
	// Implied server time from this next sample is 10.05 (a perfectly
	// on-time packet for a 20 Hz/0.05s tick) -- a small, ordinary gap that
	// should ease in, not jump straight to 10.05.
	e.on_snapshot(10.05, 0.0);
	CHECK(e.estimate_seconds() > 10.0);
	CHECK(e.estimate_seconds() < 10.05);
}

TEST_CASE("server time estimator converges to a sustained implied value "
		"repeated across several samples") {
	ServerTimeEstimator e;
	e.on_snapshot(10.0, 0.0);
	// Every later sample implies the same 10.3 (e.g. a steady latency this
	// estimator hasn't fully caught up to yet after the first ease) --
	// under the snap threshold, so each one nudges rather than jumps.
	for (int i = 0; i < 40; ++i) {
		e.on_snapshot(10.3, 0.0);
	}
	CHECK(e.estimate_seconds() == doctest::Approx(10.3).epsilon(0.001));
}

TEST_CASE("server time estimator snaps instead of easing across a large gap "
		"(a long stall or a reused estimator after reconnect)") {
	ServerTimeEstimator e;
	e.on_snapshot(10.0, 0.0);
	e.on_snapshot(500.0, 0.0);
	CHECK(e.estimate_seconds() == doctest::Approx(500.0));
}
