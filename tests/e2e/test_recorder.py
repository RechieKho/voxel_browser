"""`--automation-record`: turn a play session into a vbtest script (docs/e2e-automation.md §8).

The recorder watches the same input and game state a human produces. Here the "human" is a
scripted client (its synthetic input goes through exactly the code a keyboard/mouse does), so
we can check what the script says AND replay it on a brand-new world: if the generated script
isn't runnable, it isn't a recording of anything.
"""
import pathlib
import re

import pytest

from vbtest import expect
from vbtest.scene import block_under_feet, clear_corridor
from vbtest.stack import ClientFactory, start_server

REPO = pathlib.Path(__file__).resolve().parents[2]


SETUP = """\
    # --- scene setup (not recorded): two stone blocks in front of the spawn, and some stone to place
    clear_corridor(server, {x}, {y}, {z})
    server.set_block(({x}, {y}, {z} - 2), "base:stone")
    server.set_block(({x}, {y}, {z} - 3), "base:stone")  # something solid to place against later
    server.give(alice, "base:stone", 3)
    expect(alice).to_have_inventory("base:stone", 3)
"""


@pytest.fixture(scope="module")
def recorded(binaries, tmp_path_factory):
    root = tmp_path_factory.mktemp("recorded")
    art = root / "artifacts"
    (root / "srv").mkdir()
    art.mkdir()
    procs = []
    try:
        server = start_server(binaries["server"], REPO, art, root / "srv", procs)
        clients = ClientFactory(binaries["client"], server, art, root, procs)
        out = root / "session.py"
        (alice,) = clients(1, names=["Alice"], tcp=True, extra_args=["--automation-record", str(out)])

        expect(alice).to_be_on_ground()
        x, y, z = block_under_feet(alice)
        exec(SETUP.replace("    # ---", "# ---").replace("\n    ", "\n").format(x=x, y=y, z=z),
             {"clear_corridor": clear_corridor, "server": server, "alice": alice, "expect": expect})
        target = (x, y, z - 2)

        # ---- the recorded part: everything below is what "the player" does ----
        alice.walk_to((x + 0.5, None, z + 2.5), tolerance=0.7)
        expect(alice).to_be_on_ground()
        alice.select_slot(2)
        alice.select_slot(1)
        alice.break_block(target)
        expect(alice).to_see_block(target, "base:air")

        alice.place_block((x, y, z - 3), face=(0, 0, 1))  # put it back, against the block behind it
        expect(alice).to_see_block(target, "base:stone")

        alice.chat("hello recorder")
        expect(alice).to_have_chat("Alice: hello recorder")
        alice.key_press("inventory")
        expect(alice).to_have_ui_open("base:inventory")
        alice.ui("close").click()
        expect(alice).not_.to_have_ui_open("base:inventory")

        alice.close()  # quit: the client writes its final script
        return {"source": out.read_text(), "pos": (x, y, z), "target": target}
    finally:
        for p in reversed(procs):
            p.close()


def test_the_script_says_what_was_done_in_order(recorded):
    src = recorded["source"]
    assert "def test_recorded_session(server, clients):" in src
    assert '(alice,) = clients(1, names=["Alice"])' in src
    tx, ty, tz = recorded["target"]
    wanted = [
        r"alice\.walk_to\(\(-?\d+\.\d, None, -?\d+\.\d\), tolerance=0\.7\)",
        r"alice\.select_slot\(2\)",
        r"alice\.select_slot\(1\)",
        r"alice\.break_block\(\(%d, %d, %d\)\)" % (tx, ty, tz),
        r'expect\(alice\)\.to_see_block\(\(%d, %d, %d\), "base:air"\)' % (tx, ty, tz),
        r"alice\.place_block\(\(%d, %d, %d\), face=\(0, 0, 1\)\)" % (tx, ty, tz - 1),
        r'expect\(alice\)\.to_see_block\(\(%d, %d, %d\), "base:stone"\)' % (tx, ty, tz),
        r'alice\.chat\("hello recorder"\)',
        r'expect\(alice\)\.to_have_chat\("Alice: hello recorder"\)',
        r'alice\.key_press\("inventory"\)',
        r'expect\(alice\)\.to_have_ui_open\("base:inventory"\)',  # the screen the key opened
        r'alice\.ui\("close"\)\.click\(\)',
        r'expect\(alice\)\.not_\.to_have_ui_open\("base:inventory"\)',
    ]
    pos = 0
    for pattern in wanted:
        m = re.search(pattern, src[pos:])
        assert m, "missing (or out of order): %s\n--- recorded script ---\n%s" % (pattern, src)
        pos += m.end()
    assert src.count("break_block(") == 1, "one block, however many clicks, is one break"
    compile(src, "recorded_session.py", "exec")  # at least it is valid Python


def test_the_script_replays_on_a_fresh_world(recorded, server, clients):
    # Scene setup is not recorded; add it, as a user of a recording would.
    x, y, z = recorded["pos"]
    src = recorded["source"].replace(
        "expect(alice).to_be_on_ground()\n",
        "expect(alice).to_be_on_ground()\n" + SETUP.format(x=x, y=y, z=z), 1)
    namespace = {"clear_corridor": clear_corridor}
    exec(compile(src, "recorded_session.py", "exec"), namespace)
    namespace["test_recorded_session"](server, clients)  # raises if any step or assertion fails
