#!/usr/bin/env node
/*
 * bm Studio and bm Animator on the console (carts/studio, carts/animator):
 * bm Studio's sprite sheet (the starter tiles of bm Studio on the PC, the
 * tiles of a new project) and the two covers (a house of blocks; the
 * villager of bm Animator in three poses), drawn by the sprite renderer of
 * sdk/studio/js. The PNGs are in git: `make` does not need Node. To change
 * them, change this script and run it again:
 *
 *   node carts/studio/mkassets.js
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
  const background = () => {
    const img = BM.newImage(128, 80);
    for (let i = 0; i < img.px.length; i += 4) {
      const k = ((i / 4) / 128 | 0) / 80;
      img.px[i] = 0x14 + 10 * k; img.px[i + 1] = 0x16 + 12 * k; img.px[i + 2] = 0x1E + 26 * k; img.px[i + 3] = 255;
    }
    return img;
  };
  const blit = (img, spr, row, w, h, dx, dy) => {
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        const s = ((row * h + y) * spr.img.w + x) * 4;
        if (spr.img.px[s + 3] < 128) continue;
        const X = x + dx, Y = y + dy;
        if (X < 0 || Y < 0 || X >= 128 || Y >= 80) continue;
        const o = (Y * 128 + X) * 4;
        img.px[o] = spr.img.px[s]; img.px[o + 1] = spr.img.px[s + 1]; img.px[o + 2] = spr.img.px[s + 2];
      }
  };
  // letters in the corner, in the orange of the tools
  const GLYPHS = {
    3: ['####.', '....#', '..##.', '....#', '....#', '#...#', '.###.'],
    D: ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
    S: ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    A: ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
  };
  const letters = (img, text) => {
    let gx = 6;
    for (const ch of text) {
      const g = GLYPHS[ch];
      for (let y = 0; y < 7; y++)
        for (let x = 0; x < 5; x++)
          if (g[y][x] === '#')
            for (let yy = 0; yy < 3; yy++)
              for (let xx = 0; xx < 3; xx++) {
                const o = ((6 + y * 3 + yy) * 128 + gx + x * 3 + xx) * 4;
                img.px[o] = 0xFF; img.px[o + 1] = 0xC0; img.px[o + 2] = 0x50;
              }
      gx += 18;
    }
  };

  // bm Studio: the house of blocks, turned a little (direction 7 of 8)
  const house = BM.sprites.render({ name: 'house', faces }, sheet, null, { w: 112, h: 80, pitch: 28,
    projection: 'ortho', ambient: 0.5, outline: 0x101018, supersample: 3, dirs: 8 });
  const studio = background();
  blit(studio, house, 7, 112, 80, 16, 0);
  letters(studio, 'S');
  fs.writeFileSync(path.join(__dirname, 'cover.png'), await BM.encodePNG(studio));

  // bm Animator: the villager in three poses (idle, walk, wave)
  const animator = background();
  [[0, 0.4, 2], [1, 0.25, 1], [2, 0.3, 0]].forEach(([clip, t, i]) => {
    const pose = BM.rig.samplePose(v.rig, v.rig.clips[clip], t);
    const one = { name: 'v', faces: BM.rig.posedFaces(v, pose).map(f => ({ p: f.p, uv: f.uv, c: f.c })) };
    const spr = BM.sprites.render(one, sheet, null, { w: 40, h: 64, pitch: 15, projection: 'ortho', ambient: 0.55,
      outline: 0x101018, supersample: 3, dirs: 8 });
    blit(animator, spr, [0, 7, 1][i], 40, 64, 30 + i * 32, 10);
  });
  letters(animator, 'A');
  fs.writeFileSync(path.join(__dirname, '..', 'animator', 'cover.png'), await BM.encodePNG(animator));
  console.log('carts/studio: sheet.png (256x48), cover.png; carts/animator: cover.png (128x80)');
})();
