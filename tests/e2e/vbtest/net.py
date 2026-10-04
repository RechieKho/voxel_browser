"""Ports and the "never aim test bots at a real server" guard."""
import socket

LOOPBACK_HOSTS = ("127.0.0.1", "::1", "localhost")


class UnsafeHostError(Exception):
    pass


def free_udp_port():
    """GameNetworkingSockets can't listen on port 0 (docs/automation-protocol.md), so pick one."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def check_host(host, allow_remote):
    """docs/e2e-automation.md §7.4: automation clients only ever talk to loopback unless asked."""
    if host in LOOPBACK_HOSTS or allow_remote:
        return host
    raise UnsafeHostError(
        "refusing to connect an automation client to %r: only %s are allowed. Pass "
        "--vb-allow-remote-host to test against a dedicated *dev* server on another machine."
        % (host, ", ".join(LOOPBACK_HOSTS)))
