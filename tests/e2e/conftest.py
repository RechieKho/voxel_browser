"""Fixtures for the end-to-end multiplayer tests (docs/e2e-automation.md §6).

    def test_x(server, clients):
        alice, bob = clients(2, names=["Alice", "Bob"])

`server` is a real voxel_browser_server on a free loopback port; `clients(n)` spawns
n real headless voxel_browser processes, each with its own *empty* asset cache (so
asset sync really runs) and config, and waits until they joined. Everything is
driven over the stdio JSON-lines automation channel; see tests/e2e/README.md.
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

from vbtest import Client, ProcessDied, Server, expect, traceview  # noqa: E402
from vbtest.net import check_host, free_udp_port  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""


def pytest_addoption(parser):
    g = parser.getgroup("voxel_browser e2e")
    g.addoption("--vb-build-dir", default=os.environ.get("VB_BUILD_DIR", str(REPO / "build")),
                help="directory holding voxel_browser and voxel_browser_server (env VB_BUILD_DIR)")
    g.addoption("--vb-artifacts", default=os.environ.get("VB_E2E_ARTIFACTS", str(HERE / "artifacts")),
                help="where logs/traces of failed tests are kept")
    g.addoption("--vb-keep-artifacts", action="store_true", help="keep artifacts of passing tests too")
    g.addoption("--vb-allow-remote-host", action="store_true",
                help="allow clients to connect to a non-loopback host (a dedicated DEV server only!)")


def pytest_configure(config):
    config.addinivalue_line("markers", "vb_server(**toml): extra server.toml keys for this test's server")
    config.addinivalue_line("markers", "net_sim(**params): fake network conditions for the server and every client "
                            "(lag_ms, jitter_ms, loss_pct, reorder_pct, dup_pct)")


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, "rep_" + rep.when, rep)


@pytest.fixture(scope="session")
def binaries(request):
    build = pathlib.Path(request.config.getoption("--vb-build-dir")).resolve()
    paths = {"client": build / ("voxel_browser" + EXE), "server": build / ("voxel_browser_server" + EXE)}
    for what, p in paths.items():
        if not p.exists():
            pytest.fail("%s binary not found at %s (build it, or pass --vb-build-dir / set VB_BUILD_DIR)" % (what, p),
                        pytrace=False)
        version = subprocess.run([str(p), "--version"], capture_output=True, text=True, timeout=30).stdout
        if "automation" not in version.lower():
            pytest.fail("%s was not built with -DVB_WITH_AUTOMATION=ON (--version: %r)" % (p, version.strip()),
                        pytrace=False)
    return paths


def net_sim_args(request):
    """`--net-sim ...` for a test marked @pytest.mark.net_sim(lag_ms=..., ...), else nothing."""
    params = {}
    for marker in request.node.iter_markers("net_sim"):
        params.update(marker.kwargs)
    if not params:
        return []
    return ["--net-sim", ",".join("%s=%s" % (k, v) for k, v in params.items())]


@pytest.fixture
def artifact_dir(request):
    root = pathlib.Path(request.config.getoption("--vb-artifacts"))
    d = root / re.sub(r"[^A-Za-z0-9_.-]+", "_", request.node.nodeid)
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    yield d
    rep = getattr(request.node, "rep_call", None)
    failed = rep is None or rep.failed
    if failed:
        request.node.add_report_section("teardown", "vb artifacts", "logs, command traces and final state: %s" % d)
    elif not request.config.getoption("--vb-keep-artifacts"):
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def _procs(request, artifact_dir):
    """Every process a test starts; closed (clients first) after the test, with a final
    state snapshot written to the artifacts dir when the test failed."""
    procs = []
    yield procs
    rep = getattr(request.node, "rep_call", None)
    if rep is None or rep.failed:
        for p in procs:
            if p.alive:
                try:
                    with open(artifact_dir / (p.name + ".final_state.json"), "w") as f:
                        json.dump(p.call("state", _timeout=3.0), f, indent=2)
                except Exception as e:  # a hung process is exactly what we want to record
                    (artifact_dir / (p.name + ".final_state.json")).write_text("unavailable: %s\n" % e)
    for p in reversed(procs):
        p.close()
    if rep is None or rep.failed:
        try:
            traceview.write(str(artifact_dir))  # one HTML timeline across all processes
        except Exception as e:  # never let a viewer bug hide the real failure
            (artifact_dir / "trace.html.error").write_text("%s\n" % e)


@pytest.fixture
def server(request, binaries, artifact_dir, tmp_path, _procs):
    cfg = {"world_seed": 7, "view_distance": 3, "persist_world": False, "tick_rate": 20}
    for marker in request.node.iter_markers("vb_server"):
        cfg.update(marker.kwargs)
    # A private copy of the pack: nothing a test does (vb.storage, ...) can dirty the repo.
    pack = tmp_path / "content" / "base"
    shutil.copytree(REPO / "content" / "base", pack)
    last = None
    for _ in range(3):  # a free port can be taken between picking it and binding it
        port = free_udp_port()
        lines = ['port = %d' % port, 'content_pack = "%s"' % pack.as_posix(),
                 'world_dir = "%s"' % (tmp_path / "world").as_posix()]
        for k, v in cfg.items():
            lines.append("%s = %s" % (k, json.dumps(v).lower() if isinstance(v, bool) else json.dumps(v)))
        toml = tmp_path / "server.toml"
        toml.write_text("\n".join(lines) + "\n")
        proc = Server("server", [str(binaries["server"]), "--config", str(toml), "--automation", "stdio"] + net_sim_args(request),
                      artifact_dir, cwd=str(tmp_path))
        _procs.append(proc)
        try:
            info = proc.hello()
            proc.port = port
            assert info["role"] == "server"
            return proc
        except (ProcessDied, TimeoutError) as e:
            last = e
            proc.close()
            _procs.remove(proc)
    raise last


@pytest.fixture
def clients(request, binaries, server, artifact_dir, tmp_path, _procs):
    counter = [0]
    allow_remote = request.config.getoption("--vb-allow-remote-host")

    def make(n=1, names=None, host="127.0.0.1", port=None, join=True, render_distance=2, windowed=False,
             via_menu=False):
        """windowed=True opens a real window (needs a display, e.g. `xvfb-run -a pytest ...`);
        via_menu=True starts on the main menu instead of connecting (drive it with `menu_connect`)."""
        check_host(host, allow_remote)  # §7.4: test bots never go near a real server
        if via_menu:
            windowed, join = True, False  # the menu only exists in a window
        if windowed and not os.environ.get("DISPLAY"):
            pytest.skip("windowed tests need a display: run under xvfb-run (e.g. `xvfb-run -a ctest -L e2e`)")
        made = []
        for i in range(n):
            counter[0] += 1
            idx = counter[0]
            name = names[i] if names else "Player%d" % idx
            home = tmp_path / ("client%d" % idx)
            cache = home / "cache"
            cache.mkdir(parents=True)
            conf = home / "client.toml"
            conf.write_text('player_name = "%s"\nrender_distance = %d\n' % (name, render_distance) +
                            ('window_width = 640\nwindow_height = 360\nvsync = false\n' if windowed else ""))
            env = dict(os.environ, HOME=str(home), XDG_CACHE_HOME=str(cache), LOCALAPPDATA=str(cache))
            argv = [str(binaries["client"])] + ([] if windowed else ["--headless"])
            if not via_menu:  # --server connects straight away; without it a windowed client opens on the menu
                argv += ["--server", host, "--port", str(port or server.port)]
            argv += ["--name", name, "--config", str(conf), "--render-distance", str(render_distance),
                     "--automation", "stdio"] + net_sim_args(request)
            c = Client("client-" + name, argv, artifact_dir, env=env)
            c.player_name = name
            c.cache_dir = cache
            _procs.append(c)
            made.append(c)
        for c in made:
            c.hello()  # headless: answered once connected (or it exits with its error); windowed: at once
        if join:
            for c in made:
                expect(c).to_be_joined()
                expect(c).to_have_loaded_chunks(8)
                # Windowed clients sit on a loading screen until their first chunks are meshed.
                expect(c).to_be_in_state("playing", timeout=60)
        return made

    return make


@pytest.fixture
def client(clients):
    return clients(1)[0]
