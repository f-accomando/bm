#!/usr/bin/env node
/*
 * The files the 3D studio of the console wrote in its host test
 * (tests/studio/studio3d_host.lua), read by bm Studio's own parser:
 * - BLOCKS.BM: the faces are the very ones bm Studio makes with the same
 *   tools (a block, a floor tile painted red, a tile on the far wall), the
 *   skeleton and the animation are there;
 * - COPY3D.BM: the village saved with no edits has the same models.
 *
 *   node tests/studio/check_studio3d.js SDDIR VILLAGE.bm
 */
'use strict';
const fs = require('fs');
const path = require('path');

const JS = path.join(__dirname, '..', '..', 'sdk', 'studio', 'js');
for (const f of ['core.js', 'tiles.js', 'edit.js', 'rig.js']) require(path.join(JS, f));
const BM = globalThis.BM, E = BM.edit, Q = BM.rig.Q;

let checks = 0, fails = 0;
function check(ok, msg) {
  checks++;
  if (!ok) { fails++; console.log('FAIL ' + msg); }
}

const sd = process.argv[2], village = process.argv[3];
const open = p => BM.parseCart(new Uint8Array(fs.readFileSync(p)));   // not a Buffer: its slice() is a view
const r4 = v => Math.round(v * 1e4) / 1e4;
const faceKey = f => JSON.stringify({ p: f.p.map(p => p.map(r4)), uv: f.c == null ? f.uv.map(q => q.map(r4)) : null, c: f.c });

// the new project
{
  const { project, warnings } = open(path.join(sd, 'carts', 'blocks.bm'));
  check(!warnings.length, 'BLOCKS.BM reads with no warnings: ' + warnings.join('; '));
  check(project.models.length === 1 && project.models[0].name === 'model', 'one model, "model"');
  const m = project.models[0];
  const grass = { rect: [0, 0, 16, 16], rot: 0, flip: false, c: null }, camF = [0, 0, 1];
  const pc = [];
  E.placeFaces(pc, E.blockFaces([0, 0, 0], grass, camF), true);
  E.placeFaces(pc, [E.tileFace([1, 0, 0], E.basis(1, 1, camF), { c: 0xE84A5A })], false);
  E.placeFaces(pc, [E.tileFace([1, 0, 1], E.basis(2, -1, camF), grass)], false);
  const a = m.faces.map(faceKey).sort(), b = pc.map(faceKey).sort();
  check(a.length === 8 && JSON.stringify(a) === JSON.stringify(b),
    'the faces are the ones bm Studio makes with the same tools:\n  ' + a.join('\n  ') + '\nvs\n  ' + b.join('\n  '));
  const rig = m.rig;
  check(rig && rig.bones.map(x => x.name).join() === 'root,bone2' && rig.bones[1].parent === 0, 'the skeleton: root and bone2');
  const clip = rig && rig.clips[0];
  check(clip && clip.name === 'anim1' && clip.keys.length === 2 && !clip.loop && Math.abs(clip.length - 13 / 12) < 1e-5,
    'the animation: anim1, 2 keyframes, no loop, 13 frames');
  if (clip) {
    const k = clip.keys[1], want = Q.axisAngle([1, 0, 0], 15 * Math.PI / 180);
    check(Math.abs(k.t - 0.25) < 1e-5 && k.pose[0].q.every((v, i) => Math.abs(v - want[i]) < 1e-5),
      'at 0.25 s the root is turned 15 degrees around x: ' + JSON.stringify(k));
  }
  check(m.faces.every(f => f.b && f.b.every(x => x === 0 || x === 1)), 'every corner follows a bone');
}

// the village, saved as a copy with no edits
{
  const a = open(path.join(sd, 'carts', 'copy3d.bm')).project, b = open(village).project;
  check(a.models.map(m => m.name + ':' + m.faces.length).join() === b.models.map(m => m.name + ':' + m.faces.length).join(),
    'COPY3D.BM has the models of the village');
  check(JSON.stringify(a.models.map(m => m.rig)) === JSON.stringify(b.models.map(m => m.rig)), 'and the same skeletons');
  check(a.title === b.title && a.lua === b.lua, 'and the same title and code');
}

console.log(`studio3d files: ${checks - fails}/${checks} checks passed`);
process.exit(fails ? 1 : 0);
