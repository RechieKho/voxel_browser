"""One automation-enabled voxel_browser / voxel_browser_server process.

Speaks the JSON-lines protocol of docs/automation-protocol.md over the child's
stdin/stdout. A reader thread routes responses to waiting callers by `id`; every
request/response is also appended to a JSONL trace next to the stderr log, so a
failed test leaves a timeline of what each process was asked and answered.
"""
import json
import os
import socket
import subprocess
import threading
import time

PROTO = 1


def timeout_scale():
    """Sanitizer / loaded CI runners are slower: VB_E2E_TIMEOUT_SCALE multiplies every wait."""
    try:
        return max(0.1, float(os.environ.get("VB_E2E_TIMEOUT_SCALE", "1")))
    except ValueError:
        return 1.0


class AutomationError(Exception):
    """The process answered `ok: false`."""

    def __init__(self, cmd, error):
        self.cmd = cmd
        self.code = error.get("code", "")
        self.message = error.get("message", "")
        self.error = error
        super().__init__("%s failed: %s: %s %s" % (
            cmd, self.code, self.message,
            json.dumps({k: v for k, v in error.items() if k not in ("code", "message")})))


class ProcessDied(Exception):
    pass


def _connect(host, port, token, timeout=10.0):
    """Connect to an `--automation tcp` endpoint and authenticate. Returns (socket, reader, writer)."""
    sock = socket.create_connection((host, port), timeout=timeout)
    reader, writer = sock.makefile("r", newline="\n"), sock.makefile("w", newline="\n")
    writer.write(json.dumps({"cmd": "auth", "args": {"token": token}}) + "\n")
    writer.flush()
    reply = reader.readline()
    if not reply:
        sock.close()
        raise AutomationError("auth", {"code": "unauthorized", "message": "connection closed during auth"})
    reply = json.loads(reply)
    if not reply.get("ok"):
        sock.close()
        raise AutomationError("auth", reply.get("error", {}))
    sock.settimeout(None)
    return sock, reader, writer


def read_info(path, timeout=30.0):
    """The {host, port, token, pid} file a game writes with `--automation-info`."""
    deadline = time.time() + timeout * timeout_scale()
    while time.time() < deadline:
        try:
            with open(path) as f:
                return json.load(f)
        except (OSError, ValueError):
            time.sleep(0.05)
    raise TimeoutError("no automation info appeared at %s" % path)


class Process:
    """A game process spoken to over its stdio (default), or over `--automation tcp`.

    tcp=True: argv should contain `--automation tcp`; this adds `--automation-info`, starts the
    process and attaches. attach={host, port, token}: no process is started, we just connect to
    a game that is already running (the process, if any, is then not ours to quit).
    """

    def __init__(self, name, argv, log_dir, env=None, cwd=None, tcp=False, attach=None):
        self.name = name
        self.argv = argv
        os.makedirs(log_dir, exist_ok=True)
        self.stderr_path = os.path.join(log_dir, name + ".stderr.log")
        self.trace_path = os.path.join(log_dir, name + ".trace.jsonl")
        self._stderr = open(self.stderr_path, "w")
        self._trace = open(self.trace_path, "w")
        self._t0 = time.time()
        self._lock = threading.Lock()
        self._cond = threading.Condition(self._lock)
        self._replies = {}
        self._next_id = 0
        self._eof = False
        self.events = []
        self._proc = None
        self._sock = None
        self.endpoint = None  # {host, port, token} when attached over TCP
        if attach is None:
            if tcp:
                info_path = os.path.join(log_dir, name + ".automation.json")
                self._proc = subprocess.Popen(
                    argv + ["--automation-info", info_path], stdin=subprocess.DEVNULL,
                    stdout=open(os.path.join(log_dir, name + ".stdout.log"), "w"), stderr=self._stderr,
                    env=env, cwd=cwd)
                attach = read_info(info_path)
            else:
                self._proc = subprocess.Popen(
                    argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._stderr,
                    text=True, bufsize=1, env=env, cwd=cwd)
        if attach is not None:
            self.endpoint = dict(attach)
            self._sock, self._out, self._in = _connect(attach["host"], attach["port"], attach["token"])
        else:
            self._out, self._in = self._proc.stdout, self._proc.stdin
        self._reader = threading.Thread(target=self._read, args=(self._out,), daemon=True)
        self._reader.start()

    # -- plumbing ----------------------------------------------------------
    def _log(self, direction, obj):
        with self._lock:
            now = time.time()
            # `ts` (epoch seconds) lets tools interleave the traces of several processes.
            self._trace.write(json.dumps({"ts": round(now, 4), "t": round(now - self._t0, 3), "dir": direction,
                                          "msg": obj}) + "\n")
            self._trace.flush()

    def _read(self, stream):
        try:
            self._read_lines(stream)
        except (OSError, ValueError):
            pass  # the socket/pipe went away
        with self._cond:
            self._eof = True
            self._cond.notify_all()

    def _read_lines(self, stream):
        for line in stream:
            line = line.strip()
            if not line:
                continue
            try:
                frame = json.loads(line)
            except ValueError:
                self._log("garbage", line)
                continue
            self._log("in", frame)
            with self._cond:
                if "id" in frame:
                    self._replies[frame["id"]] = frame
                else:
                    self.events.append(frame)
                self._cond.notify_all()

    @property
    def alive(self):
        if self._proc is not None:
            return self._proc.poll() is None
        return not self._eof  # attached to someone else's process: as alive as our connection

    def stderr_tail(self, lines=40):
        try:
            with open(self.stderr_path) as f:
                return "".join(f.readlines()[-lines:])
        except OSError:
            return ""

    # -- protocol ----------------------------------------------------------
    def send(self, cmd, **args):
        """Fire a request without waiting; returns its id (pair with `wait_reply`)."""
        with self._lock:
            self._next_id += 1
            rid = self._next_id
        frame = {"id": rid, "cmd": cmd, "args": args}
        self._log("out", frame)
        try:
            self._in.write(json.dumps(frame) + "\n")
            self._in.flush()
        except (BrokenPipeError, ValueError, OSError):
            raise ProcessDied("%s: process is gone (cannot send %s)\n%s" % (self.name, cmd, self.stderr_tail()))
        return rid

    def wait_reply(self, rid, cmd, timeout):
        deadline = time.time() + timeout
        with self._cond:
            while rid not in self._replies:
                if self._eof:
                    raise ProcessDied("%s exited before answering %s\n%s" % (self.name, cmd, self.stderr_tail()))
                left = deadline - time.time()
                if left <= 0:
                    raise TimeoutError("%s: no reply to %s within %.1fs" % (self.name, cmd, timeout))
                self._cond.wait(left)
            return self._replies.pop(rid)

    def call_raw(self, cmd, _timeout=30.0, **args):
        """Full reply frame, `ok` true or false."""
        rid = self.send(cmd, **args)
        return self.wait_reply(rid, cmd, _timeout * timeout_scale())

    def call(self, cmd, _timeout=30.0, **args):
        """Result dict; raises AutomationError if the process said no."""
        reply = self.call_raw(cmd, _timeout, **args)
        if not reply.get("ok"):
            raise AutomationError(cmd, reply.get("error", {}))
        return reply.get("result", {})

    def hello(self):
        return self.call("hello", proto=PROTO, _timeout=60.0)

    def state(self):
        return self.call("state")

    def detach(self):
        """Drop our connection but leave the game running (TCP only). `reattach()` reconnects."""
        if self._sock is None:
            raise RuntimeError("%s is not attached over TCP" % self.name)
        try:
            self._sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self._sock.close()
        self._sock = None
        self._reader.join(5)

    def reattach(self):
        """Connect again to the same endpoint (after `detach()`, or after the connection dropped)."""
        if self.endpoint is None:
            raise RuntimeError("%s was not started in TCP mode" % self.name)
        if self._sock is not None:
            self.detach()
        e = self.endpoint
        self._sock, self._out, self._in = _connect(e["host"], e["port"], e["token"])
        with self._cond:
            self._eof = False
            self._replies.clear()
        self._reader = threading.Thread(target=self._read, args=(self._out,), daemon=True)
        self._reader.start()

    def close(self):
        """quit -> wait -> kill. Never raises. For an attached-only handle (no process of ours) this
        just disconnects."""
        try:
            if self._proc is None:
                if self._sock is not None:
                    self.detach()
                return
            if self.alive:
                try:
                    self.call_raw("quit", 5.0)
                except Exception:
                    pass
            for closer in (lambda: self._in.close(), lambda: self._sock and self._sock.close()):
                try:
                    closer()
                except Exception:
                    pass
            try:
                self._proc.wait(10 * timeout_scale())
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait(5)
        finally:
            for f in (self._stderr, self._trace):
                try:
                    f.close()
                except Exception:
                    pass
