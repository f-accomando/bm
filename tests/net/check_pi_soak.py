#!/usr/bin/env python3
"""tools/pi_soak.py against a fake bridge (a Unix socket in a thread): the verdicts, the exit codes, the requests it makes.
Needs Unix sockets: run it in WSL."""
import contextlib
import io
import os
import shutil
import socket
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))

fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


UP = ("status: up - bm v0.2.5 at 192.168.1.108\nprevious run: ended cleanly, or the power went (no record in RAM)\n"
      "checked: 2026-10-16T12:00:00")
DOWN = "status: down - no answer from 192.168.1.108:3333 (off, restarting, frozen, or another client connected)"
FREEZE = UP.replace("ended cleanly, or the power went (no record in RAM)",
                    "ended without a clean exit (a freeze, or a restart) while: frame, 1800 s after boot, kernel x")
FPS = 'bm: "Stages" 108000 frames, 59.8 fps, 3 dropped'
FPS_LOW = 'bm: "Stages" 108000 frames, 41.0 fps, 3 dropped'
BLOCKED = "[bm_net] the keys go to the application (Ctrl-\\ back to the menu): back to the menu first (Ctrl-Esc on the console)"


class Bridge:
    """Answers each request with script(req); the requests seen, in order."""

    def __init__(self, path, script):
        self.script, self.seen = script, []
        self.srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.srv.bind(path)
        self.srv.listen(8)
        self.t = threading.Thread(target=self.loop, daemon=True)
        self.t.start()

    def loop(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            req = b""
            while not req.endswith(b"\n"):
                d = c.recv(4096)
                if not d:
                    break
                req += d
            r = req.decode().strip()
            self.seen.append(r)
            c.sendall(self.script(r).encode())
            c.close()

    def close(self):
        self.srv.close()


def run(pi_soak, tmp, script, args, sock=None):
    """(exit code, stdout lines, requests seen) of one pi_soak.main against a fake bridge."""
    path = sock or os.path.join(tmp, "b.sock")
    br = None
    if sock is None:
        br = Bridge(path, script)
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        rc = pi_soak.main(args + ["--sock", path], sleep=lambda s: None, now=lambda: 0.0)
    if br:
        br.close()
        os.unlink(path)
    return rc, out.getvalue().strip().splitlines(), (br.seen if br else [])


def seq_script(reboot_down=2, status_down_at=None, fps=FPS, lastrun=FPS, after=UP, keys=None, play=None):
    """A Pi that is up, plays, and (after 'line reboot') is down reboot_down times, then comes back with `after`."""
    st = {"status": 0, "rebooted": False, "down_left": reboot_down}

    def script(req):
        if req == "status":
            st["status"] += 1
            if st["rebooted"]:
                if st["down_left"] > 0:
                    st["down_left"] -= 1
                    return DOWN
                return after
            if st["status"] == status_down_at:
                return DOWN
            return UP
        if req.startswith("play "):
            return play if play is not None else "123456 bytes in 2.0 s"
        if req == "keys game-exit":
            return "ok"
        if req == "keys monitor":
            return keys if keys is not None else fps
        if req == "line reboot":
            st["rebooted"] = True
            return "rebooting..."
        if req == "logs lastrun":
            return "log\n" + lastrun + "\nmore"
        return "refused: " + req
    return script


def main():
    if not hasattr(socket, "AF_UNIX"):
        print("pi_soak: skipped (needs Unix sockets, run it in WSL)")
        return 0
    import pi_soak
    tmp = tempfile.mkdtemp(prefix="pisoak-")
    cart = os.path.join(tmp, "game.bm")
    base = ["--cart", cart, "--minutes", "30", "--every", "60"]
    try:
        # 1. everything fine, with the reboot
        rc, out, seen = run(pi_soak, tmp, seq_script(), base + ["--reboot"])
        check(rc == 0 and len(out) == 3 and all(l.startswith("soak: ") for l in out), "1: all fine -> exit 0, three soak: lines")
        check("verdict OK" in out[-1] and "up 30 down 0" in out[0] and "59.8 fps" in out[1], "1: verdict OK, 30 checks up, the fps line")
        order = [r for r in seen if r.startswith(("play ", "keys ", "line ", "logs "))]
        check(order == ["play " + os.path.abspath(cart), "keys game-exit", "keys monitor", "line reboot", "logs lastrun"],
              "1: play (absolute path), keys game-exit, keys monitor, line reboot, logs lastrun, in that order")
        check(seen.count("status") == 1 + 30 + 3, "1: a status at the start, 30 checks, 3 while the Pi restarts")

        # 2. one check down: the session goes on, the verdict fails
        rc, out, seen = run(pi_soak, tmp, seq_script(status_down_at=6), base)
        check(rc == 1 and "up 29 down 1" in out[0] and "verdict FAIL" in out[-1] and "1 status down" in out[-1],
              "2: one status down -> exit 1, FAIL")
        check(seen.count("status") == 1 + 30, "2: all the checks were made")

        # 3. fps below the threshold
        rc, out, seen = run(pi_soak, tmp, seq_script(fps=FPS_LOW), base + ["--min-fps", "55"])
        check(rc == 1 and "below" in out[-1], "3: fps 41.0 under 55 -> exit 1, below")

        # 4. the run before was a freeze
        rc, out, seen = run(pi_soak, tmp, seq_script(after=FREEZE), base + ["--reboot"])
        check(rc == 1 and "previous run: ended without a clean exit" in out[-1], "4: previous run frozen -> exit 1")

        # 5. no fps line anywhere
        rc, out, seen = run(pi_soak, tmp, seq_script(fps="nothing here", lastrun="nothing"), base + ["--reboot"])
        check(rc == 1 and out[1] == "soak: no fps line" and "fps line missing" in out[-1], "5: no fps line -> exit 1")

        # 6. no reboot: the fps are enough, nothing is rebooted or read after
        rc, out, seen = run(pi_soak, tmp, seq_script(), base)
        check(rc == 0 and "line reboot" not in seen and "logs lastrun" not in seen and "previous run not checked" in out[-1],
              "6: without --reboot -> exit 0, no reboot, no log, previous run not checked")

        # 7. the Pi is down at the start
        rc, out, seen = run(pi_soak, tmp, lambda r: DOWN, base)
        check(rc == 3 and not any(r.startswith("play") for r in seen), "7: down at the start -> exit 3, no play")

        # 8. fps equal to the threshold passes
        rc, out, seen = run(pi_soak, tmp, seq_script(), base + ["--min-fps", "59.8"])
        check(rc == 0, "8: fps equal to --min-fps -> exit 0")

        # 9. the game cannot be ended from the PC
        rc, out, seen = run(pi_soak, tmp, seq_script(keys=BLOCKED), base + ["--reboot"])
        check(rc == 1 and "cannot end the game from the PC" in out[-1] and "line reboot" not in seen,
              "9: keys monitor blocked -> exit 1, no reboot")

        # 10. play refused
        rc, out, seen = run(pi_soak, tmp, seq_script(play="refused: not inside the repo"), base)
        check(rc == 1 and "play failed" in out[-1] and not any(r.startswith("keys") for r in seen),
              "10: play refused -> exit 1, no keys")

        # 11. usage: no whole --every in --minutes; no bridge
        rc, out, seen = run(pi_soak, tmp, seq_script(), ["--cart", cart, "--minutes", "0.1", "--every", "60"])
        check(rc == 2 and seen == [], "11: minutes under one --every -> exit 2, nothing asked")
        rc, out, seen = run(pi_soak, tmp, None, base, sock=os.path.join(tmp, "nobody.sock"))
        check(rc == 2, "11: no bridge socket -> exit 2")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("pi_soak: all passed" if not fails else "pi_soak: %d FAILED" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
