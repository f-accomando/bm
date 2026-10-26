// Test of web/bmcart.js: a cartridge built in memory, bad files, and (when
// Python is there) a cartridge packed by scripts/mkbm.py.
// Run: node tests/webplayer/test_bmcart.js
"use strict";
const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const cp = require("child_process");
const bmCart = require("../../web/bmcart.js");

function build(opts) {
  opts = opts || {};
  const lua = Buffer.from("function _draw() cls() end\n");
  const info = Buffer.from("name: Test\nauthor: me\nlicense: MIT\n[sprite a]\nx: 1\n");
  const cover = Buffer.alloc(4 + 2 * 2 * 4);
  cover.writeUInt16LE(2, 0); cover.writeUInt16LE(2, 2); cover.fill(0x7f, 4);
  const bodies = [[4, cover], [1, lua], [10, info]];
  if (opts.noLua) bodies.splice(1, 1);
  let off = 128 + bodies.length * 16;
  const table = Buffer.alloc(bodies.length * 16), blobs = [];
  bodies.forEach(([type, b], i) => {
    table.writeUInt32LE(type, i * 16); table.writeUInt32LE(off, i * 16 + 4); table.writeUInt32LE(b.length, i * 16 + 8);
    const pad = (4 - (b.length & 3)) & 3;
    blobs.push(b, Buffer.alloc(pad)); off += b.length + pad;
  });
  const head = Buffer.alloc(128);
  head.write("BMCART\0\0", 0, "latin1");
  head.writeUInt16LE(1, 8); head.writeUInt16LE(128, 10);
  head.writeUInt16LE(opts.w || 360, 12); head.writeUInt16LE(opts.h || 360, 14);
  head[16] = 1; head[17] = bodies.length; head.writeUInt16LE(opts.flags || 0, 18);
  head.write("Test Title", 24, "utf8"); head.write("Ada", 72, "utf8");
  const file = Buffer.concat([head, table, ...blobs]);
  file.writeUInt32LE((bmCart.crc32(file, 128, file.length) ^ (opts.badCrc ? 1 : 0)) >>> 0, 20);
  return file;
}

// CRC-32 check value
assert.strictEqual(bmCart.crc32(Buffer.from("123456789"), 0, 9), 0xCBF43926);

let c = bmCart.parse(build(), "test.bm");
assert.ok(c.ok, c.error);
assert.strictEqual(c.title, "Test Title");
assert.strictEqual(c.author, "Ada");
assert.strictEqual(c.width + "x" + c.height, "360x360");
assert.ok(!c.project && c.kind === "game" && c.extension === "bm");
assert.ok(c.crcOk);
assert.deepStrictEqual(c.sections.map((s) => s.name), ["COVER", "LUA", "INFO"]);
assert.strictEqual(c.info.license, "MIT");
assert.strictEqual(c.info.x, undefined);          // after the first [block]
assert.strictEqual(c.cover.width, 2);
assert.strictEqual(Buffer.from(c.lua).toString().slice(0, 8), "function");

assert.ok(!bmCart.parse(build({ badCrc: true }), "a.bm").crcOk);
assert.ok(!bmCart.parse(build({ noLua: true }), "a.bm").ok);
assert.ok(!bmCart.parse(build({ w: 100, h: 100 }), "a.bm").ok);
assert.ok(!bmCart.parse(Buffer.from("tiny"), "a.bm").ok);
const bad = build(); bad.write("XXXXXXXX", 0, "latin1");
assert.match(bmCart.parse(bad, "a.bm").error, /magic/);
const res = build(); res.write("BMRES\0\0\0", 0, "latin1");
assert.match(bmCart.parse(res, "a.bmm").error, /resource/);
const cut = build().subarray(0, 140);
assert.ok(!bmCart.parse(cut, "a.bm").ok);

// A cartridge from the real packer
const lua = path.join(os.tmpdir(), "bmcart_test.lua"), out = path.join(os.tmpdir(), "bmcart_test.bm");
fs.writeFileSync(lua, "function _draw() cls(1) end\n");
let real = "skipped (no python)";
for (const py of ["python3", "python"]) {
  const r = cp.spawnSync(py, ["scripts/mkbm.py", "-o", out, "--lua", lua, "--title", "Real", "--author", "Bo", "--res", "360x360"],
    { cwd: path.join(__dirname, "../.."), encoding: "utf8" });
  if (r.status === 0) {
    const rc = bmCart.parse(fs.readFileSync(out), "real.bm");
    assert.ok(rc.ok, rc.error);
    assert.ok(rc.crcOk, "mkbm.py CRC");
    assert.strictEqual(rc.title, "Real");
    assert.strictEqual(rc.author, "Bo");
    assert.ok(rc.sections.some((s) => s.name === "LUA"));
    real = "ok";
    break;
  }
}
console.log("bmcart: all checks passed; mkbm.py cart: " + real);
