"""Real server + real clients, driven end to end (docs/e2e-automation.md).

Each test starts its own dedicated server and headless client processes, so they are
independent. Scenes are built next to the players: the server refuses edits outside
loaded chunks.
"""
import pytest

from vbtest import UnsafeHostError, expect
from vbtest.scene import block_under_feet, clear_corridor, step_aside


def test_clients_join_and_see_each_other(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])
    expect(server).to_have_player_count(2)
    expect(alice).to_see_entity("Bob")
    expect(bob).to_see_entity("Alice")
    # Cold cache: the base pack really was downloaded and cached, not assumed present.
    assert any(p.is_file() for p in alice.cache_dir.rglob("*")), "client cached no assets"


def test_chat_reaches_the_other_player(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])
    alice.chat("hello bob")
    expect(bob).to_have_chat("Alice: hello bob")
    expect(alice).to_have_chat("Alice: hello bob")  # the sender sees their own line too


def test_block_break_replicates_to_other_client(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])
    x, y, z = block_under_feet(alice)
    step_aside(server, bob, dx=6)
    clear_corridor(server, x, y, z)
    target = (x, y, z - 2)
    server.set_block(target, "base:stone")
    expect(alice).to_see_block(target, "base:stone")
    expect(bob).to_see_block(target, "base:stone")

    result = alice.break_block(target)
    assert result["block_before"] == "base:stone"
    expect(bob).to_see_block(target, "base:air")
    expect(server).to_see_block(target, "base:air")


def test_block_place_replicates_and_spends_the_item(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])
    x, y, z = block_under_feet(alice)
    clear_corridor(server, x, y, z)
    support = (x, y, z - 2)
    server.set_block(support, "base:stone")
    server.give(alice, "base:stone", 5)
    expect(alice).to_have_inventory("base:stone", 5)

    alice.select_slot(1)
    placed_at = (x, y + 1, z - 2)
    alice.place_block(support, face=(0, 1, 0))
    expect(bob).to_see_block(placed_at, "base:stone")
    expect(alice).not_.to_have_inventory("base:stone", 5)  # one was spent


def test_craft_planks_with_the_chat_command(server, clients):
    (alice,) = clients(1, names=["Alice"])
    server.give(alice, "base:wood", 1)
    expect(alice).to_have_inventory("base:wood", 1)
    alice.chat("/craft base:planks")
    expect(alice).to_have_inventory("base:planks", 4)
    expect(alice).not_.to_have_inventory("base:wood", 1)


def test_inventory_screen_opens_and_closes(server, clients):
    (alice,) = clients(1, names=["Alice"])
    server.give(alice, "base:wood", 2)
    expect(alice).to_have_inventory("base:wood", 2)
    alice.key_press("inventory")  # the pack's keybind -> server opens the screen
    expect(alice).to_have_ui_open("base:inventory")
    expect(alice).to_have_widget("close")
    alice.ui("close").click()
    expect(alice).not_.to_have_ui_open("base:inventory")


def test_server_can_move_and_hurt_players(server, clients):
    (alice,) = clients(1, names=["Alice"])
    x, y, z = block_under_feet(alice)
    destination = (x + 3.5, y, z + 3.5)
    server.teleport(alice, destination)
    expect(alice).to_be_near(destination, radius=1.5)
    expect(server).to_have_player_near("Alice", destination, radius=1.5)

    server.set_health("Alice", 5)
    assert server.player("Alice")["health"] == pytest.approx(5)


def test_walking_is_seen_by_other_players(server, clients):
    alice, bob = clients(2, names=["Alice", "Bob"])
    x, y, z = block_under_feet(alice)
    goal = (x + 4.5, None, z + 0.5)
    alice.walk_to(goal, tolerance=0.7)
    expect(bob).to_see_entity("Alice", near=(goal[0], y, goal[2]), radius=2.5)


def test_player_can_reconnect_after_being_kicked(server, clients):
    (alice,) = clients(1, names=["Alice"])
    expect(server).to_have_player_count(1)
    server.kick("Alice", reason="see you soon")
    expect(server).to_have_player_count(0)

    (again,) = clients(1, names=["Alice"])  # a brand-new client process, same name
    expect(server).to_have_player_count(1)
    expect(again).to_be_joined()


def test_harness_refuses_non_loopback_hosts(server, clients):
    """docs/e2e-automation.md §7.4: test bots never connect anywhere but this machine."""
    with pytest.raises(UnsafeHostError):
        clients(1, host="play.example.com")
