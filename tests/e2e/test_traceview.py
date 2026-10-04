"""The trace viewer is plain Python: test it without starting any game process."""
import json

from vbtest import traceview


def _write(path, events):
    path.write_text("".join(json.dumps(e) + "\n" for e in events))


def test_trace_viewer_merges_processes_in_time_order_and_escapes_text(tmp_path):
    _write(tmp_path / "server.trace.jsonl", [
        {"ts": 100.000, "t": 0.0, "dir": "out", "msg": {"id": 1, "cmd": "set_block", "args": {"pos": [1, 2, 3]}}},
        {"ts": 100.020, "t": 0.02, "dir": "in", "msg": {"id": 1, "ok": True, "result": {"changed": True}}},
    ])
    _write(tmp_path / "client-Alice.trace.jsonl", [
        {"ts": 100.010, "t": 0.0, "dir": "out", "msg": {"id": 1, "cmd": "chat.send", "args": {"text": "<script>alert(1)</script>"}}},
        {"ts": 100.030, "t": 0.02, "dir": "in", "msg": {"id": 1, "ok": False, "error": {"code": "timeout", "last": {}}}},
        {"ts": 100.040, "t": 0.03, "dir": "garbage", "msg": "gdb noise"},
    ])
    (tmp_path / "client-Alice.final_state.json").write_text('{"app_state": "playing"}')
    (tmp_path / "server.stderr.log").write_text("line1\nline2 <b>\n")

    page = traceview.build(str(tmp_path))
    # one column per process, in a stable order
    assert page.index("<th>client-Alice</th>") < page.index("<th>server</th>")
    # rows interleave by wall clock: server set_block, Alice chat, server reply, Alice error
    order = [page.index(s) for s in ("set_block", "chat.send", "changed", "ERROR")]
    assert order == sorted(order)
    assert "class=err" in page and "class=ok" in page and "class=req" in page and "class=raw" in page
    # game text is escaped, never injected
    assert "<script>alert(1)</script>" not in page and "&lt;script&gt;" in page
    assert "line2 &lt;b&gt;" in page and "playing" in page

    assert traceview.write(str(tmp_path)).endswith("trace.html")


def test_trace_viewer_copes_with_an_empty_directory(tmp_path):
    assert "<table>" in traceview.build(str(tmp_path))
