#!/usr/bin/env python3
"""Publishing to the Market (M25, step 6): build/host/test_github runs
src/net/github.c against a fake GitHub API served here, with the endpoints
it uses (user, forks made in the background, merge-upstream, refs,
contents, pulls), then the repositories are checked: the owner gets a
branch in the market, anyone else a fork; an update keeps the name of the
.bm already there; a bad token and a failing call give their message.
Then the reports (github_put): a file on the reports branch of
f-accomando/bm, the branch made from the main one the first time."""
import base64
import hashlib
import http.server
import json
import os
import re
import socketserver
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mkmarket  # noqa: E402

TOKENS = {"tok-owner": "f-accomando", "tok-friend": "friend"}
MARKET = "f-accomando/bm-market"
BM = "f-accomando/bm"
fails = 0


def check(ok, what):
    global fails
    print(("ok  " if ok else "FAIL") + " " + what)
    fails += not ok


def sha(data):
    return hashlib.sha1(data).hexdigest()


class Repo:
    def __init__(self, files=None):
        self.branches = {"main": dict(files or {})}     # branch -> {path: bytes}

    def head(self, branch):
        return sha(json.dumps(sorted((p, sha(d)) for p, d in self.branches[branch].items())).encode())


class World:
    def __init__(self):
        self.repos = {MARKET: Repo({"README.md": b"bm Market\n",
                                    "games/pong/pong.bm": b"old pong", "games/pong/info.txt": b"version: 1\n"}),
                      BM: Repo({"README.md": b"bm\n"})}
        self.pulls = []
        self.fork_wait = 0          # GETs of a new fork's ref before it exists
        self.fail_put = False
        self.calls = []
        self.messages = []


W = World()


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def reply(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def user(self):
        auth = self.headers.get("Authorization", "")
        return TOKENS.get(auth[7:]) if auth.startswith("Bearer ") else None

    def body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}")

    def route(self, method):
        W.calls.append((method, self.path))
        login = self.user()
        if not login:
            return self.reply(401, {"message": "Bad credentials"})
        if not self.headers.get("User-Agent") or not any("github" in a for a in self.headers.get_all("Accept", [])):
            return self.reply(400, {"message": "headers"})
        path, _, query = self.path.partition("?")
        if method == "GET" and path == "/user":
            return self.reply(200, {"login": login, "id": 7, "name": "Someone"})
        m = re.match(r"^/repos/([^/]+/[^/]+)(/.*)?$", path)
        if not m or m.group(1) not in W.repos and not (method == "POST" and m.group(2) == "/forks"):
            return self.reply(404, {"message": "Not Found"})
        full, rest = m.group(1), m.group(2) or ""
        if method == "POST" and rest == "/forks":
            fork = f"{login}/bm-market"
            if fork not in W.repos:
                W.repos[fork] = Repo(W.repos[MARKET].branches["main"])
                W.fork_wait = 2
            return self.reply(202, {"id": 9, "name": "bm-market", "full_name": fork,
                                    "owner": {"login": login}, "parent": {"full_name": MARKET}})
        repo = W.repos[full]
        if method == "GET" and rest == "":
            return self.reply(200, {"full_name": full, "default_branch": "main"})
        if method == "GET" and rest.startswith("/git/ref/heads/"):
            if W.fork_wait and full != MARKET:
                W.fork_wait -= 1
                return self.reply(404, {"message": "Not Found"})
            b = rest[len("/git/ref/heads/"):]
            if b not in repo.branches:
                return self.reply(404, {"message": "Not Found"})
            return self.reply(200, {"ref": f"refs/heads/{b}", "node_id": "x",
                                    "object": {"sha": repo.head(b), "type": "commit"}})
        if method == "POST" and rest == "/merge-upstream":
            repo.branches["main"] = dict(W.repos[MARKET].branches["main"])
            return self.reply(200, {"message": "Successfully fetched and fast-forwarded"})
        if method == "POST" and rest == "/git/refs":
            body = self.body()
            name = body["ref"][len("refs/heads/"):]
            if name in repo.branches:
                return self.reply(422, {"message": "Reference already exists"})
            src = [b for b in repo.branches if repo.head(b) == body["sha"]]
            if not src:
                return self.reply(422, {"message": "Object does not exist"})
            repo.branches[name] = dict(repo.branches[src[0]])
            return self.reply(201, {"ref": body["ref"], "object": {"sha": body["sha"]}})
        if rest.startswith("/contents/"):
            p = rest[len("/contents/"):]
            if method == "GET":
                ref = re.search(r"ref=([^&]+)", query)
                files = repo.branches[ref.group(1) if ref else "main"]
                if p in files:                      # a file: its record
                    return self.reply(200, {"name": p.rsplit("/", 1)[-1], "path": p, "sha": sha(files[p]),
                                            "size": len(files[p]), "type": "file",
                                            "html_url": f"https://github.com/{full}/blob/{ref.group(1)}/{p}"})
                inside = sorted(f for f in files if f.startswith(p + "/"))
                if not inside:
                    return self.reply(404, {"message": "Not Found"})
                return self.reply(200, [{"name": f[len(p) + 1:], "path": f, "sha": sha(files[f]),
                                         "size": len(files[f]), "type": "file"} for f in inside])
            if method == "PUT":
                body = self.body()
                if W.fail_put:
                    return self.reply(422, {"message": "Invalid request. content is too big"})
                files = repo.branches.get(body.get("branch"))
                if files is None:
                    return self.reply(404, {"message": "Branch not found"})
                if p in files and "sha" not in body:   # as GitHub answers it
                    return self.reply(422, {"message": "Invalid request.\n\n\"sha\" wasn't supplied."})
                if p in files and body.get("sha") != sha(files[p]):
                    return self.reply(409, {"message": f"{p} does not match"})
                if p not in files and "sha" in body:
                    return self.reply(422, {"message": "sha given for a new file"})
                code = 200 if p in files else 201
                files[p] = base64.b64decode(body["content"])
                W.messages.append(body["message"])
                return self.reply(code, {"content": {"path": p, "sha": sha(files[p]),
                                                     "html_url": f"https://github.com/{full}/blob/{body['branch']}/{p}"}})
        if method == "POST" and rest == "/pulls":
            body = self.body()
            W.pulls.append(body)
            n = len(W.pulls)
            return self.reply(201, {"url": "api", "id": 3, "html_url": f"https://github.com/{MARKET}/pull/{n}",
                                    "number": n})
        return self.reply(404, {"message": "Not Found"})

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PUT(self):
        self.route("PUT")


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


srv = Server(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
port = str(srv.server_address[1])


def run(token, gid, name, cart, title, version, branch):
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(cart)
    r = subprocess.run([sys.argv[1], port, token, gid, name, f.name, title, version, branch],
                       capture_output=True, text=True, timeout=60)
    os.remove(f.name)
    out = r.stdout
    check("idfail" not in out, "ids from file names" + ("" if "idfail" not in out else ": " + out))
    m = re.search(r"^(url|error) (.*)$", out, re.M)
    return (m.group(1), m.group(2), out) if m else ("crash", r.stderr, out)


big = os.urandom(1_300_000)                 # a big cartridge: the body is ~1.7 MB of base64
kind, what, out = run("tok-owner", "snake", "snake.bm", big, "Snake", "2026.10.01", "snake-1")
check(kind == "url" and what == f"https://github.com/{MARKET}/pull/1", "owner: the pull request's address")
m = W.repos[MARKET].branches
check("snake-1" in m and m["snake-1"]["games/snake/snake.bm"] == big, "owner: a branch in the market, the .bm intact")
check(b"version: 2026.10.01" in m["snake-1"]["games/snake/info.txt"] and
      b'"quoted"' in m["snake-1"]["games/snake/info.txt"], "owner: info.txt")
check("games/snake/snake.bm" not in m["main"], "owner: main untouched")
pr = W.pulls[0]
check(pr["head"] == "snake-1" and pr["base"] == "main" and pr["title"] == "Add Snake 2026.10.01" and
      "games/snake/" in pr["body"], "owner: head, base, title, body")
check(not any(p == "/repos/f-accomando/bm-market/forks" for _, p in W.calls), "owner: no fork")
check("step token of f-accomando" in out and "step sending snake.bm" in out, "progress lines")

# the files of the pull request pass the market's own check
with tempfile.TemporaryDirectory() as d:
    gd = os.path.join(d, "games", "snake")
    os.makedirs(gd)
    for p, data in m["snake-1"].items():
        if p.startswith("games/snake/"):
            with open(os.path.join(gd, os.path.basename(p)), "wb") as f:
                f.write(data)
    info = mkmarket.read_info(os.path.join(gd, "info.txt"))
    check(info == {"version": "2026.10.01", "license": "MIT", "about": 'Sent by the test, "quoted".'},
          "info.txt reads as mkmarket reads it")

# someone else: a fork (made in the background), then a pull request from it
W.calls.clear()
kind, what, out = run("tok-friend", "pong", "Pong_v2.bm", b"new pong bytes", "Pong", "2", "pong-2")
check(kind == "url" and what.endswith("/pull/2"), "friend: the pull request")
fork = W.repos.get("friend/bm-market")
check(fork is not None and "pong-2" in fork.branches, "friend: a branch in the fork")
check(fork and fork.branches["pong-2"].get("games/pong/pong.bm") == b"new pong bytes" and
      "games/pong/Pong_v2.bm" not in fork.branches["pong-2"], "update: the .bm keeps the name already there")
check(W.pulls[1]["head"] == "friend:pong-2" and W.pulls[1]["title"] == "Update Pong 2", "friend: head and title")
check(("POST", "/repos/friend/bm-market/merge-upstream") in W.calls and "waiting for the fork" in out,
      "friend: waited for the fork, brought it up to date")
check("games/pong/pong.bm" in W.repos[MARKET].branches["main"] and
      W.repos[MARKET].branches["main"]["games/pong/pong.bm"] == b"old pong", "friend: the market untouched")

# errors: their message
kind, what, _ = run("tok-nobody", "x", "x.bm", b"x", "X", "1", "x-1")
check(kind == "error" and "the token is not valid" in what, f"bad token: {what}")
kind, what, _ = run("tok-owner", "snake", "snake.bm", b"y", "Snake", "2", "snake-1")
check(kind == "error" and "the branch: HTTP 422, Reference already exists" in what, f"branch taken: {what}")
W.fail_put = True
kind, what, _ = run("tok-owner", "snake", "snake.bm", b"y", "Snake", "3", "snake-3")
check(kind == "error" and "snake.bm: HTTP 422, Invalid request. content is too big" in what, f"PUT refused: {what}")
W.fail_put = False

# the reports: github_put on the reports branch of the bm repository


def put(token, branch, path, data):
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(data)
    r = subprocess.run([sys.argv[1], "put", port, token, BM, branch, path, f.name],
                       capture_output=True, text=True, timeout=60)
    os.remove(f.name)
    m = re.search(r"^(url|error) (.*)$", r.stdout, re.M)
    return (m.group(1), m.group(2), r.stdout) if m else ("crash", r.stderr, r.stdout)


report = b"bm report\nkind: gpu\n\nstep 1 ok\n"
path = "reports/bm-core/20261004-153012_gpu_pi-zero-w_v1.txt"
kind, what, out = put("tok-owner", "reports", path, report)
bm = W.repos[BM].branches
check(kind == "url" and what == f"https://github.com/{BM}/blob/reports/{path}", f"report: its address ({what})")
check("reports" in bm and bm["reports"].get(path) == report, "report: the reports branch made, the file in it")
check(path not in bm["main"] and "new branch reports" in out, "report: main untouched, the branch said")
check(W.messages[-1] == "report gpu from Pi Zero W (v1, bm-core)", "report: the commit's message")
path2 = "reports/bm-core/20261004-153513_log_pi-zero-w_v1.txt"
kind, what, out = put("tok-owner", "reports", path2, b"bm report\nkind: log\n\nboot\n")
check(kind == "url" and bm["reports"].get(path2) and "new branch" not in out, "report: a second one, the branch there")
kind, what, _ = put("tok-owner", "reports", path, b"again")
check(kind == "error" and "wasn't supplied" in what and bm["reports"][path] == report,
      f"report: the same name with other bytes refused ({what})")
# the same report again (its first send reached GitHub, the answer did not
# reach the console): already there, so sent, and the file untouched
n_msgs = len(W.messages)
kind, what, out = put("tok-owner", "reports", path, report)
check(kind == "url" and what == f"https://github.com/{BM}/blob/reports/{path}" and "was there already" in out
      and bm["reports"][path] == report and len(W.messages) == n_msgs,
      f"report: sent again, already there ({kind} {what})")
kind, what, _ = put("tok-nobody", "reports", "reports/x/y.txt", b"x")
check(kind == "error" and "the token is not valid" in what, f"report: bad token ({what})")
srv.shutdown()
r = subprocess.run([sys.argv[1], "1", "tok-owner", "a", "a.bm", sys.argv[1], "A", "1", "a-1"],
                   capture_output=True, text=True, timeout=30)
check("error GET /user: connection refused" in r.stdout, "no server: " + r.stdout.strip().splitlines()[-1])

print("github: all passed" if not fails else f"github: {fails} FAILED")
sys.exit(1 if fails else 0)
