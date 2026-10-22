// Test of the browser runner (web/runner/bmweb.c) under Node, with the same hooks as web/player.js:
// the cartridge draws 60 frames, the pad moves its square, the sound mix comes at 800 frames of 48 kHz
// per video frame, and bmweb_quit ends the run.
// Run: node tests/webplayer/test_bmweb.js BUILD_DIR   (after make web-player and mkbm.py of play.lua)
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");

const build = path.resolve(process.argv[2] || "build");
const dir = path.join(build, "web", "player");
const cart = fs.readFileSync(path.join(build, "webtest", "play.bm"));
const createBmWeb = require(path.join(dir, "bmweb.js"));

let mod, frames = 0, audio = 0, pixels = null, w = 0, h = 0, ended = false;
const RIGHT = 2;

function squareLeft() {
  // the red square: first column with a red pixel below the text rows
  for (let x = 0; x < w; x++)
    for (let y = 100; y < h; y++) {
      const i = (y * w + x) * 4;
      if (pixels[i] > 200 && pixels[i + 1] < 120 && pixels[i + 2] < 120) return x;
    }
  return -1;
}

createBmWeb({
  bmFrame(ptr, fw, fh) {
    frames++; w = fw; h = fh;
    pixels = Buffer.from(mod.HEAPU8.buffer.slice(ptr, ptr + fw * fh * 4));
  },
  bmAudio(ptr, n) { audio += n; },
  bmPace() {
    if (frames === 90) { x0 = squareLeft(); mod._bmweb_input(RIGHT, 1, 0, 0, 0); }
    if (frames === 150) { x1 = squareLeft(); mod._bmweb_quit(); }
    return new Promise((r) => setImmediate(r));
  },
  bmDone() { ended = true; },
}).then((m) => {
  mod = m;
  const p = m._malloc(cart.length);
  m.HEAPU8.set(cart, p);
  m._bmweb_start(p, cart.length);
});

let x0 = -1, x1 = -1;
const t = setInterval(() => {
  if (!ended) return;
  clearInterval(t);
  assert.strictEqual(w + "x" + h, "640x360");
  assert.ok(frames >= 150 && frames < 200, "frames " + frames);
  assert.ok(x0 > 0 && x1 > x0 + 40, "the square moves right with the pad: " + x0 + " -> " + x1);
  assert.ok(Math.abs(audio / frames - 800) < 40, "audio frames per video frame " + audio / frames);
  console.log("bmweb: ok, " + frames + " frames, square " + x0 + " -> " + x1 + ", " + audio + " audio frames");
}, 50);
setTimeout(() => { console.error("bmweb: FAIL timeout"); process.exit(1); }, 60000).unref();
