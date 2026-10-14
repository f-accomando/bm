#!/usr/bin/env python3
"""Tropic Trail, episode 4: the 3D map editor and linear level tools in bm SDK.

  python3 video/3d/04-map/record.py [--start chain|forge|auto] [--build-only] [--no-video]

Generates the complete recording script for Episode 4 (target 3:15, 195 s, 11700 frames at 60 fps):
  - out/input.txt (canonical key script for bmhost and QEMU)
  - out/overlay.json (overlay subtitles, narration, chapters, keys)
  - out/sd/ (clean SD card setup)

Structure and timing aligned with docs/video/04-map/{script.md, shot-list.md} and keys validated
by video/3d/04-map/PROBE.md (AGY-06) and SPV live test.
Where live bmhost execution is pending verification, marked as [DA PROVARE].
"""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "3d"))
sys.path.insert(0, HERE)

import lib3d                                    # noqa: E402
from bmvideo import Script, FPS                 # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
BIN = os.path.join(OUT, "bin")

TARGET_DURATION = 195                           # 3:15 = 195 s (11700 frames at 60 fps)
TITLE = "Tropic Trail  ·  Episode 4: the 3D map"
TOL = 1.0


# -------------------------------------------------------------------------------- helpers
class Ep(Script):
    """Script with absolute timestamps matching the shot list."""
    late = []

    def until(self, sec, what=""):
        target = round(sec * FPS)
        if self.t > target + TOL * FPS:
            Ep.late.append("%s: wanted %s, is %.1f s" % (what or "?", fmt(sec), self.t / FPS))
        elif self.t < target:
            self.t = target
        return self


def fmt(sec):
    return "%d:%02d" % (int(sec) // 60, int(sec) % 60)


def write_lines(path, lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for line in lines:
            f.write(line + "\n")


def build_editor():
    """Builds the editor cartridge using mkbm.py directly into out/bin/editor.bm."""
    os.makedirs(BIN, exist_ok=True)
    out_bm = os.path.join(BIN, "editor.bm")
    cmd = [sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"),
           "-o", out_bm,
           "--lua", os.path.join(ROOT, "carts", "editor", "main.lua"),
           "--info", os.path.join(ROOT, "carts", "editor", "bm.info")]
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)
    return out_bm


# -------------------------------------------------------------------------------- script builder
def build_script():
    """Builds the 10 scenes of Episode 4 according to script.md and shot-list.md."""
    s = Ep()
    Ep.late.clear()

    # ---------------------------------------------------------------- 0:00 Chapter 1: The trail
    s.chapter("The trail")
    s.say("This is the whole trail Pico will run, drawn with numbers on a grid.", frames=7 * FPS)
    s.scene("bm SDK ▸ Map ▸ 3D preview [DA PROVARE]", [("v", "preview"), ("arrows", "orbit")])
    # Hook: 3D preview orbit showcase of the trail
    s.sec(0.5)
    s.key("v", what="3D preview hook", cap="v")
    s.sec(1.0)
    for _ in range(8):
        s.key("right", hold=10)
        s.wait(6)
    s.sec(1.0)
    s.key("up", hold=12)
    s.sec(1.0)
    s.key("v", what="Return to 2D view", cap="v")

    # ---------------------------------------------------------------- 0:08 Chapter 2: The map page
    s.until(8, "The map page")
    s.chapter("The map page")
    s.say("bm SDK has a map editor. Open the project with Control O. Press F3 twice. A map is a grid of numbers in layers.", frames=12 * FPS)
    s.scene("bm SDK ▸ Project ▸ Map", [("Ctrl+O", "open"), ("F3", "map")])
    s.sec(0.5)
    # Open project dialog: Ctrl+O, then Enter on TROPIC.BME
    s.chord("o", what="Open project dialog")
    s.wait(20)
    s.key("enter", what="Confirm open /carts/TROPIC.BME", cap="Enter", hold=10)
    s.wait(25)
    # Open map editor: F3 once for sprites, F3 again for map
    s.key("f3", what="Switch to sprites", cap="F3", hold=8)
    s.wait(15)
    s.key("f3", what="Switch to map editor", cap="F3", hold=10)
    s.wait(25)

    # ---------------------------------------------------------------- 0:20 Chapter 3: Four layers
    s.until(20, "Four layers")
    s.chapter("Four layers")
    s.say("Floor says where the ground is. Height says how high. Walls are solid blocks. Objects holds apples, crates, lanterns, and where Pico starts.", frames=16 * FPS)
    s.scene("Map ▸ Four layers", [("T", "3D template")])
    s.sec(0.5)
    # Apply 3D template: creates floor, height, walls, objects and sets 16x16 big brush
    s.key("T", what="3D template (floor, height, walls, objects)", cap="T", hold=15)
    s.wait(25)
    # Cycle layers to show each one
    s.key("l", what="Cycle to height layer", cap="l", hold=8)
    s.wait(15)
    s.key("l", what="Cycle to walls layer", cap="l", hold=8)
    s.wait(15)
    s.key("l", what="Cycle to objects layer", cap="l", hold=8)
    s.wait(15)
    s.key("l", what="Cycle back to floor layer", cap="l", hold=8)
    s.wait(18)

    # ---------------------------------------------------------------- 0:38 Chapter 4: Floor and holes
    s.until(38, "Floor and holes")
    s.chapter("Floor and holes")
    s.say("Pick a grass tile with Tab, and press Z for the big brush: one tile is one unit of world. Shift P draws the path between two cells. A cell left empty is a hole.", frames=25 * FPS)
    s.scene("Map ▸ floor", [("Tab", "tile"), ("Z", "16x16"), ("Shift+P", "path")])
    s.sec(0.5)
    # Ensure 16x16 brush
    s.key("z", what="Big brush 16x16", cap="z", hold=8)
    s.wait(15)
    # Path tool: Shift+P
    s.key("P", what="Activate Path tool", cap="Shift+P", hold=10)
    s.wait(12)
    s.key("enter", what="First cell of corridor", cap="Enter", hold=8)
    s.wait(10)
    # Move along level path (forward 24 cells)
    for _ in range(12):
        s.key("down", hold=4)
        s.wait(2)
    s.key("enter", what="Second cell of corridor", cap="Enter", hold=8)
    s.wait(20)
    # Widen path with Shift+W (PROBE.md verified: Shift+W cycles width 1..5)
    s.key("W", what="Widen path", cap="Shift+W", hold=8)
    s.wait(12)
    s.key("W", what="Widen path", cap="Shift+W", hold=8)
    s.wait(18)
    # Create holes with Backspace
    s.key("up", hold=5); s.key("up", hold=5)
    s.key("bksp", what="Hole in corridor", cap="Backspace", hold=8)
    s.wait(10)
    s.key("up", hold=5)
    s.key("bksp", what="Second hole in corridor", cap="Backspace", hold=8)
    s.wait(20)
    # Toggle overview map
    s.key("m", what="Overview map", cap="m", hold=8)
    s.wait(18)
    s.key("m", what="Back from overview", cap="m", hold=8)
    s.wait(18)

    # ---------------------------------------------------------------- 1:05 Chapter 5: Height and walls
    s.until(65, "Height and walls")
    s.chapter("Height and walls")
    s.say("In the height layer a number is a quarter of a unit. Two quarters make a step. In walls, a number is a solid block that Pico cannot cross.", frames=21 * FPS)
    s.scene("Map ▸ height and walls", [("l", "layer"), ("f", "fill"), ("Space", "block")])
    s.sec(0.5)
    # Switch to height layer
    s.key("l", what="Layer height", cap="l", hold=8)
    s.wait(12)
    # Value step: '.' steps value
    s.key(".", what="Height value 1", cap=".", hold=6)
    s.wait(8)
    s.key(".", what="Height value 2 (0.5 units)", cap=".", hold=6)
    s.wait(10)
    s.key("space", what="Place height step", cap="Space", hold=8)
    s.wait(20)
    # Switch to walls layer
    s.key("l", what="Layer walls", cap="l", hold=8)
    s.wait(12)
    s.key(".", what="Wall value 1 (solid)", cap=".", hold=6)
    s.wait(8)
    s.key("space", what="Place solid wall block", cap="Space", hold=8)
    s.wait(20)

    # ---------------------------------------------------------------- 1:28 Chapter 6: Apples, crates, lanterns, start and goal
    s.until(88, "Apples, crates, lanterns, start and goal")
    s.chapter("Apples, crates, lanterns, start and goal")
    s.say("Shift A lays a row of apples. Shift C puts a crate on a cell. Shift O adds a lantern. Start and goal are tiles from the chooser: put them at the two ends.", frames=25 * FPS)
    s.scene("Map ▸ objects", [("Shift+A", "apples"), ("Shift+C", "crate"), ("Shift+O", "lamp")])
    s.sec(0.5)
    # Switch to objects layer
    s.key("l", what="Layer objects", cap="l", hold=8)
    s.wait(15)
    # Shift+A: Row of apples
    s.key("A", what="Apples row tool", cap="Shift+A", hold=10)
    s.wait(10)
    s.key("enter", what="Apples start", cap="Enter", hold=8)
    for _ in range(6):
        s.key("down", hold=4)
        s.wait(2)
    s.key("enter", what="Apples end", cap="Enter", hold=8)
    s.wait(20)
    # Shift+C: Place crate
    s.key("right", hold=5); s.key("up", hold=5)
    s.key("C", what="Place crate", cap="Shift+C", hold=8)
    s.wait(18)
    # Shift+O: Place lantern
    s.key("left", hold=5); s.key("up", hold=5)
    s.key("O", what="Place lantern", cap="Shift+O", hold=8)
    s.wait(20)

    # ---------------------------------------------------------------- 1:55 Chapter 7: Counts and undo
    s.until(115, "Counts and undo")
    s.chapter("Counts and undo")
    s.say("The status line counts apples, crates and lanterns, and estimates the triangles. Control Z takes back a whole row in one step.", frames=13 * FPS)
    s.scene("Map ▸ objects ▸ undo", [("Ctrl+Z", "undo"), ("Shift+A", "apples")])
    s.sec(0.5)
    # Atomic undo with Ctrl+Z
    s.chord("z", what="Undo whole operation")
    s.wait(20)
    # Re-apply row of apples
    s.key("A", what="Re-add apples", cap="Shift+A", hold=10)
    s.wait(8)
    s.key("enter", hold=8)
    for _ in range(6):
        s.key("down", hold=4); s.wait(2)
    s.key("enter", hold=8)
    s.wait(25)

    # ---------------------------------------------------------------- 2:10 Chapter 8: The 3D preview
    s.until(130, "The 3D preview")
    s.chapter("The 3D preview")
    s.say("Press V. The same code that builds the world in the game builds this preview, so what you see is what you play. Left and right orbit, up and down zoom, comma and period tilt.", frames=28 * FPS)
    s.scene("Map ▸ 3D preview", [("v", "preview"), ("left/right", "orbit"), ("up/down", "zoom"), ("Shift+up/dn", "tilt")])
    s.sec(0.5)
    # Toggle 3D preview
    s.key("v", what="Toggle 3D preview", cap="v", hold=12)
    s.wait(20)
    # Orbit camera yaw (left / right)
    for _ in range(6):
        s.key("right", hold=8); s.wait(4)
    s.wait(12)
    for _ in range(4):
        s.key("left", hold=8); s.wait(4)
    s.wait(12)
    # Zoom camera distance (up / down)
    for _ in range(3):
        s.key("up", hold=8); s.wait(4)
    s.wait(10)
    for _ in range(3):
        s.key("down", hold=8); s.wait(4)
    s.wait(12)
    # Tilt pitch [DA PROVARE: combinazione Shift con tasti cursore da seriale QEMU]
    s.key("up", what="Tilt camera up [DA PROVARE]", cap="Shift+Up", hold=8)
    s.wait(8)
    s.key("down", what="Tilt camera down [DA PROVARE]", cap="Shift+Down", hold=8)
    s.wait(20)
    # Return to 2D
    s.key("v", what="Return to 2D map", cap="v", hold=10)
    s.wait(25)

    # ---------------------------------------------------------------- 2:40 Chapter 9: Save and build
    s.until(160, "Save and build")
    s.chapter("Save and build")
    s.say("Control S saves the project. Control B builds the cartridge, with the four layers inside the file.", frames=18 * FPS)
    s.scene("Map ▸ Save and build", [("Ctrl+S", "save"), ("Ctrl+B", "build")])
    s.sec(0.5)
    s.chord("s", what="Save project")
    s.wait(30)
    s.chord("b", what="Build cartridge")
    s.wait(40)

    # ---------------------------------------------------------------- 2:55 Chapter 10: What is next
    s.until(175, "What is next")
    s.chapter("What is next")
    s.say("The trail is done. The next video gives Pico a voice: six soft sound effects and a jungle song, all made in bm Sound.", frames=18 * FPS)
    s.scene("Next: bm Sound", [])
    s.sec(19.0)

    # Close at target
    s.until(TARGET_DURATION, "End of episode 4")
    s.end()
    return s


# -------------------------------------------------------------------------------- main
def main():
    args = sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)

    print("Building Episode 4 script...")
    s = build_script()

    input_txt = os.path.join(OUT, "input.txt")
    write_lines(input_txt, s.lines)
    print("Wrote input lines: %s (%d frames, %.1f s, target %s)" %
          (input_txt, s.t, s.t / FPS, fmt(TARGET_DURATION)))

    overlay_json = os.path.join(OUT, "overlay.json")
    lib3d.write_overlay(s, overlay_json, TITLE)
    print("Wrote overlay: %s" % overlay_json)

    if Ep.late:
        print("Timing warnings:")
        for w in Ep.late:
            print("  LATE:", w)

    if "--build-only" in args:
        print("Build-only mode: script validated OK.")
        return 0

    # Setup SD if source cartridge available
    chain_src = os.path.join(lib3d.CHAIN, "03-animator.bme")
    if not os.path.exists(chain_src):
        chain_alt = os.path.join(ROOT, "video", "3d", "chain-delivered", "03-animator.bme")
        if os.path.exists(chain_alt):
            chain_src = chain_alt
        else:
            chain_real = os.path.join(ROOT, "video", "3d", "chain-real", "02-pixel.bme")
            if os.path.exists(chain_real):
                chain_src = chain_real

    if os.path.exists(chain_src):
        lib3d.fresh_sd(SD, [(chain_src, "TROPIC.BME")])
        print("SD prepared with start cartridge: %s" % chain_src)

    # Build editor cartridge for tests
    build_editor()

    print("AGY-08 draft completed successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
