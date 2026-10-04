#!/usr/bin/env python3
"""
bmnet (src/script/bmnet.lua) between two bmhost on this PC: the cartridge
tests/bmnet/cart.lua on the LAN (BMHOST_NET_ID 0 and 1) and through the
relay (tools/overbit_relay.py). Both consoles must pass their checks and
give the same hash of the match.

  tests/bmnet/run.py BUILD_DIR
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")


def pack(build, name, prefix=""):
    src = os.path.join(build, "bmnet", name + ".lua")
    with open(src, "w") as f:
        f.write(prefix + open(os.path.join(HERE, "cart.lua")).read())
    out = os.path.join(build, "bmnet", name + ".bm")
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", out, "--lua", src,
                    "--title", "bmnet test"], check=True, capture_output=True)
    return out


def pair(build, cart):
    procs = []
    for k in range(2):
        env = dict(os.environ, BMHOST_NET_ID=str(k))
        procs.append(subprocess.Popen([os.path.join(build, "host", "bmhost-bin"), cart, "--seconds", "30",
                                       "--realtime"], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      text=True))
    return [p.communicate()[0] for p in procs]


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    os.makedirs(os.path.join(build, "bmnet"), exist_ok=True)
    fails = 0
    runs = [("lan", pack(build, "lan"), None)]
    relay = subprocess.Popen([sys.executable, os.path.join(ROOT, "tools", "overbit_relay.py"), "--port", "47391",
                              "--quiet"])
    try:
        runs.append(("relay", pack(build, "relay", 'RELAY = "127.0.0.1:47391"\n'), relay))
        for name, cart, _ in runs:
            logs = pair(build, cart)
            for who, log in zip(("host", "guest"), logs):
                m = re.search(r"bmnet: (\d+)/(\d+) checks passed", log)
                ok = bool(m) and m.group(1) == m.group(2)
                print(("ok   " if ok else "FAIL ") + f"{name} {who}: " + (m.group(0) if m else "no result"))
                if not ok:
                    fails += 1
                    print("\n".join(l for l in log.splitlines() if "bmnet" in l or "error" in l)[-3000:])
            hashes = [re.findall(r"bmnet hash (\d+) ([0-9a-f]+)", g) for g in logs]
            same = hashes[0] and hashes[0] == hashes[1]
            print(("ok   " if same else "FAIL ") + f"{name}: the same match on both consoles {hashes}")
            fails += 0 if same else 1
    finally:
        relay.kill()
    print(f"bmnet: {'all passed' if not fails else str(fails) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
