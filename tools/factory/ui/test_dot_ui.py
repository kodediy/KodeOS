#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The station's page: one local web page for the operator of the final test.

    python3 tools/factory/ui/test_dot_ui.py [-p /dev/cu.usbmodem101] [--http-port 8770]
                                            [--simulator] [--out DIR] [--no-open] [-- extra program options]

Launches test_dot_final.py (or, with --simulator, ui/test_dot_sim.py and its
fake Kode) with --events, reads its `@@TD {json}` lines and hands them to the
browser as server-sent events. The page (page.html) draws the run from them
alone; the contract is EVENTS.md. Anything after `--` goes to the program as it
is (`-- --no-write`, `-- --operator-sim`). In the simulator, TEST_DOT_CASE and
TEST_DOT_SPEED pick the scenario and its pace (ui/test_dot_sim.py --help).

Reports stay where the program writes them, under the user's home
(~/kode-factory/reports, ~/kode-factory/sim-reports for the simulator); the
page serves them at /reports/ through reports.py and never serves a file from
anywhere else. Standard library only; the port is never opened here, only
looked at with lsof to say who holds it.
"""

import argparse
import json
import os
import queue
import signal
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import reports  # noqa: E402

PROGRAM = HERE.parent / "test_dot_final.py"
SIMULATOR = HERE / "test_dot_sim.py"
REPORTS_REAL = Path("~/kode-factory/reports").expanduser()
REPORTS_SIM = Path("~/kode-factory/sim-reports").expanduser()
EVENT_PREFIX = "@@TD "
DEFAULT_PORT = "/dev/cu.usbmodem101"
HTTP_PORT = 8770
PORT_LOOK_S = 1.5             # lsof is asked at most this often
STOP_GRACE_S = 8              # SIGTERM, then SIGKILL after this
SSE_PING_S = 10
MAX_FILE_BYTES = 8_000_000

TYPES = {".html": "text/html; charset=utf-8", ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
         ".json": "application/json", ".txt": "text/plain; charset=utf-8", ".svg": "image/svg+xml",
         ".csv": "text/csv; charset=utf-8"}


class State:
    def __init__(self, simulator, port, out, extra):
        self.simulator, self.port, self.extra = simulator, port, list(extra)
        self.reports = Path(out).expanduser() if out else (REPORTS_SIM if simulator else REPORTS_REAL)
        self.lock = threading.Lock()
        self.proc, self.events, self.subs = None, [], []
        self.running, self.stopped, self.run_id = False, False, 0
        self.held_t, self.held, self.held_p = 0.0, [], None

    def publish(self, ev):
        ev["rx"] = time.time()
        if ev.get("type") == "end" and ev.get("report"):
            threading.Thread(target=self.simple_page, args=(Path(str(ev["report"])).parent,), daemon=True).start()
        with self.lock:
            self.events.append(ev)
            subs = list(self.subs)
        for q in subs:
            q.put(ev)

    def system(self, kind, **kw):
        self.publish(dict(type="system", kind=kind, **kw))

    def simple_page(self, folder):
        """The run's simple report and the list of runs (reports.py)."""
        try:
            if folder.parent.resolve() == self.reports.resolve():
                reports.write(self.reports, folder.name)
        except Exception as e:  # noqa: BLE001 - a report that cannot be written must not stop the page
            self.publish(dict(type="log", level="warn", text=f"simple report: {type(e).__name__}: {e}"))


def port_holders(port):
    """Who has the port open, from lsof (the port is never opened here): [{pid, cmd, line}]."""
    out, seen = [], set()
    for path in dict.fromkeys([port, port.replace("/cu.", "/tty.")]):
        try:
            r = subprocess.run(["lsof", "-F", "pc", path], capture_output=True, text=True, timeout=5)
        except (OSError, subprocess.SubprocessError):
            continue
        pid = None
        for ln in r.stdout.splitlines():
            if ln.startswith("p"):
                pid = ln[1:]
            elif ln.startswith("c") and pid and pid not in seen:
                seen.add(pid)
                line = ln[1:]
                try:
                    line = subprocess.run(["ps", "-o", "command=", "-p", pid], capture_output=True, text=True,
                                          timeout=3).stdout.strip() or line
                except (OSError, subprocess.SubprocessError):
                    pass
                out.append(dict(pid=int(pid), cmd=ln[1:], line=line[:160]))
    return out


def port_state(st, port=None):
    port = port or st.port
    now = time.time()
    if now - st.held_t > PORT_LOOK_S or st.held_p != port:
        st.held, st.held_t, st.held_p = port_holders(port), now, port
    mine = {st.proc.pid} if st.proc and st.proc.poll() is None else set()
    others = [x for x in st.held if x["pid"] not in mine and x["pid"] != os.getpid()]
    return dict(port=port, busy=bool(others), others=others, own=any(x["pid"] in mine for x in st.held))


def launch(st):
    with st.lock:
        if st.running:
            return False, "A test is already running"
    st.held_t = 0.0
    if not st.simulator:
        if not PROGRAM.exists():
            return False, f"{PROGRAM} does not exist"
        who = ", ".join(f"{x['cmd']} (pid {x['pid']})" for x in port_holders(st.port))
        if who:
            st.system("port_busy", text=f"The port {st.port} is held by {who}. Close it and start again.")
            return False, "Port busy"
    script = SIMULATOR if st.simulator else PROGRAM
    cmd = [sys.executable, "-u", str(script), "--events", "--no-open", "-p", st.port, "--out", str(st.reports)]
    cmd += st.extra
    try:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
                             cwd=str(script.parent), start_new_session=True)
    except OSError as e:
        st.system("error", text=f"Could not start the test: {e}")
        return False, str(e)
    with st.lock:
        st.proc, st.running, st.stopped = p, True, False
        st.run_id += 1
        rid = st.run_id
        st.events = []
    st.system("launched", run=rid, simulator=st.simulator)
    threading.Thread(target=reader, args=(st, p), daemon=True).start()
    return True, "ok"


def reader(st, p):
    saw_end = False
    for line in p.stdout:
        line = line.rstrip("\n")
        if line.startswith(EVENT_PREFIX):
            try:
                ev = json.loads(line[len(EVENT_PREFIX):])
            except ValueError:
                ev = dict(type="log", text=line)
            if ev.get("type") == "end":
                saw_end = True
        elif line.strip():
            ev = dict(type="log", text=line)
        else:
            continue
        st.publish(ev)
    code = p.wait()
    with st.lock:
        st.running = False
        stopped = st.stopped
    if stopped:
        st.system("stopped", text="Test stopped.")
    elif not saw_end:
        st.system("died", code=code, text=f"The test ended unexpectedly (code {code}) without a final report.")
    st.system("finished", code=code)


def stop(st):
    with st.lock:
        p = st.proc
        alive = st.running and p is not None and p.poll() is None
    if not alive:
        return False
    st.stopped = True
    try:
        os.killpg(p.pid, signal.SIGTERM)
    except OSError:
        p.terminate()

    def kill():
        try:
            p.wait(timeout=STOP_GRACE_S)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except OSError:
                p.kill()
    threading.Thread(target=kill, daemon=True).start()
    return True


def history(st, n=25):
    out = []
    if not st.reports.is_dir():
        return out
    for d in sorted((x for x in st.reports.iterdir() if x.is_dir()), reverse=True)[:n]:
        date, _, serial = d.name.partition("_")
        verdict, fails = None, []
        try:
            j = json.loads((d / "results.json").read_text())
            verdict = j.get("verdict")
            for s in (j.get("steps") or {}).values():
                if isinstance(s, dict) and s.get("status") not in (None, "PASS"):
                    fails.append(s.get("id") or s.get("name"))
        except (OSError, ValueError, AttributeError):
            pass
        rep = d / "report.html"
        out.append(dict(folder=d.name, date=date, serial=serial, verdict=verdict, fails=fails,
                        report=str(rep) if rep.exists() else None, simple=f"/reports/{d.name}/simple.html"))
    return out


def allowed(st, path):
    """A file the page may serve: only under this page's reports directory."""
    try:
        f = Path(path).resolve()
        f.relative_to(st.reports.resolve())
    except (OSError, ValueError, RuntimeError):
        return None
    if f.is_file() and f.stat().st_size <= MAX_FILE_BYTES:
        return f
    return None


def handler(st):
    class H(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass

        def send(self, code, body, ctype="application/json"):
            if isinstance(body, (dict, list)):
                body = json.dumps(body, ensure_ascii=False)
            if isinstance(body, str):
                body = body.encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            if u.path == "/":
                self.send(200, (HERE / "page.html").read_bytes(), "text/html; charset=utf-8")
            elif u.path == "/history":
                self.send(200, history(st))
            elif u.path == "/info":
                self.send(200, dict(simulator=st.simulator, port=st.port, running=st.running,
                                    reports=str(st.reports)))
            elif u.path == "/port":
                self.send(200, port_state(st, q.get("p", [None])[0]))
            elif u.path == "/file":
                f = allowed(st, q.get("path", [""])[0])
                if not f:
                    self.send(404, dict(error="not found"))
                else:
                    self.send(200, f.read_bytes(), TYPES.get(f.suffix.lower(), "application/octet-stream"))
            elif u.path == "/events":
                self.sse()
            elif u.path in ("/reports", "/reports/", "/reports/index.html"):
                try:
                    reports.write(st.reports)
                except OSError as e:
                    self.send(500, dict(error=str(e)))
                    return
                self.send(200, (st.reports / "index.html").read_bytes(), "text/html; charset=utf-8")
            elif u.path.startswith("/reports/"):
                self.report(u.path[len("/reports/"):])
            else:
                self.send(404, dict(error="no such page"))

        def do_POST(self):
            u = urlparse(self.path)
            n = int(self.headers.get("Content-Length") or 0)
            try:
                data = json.loads(self.rfile.read(n) or b"{}")
            except ValueError:
                data = {}
            if u.path == "/launch":
                if data.get("port") and not st.simulator:
                    st.port = str(data["port"])
                ok, msg = launch(st)
                self.send(200, dict(ok=ok, msg=msg))
            elif u.path == "/stop":
                self.send(200, dict(ok=stop(st)))
            else:
                self.send(404, dict(error="no such page"))

        def report(self, rest):
            """/reports/<folder>/ and its files (simple.html, report.html, pictures), inside the reports directory only."""
            parts = [x for x in rest.split("/") if x]
            if not parts or any(x in ("..", ".") or x.startswith(".") for x in parts):
                self.send(404, dict(error="no such page"))
                return
            base = st.reports.resolve()
            d = (base / parts[0]).resolve()
            if d.parent != base or not d.is_dir():
                self.send(404, dict(error="no such page"))
                return
            if len(parts) == 1 or parts[1] == "simple.html":
                try:
                    reports.write(base, d.name)
                except OSError as e:
                    self.send(500, dict(error=str(e)))
                    return
                parts = [parts[0], "simple.html"]
            f = (d / "/".join(parts[1:])).resolve()
            if d not in f.parents or not f.is_file():
                self.send(404, dict(error="no such page"))
                return
            self.send(200, f.read_bytes(), TYPES.get(f.suffix.lower(), "application/octet-stream"))

        def sse(self):
            q = queue.Queue()
            with st.lock:
                earlier = list(st.events)
                st.subs.append(q)
            try:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Connection", "keep-alive")
                self.end_headers()
                self.wfile.write(b"retry: 1000\n\n")
                for ev in earlier:
                    self.wfile.write(b"data: " + json.dumps(ev, ensure_ascii=False).encode() + b"\n\n")
                self.wfile.flush()
                while True:
                    try:
                        ev = q.get(timeout=SSE_PING_S)
                        self.wfile.write(b"data: " + json.dumps(ev, ensure_ascii=False).encode() + b"\n\n")
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
            except OSError:
                pass
            finally:
                with st.lock:
                    if q in st.subs:
                        st.subs.remove(q)
    return H


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        if not isinstance(sys.exc_info()[1], (ConnectionError, TimeoutError)):   # a closed tab is no error
            super().handle_error(request, client_address)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    extra = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description="The station's page for the final test.")
    ap.add_argument("--simulator", action="store_true", help="run ui/test_dot_sim.py and its fake Kode")
    ap.add_argument("--http-port", type=int, default=HTTP_PORT)
    ap.add_argument("-p", "--port", default=DEFAULT_PORT, help="the Kode's USB port")
    ap.add_argument("--out", help="reports directory (default ~/kode-factory/reports, sim-reports for the simulator)")
    ap.add_argument("--no-open", action="store_true", help="do not open the browser")
    a = ap.parse_args(argv)
    st = State(a.simulator, a.port, a.out, extra)
    try:
        srv = Server(("127.0.0.1", a.http_port), handler(st))
    except OSError as e:
        sys.exit(f"Cannot use the port {a.http_port} ({e}). Another program holds it; try --http-port {a.http_port + 1}")
    url = f"http://localhost:{a.http_port}"
    print(f"Station page on {url}" + (" (SIMULATOR)" if a.simulator else ""), flush=True)
    if not a.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop(st)
    return 0


if __name__ == "__main__":
    sys.exit(main())
