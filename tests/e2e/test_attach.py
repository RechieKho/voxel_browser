"""`--automation tcp`: attaching to a game that is already running (docs/e2e-automation.md §3.1).

Loopback only, one token-authenticated connection at a time, and the game survives a
disconnect, which is what lets a script attach to a client a human is playing.
"""
import os

import pytest

from vbtest import AutomationError, Client, expect


def test_attach_over_tcp_and_survive_a_detach(server, clients):
    (alice,) = clients(1, names=["Alice"], tcp=True)
    assert alice.endpoint["host"] == "127.0.0.1" and alice.endpoint["port"] > 0
    expect(alice).to_be_joined()
    assert alice.state()["joined"] is True

    alice.detach()                          # the script goes away ...
    expect(server).to_have_player_count(1)  # ... the game keeps running, still connected
    with pytest.raises(Exception):
        alice.state()                       # and our handle is closed

    alice.reattach()                        # a later script picks up where it left off
    expect(alice).to_be_joined()
    alice.chat("back again")
    expect(alice).to_have_chat("Alice: back again")


def test_a_second_session_can_attach_to_the_same_running_game(server, clients, artifact_dir):
    (alice,) = clients(1, names=["Alice"], tcp=True)
    alice.detach()
    other = Client("second-attach", None, artifact_dir, attach=alice.endpoint)  # e.g. another tool
    try:
        assert other.state()["joined"] is True
    finally:
        other.close()
    alice.reattach()
    expect(alice).to_be_joined()


def test_wrong_token_is_refused(server, clients, artifact_dir):
    (alice,) = clients(1, names=["Alice"], tcp=True)
    alice.detach()  # one connection is served at a time: a second would just wait its turn
    bad = dict(alice.endpoint, token="not-the-token")
    with pytest.raises(AutomationError) as e:
        Client("intruder", None, artifact_dir, attach=bad)
    assert e.value.code == "unauthorized"
    alice.reattach()  # the rejected attempt left the real endpoint working
    expect(alice).to_be_joined()


def _listening_addresses(port):
    """IPs (dotted) the kernel reports as LISTENing on `port`, from /proc/net/tcp (Linux)."""
    found = set()
    for table in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            lines = open(table).read().splitlines()[1:]
        except OSError:
            continue
        for line in lines:
            fields = line.split()
            ip_hex, port_hex = fields[1].split(":")
            if int(port_hex, 16) == port and fields[3] == "0A":  # 0A = LISTEN
                if len(ip_hex) == 8:  # IPv4, little-endian hex
                    found.add(".".join(str(int(ip_hex[i:i + 2], 16)) for i in (6, 4, 2, 0)))
                else:
                    found.add(ip_hex)
    return found


@pytest.mark.skipif(not os.path.exists("/proc/net/tcp"), reason="reads the kernel's socket table (Linux)")
def test_listener_is_bound_to_loopback_only(server, clients):
    (alice,) = clients(1, names=["Alice"], tcp=True)
    assert _listening_addresses(alice.endpoint["port"]) == {"127.0.0.1"}, \
        "the automation listener must be reachable only from this machine"
