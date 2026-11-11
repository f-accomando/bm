#!/usr/bin/env python3
"""
timing_test.py - the exit test of phase 2: the sequencer of bm Tape keeps time whatever the frame rate.

  python3 tests/tape/timing_test.py build/host/bmhost-bin TEST_BM_60FPS TEST_BM_20FPS TEST_BM_7FPS

The three cartridges are test builds (TAPE_TEST=1, 3, 8 in carts/tape/build/build.py). They log every note the sequencer
queues with play_at: "TAPE_AT time track note velocity tick", the time in seconds from the transport start.
bmhost has no frame rate option: it runs a frame every 1/60 s of virtual time. The test builds update the sequencer every
N-th frame instead, which is the same as frames of N/60 s: 60, 20 and 7.5 frames a second (a frame of 133 ms is shorter
than the 150 ms look-ahead). The demo pattern (16 steps, 120 BPM) runs for 4 bars. Checks:
  - the three runs give the same list of events;
  - each event time equals its grid time, tick * 60 / (BPM * 24) seconds, within 1.5 ms (also with swing and another BPM,
    set with the encoders of the TEMPO page); a swung step is late by the swing ticks;
  - each loop of the pattern holds the same events as the first loop: none lost, none doubled;
  - Stop (Space) queues nothing more, and Space again starts at tick 0.
SONG mode: the test hook (F9) builds a 4-track song of 8 slots with a loop over the slots 1 to 6 (16 steps each, swing 1, 132 BPM,
patterns A to D), and P switches to the transport's SONG mode. The song runs for 2 loops (about 22 s). The same checks hold:
the same list of events at the three frame rates, each event on its grid time, every loop holds the events of the first loop
once (no loss, no double at the loop end), and all four tracks and all six slots sound.
"""
import os, re, subprocess, sys, tempfile

HOST = sys.argv[1]
CARTS = sys.argv[2:5]                       # 60, 20 and 7.5 frames a second
SPACE, F8, KEY_W, KEY_Q, F9, KEY_P = 0x2C, 0x41, 0x27, 0x26, 0x42, 0x31   # TEMPO is F8; encoders 2 and 1 up are 0 and 9; P is now the backslash
LOOP, BARS = 96, 4                          # ticks of the 16-step pattern, bars played
TOL = 0.0015

fails = []


def check(name, ok, detail=""):
    if not ok:
        fails.append(name)
    print(("ok   " if ok else "FAIL ") + name + ("  " + detail if detail and not ok else ""))


def press(frame, usage, n=1):
    """Script lines: the key is down for 2 frames, n times."""
    out = []
    for i in range(n):
        out += ["%d keys %d" % (frame + 6 * i, usage), "%d keys none" % (frame + 6 * i + 2)]
    return out


def run(cart, script, seconds):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(script) + "\n")
    p = subprocess.run([HOST, cart, "--input", f.name, "--seconds", str(seconds)], capture_output=True, text=True)
    os.unlink(f.name)
    ev = [(float(m[0]), int(m[1]), int(m[2]), int(m[3]), int(m[4]))
          for m in re.findall(r"TAPE_AT (\S+) (\d+) (\d+) (\d+) (\d+)", p.stdout + p.stderr)]
    ok = p.returncode == 0 and " ok" in p.stderr
    return ev, ok


def grid(bpm, swing, ticks):
    """The grid time of a tick, and whether it is a tick a step may start on."""
    return ticks * 60.0 / (bpm * 24), (ticks % 6 == 0 and (ticks // 6) % 2 == 0) or (ticks % 6 == swing and (ticks // 6) % 2 == 1)


def scenario(name, setup, bpm, swing, rates=(0, 1, 2), loop=LOOP, bars=BARS, seconds=9.6, minimum=20, slots=1):
    play = press(60, SPACE)
    base = None
    for r in rates:
        ev, ok = run(CARTS[r], setup + play, seconds)
        label = "%s, %s" % (name, ("60", "20", "7.5")[r] + " frames a second")
        check(label + ": the run ends without a Lua error", ok)
        ev = [e for e in ev if e[4] < loop * bars]
        check(label + ": the sequencer queued notes in all %d loops" % bars, len({e[4] // loop for e in ev}) == bars)
        worst = max((abs(e[0] - grid(bpm, swing, e[4])[0]) for e in ev), default=1)
        check(label + ": every event is on its grid time within 1.5 ms (worst %.3f ms)" % (worst * 1000), worst < TOL)
        check(label + ": every event is on a step tick, odd steps late by the swing", all(grid(bpm, swing, e[4])[1] for e in ev))
        loop0 = [(e[1:3] + e[3:4], e[4]) for e in ev if e[4] < loop]
        # the notes of a loop, tick relative to its start: the same in every loop (none lost, none doubled)
        same = all([((e[1], e[2], e[3]), e[4] - k * loop) for e in ev if k * loop <= e[4] < (k + 1) * loop] ==
                   [((e[1], e[2], e[3]), e[4]) for e in ev if e[4] < loop] for k in range(bars))
        check(label + ": every loop holds the events of the first loop once (%d each)" % len(loop0), same and len(loop0) > minimum)
        if slots > 1:
            check(label + ": all 4 tracks and all %d slots sound" % slots,
                  {e[1] for e in ev} == {1, 2, 3, 4} and {e[4] // 96 for e in ev if e[4] < loop} == set(range(slots)))
        check(label + ": no event is doubled", len(set(e[1:] for e in ev)) == len(ev))
        if base is None:
            base = ev
        else:
            check(label + ": the same events as the 60 frames a second run", ev == base)


# 1. the demo pattern: 120 BPM, no swing
scenario("120 BPM", [], 120, 0)
# 2. the TEMPO page (F8): swing 2 (encoder 2 up twice) and 123 BPM (encoder 1 up three times)
setup = press(5, F8) + press(12, KEY_W, 2) + press(26, KEY_Q, 3)
scenario("swing 2, 123 BPM", setup, 123, 2)

# 3. SONG mode: F9 builds the test song (132 BPM, swing 1), P switches to SONG mode; 6 slots of 96 ticks, 2 loops
scenario("SONG, 6 slots", press(5, F9) + press(20, KEY_P), 132, 1, loop=6 * 96, bars=2, seconds=24, minimum=60, slots=6)

# 4. Stop and start: Space at frame 60, Space at 180 (3 s later at 120 BPM), Space at 240; 3 more seconds
ev, ok = run(CARTS[0], press(60, SPACE) + press(180, SPACE) + press(240, SPACE), 7.5)
ticks = [e[4] for e in ev]
restart = [i for i in range(1, len(ticks)) if ticks[i] < ticks[i - 1]]
check("stop: the run ends without a Lua error", ok)
check("stop: Space again starts at tick 0 (one restart in the log)", len(restart) == 1 and ticks[restart[0]] == 0)
if restart:
    first, second = ev[:restart[0]], ev[restart[0]:]
    # the first run was stopped at 3 s: its last queued event is at most 150 ms ahead of the stop
    check("stop: no note is queued after the stop (last tick %d, %.3f s)" % (first[-1][4], first[-1][0]), first[-1][0] <= 3.0 + 0.15 + 0.07)
    n = min(len(first), len(second))
    check("stop: the second start plays the same events as the first", [e[1:] for e in first[:n]] == [e[1:] for e in second[:n]])

print("tape timing: %s" % ("FAIL" if fails else "ok"))
sys.exit(1 if fails else 0)
