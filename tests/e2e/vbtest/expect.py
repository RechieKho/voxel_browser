"""Playwright-style auto-waiting assertions.

    expect(bob).to_see_block((4, 70, 4), "base:air")
    expect(alice).not_.to_have_ui_open("base:inventory")

Each assertion is a *single* `wait_for` sent to the process, which re-evaluates
the predicate every frame/tick, so there is no polling and no sleeping in tests.
On timeout the AssertionError says what the process last saw.
"""
import json

from .process import AutomationError


class Expectation:
    def __init__(self, handle, negate=False):
        self._h = handle
        self._neg = negate

    @property
    def not_(self):
        return Expectation(self._h, not self._neg)

    def _check(self, description, pred, timeout):
        if self._neg:
            pred = {"not": pred}
            description = "NOT " + description
        try:
            self._h.wait_for(pred, timeout=timeout)
        except AutomationError as e:
            if e.code != "timeout":
                raise
            raise AssertionError("%s: expected %s within %.1fs, last saw %s"
                                 % (self._h.name, description, timeout, json.dumps(e.error.get("last")))) from None

    # -- either role -------------------------------------------------------
    def to_see_block(self, pos, block, timeout=5.0):
        return self._check("block %s at %s" % (block, list(pos)),
                           {"block_is": {"pos": list(pos), "block": block}}, timeout)

    def to_have_chunk_loaded(self, pos, timeout=10.0):
        """The chunk containing `pos` exists in this process's world (build scenes only after this)."""
        return self._check("chunk containing %s loaded" % list(pos), {"chunk_loaded": {"pos": list(pos)}}, timeout)

    # -- client ------------------------------------------------------------
    def to_be_in_state(self, app_state, timeout=30.0):
        """menu | connecting | loading | playing | error | ..."""
        return self._check("app state %r" % app_state, {"app_state": {"is": app_state}}, timeout)

    def to_be_joined(self, timeout=30.0):
        return self._check("to be joined", {"joined": True}, timeout)

    def to_be_on_ground(self, timeout=10.0):
        """Landed after the spawn drop. Wait for this before using the player's position."""
        return self._check("to be on the ground", {"on_ground": True}, timeout)

    def to_have_rtt_at_least(self, ms, timeout=15.0):
        """Measured round-trip time to the server (needs a real connection; GNS takes a moment to measure)."""
        return self._check("round-trip time >= %g ms" % ms, {"rtt_ms": {"op": ">=", "value": ms}}, timeout)

    def to_have_loaded_chunks(self, minimum, timeout=30.0):
        return self._check("%d chunks loaded" % minimum, {"chunks_loaded": {"min": minimum}}, timeout)

    def to_have_chat(self, text=None, regex=None, timeout=5.0):
        arg = {"regex": regex} if regex is not None else {"text": text}
        return self._check("chat containing %r" % (regex if regex is not None else text),
                           {"chat_contains": arg}, timeout)

    def to_have_inventory(self, item, count=1, timeout=5.0):
        return self._check("at least %d x %s" % (count, item),
                           {"inventory_has": {"item": item, "count": count}}, timeout)

    def to_have_ui_open(self, name, timeout=5.0):
        return self._check("UI %r open" % name, {"ui_open": {"name": name}}, timeout)

    def to_have_widget(self, widget_id, text=None, timeout=5.0):
        arg = {"id": widget_id}
        if text is not None:
            arg["text"] = text
        return self._check("widget %r" % widget_id, {"widget": arg}, timeout)

    def to_be_near(self, pos, radius=1.0, timeout=5.0):
        return self._check("to be within %.1f of %s" % (radius, list(pos)),
                           {"pos_near": {"pos": list(pos), "radius": radius}}, timeout)

    def to_see_entity(self, name, near=None, radius=2.0, timeout=5.0):
        if near is None:
            return self._check("entity %r visible" % name, {"entity_visible": {"name": name}}, timeout)
        return self._check("entity %r within %.1f of %s" % (name, radius, list(near)),
                           {"entity_near": {"name": name, "pos": list(near), "radius": radius}}, timeout)

    # -- server ------------------------------------------------------------
    def to_have_player_count(self, n, timeout=5.0):
        return self._check("%d players" % n, {"player_count": {"op": "==", "value": n}}, timeout)

    def to_have_player_near(self, name, pos, radius=1.5, timeout=5.0):
        return self._check("player %r within %.1f of %s" % (name, radius, list(pos)),
                           {"player_near": {"name": name, "pos": list(pos), "radius": radius}}, timeout)


def expect(handle):
    return Expectation(handle)
