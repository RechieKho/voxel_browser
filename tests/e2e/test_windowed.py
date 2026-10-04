"""Windowed clients: a real window, the raygui main menu, screenshots (docs/e2e-automation.md).

These need a display; run the suite under `xvfb-run -a` (CI does). Without one they skip.
raygui reads raylib's real mouse state, which a script can't produce, so the menu commands
inject the *result* of a click (and fill the same fields a player would type into).
"""
import pytest

from vbtest import expect
from vbtest.png import stats
from vbtest.scene import block_under_feet


def test_main_menu_connect_and_world_screenshots(server, clients, artifact_dir):
    (alice,) = clients(1, names=["Alice"], via_menu=True)
    expect(alice).to_be_in_state("menu")

    menu_shot = alice.screenshot(artifact_dir / "menu.png")
    assert (menu_shot["width"], menu_shot["height"]) == (640, 360)
    menu = stats(menu_shot["path"])
    assert menu["distinct_colors"] >= 4, "the menu screenshot looks blank: %r" % menu

    alice.menu_connect("127.0.0.1", server.port, name="Alice")
    expect(alice).to_be_in_state("playing", timeout=60)  # connecting -> asset sync -> loading screen -> playing
    expect(server).to_have_player_count(1)
    assert server.player("Alice") is not None
    expect(alice).to_be_on_ground()

    world_shot = alice.screenshot(artifact_dir / "world.png")
    world = stats(world_shot["path"])
    assert world["distinct_colors"] >= 40, "the in-game screenshot looks blank: %r" % world
    assert world["mean_rgb"] != menu["mean_rgb"], "the screenshot did not change after joining"


@pytest.mark.parametrize("windowed", [False, True], ids=["headless", "windowed"])
def test_typing_into_the_chat_box(server, clients, windowed):
    alice, bob = clients(2, names=["Alice", "Bob"], windowed=windowed)
    alice.type("hello ")
    alice.type("from the chat box")
    assert alice.state()["chat_open"] is True
    expect(bob).not_.to_have_chat("from the chat box", timeout=1)  # nothing is sent until Enter
    alice.key_press("enter")
    expect(bob).to_have_chat("Alice: hello from the chat box")
    assert alice.state()["chat_open"] is False


def test_screenshot_needs_a_window(server, clients, artifact_dir):
    (alice,) = clients(1, names=["Alice"])  # headless
    from vbtest import AutomationError
    with pytest.raises(AutomationError) as e:
        alice.screenshot(artifact_dir / "nope.png")
    assert e.value.code == "unsupported"
