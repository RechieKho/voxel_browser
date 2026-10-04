"""One automation-enabled voxel_browser / voxel_browser_server process.

Speaks the JSON-lines protocol of docs/automation-protocol.md over the child's
stdin/stdout. A reader thread routes responses to waiting callers by `id`; every
request/response is also appended to a JSONL trace next to the stderr log, so a
failed test leaves a timeline of what each process was asked and answered.
"""
import json
import os
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


class Process:
    def __init__(self, name, argv, log_dir, env=None, cwd=None):
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
        self._proc = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._stderr,
            text=True, bufsize=1, env=env, cwd=cwd)
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    # -- plumbing ----------------------------------------------------------
    def _log(self, direction, obj):
        with self._lock:
            self._trace.write(json.dumps({"t": round(time.time() - self._t0, 3), "dir": direction, "msg": obj}) + "\n")
            self._trace.flush()

    def _read(self):
        for line in self._proc.stdout:
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
        with self._cond:
            self._eof = True
            self._cond.notify_all()

    @property
    def alive(self):
        return self._proc.poll() is None

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
            self._proc.stdin.write(json.dumps(frame) + "\n")
            self._proc.stdin.flush()
        except (BrokenPipeError, ValueError):
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

    def close(self):
        """quit -> wait -> kill. Never raises."""
        try:
            if self.alive:
                try:
                    self.call_raw("quit", 5.0)
                except Exception:
                    pass
            try:
                self._proc.stdin.close()
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
