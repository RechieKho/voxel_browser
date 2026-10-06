"""Gameplay on a degraded network (--net-sim, docs/e2e-automation.md §5.4).

Each side delays/drops what *it sends*, so the marker is applied to the server and to every
client: lag_ms=100 shows up as about 200 ms of round-trip time.
"""
import pytest

from vbtest import expect
from vbtest.scene import block_under_feet, clear_corridor, step_aside


@pytest.mark.net_sim(lag_ms=100)
def test_net_sim_really_adds_latency(server, clients):
    (alice,) = clients(1, names=["Alice"])
    expect(alice).to_have_rtt_at_least(150)  # ~2 x 100 ms; a clean loopback link is well under 5 ms


@pytest.mark.net_sim(lag_ms=60, jitter_ms=20, loss_pct=3)
def test_gameplay_survives_lag_jitter_and_loss(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])

    # Reliable lanes (chat, world edits) must still get through ...
    alice.chat("still here")
    expect(bob).to_have_chat("Alice: still here", timeout=10)

    x, y, z = block_under_feet(alice)
    clear_corridor(server, x, y, z)
    step_aside(server, bob, dx=6)
    target = (x, y, z - 2)
    server.set_block(target, "base:stone")
    expect(alice).to_see_block(target, "base:stone", timeout=10)
    alice.break_block(target, timeout=15)
    expect(bob).to_see_block(target, "base:air", timeout=10)

    # ... and so must the unreliable input lane that drives prediction/reconciliation.
    # clear_corridor only opens the aiming box (x-1..x+1); the walk continues to x+3.5 over
    # whatever the random terrain put there, and a two-block wall can't be stepped or jumped.
    lo, hi = (x + 2, y, z), (x + 4, y + 2, z)
    for cx in (lo[0], hi[0]):
        expect(server).to_have_chunk_loaded((cx, y, z))
    assert server.fill(lo, hi, "base:air")["not_loaded"] == 0
    goal = (x + 3.5, None, z + 0.5)
    alice.walk_to(goal, tolerance=0.8, timeout=25)
    expect(bob).to_see_entity("Alice", near=(goal[0], y, goal[2]), radius=3.0, timeout=10)
