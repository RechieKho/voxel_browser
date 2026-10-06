"""Turns the per-process traces of a failed test into one self-contained HTML timeline.

    python3 -m vbtest.traceview tests/e2e/artifacts/<test>      # writes <test>/trace.html

One column per process (server, each client, and an IdP column when the test had one), rows in wall-clock order, so "Alice did X but
Bob never saw it" is a matter of reading across. Requests are blue, replies green, errors red,
unsolicited events grey. Stdlib only; everything is HTML-escaped (traces contain game text).
"""
import glob
import html
import json
import os
import sys

MAX_CELL = 220


def _load(path):
    events = []
    with open(path) as f:
        for line in f:
            try:
                events.append(json.loads(line))
            except ValueError:
                pass
    return events


def _short(obj):
    text = json.dumps(obj, separators=(",", ":"), ensure_ascii=False)
    return text if len(text) <= MAX_CELL else text[:MAX_CELL] + "..."


def _describe(ev):
    """(css class, text) for one trace record."""
    msg, direction = ev.get("msg"), ev.get("dir")
    if direction == "out":
        args = msg.get("args") or {}
        return "req", "#%s %s %s" % (msg.get("id"), msg.get("cmd"), _short(args) if args else "")
    if direction == "in" and isinstance(msg, dict) and "id" in msg:
        if msg.get("ok"):
            return "ok", "#%s ok %s" % (msg.get("id"), _short(msg.get("result", {})))
        return "err", "#%s ERROR %s" % (msg.get("id"), _short(msg.get("error", {})))
    if direction == "in":
        return "evt", "event %s" % _short(msg)
    return "raw", "%s %s" % (direction, _short(msg))


def _describe_idp(entry):
    """One identity-provider request (vbtest/mock_keycloak.py's request log)."""
    status = entry.get("status")
    params = entry.get("params") or {}
    text = "%s %s -> %s%s %s" % (entry.get("method"), entry.get("endpoint") or entry.get("path"), status,
                                  " [FAULT %s]" % entry["fault"] if entry.get("fault") else "",
                                  _short(params) if params else "")
    bad = not isinstance(status, int) or status >= 400 or entry.get("fault")
    return ("err" if bad else "ok"), text


def build(artifact_dir):
    """The HTML for every *.trace.jsonl in `artifact_dir`."""
    procs = sorted(os.path.basename(p)[:-len(".trace.jsonl")] for p in glob.glob(os.path.join(artifact_dir, "*.trace.jsonl")))
    rows = []
    for name in procs:
        for ev in _load(os.path.join(artifact_dir, name + ".trace.jsonl")):
            rows.append((ev.get("ts", 0.0), name, ev))
    idp_path = os.path.join(artifact_dir, "idp.requests.jsonl")
    if os.path.exists(idp_path):
        procs.append("IdP")
        for entry in _load(idp_path):
            rows.append((entry.get("t", 0.0), "IdP", {"idp": entry}))
    rows.sort(key=lambda r: r[0])
    t0 = rows[0][0] if rows else 0.0

    out = ["<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>",
           "<title>e2e trace: %s</title>" % html.escape(os.path.basename(os.path.abspath(artifact_dir))),
           "<style>:root{--bg:#fff;--fg:#1a1a1a;--mut:#666;--line:#ddd;--req:#1b5fb8;--ok:#1f7a3a;--err:#b3261e;--evt:#777}"
           "@media(prefers-color-scheme:dark){:root{--bg:#16161a;--fg:#e6e6e6;--mut:#999;--line:#333;--req:#6aa6ff;--ok:#5fcf80;--err:#ff7b72;--evt:#999}}"
           "body{background:var(--bg);color:var(--fg);font:13px/1.35 ui-monospace,Menlo,Consolas,monospace;margin:16px}"
           "table{border-collapse:collapse;width:100%}th,td{border:1px solid var(--line);padding:3px 6px;vertical-align:top;text-align:left}"
           "th{position:sticky;top:0;background:var(--bg)}td.t{color:var(--mut);white-space:nowrap}td{word-break:break-word}"
           ".req{color:var(--req)}.ok{color:var(--ok)}.err{color:var(--err);font-weight:700}.evt,.raw{color:var(--evt)}"
           "details{margin:8px 0}pre{white-space:pre-wrap}</style>",
           "<h1>%s</h1>" % html.escape(os.path.basename(os.path.abspath(artifact_dir))),
           "<table><tr><th>+ms</th>%s</tr>" % "".join("<th>%s</th>" % html.escape(p) for p in procs)]
    for ts, name, ev in rows:
        cls, text = _describe_idp(ev["idp"]) if "idp" in ev else _describe(ev)
        cells = "".join("<td class=%s>%s</td>" % (cls, html.escape(text)) if p == name else "<td></td>" for p in procs)
        out.append("<tr><td class=t>%d</td>%s</tr>" % (round((ts - t0) * 1000), cells))
    out.append("</table>")
    for p in procs:
        for suffix, title in ((".final_state.json", "final state"), (".stderr.log", "stderr (tail)")):
            path = os.path.join(artifact_dir, p + suffix)
            if os.path.exists(path):
                with open(path, errors="replace") as f:
                    body = f.read()
                if suffix == ".stderr.log":
                    body = "".join(body.splitlines(True)[-60:])
                out.append("<details><summary>%s: %s</summary><pre>%s</pre></details>" % (html.escape(p), title, html.escape(body)))
    return "\n".join(out)


def write(artifact_dir):
    path = os.path.join(artifact_dir, "trace.html")
    with open(path, "w") as f:
        f.write(build(artifact_dir))
    return path


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python3 -m vbtest.traceview <artifact-dir>")
    print(write(sys.argv[1]))
