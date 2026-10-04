"""Helpers for building a scene next to the players (shared by the e2e tests)."""
import math

from .expect import expect


def block_under_feet(client):
    """The voxel the client is standing in (its feet), as ints.

    Players spawn a little above the ground and drop, so wait for the landing first:
    positions sampled mid-fall are a block off by the time anything acts on them.
    """
    expect(client).to_be_on_ground()
    # A landed player's feet.y is e.g. 62.9999 (float error): floor() of that would name the
    # ground row, not the cell they stand in.
    return tuple(int(math.floor(v + 0.01)) for v in client.feet)


def clear_corridor(server, x, y, z):
    """Air in front of (x, y, z): 3 wide, 3 tall, 4 deep towards -Z, so nothing blocks aiming.

    The server's world can lag a moment behind the client's join, and it refuses edits
    outside loaded chunks, so wait for every chunk the box touches first (an in-process
    wait, not a sleep) and check that nothing was skipped.
    """
    lo, hi = (x - 1, y, z - 4), (x + 1, y + 2, z)
    for cx in (lo[0], hi[0]):
        for cy in (lo[1], hi[1]):
            for cz in (lo[2], hi[2]):
                expect(server).to_have_chunk_loaded((cx, cy, cz))
    result = server.fill(lo, hi, "base:air")
    assert result["not_loaded"] == 0, result


def step_aside(server, player, dx):
    """Players spawn on the very same spot, and a punch resolves to the nearest *player*
    before any block, so Alice's punches would land on Bob. Move one of them away."""
    x, y, z = block_under_feet(player)
    dest = (x + dx + 0.5, y, z + 0.5)
    server.teleport(player, dest)
    # The terrain over there may be lower, so he can drop a block or two: only "clearly away" matters.
    expect(player).to_be_near(dest, radius=4.0)
