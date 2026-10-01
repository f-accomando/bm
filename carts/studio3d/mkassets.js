#!/usr/bin/env node
/*
 * The 3D studio of the console (carts/studio3d): its sprite sheet (the
 * starter tiles of bm Studio, the tiles of a new project) and its cover
 * (a house of blocks and the villager of bm Animator, drawn by the
 * sprite renderer of sdk/studio/js). Both PNGs are in git: `make` does not
 * need Node. To change them, change this script and run it again:
 *
 *   node carts/studio3d/mkassets.js
 */
'use strict';
const fs = require('fs');
const path = require('path');

const STUDIO = path.join(__dirname, '..', '..', 'sdk', 'studio', 'js');
for (const f of ['core.js', 'tiles.js', 'edit.js', 'rig.js', 'sprites.js', 'examples.js']) require(path.join(STUDIO, f));
const BM = globalThis.BM, E = BM.edit;

const T = 16, LOOK = [0, 0, 1];
const tile = name => { const [x, y] = BM.STARTER[name]; return { rect: [x * T, y * T, T, T], rot: 0, flip: false, c: null }; };

(async () => {
  const sheet = BM.starterSheet();

  // the sheet: the three rows of starter tiles
  const top = BM.newImage(256, 48);
  top.px.set(sheet.px.subarray(0, 256 * 48 * 4));
  fs.writeFileSync(path.join(__dirname, 'sheet.png'), await BM.encodePNG(top));

  // the cover: a little house of blocks with the villager beside it
  const faces = [];
  for (const [x, z] of [[0, 0], [1, 0], [0, 1], [1, 1]]) {
    E.placeFaces(faces, E.blockFaces([x, 0, z], tile('bricks'), LOOK), true);
    E.placeFaces(faces, E.blockFaces([x, 1, z], tile('planks'), LOOK), true);
  }
  for (const [x, z] of [[0, 0], [1, 0], [0, 1], [1, 1]])
    E.placeFaces(faces, E.blockFaces([x, 2, z], tile('roof'), LOOK), true);
  E.placeFaces(faces, [E.tileFace([1, 1, 0], E.basis(2, -1, LOOK), tile('window'))], false);
  E.placeFaces(faces, [E.tileFace([0, 0, 0], E.basis(2, -1, LOOK), tile('doorBottom'))], false);
  E.placeFaces(faces, [E.tileFace([0, 1, 0], E.basis(2, -1, LOOK), tile('doorTop'))], false);
  for (let x = -2; x < 4; x++)
    for (let z = -2; z < 3; z++)
      E.placeFaces(faces, [E.tileFace([x, 0, z], E.basis(1, 1, LOOK), tile(z < 0 && x > 1 ? 'gravel' : 'grass'))], false);
  const v = BM.examples.villager();
  const scene = { name: 'cover', faces: faces.slice() };
  // the villager waves, in front of the house
  const pose = BM.rig.samplePose(v.rig, v.rig.clips[2], 0.3);
  for (const f of BM.rig.posedFaces(v, pose))
    scene.faces.push({ p: f.p.map(p => [p[0] * 0.9 + 2.6, p[1] * 0.9, p[2] * 0.9 - 1.2]), uv: f.uv, c: f.c });
  const spr = BM.sprites.render(scene, sheet, null, { w: 112, h: 80, pitch: 28, projection: 'ortho', ambient: 0.5,
    outline: 0x101018, supersample: 3, dirs: 8 });
  // direction 7 of 8: the scene turned a little, the house's front and side show
  const d = 7;
  const cover = BM.newImage(128, 80);
  for (let i = 0; i < cover.px.length; i += 4) {
    const y = (i / 4) / 128 | 0;
    const k = y / 80;
    cover.px[i] = 0x14 + 10 * k; cover.px[i + 1] = 0x16 + 12 * k; cover.px[i + 2] = 0x1E + 26 * k; cover.px[i + 3] = 255;
  }
  for (let y = 0; y < 80; y++)
    for (let x = 0; x < 112; x++) {
      const s = ((d * 80 + y) * spr.img.w + x) * 4;
      if (spr.img.px[s + 3] < 128) continue;
      const o = (y * 128 + x + 16) * 4;
      cover.px[o] = spr.img.px[s]; cover.px[o + 1] = spr.img.px[s + 1]; cover.px[o + 2] = spr.img.px[s + 2];
    }
  // "3D" in the corner, in the orange of the tools
  const GLYPHS = {
    3: ['####.', '....#', '..##.', '....#', '....#', '#...#', '.###.'],
    D: ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
  };
  let gx = 6;
  for (const ch of '3D') {
    const g = GLYPHS[ch];
    for (let y = 0; y < 7; y++)
      for (let x = 0; x < 5; x++)
        if (g[y][x] === '#')
          for (let yy = 0; yy < 3; yy++)
            for (let xx = 0; xx < 3; xx++) {
              const o = ((6 + y * 3 + yy) * 128 + gx + x * 3 + xx) * 4;
              cover.px[o] = 0xFF; cover.px[o + 1] = 0xC0; cover.px[o + 2] = 0x50;
            }
    gx += 18;
  }
  fs.writeFileSync(path.join(__dirname, 'cover.png'), await BM.encodePNG(cover));
  console.log('carts/studio3d: sheet.png (256x48), cover.png (128x80)');
})();
