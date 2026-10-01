#!/usr/bin/env node
/*
 * Tests of bm Studio without a browser (sdk/studio/js: core, tiles,
 * edit): the .bm it writes, PNG, glTF, the editing geometry. Writes a
 * cartridge for tests/studio/check_cart.py (the Python side, mkbm.py's
 * bmmesh, must read the same thing).
 *
 *   node tests/studio/test_core.js OUT.bm
 */
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const JS = path.join(__dirname, '..', '..', 'sdk', 'studio', 'js');
for (const f of ['core.js', 'tiles.js', 'edit.js', 'rig.js', 'sprites.js', 'examples.js', 'gltfskin.js']) require(path.join(JS, f));
const BM = globalThis.BM, E = BM.edit, v3 = BM.v3;

let checks = 0, fails = 0;
function check(ok, msg) {
  checks++;
  if (!ok) { fails++; console.log('FAIL ' + msg); }
}
const near = (a, b, e = 1e-5) => Math.abs(a - b) < e;
const nearV = (a, b, e = 1e-5) => a.every((v, i) => near(v, b[i], e));

function sampleProject() {
  const sheet = BM.starterSheet();
  const T = 16, grass = { rect: [0, 0, T, T], rot: 0, flip: false, c: null }, red = { c: 0xE84A5A };
  const house = [];
  E.placeFaces(house, E.blockFaces([0, 0, 0], grass, [0, 0, 1]), true);
  E.placeFaces(house, E.blockFaces([1, 0, 0], grass, [0, 0, 1]), true);
  house.push({ p: [[0, 1, 0], [0.5, 1.8, 0.5], [1, 1, 0]], uv: [[0, 0], [0, 0], [0, 0]], c: 0x33CC66 });
  const sign = [];
  E.placeFaces(sign, E.stampFaces(E.makeStamp({ x: 1, y: 1, w: 1, h: 2 }, T), [0, 0, 0], E.basis(2, -1, [0, 0, 1])), false);
  E.placeFaces(sign, [E.tileFace([2, 0, 0], E.basis(2, -1, [0, 0, 1]), red)], false);
  return {
    title: 'Città di prova', author: 'tests', res: '320x180', lua: BM.viewerLua(), sheet, map: new Uint8Array([2, 0, 1, 0, 7, 0, 9, 0]),
    cover: BM.makeCover(sheet), models: [{ name: 'house', faces: house }, { name: 'sign', faces: sign }, { name: 'empty', faces: [] }],
    uvInset: 0.5, extras: [{ type: 42, data: new Uint8Array([9, 8, 7, 6, 5]) }],
  };
}

function testGeometry() {
  // a tile laid on the floor seen from above shows upwards; on a wall seen
  // from -z it shows towards -z; the texture is upright
  const floor = E.tileFace([2, 0, 3], E.basis(1, 1, [0, -0.7, 0.7]), { rect: [16, 0, 16, 16], rot: 0, flip: false, c: null });
  check(nearV(BM.faceNormal(floor), [0, 1, 0]), 'floor tile faces up');
  check(nearV(floor.p[0], [2, 0, 3]) && nearV(floor.p[1], [2, 0, 4]) && nearV(floor.p[2], [3, 0, 4]), 'floor corners: bottom left, top left, top right');
  check(nearV(floor.uv[0], [16, 16]) && nearV(floor.uv[1], [16, 0]) && nearV(floor.uv[2], [32, 0]), 'floor texture corners');
  const wall = E.tileFace([0, 0, 0], E.basis(2, -1, [0, 0, 1]), { rect: [0, 0, 16, 16], rot: 0, flip: false, c: null });
  check(nearV(BM.faceNormal(wall), [0, 0, -1]), 'wall seen from -z faces -z');
  check(wall.p[1][1] > wall.p[0][1] && wall.p[2][0] > wall.p[1][0], 'wall: up is +y, right is +x');
  const back = E.tileFace([0, 0, 0], E.basis(2, 1, [0, 0, -1]), { rect: [0, 0, 16, 16], rot: 0, flip: false, c: null });
  check(nearV(BM.faceNormal(back), [0, 0, 1]) && back.p[2][0] < back.p[1][0], 'wall seen from +z: right is -x');

  // stamps: 2 x 1 tiles, turned and mirrored
  const st = E.makeStamp({ x: 3, y: 2, w: 2, h: 1 }, 16);
  check(st.w === 2 && st.h === 1 && st.tiles[0].dx === 0 && st.tiles[1].dx === 1 && st.tiles[1].rect[0] === 64, 'stamp 2x1');
  const rot = E.makeStamp({ x: 3, y: 2, w: 2, h: 1 }, 16, 1);
  check(rot.w === 1 && rot.h === 2 && rot.tiles[0].dy === 1 && rot.tiles[1].dy === 0 && rot.tiles[0].rot === 1, 'stamp turned: 1x2, first tile on top');
  const fl = E.makeStamp({ x: 3, y: 2, w: 2, h: 1 }, 16, 0, true);
  check(fl.tiles[0].dx === 1 && fl.tiles[0].flip, 'stamp mirrored');
  const uv = E.rectUV([0, 0, 16, 16], 1);
  check(nearV(uv[1], [0, 16]) && nearV(uv[2], [0, 0]), 'texture turned clockwise: bottom left goes to top left');

  // blocks: six faces outwards; two blocks side by side lose the wall between
  const faces = [];
  E.placeFaces(faces, E.blockFaces([0, 0, 0], { c: 0xFF0000 }, [0, 0, 1]), true);
  check(faces.length === 6, 'a block has 6 faces');
  for (const f of faces) check(v3.dot(BM.faceNormal(f), v3.sub(BM.faceCenter(f), [0.5, 0.5, 0.5])) > 0, 'block faces show outwards');
  E.placeFaces(faces, E.blockFaces([1, 0, 0], { c: 0x00FF00 }, [0, 0, 1]), true);
  check(faces.length === 10, 'two blocks: 10 faces, got ' + faces.length);
  // the same tile again replaces (a new texture), it does not add
  E.placeFaces(faces, [E.tileFace([0, 1, 0], E.basis(1, 1, [0, 0, 1]), { c: 0x0000FF })], false);
  check(faces.length === 10 && faces.some(f => f.c === 0x0000FF), 'a tile on a tile gives it a new colour');

  // rays
  const hit = E.raycast(faces, { o: [0.5, 5, 0.5], d: [0, -1, 0] }, true);
  check(hit && near(hit.t, 4) && hit.face.c === 0x0000FF, 'ray down hits the top');
  const behind = E.raycast(faces, { o: [0.5, 0.5, 0.5], d: [0, -1, 0] }, true);
  check(!behind, 'from inside: the backs are not there (culling)');
  check(E.raycast(faces, { o: [0.5, 0.5, 0.5], d: [0, -1, 0] }, false), 'without culling, they are');

  // turning, merging
  const pts = [[1, 0, 0], [0, 0, 1]];
  E.turnY(pts, [0, 0, 0], 1);
  check(nearV(pts[0], [0, 0, -1]) && nearV(pts[1], [1, 0, 0]), 'a quarter turn clockwise seen from above');
  const q = [{ p: [[0, 0, 0], [0, 1, 0], [1, 1, 0], [1, 0, 0]], uv: [[0, 0], [0, 0], [0, 0], [0, 0]], c: 1 }];
  E.mergeVertices(q, new Set([BM.posKey([0, 1, 0]), BM.posKey([1, 1, 0])]));
  check(q.length === 1 && q[0].p.length === 3 && nearV(q[0].p[1], [0.5, 1, 0]), 'two corners of a tile merged: a triangle');

  // flip: the other side shows, the texture stays on its corners
  const f2 = BM.cloneFace(wall);
  E.flipFace(f2);
  check(nearV(BM.faceNormal(f2), [0, 0, 1]) && nearV(f2.uv[0], wall.uv[3]), 'flip');
}

async function testFiles(out) {
  const p = sampleProject(), bytes = BM.buildCart(p);
  fs.writeFileSync(out, bytes);
  const { project: q, warnings } = BM.parseCart(bytes);
  check(!warnings.length, 'no warnings: ' + warnings);
  check(q.title === p.title && q.author === 'tests' && q.res === '320x180', 'header');
  check(q.lua === p.lua && q.lua.startsWith(BM.VIEWER_MARK), 'code');
  check(q.map && q.map.length === 8 && q.map[4] === 7 && q.map[6] === 9, 'map kept');
  check(q.extras.length === 1 && q.extras[0].type === 42 && q.extras[0].data[4] === 5, 'unknown section kept');
  // section numbers: MESH 8, ANIM 9; 6 is the console's sound bank, kept as it is
  const types = Array.from({ length: bytes[17] }, (_, i) => new DataView(bytes.buffer, bytes.byteOffset).getUint32(128 + i * 16, true));
  check(types.includes(8) && !types.includes(6) && !types.includes(7), 'MESH is section 8: ' + types);
  {
    const bank = new Uint8Array(20);
    bank.set([0x42, 0x4D, 0x41, 0x55]);                         // "BMAU"
    const withBank = BM.buildCart(Object.assign({}, p, { extras: [{ type: 6, data: bank }] }));
    const r = BM.parseCart(withBank).project;
    check(r.models.length === 2 && r.extras.length === 1 && r.extras[0].type === 6 && r.extras[0].data[3] === 0x55,
      'a sound bank (6, BMAU) stays a section of its own, next to the models');
    // the first bm Studio files: MESH as 6, ANIM as 7
    const old = new Uint8Array(bytes), dv = new DataView(old.buffer);
    for (let i = 0; i < old[17]; i++) {
      const t = dv.getUint32(128 + i * 16, true);
      if (t === 8) dv.setUint32(128 + i * 16, 6, true);
      if (t === 9) dv.setUint32(128 + i * 16, 7, true);
    }
    const o = BM.parseCart(old).project;
    check(o.models.length === 2 && o.models[0].faces.length === q.models[0].faces.length, 'a file with MESH as 6 (the first bm Studio) still opens');
  }
  check(q.cover && q.cover.w === 128 && q.cover.h === 80, 'cover');
  check(Buffer.compare(Buffer.from(q.sheet.px), Buffer.from(p.sheet.px)) === 0, 'sheet (SHEET8) the same');
  check(q.models.length === 2 && q.models[0].name === 'house' && q.models[1].name === 'sign', 'empty models are not saved');
  check(near(q.uvInset, 0.5), 'texture inset');
  const src = p.models[0].faces, dst = q.models[0].faces;
  check(dst.length === src.length, `faces: ${dst.length} of ${src.length}`);
  check(dst.filter(f => f.p.length === 4).length === src.filter(f => f.p.length === 4).length, 'tiles come back as tiles');
  const sameFace = (a, b) => a.c === b.c && a.p.length === b.p.length && a.p.every((t, i) => nearV(t, b.p[i])) &&
    a.uv.every((t, i) => nearV(t, b.uv[i], 1 / 16));
  check(dst.every((f, i) => sameFace(f, src[i])), 'models come back the same (float32 corners, 1/8 pixel textures)');
  const dv = new DataView(bytes.buffer);
  check(BM.crc32(bytes, 128) === dv.getUint32(20, true), 'CRC');
  check(bytes[17] === 6, 'sections: cover, code, sheet, map, models, other: ' + bytes[17]);
  check(dv.getUint32(128, true) === BM.SEC.COVER, 'the cover goes first (the menu reads only the start)');

  // a sheet with more than 256 colours goes as SHEET
  const big = BM.newImage(32, 32);
  for (let i = 0; i < 1024; i++) big.px.set([i & 255, i >> 2, 7, 255], i * 4);
  const b2 = BM.buildCart({ ...p, sheet: big, cover: null });
  const q2 = BM.parseCart(b2).project;
  check(Buffer.compare(Buffer.from(q2.sheet.px), Buffer.from(big.px)) === 0, 'RGBA sheet');
  check(new DataView(b2.buffer).getUint32(128 + 16, true) === BM.SEC.SHEET, 'more than 256 colours: SHEET');

  // limits the kernel would refuse
  const many = { name: 'big', faces: [] };
  for (let i = 0; i < 1100; i++) many.faces.push({ p: [[i, 0, 0], [i, 1, 0], [i + 0.5, 1, 0], [i + 0.5, 0, 0]], uv: [[0, 0], [0, 0], [0, 0], [0, 0]], c: 1 });
  check(BM.checkProject({ ...p, models: [many] }).some(s => s.includes('vertices')), 'too many vertices found');
  check(BM.checkProject({ ...p, models: [{ name: 'a', faces: src }, { name: 'a', faces: src }] }).length === 1, 'two models with one name');
  check(BM.checkProject({ ...p, models: [{ name: 'a_very_long_name', faces: src }] }).length === 1, 'name too long');

  // PNG: RGBA both ways; an RGB PNG with a filter, as other programs write
  const png = await BM.encodePNG(p.sheet), back = await BM.decodePNG(png);
  check(back.w === 256 && Buffer.compare(Buffer.from(back.px), Buffer.from(p.sheet.px)) === 0, 'PNG RGBA');
  const w = 3, h = 2, rows = [];
  for (let y = 0; y < h; y++) {
    const r = [2];                                   // filter Up
    for (let x = 0; x < w; x++) r.push(10 * x, y ? 1 : 100, 5);
    rows.push(...r);
  }
  const chunk = (t, d) => {
    const b = Buffer.alloc(12 + d.length);
    b.writeUInt32BE(d.length, 0); b.write(t, 4, 'ascii'); Buffer.from(d).copy(b, 8);
    b.writeUInt32BE(BM.crc32(new Uint8Array(b.subarray(4, 8 + d.length))), 8 + d.length);
    return b;
  };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
  const rgb = Buffer.concat([Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]), chunk('IHDR', ih),
    chunk('IDAT', zlib.deflateSync(Buffer.from(rows))), chunk('IEND', Buffer.alloc(0))]);
  const img = await BM.decodePNG(new Uint8Array(rgb));
  check(img.px[4] === 10 && img.px[5] === 100 && img.px[3] === 255, 'PNG RGB, first row');
  check(img.px[(w + 2) * 4] === 40 && img.px[(w + 2) * 4 + 1] === 101 && img.px[(w + 2) * 4 + 2] === 10, 'PNG filter Up');

  // glTF: out and back; the faces keep showing the same side
  const glb = await BM.exportGLB(p, p.models.filter(m => m.faces.length));
  fs.writeFileSync(out.replace(/\.bm$/, '.glb'), glb);
  const g = BM.importGLB(glb);
  check(g.bm && g.bm.sheet_w === 256 && g.images.length === 1, 'bm Studio .glb with its sheet');
  check(g.models.length === 2 && g.models[0].name === 'house', 'models of the .glb');
  const quads = E.trisToQuads(g.models[0].faces);
  check(quads.length === src.length, `tiles again after the .glb: ${quads.length} of ${src.length}`);
  let same = 0;
  for (const f of src) {
    const c = BM.faceCenter(f), n = BM.faceNormal(f);
    const m = quads.find(x => nearV(BM.faceCenter(x), c, 1e-4));
    if (m && nearV(BM.faceNormal(m), n, 1e-4)) same++;
  }
  check(same === src.length, `faces show the same side after the .glb: ${same} of ${src.length}`);
  const tex = quads.find(f => f.c == null);
  check(tex && tex.uv.every(t => t[0] >= 0 && t[0] <= 1 && t[1] >= 0 && t[1] <= 1), 'uv of the .glb in 0..1');
  const red = g.models[1].faces.find(f => f.c != null);
  check(red && red.c === 0xE84A5A, 'plain colour through the .glb (sRGB, linear, sRGB): ' + (red && red.c.toString(16)));
  const js = BM.glbParse(glb).js;
  check(js.accessors.filter(a => a.min && a.max).length >= 2, 'POSITION has min and max');

  // Lua code of a model
  const lua = BM.modelToLua(p.models[1]);
  check(/^local sign = mesh\(\{/m.test(lua) && lua.includes('-1') && lua.includes('0xE84A5A'), 'Lua export');
}

/* a figure of two blocks: a body and an arm, the arm turning around the
 * shoulder */
function riggedModel() {
  const faces = [];
  E.placeFaces(faces, E.blockFaces([0, 0, 0], { c: 0x3366CC }, [0, 0, 1]), true);
  const arm = E.blockFaces([1, 0.5, 0], { c: 0xCC6633 }, [0, 0, 1]);
  arm.forEach(f => { f.b = f.p.map(() => 1); });
  faces.push(...arm);
  const R = BM.rig, Q = R.Q;
  const up = { q: Q.axisAngle([0, 0, 1], Math.PI / 2), t: [0, 0.25, 0] };
  return {
    name: 'figure', faces,
    rig: {
      bones: [{ name: 'body', parent: -1, head: [0.5, 0, 0.5], tail: [0.5, 1, 0.5] },
        { name: 'arm.R', parent: 0, head: [1, 1, 0.5], tail: [2, 1, 0.5] }],
      clips: [
        { name: 'wave', length: 1, loop: true, mode: 'smooth', keys: [{ t: 0, pose: R.restPose(2) }, { t: 0.5, pose: [R.restPose(1)[0], up] }] },
        { name: 'still', length: 0.5, loop: false, mode: 'step', keys: [{ t: 0, pose: R.restPose(2) }] },
      ],
    },
  };
}

function testRig() {
  const R = BM.rig, Q = R.Q, m = riggedModel();
  const e = [20, -35, 70];
  check(Q.toEuler(Q.fromEuler(e)).every((v, i) => near(v, e[i], 1e-6)), 'Euler angles both ways');
  // halfway between rest and the arm up: 45 degrees, smoothstep at u = 0.5 is 0.5
  const half = R.samplePose(m.rig, m.rig.clips[0], 0.25);
  check(nearV(half[1].q, Q.axisAngle([0, 0, 1], Math.PI / 4), 1e-6) && near(half[1].t[1], 0.125), 'halfway pose');
  // looping: 0.75 s is halfway from the last key back to the first
  check(nearV(R.samplePose(m.rig, m.rig.clips[0], 0.75)[1].q, half[1].q, 1e-6), 'the loop goes back to the first key');
  check(nearV(R.samplePose(m.rig, m.rig.clips[0], 1.25)[1].q, half[1].q, 1e-6), 'time wraps round');
  // the arm up: its far end (x = 2) goes above the shoulder
  const up = R.samplePose(m.rig, m.rig.clips[0], 0.5), faces = R.posedFaces(m, up);
  const bone = f => (f.src.b || [0])[0];
  const top = Math.max(...faces.filter(f => bone(f) === 1).flatMap(f => f.p.map(p => p[1])));
  check(near(top, 2.25, 1e-5), `the arm turns up around the shoulder: top at ${top}`);
  const body = faces.filter(f => bone(f) === 0).flatMap(f => f.p);
  check(body.every(p => p[1] <= 1 + 1e-9), 'the body stays');
  const bones = R.posedBones(m.rig, up);
  check(nearV(bones[1].head, [1, 1.25, 0.5], 1e-6) && nearV(bones[1].tail, [1, 2.25, 0.5], 1e-6), 'posed bone ends');
  // auto skin by face: the arm block is nearer the arm bone
  const copy = { ...m, faces: m.faces.map(BM.cloneFace) };
  copy.faces.forEach(f => { delete f.b; });
  R.autoSkin(copy);
  check(copy.faces.filter(f => f.b[0] === 1).length === 6, 'auto skin: the arm block goes to the arm');
  check(R.mirrorName('arm.R') === 'arm.L' && R.mirrorName('leftFoot') === 'rightFoot' && R.mirrorName('spine') === null, 'mirrored names');
  // step: no in-between
  const st = { ...m.rig.clips[0], mode: 'step' };
  check(nearV(R.samplePose(m.rig, st, 0.49)[1].q, [0, 0, 0, 1]), 'step keeps the pose until the next key');
}

function testAnimFile(out) {
  const m = riggedModel(), p = sampleProject();
  p.models = [m, p.models[0]];
  check(!BM.checkProject(p).length, 'a rigged model is fine: ' + BM.checkProject(p));
  const bytes = BM.buildCart(p), { project: q, warnings } = BM.parseCart(bytes);
  check(!warnings.length, 'no warnings: ' + warnings);
  const r = q.models[0];
  check(r.rig && r.rig.bones.length === 2 && r.rig.bones[1].name === 'arm.R' && r.rig.bones[1].parent === 0, 'bones back');
  check(r.rig.clips.length === 2 && r.rig.clips[0].mode === 'smooth' && r.rig.clips[0].loop && !r.rig.clips[1].loop &&
    r.rig.clips[1].mode === 'step', 'clips back');
  check(nearV(r.rig.clips[0].keys[1].pose[1].q, m.rig.clips[0].keys[1].pose[1].q, 1e-6), 'keyframes back');
  check(r.faces.length === m.faces.length && r.faces.every((f, i) => f.b && f.b.join() === (m.faces[i].b || [0, 0, 0, 0]).join()),
    'the bone of every corner back');
  check(!q.models[1].rig, 'a model without a skeleton stays without');
  // corners in one place but of two bones stay two vertices
  const mesh = BM.modelToMesh(m);
  check(mesh.verts.length === 8 + 8 && mesh.bones.filter(b => b === 1).length === 8, 'two bones: their own vertices');
  // a rig that does not fit the model any more is left out, with a warning
  const bad = BM.buildCart({ ...p, models: [m] });
  const secs = [];
  const dv = new DataView(bad.buffer);
  for (let i = 0; i < bad[17]; i++) secs.push([dv.getUint32(128 + i * 16, true), dv.getUint32(132 + i * 16, true)]);
  const anim = secs.find(s => s[0] === BM.SEC.ANIM);
  check(!!anim, 'an ANIM section');
  dv.setUint16(anim[1] + 8 + 20, 3, true);                      // vertices: 3
  dv.setUint32(20, BM.crc32(bad, 128), true);
  const broken = BM.parseCart(bad);
  check(broken.warnings.length === 1 && !broken.project.models[0].rig, 'a skeleton that does not fit is left out');
  // bad rigs are refused before saving
  const twice = BM.cloneRig(m.rig);
  twice.bones[1].name = 'body';
  check(BM.checkRig({ name: 'x', rig: twice }).some(s => s.includes('two bones')), 'two bones with one name');
  fs.writeFileSync(out.replace(/\.bm$/, '-anim.bm'), bytes);
}

function testSprites() {
  const v = BM.examples.villager(), sheet = BM.starterSheet(), walk = v.rig.clips.find(c => c.name === 'walk');
  check(!BM.checkProject({ ...sampleProject(), models: [v] }).length, 'the villager is a valid model');
  const o = { frames: 6, dirs: 4, w: 32, h: 40, bands: 3, colours: 12, outline: 0x101018 };
  const r = BM.sprites.render(v, sheet, walk, o), r2 = BM.sprites.render(v, sheet, walk, o);
  check(r.img.w === 6 * 32 && r.img.h === 4 * 40 && r.cols === 6 && r.rows === 4, `grid ${r.img.w}x${r.img.h}`);
  check(Buffer.compare(Buffer.from(r.img.px), Buffer.from(r2.img.px)) === 0, 'the same input gives the same pixels');
  check(near(r.times[1], 0.8 / 6) && near(r.times[5], 5 * 0.8 / 6), 'a loop: frames over its length, no repeated first frame');
  const frame = (d, i) => { const out = []; for (let y = 0; y < 40; y++) out.push(...r.img.px.subarray(((d * 40 + y) * r.img.w + i * 32) * 4, ((d * 40 + y) * r.img.w + (i + 1) * 32) * 4)); return out; };
  check(frame(0, 0).join() !== frame(0, 3).join(), 'the frames move');
  check(frame(0, 0).join() !== frame(1, 0).join(), 'the directions differ');
  const colours = new Set();
  let outline = 0, solid = 0;
  for (let i = 0; i < r.img.px.length; i += 4) {
    if (r.img.px[i + 3] < 128) continue;
    solid++;
    const c = (r.img.px[i] << 16) | (r.img.px[i + 1] << 8) | r.img.px[i + 2];
    if (c === 0x101018) outline++; else colours.add(c);
  }
  check(colours.size <= 12 && outline > 100 && solid > 1000, `${colours.size} colours (at most 12), ${outline} outline pixels`);
  // the anchor: the model's origin (its feet) at the same pixel in every frame
  check(r.anchor[0] === 16 && r.anchor[1] > 30 && r.anchor[1] < 40, 'anchor ' + r.anchor);
  const lowest = d => { for (let y = 39; y >= 0; y--) for (let x = 0; x < 32; x++) if (frame(d, 0)[(y * 32 + x) * 4 + 3] >= 128) return y; return -1; };
  check(Math.abs(lowest(0) - r.anchor[1]) <= 2, `the feet at the anchor (${lowest(0)} / ${r.anchor[1]})`);
  // still models: one frame
  const still = BM.sprites.render({ name: 'x', faces: sampleProject().models[0].faces }, sheet, null, { dirs: 2, w: 16, h: 16 });
  check(still.cols === 1 && still.rows === 2, 'a model without animation: one frame per direction');
  // into the sheet: where there is room, the sheet grows only when it must and never loses rows
  const placed = BM.placeInSheet(sheet, r.img);
  check(placed && placed.sheet.w === 256 && placed.at[1] >= 48, 'the sprites fit in the starter sheet at ' + (placed && placed.at));
  const wide = BM.placeInSheet(sheet, BM.sprites.render(v, sheet, walk, { ...o, frames: 12 }).img);
  check(wide.sheet.w === 384 && wide.sheet.h >= 256, `a wider grid widens the sheet: ${wide.sheet.w}x${wide.sheet.h}`);
  const lua = BM.sprites.snippet('villager walk', r, placed.at, 12);
  check(lua.includes('local VILLAGER_WALK = { x = ' + placed.at[0]) && lua.includes('sspr('), 'Lua for the sprites');
}

/* the .glb with a skeleton, played by a small glTF evaluator, puts every
 * corner where bm's own skinning does */
async function testGltfSkin() {
  const v = BM.examples.villager(), R = BM.rig;
  const glb = await BM.exportAnimatedGLB({ sheet: BM.starterSheet(), uvInset: 0.25, title: 'v' }, v);
  const { js, bin } = BM.glbParse(glb);
  check(js.skins.length === 1 && js.skins[0].joints.length === 7 && js.animations.length === 3, 'skin and animations');
  const acc = i => {
    const a = js.accessors[i], bv = js.bufferViews[a.bufferView], n = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[a.type];
    const dv = new DataView(bin.buffer, bin.byteOffset + (bv.byteOffset || 0)), out = [];
    for (let k = 0; k < a.count; k++) {
      const t = [];
      for (let c = 0; c < n; c++) t.push(a.componentType === 5121 ? dv.getUint8(k * n + c) : dv.getFloat32((k * n + c) * 4, true));
      out.push(t);
    }
    return out;
  };
  for (const [name, T] of [['walk', 0.2], ['wave', 0.3]]) {
    const anim = js.animations.find(a => a.name === name), local = {};
    for (const ch of anim.channels) {
      const sm = anim.samplers[ch.sampler], ins = acc(sm.input).map(x => x[0]), k = ins.findIndex(t => Math.abs(t - T) < 1e-5);
      (local[ch.target.node] = local[ch.target.node] || {})[ch.target.path] = acc(sm.output)[k];
    }
    const joints = js.skins[0].joints, parent = {};
    js.nodes.forEach((n, i) => (n.children || []).forEach(c => { parent[c] = i; }));
    const G = {}, glob = i => {
      if (G[i]) return G[i];
      const r = R.Q.mat(local[i].rotation), t = local[i].translation, l = [r[0], r[1], r[2], t[0], r[3], r[4], r[5], t[1], r[6], r[7], r[8], t[2]];
      return (G[i] = joints.includes(parent[i]) ? R.M.mul(glob(parent[i]), l) : l);
    };
    const ibm = acc(js.skins[0].inverseBindMatrices);
    const skin = joints.map((j, k) => { const b = ibm[k]; return R.M.mul(glob(j), [b[0], b[4], b[8], b[12], b[1], b[5], b[9], b[13], b[2], b[6], b[10], b[14]]); });
    const theirs = new Set(), ours = new Set();
    for (const pr of js.meshes[0].primitives) {
      const pos = acc(pr.attributes.POSITION), jn = acc(pr.attributes.JOINTS_0);
      pos.forEach((p, i) => { const q = R.M.apply(skin[jn[i][0]], p); theirs.add([q[0], q[1], -q[2]].map(x => Math.round(x * 1000)).join()); });
    }
    const pose = R.samplePose(v.rig, v.rig.clips.find(c => c.name === name), T);
    for (const f of R.posedFaces(v, pose)) for (const p of f.p) ours.add(p.map(x => Math.round(x * 1000)).join());
    check([...ours].every(k => theirs.has(k)) && ours.size === theirs.size, `glTF ${name} at ${T} s: the same pose (${ours.size} corners)`);
  }
}

(async () => {
  const out = process.argv[2] || path.join('build', 'studio-test.bm');
  fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
  testGeometry();
  testRig();
  testAnimFile(out);
  testSprites();
  await testGltfSkin();
  await testFiles(out);
  console.log(`studio: ${checks - fails}/${checks} checks passed`);
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
