#!/usr/bin/env node
/*
 * The files bm Mesh wrote in its host test (tests/studio/mesh_host.lua),
 * read by bm Studio's own parser:
 * - ASTROWING.BM: the models made from the game's meshes and with the
 *   tools (hero, cube, plane), nothing the kernel would refuse;
 * - VILLAGE.BM: the villager, edited, keeps its skeleton of bm Animator
 *   (every corner on a bone, the animations as they were);
 * - MESHCOPY.BM: the same models as the village it was saved from.
 *
 *   node tests/studio/check_mesh.js SDDIR VILLAGE.bm
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
const open = p => BM.parseCart(new Uint8Array(fs.readFileSync(p)));   // not a Buffer: its slice() is a view

for (const name of ['astrowing.bm', 'village.bm', 'meshcopy.bm']) {
  const { project, warnings } = open(path.join(sd, 'carts', name));
  check(!warnings.length, name + ' reads with no warnings: ' + warnings.join('; '));
  const problems = BM.checkProject(project);
  check(!problems.length, name + ': nothing the kernel would refuse: ' + problems.join('; '));
}

{
  const { project } = open(path.join(sd, 'carts', 'astrowing.bm'));
  check(project.models.map(m => m.name).join() === 'hero,cube,plane', 'ASTROWING.BM: hero, cube and plane: ' +
        project.models.map(m => m.name).join());
  check(project.models.every(m => !m.rig), 'no skeletons there');
  check(!project.lua.includes('[bm Mesh begin]'), 'no code meshes left in its code');
}

{
  const a = open(path.join(sd, 'carts', 'village.bm')).project, b = open(village0).project;
  const v = a.models.find(m => m.name === 'villager'), w = b.models.find(m => m.name === 'villager');
  check(v && v.rig && v.rig.bones.length === 7 && v.rig.clips.length === 3, 'the villager keeps 7 bones and 3 animations');
  check(v && JSON.stringify(v.rig.clips) === JSON.stringify(w.rig.clips), 'the animations are the same');
  check(v && v.faces.every(f => f.b && f.b.every(x => x >= 0 && x < 7)), 'every corner follows a bone');
  check(a.models.map(m => m.name).join() === b.models.map(m => m.name).join(), 'the village has its 8 models');
  const c = open(path.join(sd, 'carts', 'meshcopy.bm')).project;
  check(JSON.stringify(c.models) === JSON.stringify(a.models) && c.lua === a.lua, 'MESHCOPY.BM is the village as saved');
}

console.log(`bm Mesh files: ${checks - fails}/${checks} checks passed`);
process.exit(fails ? 1 : 0);
