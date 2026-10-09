#!/usr/bin/env node
/*
 * The files bm Pixel wrote in its host test (tests/studio/pixel_host.lua),
 * read by bm Studio's own parser:
 * - VILLAGE.BM: the sheet 64 pixels taller, a SHEET8 whose palette is the
 *   village's plus the new colour, the models and skeletons as they were;
 * - NEWSPR.BM: a new cartridge, the viewer's code and a sheet of 256x256.
 *
 *   node tests/studio/check_pixel.js SDDIR VILLAGE.bm
 */
'use strict';
const fs = require('fs');
const path = require('path');

const JS = path.join(__dirname, '..', '..', 'sdk', 'studio', 'js');
for (const f of ['core.js', 'tiles.js', 'edit.js', 'rig.js']) require(path.join(JS, f));
const BM = globalThis.BM;

let checks = 0, fails = 0;
function check(ok, msg) {
  checks++;
  if (!ok) { fails++; console.log('FAIL ' + msg); }
}

const sd = process.argv[2], village0 = process.argv[3];
const bytes = p => new Uint8Array(fs.readFileSync(p));   // not a Buffer: its slice() is a view
const open = p => BM.parseCart(bytes(p));
// the sections of a .bm: [type, bytes]
function sections(b) {
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), out = [];
  for (let i = 0; i < b[17]; i++) {
    const t = dv.getUint32(128 + i * 16, true), off = dv.getUint32(132 + i * 16, true), n = dv.getUint32(136 + i * 16, true);
    out.push([t, b.slice(off, off + n)]);
  }
  return out;
}
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

{
  const file = path.join(sd, 'carts', 'village.bme');
  const { project, warnings } = open(file);
  const before = open(village0).project;
  check(!warnings.length, 'VILLAGE.BM reads with no warnings: ' + warnings.join('; '));
  check(project.sheet.w === before.sheet.w && project.sheet.h === before.sheet.h + 64,
        `the sheet: ${project.sheet.w}x${project.sheet.h}`);
  // what was not drawn on stays, 24 bits: the test drew in the sprites of
  // 16x16 at (16,48), (0,0) and (16,0)
  const drawn = (x, y) => (x < 32 && y < 16) || (x >= 16 && x < 32 && y >= 48 && y < 64);
  let kept = 0, changed = 0, inside = 0;
  for (let y = 0; y < before.sheet.h; y++)
    for (let x = 0; x < before.sheet.w; x++) {
      const o = (y * before.sheet.w + x) * 4, n = (y * project.sheet.w + x) * 4;
      const a = before.sheet.px.subarray(o, o + 4), b = project.sheet.px.subarray(n, n + 4);
      if (a[3] < 128 && b[3] < 128) continue;
      if (drawn(x, y)) { if (!same(a, b)) inside++; continue; }
      if (same(a, b)) kept++; else changed++;
    }
  check(kept > 10000 && changed === 0 && inside > 0,
        `the pixels not drawn on keep their 24 bits (${kept} the same, ${changed} not; ${inside} drawn)`);
  let below = 0;
  for (let i = before.sheet.w * before.sheet.h * 4 + 3; i < project.sheet.px.length; i += 4) if (project.sheet.px[i] >= 128) below++;
  check(below === 0, 'the 64 new rows are transparent');
  check(project.models.map(m => m.name).join() === before.models.map(m => m.name).join() &&
        JSON.stringify(project.models.map(m => m.rig)) === JSON.stringify(before.models.map(m => m.rig)),
        'the models and skeletons as they were');
  const secs = sections(bytes(file)), secs0 = sections(bytes(village0));
  const s8 = secs.find(s => s[0] === BM.SEC.SHEET8), s80 = secs0.find(s => s[0] === BM.SEC.SHEET8);
  check(s8 && !secs.find(s => s[0] === BM.SEC.SHEET), 'a SHEET8, no SHEET');
  if (s8 && s80) {
    const n0 = s80[1][4] | s80[1][5] << 8;
    let first = 0;
    for (let i = 0; i < n0 && s80[1][8 + i * 4 + 3] >= 128; i++)
      if (same(s8[1].subarray(8 + i * 4, 12 + i * 4), s80[1].subarray(8 + i * 4, 12 + i * 4))) first++;
    check(first > 0, `the palette begins as the village's did (${first} colours)`);
  }
  for (const t of [BM.SEC.LUA, BM.SEC.COVER, BM.SEC.MESH, BM.SEC.ANIM]) {
    const a = secs.find(s => s[0] === t), b = secs0.find(s => s[0] === t);
    check(a && b && same(a[1], b[1]), `section ${t} byte for byte as it was`);
  }
}

{
  const { project, warnings } = open(path.join(sd, 'carts', 'newspr.bme'));
  check(!warnings.length, 'NEWSPR.BM reads with no warnings: ' + warnings.join('; '));
  check(project.lua.includes('bm Pixel: a new sprite sheet') && project.sheet.w === 256 && project.sheet.h === 256,
        'a new cartridge: the viewer and a 256x256 sheet');
  let opaque = 0;
  for (let i = 3; i < project.sheet.px.length; i += 4) if (project.sheet.px[i] >= 128) opaque++;
  check(opaque === 1, `one pixel drawn (${opaque})`);
}

console.log(`bm Pixel files: ${checks - fails}/${checks} checks passed`);
process.exit(fails ? 1 : 0);
