#!/usr/bin/env python3
"""
bm_robot.py - Multi-agent, multi-session UDP controller for the Pi 1 GPIO desk robot.

Why it never conflicts with 2+ concurrent agents or sessions:
  1. Uses stateless UDP datagrams on port 3337 (SPILCD_UDP_PORT) instead of the
     single-client TCP console on port 3333. Any number of agents can fire packets
     at the exact same millisecond with zero locking or blocking.
  2. Every agent/worktree/terminal carries its own `<session_id>`. The Pi 1
     maintains an 8-slot session table with 20 s TTL, displays `NAG` (e.g. `2AG`)
     in the HUD when multiple agents are active, and arbitrates the animation
     by deterministic priority:
       MERGE (*DING!*) > DEBUGGING > COMPILING > TYPING CODE > THINKING > COFFEE
  3. When a branch is merged into `bm-core`, sends the `merge` event so the robot
     rings its desk bell (*DING!*) and increments TASKS (deduplicated on the Pi).

Usage:
  # Direct CLI command:
  python tools/bm_robot.py 192.168.1.208 -p 381648 --state compile --msg "make kernel.img"
  python tools/bm_robot.py 192.168.1.208 -p 381648 --state merge --msg "feat -> bm-core"

  # Demo with 2 concurrent agents + merge into bm-core:
  python tools/bm_robot.py 192.168.1.208 -p 381648 --demo-multi

  # Claude Code / Agent Hook mode (reads tool JSON from stdin):
  python tools/bm_robot.py --hook

  # Git post-merge hook (triggers *DING!* when merging into bm-core):
  python tools/bm_robot.py --git-post-merge
  python tools/bm_robot.py --install-git-hook
"""
import argparse
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import time

UDP_PORT = 3337
DEFAULT_HOST = os.environ.get("BM_ROBOT_HOST") or os.environ.get("BM_HOST") or "192.168.1.208"
DEFAULT_PASS = os.environ.get("BM_PASSWORD") or "381648"


def sanitize_token(s, max_len=15):
    s = re.sub(r"[^A-Za-z0-9_.-]", "", str(s or "ag"))
    return (s or "ag")[:max_len]


def sanitize_msg(s, max_len=34):
    s = re.sub(r"[\r\n\t]+", " ", str(s or "")).strip()
    return s[:max_len]


def send_robot_udp(host, password, session_id, state, msg="", port=UDP_PORT):
    """Fire-and-forget single UDP datagram; never blocks or raises on network issues."""
    sid = sanitize_token(session_id, 15)
    st = sanitize_token(state, 12).lower()
    clean_msg = sanitize_msg(msg, 34)
    pkt = f"BMROBO {password} {sid} {st}"
    if clean_msg:
        pkt += f" {clean_msg}"
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setblocking(False)
            sock.sendto(pkt.encode("utf-8", errors="replace"), (host, port))
        return True
    except OSError:
        return False


def git_current_branch():
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=1.5,
        ).strip()
        return out
    except Exception:
        return ""


def git_last_subject():
    try:
        out = subprocess.check_output(
            ["git", "log", "-1", "--pretty=%s"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=1.5,
        ).strip()
        return out
    except Exception:
        return ""


def derive_session_id(hook_data):
    """Produces a stable, short session ID unique to each concurrent agent/session."""
    raw = (
        hook_data.get("session_id")
        or hook_data.get("conversation_id")
        or os.environ.get("CLAUDE_SESSION_ID")
        or f"{os.getcwd()}:{os.getppid()}"
    )
    digest = hashlib.sha1(str(raw).encode("utf-8")).hexdigest()[:6]
    return f"ag-{digest}"


def is_bm_core_merge_cmd(cmd):
    """Detects commands that merge a branch into bm-core or push a merge to bm-core."""
    c = cmd.strip()
    if re.search(r"\bgit\s+merge\b", c):
        br = git_current_branch()
        if br == "bm-core" or "bm-core" in c:
            m = re.search(r"\bgit\s+merge\s+(?:--\S+\s+)*([^\s;]+)", c)
            src = m.group(1) if m else "branch"
            return f"{ os.path.basename(src) } -> bm-core"
    if re.search(r"\bgit\s+push\b.*\bbm-core\b", c):
        return "pushed -> bm-core"
    if re.search(r"\bgh\s+pr\s+merge\b", c):
        return "PR merged -> bm-core"
    return None


def classify_hook_event(data):
    """Maps a Claude/Agent tool event to (state, subtitle) for the desk robot."""
    event = str(data.get("hook_event_name") or "")
    if event in ("Stop", "SubagentStop", "SessionEnd"):
        return ("coffee", "session idle")

    tool = str(data.get("tool_name") or data.get("tool") or "")
    inp = data.get("tool_input") or {}
    if isinstance(inp, str):
        try:
            inp = json.loads(inp)
        except Exception:
            inp = {"command": inp}

    resp = data.get("tool_response") or {}
    exit_code = 0
    if isinstance(resp, dict):
        exit_code = resp.get("exit_code") or resp.get("returncode") or 0

    if tool in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
        fp = os.path.basename(str(inp.get("file_path") or inp.get("path") or "code"))
        return ("code", f"editing {fp}")

    if tool in ("Read", "Grep", "Glob", "LS", "WebSearch", "WebFetch", "Task"):
        target = inp.get("file_path") or inp.get("pattern") or inp.get("query") or tool
        return ("think", f"reading {os.path.basename(str(target))}")

    if tool == "Bash":
        cmd = str(inp.get("command") or "")
        merge_label = is_bm_core_merge_cmd(cmd)
        if merge_label and not exit_code:
            return ("merge", merge_label)
        if exit_code:
            return ("debug", f"exit {exit_code}: debugging")
        if re.search(r"\b(make|arm-none-eabi-gcc|aarch64-linux-gnu-gcc|mkbm\.py|mkassist\.py)\b", cmd):
            return ("compile", "building kernel/carts")
        if re.search(r"\b(qemu|pytest|gdb|bm_net\.py|consistency\.py|docs_audit\.py|test)\b", cmd):
            return ("debug", "testing & debugging")
        short_cmd = cmd.split("\n")[0].strip()[:28]
        return ("think", short_cmd or "running shell")

    if tool:
        return ("think", f"tool: {tool}")
    return (None, None)


def handle_git_post_merge(host, password, port):
    """Called by .git/hooks/post-merge: rings the bell if merged on bm-core."""
    br = git_current_branch()
    subj = git_last_subject()
    if br == "bm-core" or "bm-core" in subj:
        m = re.search(r"Merge branch '([^']+)'", subj)
        label = f"{m.group(1)} -> bm-core" if m else "merged -> bm-core"
        send_robot_udp(host, password, "git-hook", "merge", label, port=port)
        print(f"bm_robot: *DING!* ({label})")


def install_git_hook():
    git_dir = subprocess.check_output(["git", "rev-parse", "--git-dir"], text=True).strip()
    hook_path = os.path.join(git_dir, "hooks", "post-merge")
    os.makedirs(os.path.dirname(hook_path), exist_ok=True)
    line = 'python3 tools/bm_robot.py --git-post-merge >/dev/null 2>&1 || true\n'
    if os.path.exists(hook_path):
        content = open(hook_path, encoding="utf-8").read()
        if "bm_robot.py" not in content:
            with open(hook_path, "a", encoding="utf-8") as f:
                f.write("\n" + line)
    else:
        with open(hook_path, "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\n" + line)
    try:
        os.chmod(hook_path, 0o755)
    except OSError:
        pass
    print(f"Installed git post-merge hook at {hook_path}")


def run_multi_agent_demo(host, password, port):
    print(f"Simulating 2 concurrent agents on {host}:{port}...")
    print("  1) Agent-A starts THINKING (reading spilcd.c)...")
    send_robot_udp(host, password, "agent-A", "think", "reading spilcd.c", port=port)
    time.sleep(2.2)

    print("  2) Agent-B joins simultaneously TYPING CODE (2AG active, typing wins priority)...")
    send_robot_udp(host, password, "agent-A", "think", "reading spilcd.c", port=port)
    send_robot_udp(host, password, "agent-B", "code", "editing settings.c", port=port)
    time.sleep(2.2)

    print("  3) Agent-A starts COMPILING while Agent-B is still typing (2AG, compiling wins)...")
    send_robot_udp(host, password, "agent-A", "compile", "make build/kernel.img", port=port)
    send_robot_udp(host, password, "agent-B", "code", "editing settings.c", port=port)
    time.sleep(2.2)

    print("  4) Agent-B hits a test failure -> DEBUGGING (2AG, debugging wins priority)...")
    send_robot_udp(host, password, "agent-B", "debug", "fixing SPI timing", port=port)
    time.sleep(2.2)

    print("  5) Branch merged into bm-core -> *DING!* (bell latch + TASKS increment)...")
    send_robot_udp(host, password, "agent-A", "merge", "gpio-tft -> bm-core", port=port)
    print("Done! Watch the Pi 1 GPIO screen.")


def main():
    ap = argparse.ArgumentParser(description="Control the Pi 1 GPIO desk robot over stateless UDP.")
    ap.add_argument("host", nargs="?", default=DEFAULT_HOST, help="Pi 1 IP address")
    ap.add_argument("-p", "--password", default=DEFAULT_PASS, help="Console net_password")
    ap.add_argument("--port", type=int, default=UDP_PORT, help="UDP port (default 3337)")
    ap.add_argument("-s", "--state", help="think | code | compile | debug | merge | coffee | bye")
    ap.add_argument("-m", "--msg", default="", help="Subtitle shown in the top HUD bar")
    ap.add_argument("--session", default="cli", help="Session ID for multi-agent tracking")
    ap.add_argument("--hook", action="store_true", help="Read Claude/Agent hook JSON from stdin")
    ap.add_argument("--git-post-merge", action="store_true", help="Ring bell if current branch is bm-core")
    ap.add_argument("--install-git-hook", action="store_true", help="Install .git/hooks/post-merge")
    ap.add_argument("--demo-multi", action="store_true", help="Simulate 2 concurrent agents + bm-core merge")
    args = ap.parse_args()

    if args.install_git_hook:
        install_git_hook()
        return

    if args.git_post_merge:
        handle_git_post_merge(args.host, args.password, args.port)
        return

    if args.demo_multi:
        run_multi_agent_demo(args.host, args.password, args.port)
        return

    if args.hook:
        try:
            raw = sys.stdin.read()
            data = json.loads(raw) if raw.strip() else {}
        except Exception:
            data = {}
        sid = derive_session_id(data)
        st, msg = classify_hook_event(data)
        if st:
            send_robot_udp(args.host, args.password, sid, st, msg, port=args.port)
        return

    if args.state:
        ok = send_robot_udp(args.host, args.password, args.session, args.state, args.msg, port=args.port)
        if not ok:
            sys.exit(1)
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
