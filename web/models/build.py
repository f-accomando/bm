#!/usr/bin/env python3
"""Builds the test models of web/models/ (mock content, CC0, original).

    python3 web/models/build.py          rewrite NAME.bmm, NAME.png and the pages here
    python3 web/models/build.py --check  build in a temp folder, compare with the files here

Each model is a list of boxes (lo, hi, colour). The script gives them to the bm-forge tool
(.claude/skills/bm-forge: `mesh` makes the BMRES model, `preview` renders the 256x256 PNG).
The output is the same on every run: no dates, no random numbers.
Then it writes the page of each model and index.html from the same data.
Python 3.8 or later, standard library only.
"""
import html
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FORGE = os.path.join(HERE, "..", "..", ".claude", "skills", "bm-forge", "scripts", "bm_forge.py")
AUTHOR = "bm tests"
LICENCE = "Mock, made for bm tests, CC0"


def box(lo, hi, colour):
    return {"lo": lo, "hi": hi, "colour": colour}


HOUSE = [
    box([-0.6, 0.0, -0.5], [0.6, 0.7, 0.5], "#d8c8a0"),        # walls
    box([-0.7, 0.7, -0.6], [0.7, 0.8, 0.6], "#7a3b2a"),        # roof, 4 steps
    box([-0.55, 0.8, -0.5], [0.55, 0.9, 0.5], "#8a4430"),
    box([-0.4, 0.9, -0.4], [0.4, 1.0, 0.4], "#7a3b2a"),
    box([-0.25, 1.0, -0.3], [0.25, 1.1, 0.3], "#8a4430"),
    box([-0.12, 0.0, 0.5], [0.12, 0.45, 0.54], "#5a3a1e"),     # door
    box([-0.5, 0.35, 0.5], [-0.25, 0.55, 0.53], "#8ec8e8"),    # windows
    box([0.25, 0.35, 0.5], [0.5, 0.55, 0.53], "#8ec8e8"),
    box([0.3, 0.8, -0.3], [0.45, 1.2, -0.15], "#6a6a70"),      # chimney
]

TREE = [
    box([-0.1, 0.0, -0.1], [0.1, 0.6, 0.1], "#6b4423"),        # trunk
    box([-0.5, 0.6, -0.5], [0.5, 0.95, 0.5], "#2f7d32"),       # leaves, 3 layers
    box([-0.38, 0.95, -0.38], [0.38, 1.25, 0.38], "#3a9140"),
    box([-0.24, 1.25, -0.24], [0.24, 1.5, 0.24], "#46a64c"),
]

CRATE = [
    box([-0.4, 0.0, -0.4], [0.4, 0.8, 0.4], "#b98a4e"),        # body
    box([-0.42, 0.0, -0.42], [-0.34, 0.8, -0.34], "#7a5428"),  # four posts
    box([0.34, 0.0, -0.42], [0.42, 0.8, -0.34], "#7a5428"),
    box([-0.42, 0.0, 0.34], [-0.34, 0.8, 0.42], "#7a5428"),
    box([0.34, 0.0, 0.34], [0.42, 0.8, 0.42], "#7a5428"),
    box([-0.42, 0.76, -0.42], [0.42, 0.84, 0.42], "#7a5428"),  # lid
    box([-0.3, 0.3, 0.4], [0.3, 0.4, 0.44], "#7a5428"),        # front plank
]

ROBOT = [
    box([-0.3, 0.55, -0.2], [0.3, 1.05, 0.2], "#8a96a3"),      # body
    box([-0.2, 1.1, -0.15], [0.2, 1.4, 0.15], "#b0bcc8"),      # head
    box([-0.12, 1.2, 0.15], [-0.04, 1.3, 0.18], "#ff5030"),    # eyes
    box([0.04, 1.2, 0.15], [0.12, 1.3, 0.18], "#ff5030"),
    box([-0.02, 1.4, -0.02], [0.02, 1.55, 0.02], "#404850"),   # antenna
    box([-0.5, 0.65, -0.1], [-0.3, 1.0, 0.1], "#6c7884"),      # arms
    box([0.3, 0.65, -0.1], [0.5, 1.0, 0.1], "#6c7884"),
    box([-0.28, 0.0, -0.12], [-0.1, 0.55, 0.12], "#4a5560"),   # legs
    box([0.1, 0.0, -0.12], [0.28, 0.55, 0.12], "#4a5560"),
    box([-0.32, 0.0, -0.12], [-0.08, 0.1, 0.25], "#30383f"),   # feet
    box([0.08, 0.0, -0.12], [0.32, 0.1, 0.25], "#30383f"),
]

# name (the file: 1 to 8 of a-z 0-9 _ -), title, boxes, text
MODELS = [
    ("house", "House", HOUSE, "A small house with a stepped roof, a door, two windows and a chimney."),
    ("tree", "Tree", TREE, "A tree made of a trunk and three layers of leaves."),
    ("crate", "Crate", CRATE, "A wooden crate with corner posts and a lid."),
    ("walker", "Robot walker", ROBOT, "A robot with a head, two arms and two feet, standing still."),
]

STYLE = """body{font-family:sans-serif;max-width:40em;margin:1em auto;padding:0 1em;color:#222;background:#fff}
a{color:#0645ad}img{border:1px solid #999}
.card{display:inline-block;width:9em;margin:0 1em 1em 0;vertical-align:top}"""


def run(*args):
    subprocess.run([sys.executable, FORGE] + list(args), check=True, stdout=subprocess.DEVNULL)


def page(title, body):
    return ("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n"
            "<title>%s</title>\n<style>\n%s\n</style>\n</head>\n<body>\n%s</body>\n</html>\n"
            % (html.escape(title), STYLE, body))


def write(path, text):
    with open(path, "w", newline="\n") as f:
        f.write(text)


def build(out):
    tmp = tempfile.mkdtemp()
    try:
        info = {}
        for name, title, boxes, text in MODELS:
            src = os.path.join(tmp, name + ".json")
            write(src, json.dumps({"model": name, "boxes": boxes}, indent=1))
            bmm = os.path.join(out, name + ".bmm")
            run("mesh", src, "-o", bmm, "--title", title, "--author", AUTHOR)
            run("preview", bmm, "-o", os.path.join(out, name + ".png"), "--size", "256x256")
            info[name] = (os.path.getsize(bmm), 12 * len(boxes))
        cards = []
        for name, title, boxes, text in MODELS:
            cards.append("<div class=\"card\"><a href=\"%s.html\"><img src=\"%s.png\" width=\"128\" height=\"128\" "
                         "alt=\"%s\"></a><br><a href=\"%s.html\">%s</a></div>\n"
                         % (name, name, html.escape(title), name, html.escape(title)))
        idx = ("<h1>Models (test)</h1>\n<p>Test models for bm Web. %s. They are not real content.</p>\n%s"
               "<p><a href=\"../index.html\">Back to the bm web player</a></p>\n" % (LICENCE, "".join(cards)))
        write(os.path.join(out, "index.html"), page("Models (test)", idx))
        for name, title, boxes, text in MODELS:
            size, faces = info[name]
            body = ("<h1>%s</h1>\n<p><img src=\"%s.png\" width=\"256\" height=\"256\" alt=\"%s\"></p>\n"
                    "<p>%s</p>\n<ul>\n<li>File: %s.bmm</li>\n<li>Size: %d bytes</li>\n<li>Faces: %d</li>\n"
                    "<li>Licence: %s</li>\n</ul>\n<p><a href=\"%s.bmm\">Download</a></p>\n"
                    "<p><a href=\"index.html\">All models</a></p>\n"
                    % (html.escape(title), name, html.escape(title), html.escape(text), name, size, faces,
                       LICENCE, name))
            write(os.path.join(out, name + ".html"), page(title, body))
        write(os.path.join(out, "robots.txt"), "User-agent: *\nAllow: /\n")
    finally:
        shutil.rmtree(tmp)


def main():
    if "--check" in sys.argv:
        tmp = tempfile.mkdtemp()
        try:
            build(tmp)
            bad = 0
            for n in sorted(os.listdir(tmp)):
                a, b = os.path.join(tmp, n), os.path.join(HERE, n)
                if not os.path.exists(b) or open(a, "rb").read() != open(b, "rb").read():
                    print("differs: " + n)
                    bad += 1
            print("build.py --check: %s" % ("FAIL" if bad else "ok"))
            return 1 if bad else 0
        finally:
            shutil.rmtree(tmp)
    build(HERE)
    print("wrote the models in " + HERE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
