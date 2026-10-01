#!/usr/bin/env node
/*
 * Studio Village: its 3D models, built with bm Studio's own tools
 * (sdk/studio/js: the starter sheet, tiles, blocks, corners moved by
 * hand), the villager of bm Animator (skeleton, idle / walk / wave) and his
 * walk pre-rendered into sprites; all saved as models.bm, which the
 * Makefile packs into the cartridge (models, skeletons, sprite sheet).
 * models.bm is in git: `make` does not need Node. To change the village,
 * open models.bm in bm Studio or bm Animator, or change this script and
 * run it again:
 *
 *   node carts/village/mkmodels.js
 */
'use strict';
const fs = require('fs');
const path = require('path');

const STUDIO = path.join(__dirname, '..', '..', 'sdk', 'studio', 'js');
for (const f of ['core.js', 'tiles.js', 'edit.js', 'rig.js', 'sprites.js', 'examples.js']) require(path.join(STUDIO, f));
const BM = globalThis.BM, E = BM.edit;

const T = 16;
const LOOK = [0, 0, 1];                 // floors: the top of a tile looks towards +z
const tile = (name, rot = 0, flip = false) => {
  const [x, y] = BM.STARTER[name];
  return { rect: [x * T, y * T, T, T], rot, flip, c: null };
};

/* a tile on the cell with lowest corner o, on the plane of `axis`, seen
 * from `side` */
function put(faces, o, axis, side, t) {
  E.placeFaces(faces, [E.tileFace(o, E.basis(axis, side, LOOK), t)], false);
}

/* a face from its corners; turned if it does not show towards `out` */
function face(p, t, out) {
  const f = { p, uv: t.c == null ? E.rectUV(t.rect, t.rot, t.flip).slice(0, p.length) : p.map(() => [0, 0]), c: t.c };
  if (out && BM.v3.dot(BM.faceNormal(f), out) < 0) {
    f.p.reverse();
    f.uv.reverse();
  }
  return f;
}

function block(faces, min, t) { E.placeFaces(faces, E.blockFaces(min, t, LOOK), true); }

/* both sides of a tile standing in the plane of `axis` */
function card(faces, o, axis, t) {
  put(faces, o, axis, -1, t);
  put(faces, o, axis, 1, t);
}

// ---------------------------------------------------------------- house
function house() {
  const f = [], W = 5, D = 4, H = 3;
  const door = x => x === 2;
  for (let x = 0; x < W; x++)
    for (let y = 0; y < H; y++) {
      // front (z = 0, seen from -z) with the door and two windows
      let t = y === 0 ? tile('planks') : tile('bricks');
      if (door(x) && y === 0) t = tile('doorBottom');
      else if (door(x) && y === 1) t = tile('doorTop');
      else if ((x === 0 || x === 4) && y === 1) t = tile('window');
      put(f, [x, y, 0], 2, -1, t);
      put(f, [x, y, D], 2, 1, (x === 1 || x === 3) && y === 1 ? tile('window') : y === 0 ? tile('planks') : tile('bricks'));
    }
  for (let z = 0; z < D; z++)
    for (let y = 0; y < H; y++) {
      const t = y === 1 && (z === 1 || z === 2) ? tile('window') : y === 0 ? tile('planks') : tile('bricks');
      put(f, [0, y, z], 0, -1, t);
      put(f, [W, y, z], 0, 1, t);
    }
  // gable roof: ridge along x at z = D / 2, 1.5 above the walls, and eaves
  // half a unit out; two roof tiles down each slope
  const top = H + 1.5, ridge = D / 2, eave = -0.5, back = D + 0.5, lo = H - 0.3;
  for (let x = -0.5; x < W + 0.5; x++) {
    const x1 = x + 1, mz = (eave + ridge) / 2, my = (lo + top) / 2, mz2 = (back + ridge) / 2;
    f.push(face([[x, lo, eave], [x, my, mz], [x1, my, mz], [x1, lo, eave]], tile('roof'), [0, 1, -1]));
    f.push(face([[x, my, mz], [x, top, ridge], [x1, top, ridge], [x1, my, mz]], tile('roof'), [0, 1, -1]));
    f.push(face([[x1, lo, back], [x1, my, mz2], [x, my, mz2], [x, lo, back]], tile('roof'), [0, 1, 1]));
    f.push(face([[x1, my, mz2], [x1, top, ridge], [x, top, ridge], [x, my, mz2]], tile('roof'), [0, 1, 1]));
  }
  // the gables: triangles of plaster over the side walls
  for (const [x, side] of [[0, -1], [W, 1]]) {
    const plaster = tile('plaster');
    f.push(face([[x, H, 0], [x, top - 0.2, ridge], [x, H, D]], plaster, [side, 0, 0]));
  }
  // a chimney of stone on the back slope
  block(f, [3, 4, 3], tile('stone'));
  block(f, [3, 3, 3], tile('stone'));
  // doorstep
  f.push(face([[1.8, 0.02, -0.6], [1.8, 0.02, 0], [3.2, 0.02, 0], [3.2, 0.02, -0.6]], tile('cobbles'), [0, 1, 0]));
  return centre(f, W / 2, D / 2);
}

// ---------------------------------------------------------------- tree
/* two crossed cards (a trunk under a crown), as in the old games */
function tree() {
  const f = [];
  for (const axis of [0, 2]) {
    const o = axis === 2 ? [-0.5, 0, 0] : [0, 0, -0.5];
    card(f, o, axis, tile('trunk'));
    card(f, [o[0], 1, o[2]], axis, tile('treeTop'));
  }
  return f;
}

function bush() {
  const f = [];
  card(f, [-0.5, 0, 0], 2, tile('bush'));
  card(f, [0, 0, -0.5], 0, tile('bush'));
  return f;
}

// ---------------------------------------------------------------- well
/* the four sides (and the top) of a box from lo to hi, one tile each */
function box(f, lo, hi, t, top = true) {
  const [x0, y0, z0] = lo, [x1, y1, z1] = hi;
  f.push(face([[x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [x1, y0, z0]], t, [0, 0, -1]));
  f.push(face([[x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [x0, y0, z1]], t, [0, 0, 1]));
  f.push(face([[x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [x0, y0, z0]], t, [-1, 0, 0]));
  f.push(face([[x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]], t, [1, 0, 0]));
  if (top) f.push(face([[x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0]], t, [0, 1, 0]));
}

function well() {
  const f = [];
  for (let x = -1; x <= 1; x++)
    for (let z = -1; z <= 1; z++)
      if (x || z) block(f, [x, 0, z], tile('cobbles'));
  put(f, [0, 0.7, 0], 1, 1, tile('water'));
  box(f, [-0.9, 1, 0.4], [-0.7, 2.4, 0.6], tile('log'), false);
  box(f, [1.7, 1, 0.4], [1.9, 2.4, 0.6], tile('log'), false);
  f.push(face([[-1.2, 2.3, -0.6], [-1.2, 2.9, 0.5], [2.2, 2.9, 0.5], [2.2, 2.3, -0.6]], tile('planks'), [0, 1, -1]));
  f.push(face([[2.2, 2.3, 1.6], [2.2, 2.9, 0.5], [-1.2, 2.9, 0.5], [-1.2, 2.3, 1.6]], tile('planks'), [0, 1, 1]));
  return centre(f, 0.5, 0.5);
}

// ---------------------------------------------------------------- the rest
function fence() {
  const f = [];
  for (let x = 0; x < 4; x++) card(f, [x, 0, 0], 2, tile('fence'));
  return centre(f, 2, 0);
}

function crates() {
  const f = [];
  block(f, [0, 0, 0], tile('crate'));
  block(f, [1, 0, 0], tile('crate'));
  block(f, [0, 1, 0], tile('crate'));
  block(f, [0, 0, 1], tile('crate'));
  return centre(f, 1, 1);
}

/* the ground: grass with a path of gravel across it */
function ground() {
  const f = [], N = 6;
  for (let x = -N; x < N; x++)
    for (let z = -N; z < N; z++) {
      const path = (x === 0 || x === -1) && z < 2 || (z === 2 || z === 1) && x > -6;
      put(f, [x, 0, z], 1, 1, tile(path ? 'gravel' : 'grass'));
    }
  return f;
}

/* the w x h pixels of the sheet at `at` (to check they are free) */
function crop(sheet, at, img) {
  const out = new Uint8ClampedArray(img.w * img.h * 4);
  for (let y = 0; y < img.h; y++)
    out.set(sheet.px.subarray(((at[1] + y) * sheet.w + at[0]) * 4, ((at[1] + y) * sheet.w + at[0] + img.w) * 4), y * img.w * 4);
  return out;
}

function centre(faces, cx, cz) {
  for (const f of faces) for (const p of f.p) { p[0] -= cx; p[2] -= cz; }
  return faces;
}

(async () => {
  const project = {
    title: 'Studio Village', author: 'bm', res: '320x180', lua: '', sheet: BM.starterSheet(), map: null, cover: null,
    uvInset: 0.25, extras: [],
    models: [
      { name: 'ground', faces: ground() }, { name: 'house', faces: house() }, { name: 'tree', faces: tree() },
      { name: 'bush', faces: bush() }, { name: 'well', faces: well() }, { name: 'fence', faces: fence() },
      { name: 'crates', faces: crates() }, BM.examples.villager(),
    ],
  };
  // the villager's walk, pre-rendered (bm Animator's Sprites page): 8 frames
  // of 24x32 from 4 directions, at SPRITES in the sheet (main.lua knows where)
  const v = project.models.find(m => m.name === 'villager');
  const spr = BM.sprites.render(v, project.sheet, v.rig.clips.find(c => c.name === 'walk'),
    { frames: 8, dirs: 4, w: 24, h: 32, pitch: 20, bands: 3, colours: 16, outline: 0x101018 });
  const SPRITES = [0, 64];
  if (BM.freeSpot({ w: spr.img.w, h: spr.img.h, px: crop(project.sheet, SPRITES, spr.img) }, spr.img.w, spr.img.h) === null)
    throw new Error('the place of the sprites in the sheet is taken');
  for (let y = 0; y < spr.img.h; y++)
    project.sheet.px.set(spr.img.px.subarray(y * spr.img.w * 4, (y + 1) * spr.img.w * 4), ((SPRITES[1] + y) * project.sheet.w + SPRITES[0]) * 4);
  const problems = BM.checkProject(project);
  if (problems.length) throw new Error(problems.join('\n'));
  project.lua = '-- the models of Studio Village (the game is carts/village/main.lua)\nfunction _draw() cls(0) end\n';
  const bm = BM.buildCart(project);
  const out = path.join(__dirname, 'models.bm');
  fs.writeFileSync(out, bm);
  for (const m of project.models) {
    const s = BM.modelStats(m);
    console.log(`${m.name.padEnd(8)} ${String(s.tris).padStart(4)} triangles ${String(s.verts).padStart(4)} corners` +
      (m.rig ? `, ${m.rig.bones.length} bones, ${m.rig.clips.map(c => c.name).join(' ')}` : ''));
  }
  console.log(BM.sprites.snippet('villager walk', spr, SPRITES, 10));
  console.log(`${out}: ${bm.length} bytes`);
})().catch(e => { console.error(e); process.exit(1); });
