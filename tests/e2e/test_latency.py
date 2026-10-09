"""Punch latency: click -> the client seeing the server's answer.

`break_block` reports `confirm_ms`, the wall time from each click to the client
seeing the new punch count (the last entry is the block breaking). A block with
max_damage > 0 gives one S2C_BlockDamage per punch to time.

The streaming case teleports a player into fresh terrain at a larger view
distance and punches while the rest of its view is still generating and
streaming. It is slow and noisy, so it runs only with VB_E2E_BENCH=1 and
reports numbers instead of asserting a bound.
"""
import os
import statistics
import time

import pytest

from vbtest import expect
from vbtest.scene import block_under_feet, clear_corridor

TOUGH = 'vb.register_block{ name = "test:tough", max_damage = 12 }\n'
bench = pytest.mark.skipif(os.environ.get("VB_E2E_BENCH") != "1", reason="benchmark: set VB_E2E_BENCH=1")


def summary(ms):
    ms = sorted(ms)
    return "n=%d median=%.0fms p90=%.0fms max=%.0fms" % (
        len(ms), statistics.median(ms), ms[max(0, int(len(ms) * 0.9) - 1)], ms[-1])


def damage_confirms(result):
    return result["confirm_ms"][:-1]  # the last confirm is the break itself


@pytest.mark.vb_pack_files({"zz_tough.lua": TOUGH})
def test_punch_is_confirmed_promptly_when_idle(server, clients):
    """Click to crack update is a round trip plus up to one server tick (50 ms at 20 Hz).
    The bound is loose (sanitizer builds in CI are slow); it catches a punch being held
    for ticks, e.g. behind queued chunk data."""
    (alice,) = clients(1, names=["Alice"])
    x, y, z = block_under_feet(alice)
    clear_corridor(server, x, y, z)
    target = (x, y, z - 2)
    server.set_block(target, "test:tough")
    expect(alice).to_see_block(target, "test:tough")
    damage = damage_confirms(alice.break_block(target, timeout=30))
    print("\nidle punch latency:", summary(damage))
    assert len(damage) == 11
    assert statistics.median(damage) < 250


@bench
@pytest.mark.vb_server(view_distance=8)
@pytest.mark.vb_pack_files({"zz_tough.lua": TOUGH})
def test_punch_latency_while_chunks_stream(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])
    expect(alice).to_be_on_ground(timeout=60)
    # Bob keeps a far-away area loaded on the server; the scene is built there.
    far = (1500.5, 1500.5)
    server.teleport(bob, (far[0], 120, far[1]))
    deadline = time.time() + 60
    while abs(bob.feet[0] - far[0]) > 5 and time.time() < deadline:
        time.sleep(0.1)
    expect(bob).to_be_on_ground(timeout=60)  # the far terrain has to generate first
    bx, by, bz = block_under_feet(bob)
    clear_corridor(server, bx, by, bz)
    target = (bx, by, bz - 2)
    server.set_block(target, "test:tough")
    expect(bob).to_see_block(target, "test:tough")
    # Alice lands next to Bob: her client now streams a whole new view box.
    feet = bob.feet
    server.teleport(alice, (feet[0] + 1, feet[1], feet[2]))
    expect(alice).to_see_block(target, "test:tough", timeout=30)
    damage = damage_confirms(alice.break_block(target, timeout=60))
    print("\npunch latency while streaming:", summary(damage))
