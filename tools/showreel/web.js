#!/usr/bin/env node
/*
 * The showreel, first half: a villager made from nothing in bm Studio and
 * bm Animator, in a real browser (Playwright + Chromium), with a visible
 * mouse pointer. Pixel art of its textures, the model built with blocks
 * and tiles, the skeleton, a walk; then the .bm is saved for the console.
 *
 *   node tools/showreel/web.js OUTDIR
 *
 * OUTDIR gets frames/NNNNNN.png (Chromium's screencast: a frame each time
 * the page changes), frames.json (the time of each frame and the scene
 * marks) and villager.bm. tools/showreel/assemble.py makes the video.
 */
'use strict';
const fs = require('fs');
const path = require('path');

function loadPlaywright() {
  for (const p of ['playwright', '/opt/node22/lib/node_modules/playwright']) {
    try { return require(p); } catch (e) { /* next */ }
  }
  console.log('showreel: the playwright package is missing (npm install playwright)');
  process.exit(1);
}

const ROOT = path.join(__dirname, '..', '..');
const OUT = path.resolve(process.argv[2] || path.join(ROOT, 'build', 'showreel', 'web'));
const W = 1280, H = 720;

/* ------------------------------------------------------------ the art */

// colours of the villager
const C = {
  skin: 0xF1C27D, shade: 0xD9A066, cheek: 0xE8957A, hair: 0x5B3A1E, hairL: 0x7E5430,
  white: 0xFFFFFF, eye: 0x1E2433, mouth: 0x9E3A3A, shirt: 0x3F8F4F, shirtD: 0x2E6B3A,
  shirtL: 0x5FB26A, belt: 0x6B4423, buckle: 0xE8C547, pants: 0x4E5F86, pantsD: 0x3B4A6B,
  boot: 0x3B2A20, bootL: 0x5A4030,
};
// where the tiles go in the sheet (16 px tiles, row 4 is the first empty one)
const T = { face: [0, 4], hair: [2, 4], shirt: [3, 4], belt: [4, 4], skin: [5, 4], pants: [6, 4], boots: [7, 4] };
const px = (tile, x, y) => [T[tile][0] * 16 + x, T[tile][1] * 16 + y];

/* what is drawn, in order: [tool, colour, ...sheet pixels]; rect = filled
 * rectangle (two corners), line = pencil line (two ends), fill = bucket */
function artScript() {
  const s = [];
  const rect = (tile, c, x0, y0, x1, y1) => s.push(['rect', c, px(tile, x0, y0), px(tile, x1, y1)]);
  const line = (tile, c, x0, y0, x1, y1) => s.push(['line', c, px(tile, x0, y0), px(tile, x1, y1)]);
  // the face, 32 x 32 (four tiles)
  rect('face', C.skin, 0, 0, 31, 31);
  rect('face', C.hair, 0, 0, 31, 7);
  rect('face', C.hair, 0, 8, 5, 10);
  rect('face', C.hair, 11, 8, 15, 9);
  rect('face', C.hair, 22, 8, 31, 10);
  rect('face', C.hair, 0, 11, 2, 19);
  rect('face', C.hair, 29, 11, 31, 19);
  line('face', C.hairL, 3, 2, 12, 2);
  line('face', C.hairL, 17, 4, 27, 4);
  line('face', C.hair, 6, 12, 11, 12);
  line('face', C.hair, 20, 12, 25, 12);
  rect('face', C.white, 7, 14, 11, 17);
  rect('face', C.white, 20, 14, 24, 17);
  rect('face', C.eye, 9, 15, 10, 17);
  rect('face', C.eye, 21, 15, 22, 17);
  rect('face', C.shade, 14, 17, 17, 21);
  rect('face', C.cheek, 5, 20, 7, 21);
  rect('face', C.cheek, 24, 20, 26, 21);
  line('face', C.mouth, 11, 24, 20, 24);
  line('face', C.mouth, 12, 25, 19, 25);
  // hair (the other sides of the head)
  rect('hair', C.hair, 0, 0, 15, 15);
  for (const [x, y, l] of [[2, 3, 4], [9, 1, 5], [5, 8, 6], [11, 11, 3], [1, 13, 4]]) line('hair', C.hairL, x, y, x + l, y);
  // the shirt, plain and with a belt
  rect('shirt', C.shirt, 0, 0, 15, 15);
  line('shirt', C.shirtL, 0, 0, 15, 0);
  line('shirt', C.shirtD, 4, 3, 4, 14);
  line('shirt', C.shirtD, 11, 5, 11, 15);
  rect('belt', C.shirt, 0, 0, 15, 15);
  line('belt', C.shirtD, 4, 0, 4, 10);
  line('belt', C.shirtD, 11, 0, 11, 10);
  rect('belt', C.belt, 0, 11, 15, 13);
  rect('belt', C.buckle, 6, 11, 9, 13);
  // skin (the hands), the trousers, the boots
  rect('skin', C.skin, 0, 0, 15, 15);
  rect('skin', C.shade, 0, 12, 15, 15);
  rect('pants', C.pants, 0, 0, 15, 15);
  line('pants', C.pantsD, 7, 0, 7, 15);
  line('pants', C.pantsD, 8, 0, 8, 15);
  rect('boots', C.boot, 0, 0, 15, 15);
  rect('boots', C.bootL, 0, 0, 15, 3);
  line('boots', C.bootL, 2, 9, 13, 9);
  return s;
}

/* the blocks of the villager, in the order they are laid: [x, y, z] of
 * the cell, the tile, and the face clicked to put it there ("floor", or the
 * cell it is laid against and the side) */
const BUILD = [
  { tile: 'boots', at: 'floor', cell: [-1, 0, 0] },
  { tile: 'boots', at: 'floor', cell: [0, 0, 0] },
  { tile: 'pants', on: [-1, 0, 0], side: 'top' },
  { tile: 'pants', on: [0, 0, 0], side: 'top' },
  { tile: 'belt', on: [-1, 1, 0], side: 'top' },
  { tile: 'belt', on: [0, 1, 0], side: 'top' },
  { tile: 'shirt', on: [-1, 2, 0], side: 'top' },
  { tile: 'shirt', on: [0, 2, 0], side: 'top' },
  { tile: 'skin', on: [-1, 2, 0], side: '-x' },
  { tile: 'shirt', on: [-2, 2, 0], side: 'top' },
  { tile: 'skin', on: [0, 2, 0], side: '+x', view: 'right' },
  { tile: 'shirt', on: [1, 2, 0], side: 'top' },
  { tile: 'hair', on: [-1, 3, 0], side: 'top', view: 'front' },
  { tile: 'hair', on: [0, 3, 0], side: 'top' },
  { tile: 'hair', on: [-1, 4, 0], side: 'top' },
  { tile: 'hair', on: [0, 4, 0], side: 'top' },
];

/* ------------------------------------------------------------ helpers */

// a pointer drawn in the page (headless Chromium has none), and a ring on clicks
const CURSOR = () => {
  const put = () => {
    if (document.getElementById('reel-cursor') || !document.body) return;
    const c = document.createElement('div');
    c.id = 'reel-cursor';
    c.innerHTML = '<svg width="22" height="28" viewBox="0 0 22 28"><path d="M1 1 L1 22 L6.5 17 L10.5 26 L14 24.5 L10 15.5 L17.5 15.5 Z" fill="#fff" stroke="#111" stroke-width="1.6" stroke-linejoin="round"/></svg>';
    Object.assign(c.style, { position: 'fixed', left: '-40px', top: '-40px', zIndex: 2147483647, pointerEvents: 'none', filter: 'drop-shadow(0 1px 2px rgba(0,0,0,.5))' });
    const ring = document.createElement('div');
    ring.id = 'reel-ring';
    Object.assign(ring.style, { position: 'fixed', width: '30px', height: '30px', marginLeft: '-15px', marginTop: '-15px', borderRadius: '50%',
      border: '3px solid #ffd34d', zIndex: 2147483646, pointerEvents: 'none', opacity: 0, transition: 'opacity .35s, transform .35s' });
    document.body.append(ring, c);
    const move = e => { c.style.left = e.clientX - 1 + 'px'; c.style.top = e.clientY - 1 + 'px'; };
    addEventListener('pointermove', move, true);
    addEventListener('pointerdown', e => {
      move(e);
      Object.assign(ring.style, { left: e.clientX + 'px', top: e.clientY + 'px', transition: 'none', opacity: 1, transform: 'scale(.4)' });
      requestAnimationFrame(() => requestAnimationFrame(() => Object.assign(ring.style, { transition: 'opacity .4s, transform .4s', opacity: 0, transform: 'scale(1.3)' })));
    }, true);
  };
  if (document.readyState === 'loading') addEventListener('DOMContentLoaded', put); else put();
};

class Recorder {
  constructor(page, dir) { this.page = page; this.dir = dir; this.frames = []; this.marks = []; this.cdp = null; }
  async start() {
    this.cdp = await this.page.context().newCDPSession(this.page);
    this.cdp.on('Page.screencastFrame', ({ data, metadata, sessionId }) => {
      const f = String(this.frames.length).padStart(6, '0') + '.png';
      fs.writeFileSync(path.join(this.dir, f), Buffer.from(data, 'base64'));
      this.frames.push({ f, t: metadata.timestamp });
      this.cdp.send('Page.screencastFrameAck', { sessionId }).catch(() => {});
    });
    await this.cdp.send('Page.startScreencast', { format: 'png', maxWidth: W, maxHeight: H, everyNthFrame: 1 });
  }
  async stop() { if (this.cdp) { await this.cdp.send('Page.stopScreencast').catch(() => {}); await this.cdp.detach().catch(() => {}); this.cdp = null; } }
  mark(name) { this.marks.push({ name, t: Date.now() / 1000 }); console.log('scene: ' + name); }
  save() { fs.writeFileSync(path.join(OUT, 'frames.json'), JSON.stringify({ frames: this.frames, marks: this.marks, end: Date.now() / 1000 }, null, 0)); }
}

(async () => {
  const { chromium } = loadPlaywright();
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(path.join(OUT, 'frames'), { recursive: true });
  const browser = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const ctx = await browser.newContext({ viewport: { width: W, height: H }, acceptDownloads: true });
  await ctx.addInitScript(() => { try { localStorage.clear(); } catch (e) { /* file:// */ } delete window.showSaveFilePicker; delete window.showOpenFilePicker; });
  await ctx.addInitScript(CURSOR);
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  const rec = new Recorder(page, path.join(OUT, 'frames'));
  const wait = ms => page.waitForTimeout(ms);
  const ev = (fn, arg) => page.evaluate(fn, arg);
  let mx = W / 2, my = H / 2;
  const glide = async (x, y, ms = 350) => {                 // a smooth mouse move
    const steps = Math.max(2, Math.round(ms / 16));
    await page.mouse.move(x, y, { steps });
    mx = x; my = y;
  };
  const click = async (x, y, ms = 300, pause = 120) => { await glide(x, y, ms); await page.mouse.down(); await wait(60); await page.mouse.up(); await wait(pause); };
  const drag = async (x0, y0, x1, y1, ms = 400, opts = {}) => {
    await glide(x0, y0, 250);
    await page.mouse.down(opts);
    await page.mouse.move(x1, y1, { steps: Math.max(2, Math.round(ms / 16)) });
    await page.mouse.up(opts);
    mx = x1; my = y1;
    await wait(80);
  };
  const type = async (text, delay = 55) => { for (const ch of text) { await page.keyboard.type(ch); await wait(delay); } };

  await page.goto('file://' + path.join(ROOT, 'sdk', 'studio', 'index.html'));
  await page.waitForFunction(() => window.app);
  await page.mouse.move(mx, my);
  await rec.start();

  /* ---------------------------------------------- 1. pixel art */
  rec.mark('pixel');
  await wait(500);
  await click(...(await page.locator('.wstabs [data-ws=pixel]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])));
  await wait(400);
  // zoom on the empty rows where the villager's tiles go
  const ZOOM = 7;
  await ev(z => { const p = app.pixel; p.zoom = z; const r = p.canvas.getBoundingClientRect(); p.px = Math.round(r.width / 2 - 64 * z); p.py = Math.round(r.height / 2 - 80 * z); app.requestRender(); }, ZOOM);
  await wait(500);
  const pb = await page.locator('#pxc').boundingBox();
  const view = await ev(() => ({ px: app.pixel.px, py: app.pixel.py, z: app.pixel.zoom }));
  const at = ([sx, sy]) => [pb.x + view.px + (sx + 0.5) * view.z, pb.y + view.py + (sy + 0.5) * view.z];
  const tool = async (k, name) => { if ((await ev(() => app.S.pxTool)) !== name) { await page.keyboard.press(k); await wait(120); } };
  let colour = -1;
  for (const [kind, c, a, b] of artScript()) {
    if (c !== colour) { await ev(c => app.setColour(c), c); colour = c; await wait(80); }
    if (kind === 'rect') {                     // Shift: a filled rectangle
      await tool('u', 'rect');
      await glide(...at(a), 220);
      await page.keyboard.down('Shift');
      await page.mouse.down();
      await page.mouse.move(...at(b), { steps: 14 });
      await page.mouse.up();
      await page.keyboard.up('Shift');
      await wait(80);
    }
    else if (kind === 'line') { await tool('l', 'line'); await drag(...at(a), ...at(b), 220); }
  }
  await wait(600);

  /* ---------------------------------------------- 2. the model */
  rec.mark('model');
  await click(...(await page.locator('.wstabs [data-ws="3d"]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])));
  await wait(300);
  const vb = await page.locator('#glc').boundingBox();
  const cams = {
    front: { yaw: 0.55, pitch: 0.32, dist: 13.5, target: [0, 2.6, 0.5] },
    right: { yaw: -0.75, pitch: 0.32, dist: 13.5, target: [0, 2.6, 0.5] },
  };
  const setCam = c => ev(c => { Object.assign(app.view.cam, { yaw: c.yaw, pitch: c.pitch, dist: c.dist, target: c.target.slice() }); app.requestRender(); }, c);
  const orbitTo = async c => {                       // a right-button drag, then the exact view
    const cur = await ev(() => ({ yaw: app.view.cam.yaw }));
    const dx = (c.yaw - cur.yaw) * -200;
    const x0 = vb.x + vb.width / 2, y0 = vb.y + vb.height * 0.75;
    await glide(x0, y0, 250);
    await page.mouse.down({ button: 'right' });
    const steps = 30;
    for (let i = 1; i <= steps; i++) {
      await page.mouse.move(x0 + dx * i / steps, y0, { steps: 1 });
      await ev(([c, k]) => { const cam = app.view.cam; cam.yaw += (c.yaw - cam.yaw) * k; app.requestRender(); }, [c, 1 / (steps - i + 1)]);
      await wait(16);
    }
    await page.mouse.up({ button: 'right' });
    await setCam(c);
    await wait(150);
  };
  await setCam(cams.front);
  await wait(400);
  const proj = p => ev(p => { const v = app.view, w = v.canvas.clientWidth, h = v.canvas.clientHeight; v.cam.matrix(v.canvas.width / v.canvas.height); return v.cam.project(p, w, h); }, p);
  const onView = async p => { const s = await proj(p); return [vb.x + s[0], vb.y + s[1]]; };
  const tilesBox = await page.locator('#tilesc').boundingBox();
  const pickTile = async (tx, ty, w = 1, h = 1) => {
    const z = await ev(() => app.tiles.zoom), o = await ev(() => [app.tiles.px, app.tiles.py]);
    const x0 = tilesBox.x + o[0] + (tx * 16 + 8) * z, y0 = tilesBox.y + o[1] + (ty * 16 + 8) * z;
    if (w === 1 && h === 1) await click(x0, y0, 350);
    else await drag(x0, y0, x0 + (w - 1) * 16 * z, y0 + (h - 1) * 16 * z, 300);
  };
  await ev(() => { app.S.plane = 'floor'; app.S.planeLevel = [0, 0, 0]; });   // the blocks go on the floor
  await page.keyboard.press('2');                      // the Block tool
  await wait(200);
  let curTile = '';
  for (const b of BUILD) {
    if (b.view) await orbitTo(cams[b.view]);
    if (b.tile !== curTile) { await pickTile(...T[b.tile]); curTile = b.tile; }
    let p;
    if (b.at === 'floor') p = [b.cell[0] + 0.5, 0, b.cell[2] + 0.5];
    else {
      const [x, y, z] = b.on;
      p = { top: [x + 0.5, y + 1, z + 0.5], '-x': [x, y + 0.5, z + 0.5], '+x': [x + 1, y + 0.5, z + 0.5] }[b.side];
    }
    await click(...(await onView(p)), 320, 160);
  }
  const cells = await ev(() => {
    const out = new Set();
    for (const f of app.model().faces) { const c = BM.faceCenter(f), n = BM.faceNormal(f); out.add(c.map((v, k) => Math.floor(v - n[k] * 0.25)).join(',')); }
    return [...out].sort();
  });
  console.log('cells:', cells.join(' '));
  // the face: a 2 x 2 stamp of the four face tiles on the front of the head
  await page.keyboard.press('1');
  await wait(150);
  await pickTile(T.face[0], T.face[1], 2, 2);
  await click(...(await onView([-0.5, 4.5, 0])), 400, 300);
  await page.keyboard.press('3');
  await glide(vb.x + vb.width - 60, vb.y + 140, 300);
  // a look around, with the light as on bm
  await page.keyboard.press('l');
  await wait(300);
  await orbitTo({ yaw: -0.5, pitch: 0.3, dist: 12.5, target: [0, 2.8, 0.5] });
  await orbitTo({ yaw: 0.35, pitch: 0.25, dist: 12, target: [0, 2.8, 0.5] });
  await wait(500);
  // the model's name and the cartridge's title
  // the model's name, then the title in the Cartridge panel; the code starts
  // with one line: the game is written on the console, in bm Code
  await ev(() => { app.model().name = 'villager'; app.refreshModels && app.refreshModels(); });
  const center = async sel => { const b = await page.locator(sel).boundingBox(); return [b.x + b.width / 2, b.y + b.height / 2]; };
  await click(...(await center('[data-side=cart]')), 350, 250);
  await click(...(await center('#cartTitle')), 300, 60);
  await page.keyboard.press('Control+a');
  await type('My Village', 60);
  await click(...(await center('#cartAuthor')), 250, 60);
  await page.keyboard.press('Control+a');
  await type('bm', 60);
  await page.fill('#cartLua', '-- My Village: a villager on a map\n');
  await wait(500);
  const s1 = await ev(() => ({ faces: app.model().faces.length, name: app.model().name }));
  console.log('model:', JSON.stringify(s1));
  await wait(300);

  /* ---------------------------------------------- 3. the skeleton */
  await rec.stop();
  await click(...(await page.locator('.applink').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 400, 0);
  await page.waitForURL(/animator/);
  await page.waitForFunction(() => window.app && app.project && app.project.models[0] && app.project.models[0].name === 'villager');
  await page.mouse.move(mx, my);
  await rec.start();
  rec.mark('rig');
  await wait(500);
  const av = await page.locator('#glc').boundingBox();
  await ev(() => { const c = app.view.cam; Object.assign(c, { yaw: 0.35, pitch: 0.22, dist: 11, target: [0, 2.9, 0.5] }); app.requestRender(); });
  await wait(300);
  const aproj = async p => { const s = await ev(p => app.view.toScreen(p), p); return [av.x + s[0], av.y + s[1]]; };
  const bones = () => ev(() => app.rig() ? app.rig().bones.map(b => ({ name: b.name, head: b.head, tail: b.tail })) : []);
  const moveJoint = async (i, which, to, shift) => {
    const b = (await bones())[i];
    const from = await aproj(b[which]);
    if (shift) await page.keyboard.down('Shift');
    await drag(...from, ...(await aproj(to)), 450);
    if (shift) await page.keyboard.up('Shift');
    await ev(([i, which, to]) => { const b = app.rig().bones[i]; if (b[which].some((v, k) => Math.abs(v - to[k]) > 1e-6)) { app.edit('joint', () => { b[which] = to.slice(); }); } }, [i, which, to]);
  };
  const rename = async name => {
    const box = await page.locator('#bpName').boundingBox();
    await click(box.x + box.width / 2, box.y + box.height / 2, 300, 50);
    await page.keyboard.press('Control+a');
    await type(name, 45);
    await page.keyboard.press('Enter');
    await wait(150);
  };
  const plus = async () => click(...(await page.locator('[data-cmd=addBone]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 300, 200);
  const Z = 0.5;
  // the hips: from the top of the legs up to the shoulders
  await plus();
  await moveJoint(0, 'head', [0, 2, Z]);
  await moveJoint(0, 'tail', [0, 4, Z]);
  await rename('hips');
  // the head
  await ev(() => app.selectBone(0));
  await plus();
  await moveJoint(1, 'tail', [0, 6, Z]);
  await rename('head');
  // an arm: from the shoulder down to the hand (only this bone's joint moves)
  await ev(() => app.selectBone(0));
  await plus();
  await moveJoint(2, 'head', [1.5, 4, Z], true);
  await moveJoint(2, 'tail', [1.5, 2.1, Z]);
  await rename('arm.L');
  await click(...(await page.locator('[data-cmd=mirrorBones]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 250);
  // a leg: from the hip down to the foot
  await ev(() => app.selectBone(0));
  await plus();
  await moveJoint(4, 'head', [0.5, 2, Z], true);
  await moveJoint(4, 'tail', [0.5, 0.1, Z]);
  await rename('leg.L');
  await click(...(await page.locator('[data-cmd=mirrorBones]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 250);
  // the skin: each face follows the nearest bone (rigid parts)
  await click(...(await page.locator('[data-rtool=skin]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 200);
  await click(...(await page.locator('[data-cmd=autoFace]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 600);
  // the body: its faces are as near to the arms and legs as to the hips (blocks): a box, then Assign
  const tl0 = await aproj([-1, 4, 0]), br0 = await aproj([1, 2, 0]);
  await drag(tl0[0] + 6, tl0[1] + 6, br0[0] - 6, br0[1] - 6, 500);
  await ev(() => {                          // the faces behind and on the sides too
    app.selFaces = new Set(app.model().faces.filter(f => { const c = BM.faceCenter(f); return Math.abs(c[0]) <= 1.01 && c[1] > 2 && c[1] < 4; }));
    app.selectBone(0);
    app.requestRender();
  });
  await wait(300);
  await click(...(await page.locator('[data-cmd=assign]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 500);
  // the head too: its sides are near the shoulders
  const tl1 = await aproj([-1, 6, 0]), br1 = await aproj([1, 4, 0]);
  await drag(tl1[0] + 6, tl1[1] + 6, br1[0] - 6, br1[1] - 6, 450);
  await ev(() => {
    app.selFaces = new Set(app.model().faces.filter(f => BM.faceCenter(f)[1] > 4));
    app.selectBone(1);
    app.requestRender();
  });
  await wait(300);
  await click(...(await page.locator('[data-cmd=assign]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 500);
  await page.keyboard.press('Escape');
  await click(...(await page.locator('[data-rtool=bones]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 300, 300);
  console.log('bones:', JSON.stringify((await bones()).map(b => b.name)));
  console.log('skin:', JSON.stringify(await ev(() => {
    const n = {};
    for (const f of app.model().faces) { const k = app.rig().bones[f.b[0]].name; n[k] = (n[k] || 0) + 1; }
    return n;
  })));

  /* ---------------------------------------------- 4. the walk */
  rec.mark('animate');
  await click(...(await page.locator('[data-mode=anim]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 300);
  if (await ev(() => app.S.view.skin)) await click(...(await page.locator('#viewbar [data-cmd=toggleSkin]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 300, 200);
  await click(...(await page.locator('[data-cmd=newClip]').boundingBox().then(b => [b.x + b.width / 2, b.y + b.height / 2])), 350, 300);
  await page.keyboard.press('Control+a');
  await type('walk', 60);
  await page.keyboard.press('Enter');
  await wait(300);
  await page.fill('#clipLen', '0.8');
  await page.locator('#clipLen').dispatchEvent('change');
  await wait(200);
  const idx = async name => (await bones()).findIndex(b => b.name === name);
  const turn = async (name, deg) => {
    const i = await idx(name);
    const mid = await ev(i => { const b = BM.rig.posedBones(app.rig(), app.currentPose())[i]; return b.head.map((v, k) => (v + b.tail[k]) / 2); }, i);
    await click(...(await aproj(mid)), 300, 120);                 // choose the bone in the view
    await ev(i => app.selectBone(i), i);
    const box = await page.locator('#boneProps input[data-v=rot][data-k="0"]').boundingBox();
    await click(box.x + box.width / 2, box.y + box.height / 2, 250, 40);
    await page.keyboard.press('Control+a');
    await type(String(deg), 50);
    await page.keyboard.press('Enter');
    await wait(150);
  };
  await turn('leg.L', 32);
  await turn('leg.R', -32);
  await turn('arm.L', -28);
  await turn('arm.R', 28);
  await page.keyboard.press('Control+c');
  await wait(200);
  // half way: the same pose, mirrored
  const tl = await page.locator('#tlc').boundingBox();
  const tAt = t => ev(t => app.timeline.x ? app.timeline.x(t) : null, t);
  await ev(() => app.setTime(0.4));
  await page.keyboard.press('Control+v');
  await wait(150);
  await page.keyboard.press('m');
  await wait(400);
  // play it, looking around
  await ev(() => app.setTime(0));
  rec.mark('animate-play');
  await page.keyboard.press('Space');
  await wait(1200);
  const playYaw = await ev(() => app.view.cam.yaw);
  for (let i = 0; i <= 60; i++) { await ev(y => { app.view.cam.yaw = y; app.requestRender(); }, playYaw - i * 0.025); await wait(33); }
  await wait(800);
  await page.keyboard.press('Space');
  const walk = await ev(() => { const c = app.rig().clips.find(c => c.name === 'walk'); return { keys: c.keys.map(k => k.t), length: c.length, loop: c.loop }; });
  console.log('walk:', JSON.stringify(walk));
  rec.mark('end-web');
  await wait(200);
  await rec.stop();

  // the cartridge, for the console part
  const [dl] = await Promise.all([page.waitForEvent('download'), page.keyboard.press('Control+s')]);
  await dl.saveAs(path.join(OUT, 'villager.bm'));
  rec.save();
  await browser.close();
  if (errors.length) { console.log('errors in the page: ' + errors.join('; ')); process.exit(1); }
  console.log(`web: ${rec.frames.length} frames, villager.bm saved in ${OUT}`);
})().catch(e => { console.error(e); process.exit(1); });
