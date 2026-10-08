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

from vbtest import traceview  # noqa: E402
from vbtest.idp import NotSupported, auth_lua  # noqa: E402
from vbtest.mock_keycloak import MockKeycloak  # noqa: E402
from vbtest.stack import ClientFactory, start_server  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""


def pytest_addoption(parser):
    g = parser.getgroup("voxel_browser e2e")
    g.addoption("--vb-build-dir", default=os.environ.get("VB_BUILD_DIR", str(REPO / "build")),
                help="directory holding voxel_browser and voxel_browser_server (env VB_BUILD_DIR)")
    g.addoption("--vb-artifacts", default=os.environ.get("VB_E2E_ARTIFACTS", str(HERE / "artifacts")),
                help="where logs/traces of failed tests are kept")
    g.addoption("--vb-keep-artifacts", action="store_true", help="keep artifacts of passing tests too")
    g.addoption("--vb-idp", choices=("mock", "keycloak"), default=os.environ.get("VB_IDP", "mock"),
                help="identity provider the auth tests talk to: the in-process MockKeycloak (default) or a "
                     "real Keycloak in docker (tests/e2e/vbtest/keycloak_real.py; env VB_IDP)")
    g.addoption("--vb-allow-remote-host", action="store_true",
                help="allow clients to connect to a non-loopback host (a dedicated DEV server only!)")


def pytest_configure(config):
    config.addinivalue_line("markers", "vb_server(**toml): extra server.toml keys for this test's server")
    config.addinivalue_line("markers", "vb_pack_files(files): {path: text} written into this test's server pack")
    config.addinivalue_line("markers", "auth: in-engine authentication tests (test_auth*.py, test_mock_keycloak.py)")
    config.addinivalue_line("markers", "slow: waits on a real re-auth interval (>= 60 s); everything else fast-forwards "
                            "with the advance_reauth automation command")
    config.addinivalue_line("markers", "mock_only: needs mint() or fault injection, which only MockKeycloak supports")
    config.addinivalue_line("markers", "keycloak_real: compares against a real Keycloak; skipped unless --vb-idp=keycloak")
    config.addinivalue_line("markers", "realm(**options): realm options for the idp fixture (alg, audience_mapper, "
                            "group_paths, interactive, redirect_pattern, sso_session_idle_timeout, ...); a backend "
                            "that cannot do one skips the test")
    config.addinivalue_line("markers", "net_sim(**params): fake network conditions for the server and every client "
                            "(lag_ms, jitter_ms, loss_pct, reorder_pct, dup_pct)")


def pytest_collection_modifyitems(config, items):
    real = config.getoption("--vb-idp") == "keycloak"
    for item in items:
        name = pathlib.Path(str(item.fspath)).name
        if name.startswith("test_auth") or name == "test_mock_keycloak.py":
            item.add_marker(pytest.mark.auth)
        if item.get_closest_marker("keycloak_real") and not real:
            item.add_marker(pytest.mark.skip(reason="needs a real Keycloak: --vb-idp=keycloak"))
        if real and item.get_closest_marker("mock_only"):
            item.add_marker(pytest.mark.skip(reason="needs mint()/fault injection, mock only"))


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
    cfg = {}
    for marker in request.node.iter_markers("vb_server"):
        cfg.update(marker.kwargs)
    pack_files = {}
    for marker in request.node.iter_markers("vb_pack_files"):
        pack_files.update(marker.args[0])
    return start_server(binaries["server"], REPO, artifact_dir, tmp_path, _procs, config=cfg,
                        extra_args=net_sim_args(request), pack_files=pack_files or None)


@pytest.fixture
def clients(request, binaries, server, artifact_dir, tmp_path, _procs):
    return ClientFactory(binaries["client"], server, artifact_dir, tmp_path, _procs,
                         extra_args=net_sim_args(request),
                         allow_remote=request.config.getoption("--vb-allow-remote-host"), skip=pytest.skip)


@pytest.fixture
def client(clients):
    return clients(1)[0]


# -- identity provider (auth tests) ----------------------------------------------------------------
@pytest.fixture(scope="session")
def _real_keycloak(request):
    """One docker Keycloak for the whole run (started on first use, `--vb-idp=keycloak` only)."""
    from vbtest import keycloak_real
    backend = keycloak_real.start(request.config)
    request.addfinalizer(backend.stop)
    return backend


@pytest.fixture
def idp(request, artifact_dir):
    """The identity provider behind the auth tests: a fresh MockKeycloak per test, or (with
    `--vb-idp=keycloak`) the shared real one reset to its imported state. Tests use only the
    `vbtest.idp.IdpBackend` surface unless they are marked `mock_only`. On failure the requests
    it served are written to `idp.requests.jsonl` in the artifacts (and shown as a column in trace.html)."""
    options = {}
    for marker in request.node.iter_markers("realm"):
        options.update(marker.kwargs)
    if request.config.getoption("--vb-idp") == "keycloak":
        backend = request.getfixturevalue("_real_keycloak")
        try:
            backend.reset(**options)
        except NotSupported as e:
            pytest.skip("real Keycloak: %s" % e)
    else:
        backend = MockKeycloak(**options).start()
    yield backend
    rep = getattr(request.node, "rep_call", None)
    if rep is None or rep.failed:
        try:
            with open(artifact_dir / "idp.requests.jsonl", "w") as f:
                for entry in backend.request_log():
                    f.write(json.dumps(entry) + "\n")
        except Exception as e:  # never hide the real failure
            (artifact_dir / "idp.requests.error").write_text("%s\n" % e)
    if request.config.getoption("--vb-idp") != "keycloak":
        backend.stop()


@pytest.fixture
def auth_server(binaries, artifact_dir, tmp_path, _procs, idp):
    """`auth_server(**auth_lua_kwargs)`: a server whose pack requires sign-in at `idp`."""
    def make(config=None, name="server", **auth_kw):
        return start_server(binaries["server"], REPO, artifact_dir, tmp_path, _procs, config=config, name=name,
                            pack_files={"auth.lua": auth_lua(idp, **auth_kw)})
    return make


@pytest.fixture
def make_clients(binaries, artifact_dir, tmp_path, _procs):
    """`make_clients(server)(n, names=None, extra_args=())`: a ClientFactory for that server."""
    factories = {}

    def make(server):
        # One factory per server for the whole test, so calling make_clients(server)(...) twice
        # gives client1, client2, ... instead of colliding on client1's directories.
        if id(server) not in factories:
            f = ClientFactory(binaries["client"], server, artifact_dir, tmp_path, _procs)
            f._count = sum(x._count for x in factories.values())
            factories[id(server)] = f
        return factories[id(server)]
    return make


@pytest.fixture
def write_token(tmp_path):
    """`write_token(token, name="token.txt")`: a file for `--auth-token-file` (re-read on every re-auth)."""
    def make(token, name="token.txt"):
        f = tmp_path / name
        f.write_text(token)
        return f
    return make
