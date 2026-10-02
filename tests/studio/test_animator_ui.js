#!/usr/bin/env node
/*
 * bm Animator in a real browser (Playwright + Chromium): the example
 * villager, bones made and moved with the mouse, skin, a pose turned with
 * the rings becoming a keyframe, the timeline, playing, undo, sprites put
 * in the sheet, saving, the .glb with the skeleton, and the project going
 * to bm Studio and back. Screenshots go to OUTDIR.
 *
 *   node tests/studio/test_animator_ui.js OUTDIR     (make test-studio-ui)
 */
'use strict';
const fs = require('fs');
const path = require('path');

function loadPlaywright() {
  for (const p of ['playwright', '/opt/node22/lib/node_modules/playwright']) {
    try { return require(p); } catch (e) { /* next */ }
  }
  console.log('test-studio-ui: the playwright package is missing (npm install playwright)');
  process.exit(1);
}

const ROOT = path.join(__dirname, '..', '..');
const OUT = path.resolve(process.argv[2] || path.join(ROOT, 'build', 'studio'));
let checks = 0, fails = 0;
function check(ok, msg) { checks++; if (!ok) { fails++; console.log('FAIL ' + msg); } }

(async () => {
  const { chromium } = loadPlaywright();
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const ctx = await browser.newContext({ viewport: { width: 1400, height: 820 }, acceptDownloads: true });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.addInitScript(() => { delete window.showSaveFilePicker; delete window.showOpenFilePicker; });
  await page.goto('file://' + path.join(ROOT, 'sdk', 'animator', 'index.html'));
  await page.waitForFunction(() => window.app);
  const shot = name => page.screenshot({ path: path.join(OUT, name + '.png') });
  const ev = (fn, arg) => page.evaluate(fn, arg);
  const vb = await page.locator('#glc').boundingBox();
  const at = p => [vb.x + p[0], vb.y + p[1]];

  // the example: the villager, 7 bones, 3 animations
  let s = await ev(() => ({ name: app.model().name, bones: app.rig().bones.length, clips: app.rig().clips.map(c => c.name).join() }));
  check(s.name === 'villager' && s.bones === 7 && s.clips === 'idle,walk,wave', 'the example villager: ' + JSON.stringify(s));
  await shot('a1-rig');

  // Rig: click a bone in the view to choose it
  const legMid = await ev(() => { const b = app.rig().bones[5]; return app.view.toScreen(b.head.map((v, k) => (v + b.tail[k]) / 2)); });
  await page.mouse.click(...at(legMid));
  check(await ev(() => app.bone) === 5, 'a click on the left leg chooses it');

  // a new bone under the head, moved by its tail; undo
  await ev(() => app.selectBone(2));
  await page.keyboard.press('n');
  s = await ev(() => ({ n: app.rig().bones.length, b: app.rig().bones[7] }));
  check(s.n === 8 && s.b.parent === 2 && s.b.head.join() === '0,2,0', 'N: a child of the head, from its tail');
  const tail = await ev(() => app.view.toScreen(app.rig().bones[7].tail));
  await page.mouse.move(...at(tail));
  await page.mouse.down();
  await page.mouse.move(at(tail)[0] + 50, at(tail)[1], { steps: 5 });
  await page.mouse.up();
  const moved = await ev(() => app.rig().bones[7].tail);
  check(moved[0] > 0.1 && Math.abs(moved[0] * 32 - Math.round(moved[0] * 32)) < 1e-6, 'the tail moved, on the 1/32 grid: ' + moved);
  await page.keyboard.press('Control+z');
  await page.keyboard.press('Control+z');
  check(await ev(() => app.rig().bones.length) === 7, 'undo twice: the new bone is gone');

  // skin: choose the faces of the head with a box... simpler: assign one face, then Auto
  await page.click('[data-rtool=skin]');
  const faceAt = await ev(() => app.view.toScreen(BM.faceCenter(app.model().faces.find(f => f.c === 0x3478C4 && BM.faceNormal(f)[2] < -0.9))));
  await page.mouse.click(...at(faceAt));
  await ev(() => app.selectBone(2));
  await page.keyboard.press('a');
  s = await ev(() => ({ sel: app.selFaces.size, b: [...app.selFaces][0].b }));
  check(s.sel === 1 && s.b.every(x => x === 2), 'A: the chosen face follows the head');
  await page.click('[data-cmd=autoFace]');
  s = await ev(() => app.model().faces.filter(f => f.c === 0x3478C4 && BM.faceNormal(f)[2] < -0.9)[0].b[0]);
  check(s === 1, 'Auto (on the chosen faces): the body front back to the nearest bone, the spine');
  await page.keyboard.press('Escape');
  await page.click('[data-rtool=bones]');

  // Animate: the walk; choose the left arm; turn it with the red ring at a new time
  await page.keyboard.press('2');
  await ev(() => { app.clip = 1; app.refreshClips(); app.setTime(0.3); app.selectBone(3); });
  const keys0 = await ev(() => app.clipObj().keys.length);
  const ring = await ev(() => { const v = app.view, c = BM.rig.posedBones(app.rig(), app.currentPose())[3].head; return v.toScreen(v.ringPoints(0, c, v.ringRadius())[10]); });
  await page.mouse.move(...at(ring));
  await page.mouse.down();
  await page.mouse.move(at(ring)[0] + 25, at(ring)[1] + 35, { steps: 6 });
  await page.mouse.up();
  s = await ev(() => ({ keys: app.clipObj().keys.map(k => k.t), q: app.clipObj().keys.find(k => Math.abs(k.t - 0.3) < 1e-4) }));
  check(s.keys.length === keys0 + 1 && s.q && Math.abs(s.q.pose[3].q[0]) > 0.05, 'a turn on the ring: a keyframe at 0.3 s (auto key) ' + JSON.stringify(s.keys));
  await shot('a2-animate');

  // the timeline: a click scrubs, Space plays
  const tl = await page.locator('#tlc').boundingBox();
  await page.mouse.click(tl.x + tl.width * 0.75, tl.y + tl.height / 2);
  const t1 = await ev(() => app.time);
  check(t1 > 0.5 && t1 < 0.65 && Math.abs(t1 * 12 - Math.round(t1 * 12)) < 1e-6, 'a click on the timeline: a time on a frame ' + t1);
  await page.keyboard.press('Space');
  await page.waitForTimeout(400);
  await page.keyboard.press('Space');
  check(await ev(() => !app.playing && app.time !== 0.5), 'Space plays and stops');
  // K, then Del
  await ev(() => app.setTime(0.25));
  const before = await ev(() => app.clipObj().keys.length);
  await page.keyboard.press('k');
  await page.keyboard.press('Delete');
  check(await ev(() => app.clipObj().keys.length) === before, 'K adds a keyframe, Del takes it away');
  // the length cannot cut keyframes away
  await page.fill('#clipLen', '0.1');
  await page.locator('#clipLen').dispatchEvent('change');
  check(await ev(() => app.clipObj().length) >= 0.6, 'the length stays past the last keyframe');

  // Sprites: the walk from 4 directions, into the sheet
  await page.click('[data-mode=sprites]');
  await page.waitForTimeout(300);
  await page.selectOption('#sprClip', '1');
  await page.waitForTimeout(200);
  s = await ev(() => app.spr && { w: app.spr.img.w, h: app.spr.img.h, rows: app.spr.rows, len: app.project.models[0].rig.clips[1].length });
  check(s && s.rows === 4 && s.w === 48 * Math.max(1, Math.round(s.len * 12)) && s.h === 4 * 48, 'the sprites of the walk: ' + JSON.stringify(s));
  await shot('a3-sprites');
  await page.click('[data-cmd=spritesToSheet]');
  await page.waitForSelector('#modal:not([hidden])');
  const lua = await page.locator('#luaCode').textContent();
  check(/local VILLAGER_WALK = \{ x = \d+, y = \d+/.test(lua), 'Lua code of the sprites');
  await page.keyboard.press('Enter');
  s = await ev(() => { const L = app.lastSprites, sh = app.project.sheet; return { at: L.at, a: sh.px[((L.at[1] + 20) * sh.w + L.at[0] + 24) * 4 + 3] }; });
  check(s.a === 255, 'the sprites are in the sheet at ' + s.at);

  // save (download) and read back
  const [dl] = await Promise.all([page.waitForEvent('download'), page.keyboard.press('Control+s')]);
  const saved = path.join(OUT, 'animator-saved.bm');
  await dl.saveAs(saved);
  s = await ev(async b64 => {
    const { project, warnings } = BM.parseCart(Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    const m = project.models[0];
    return { warnings, bones: m.rig.bones.length, clips: m.rig.clips.map(c => c.keys.length).join(), sheet: project.sheet.h };
  }, fs.readFileSync(saved).toString('base64'));
  check(!s.warnings.length && s.bones === 7 && s.clips.split(',').length === 3, 'saved and read back: ' + JSON.stringify(s));

  // .glb with the skeleton
  await page.click('[data-mode=rig]');
  const [dlg] = await Promise.all([page.waitForEvent('download'), ev(() => app.cmd('exportGlb'))]);
  const glb = path.join(OUT, 'villager.glb');
  await dlg.saveAs(glb);
  s = await ev(b64 => { const { js } = BM.glbParse(Uint8Array.from(atob(b64), c => c.charCodeAt(0))); return [js.skins.length, js.animations.length, js.asset.generator]; },
    fs.readFileSync(glb).toString('base64'));
  check(s.join() === '1,3,bm Animator', '.glb with skin and animations: ' + s);

  // to bm Studio and back, with the skeleton
  await Promise.all([page.waitForURL(/studio/), page.click('.applink')]);
  await page.waitForFunction(() => window.app && app.project && app.project.models[0] && app.project.models[0].rig, null, { timeout: 5000 });
  s = await ev(() => ({ name: app.project.models[0].name, bones: app.project.models[0].rig.bones.length, dirty: app.dirty }));
  check(s.name === 'villager' && s.bones === 7, 'in bm Studio with its bones');
  await Promise.all([page.waitForURL(/animator/), page.click('.applink')]);
  await page.waitForFunction(() => window.app && app.project && app.project.models[0].name === 'villager' && app.fileName, null, { timeout: 5000 });
  check(await ev(() => app.rig().clips.length) === 3, 'and back in bm Animator');

  // models with tiles show the sheet, not the magenta of a missing texture
  {
    const [fc] = await Promise.all([page.waitForEvent('filechooser'), ev(() => app.cmd('open'))]);
    await fc.setFiles(path.join(ROOT, 'carts', 'village', 'models.bm'));
    await page.waitForFunction(() => app.project && app.project.models.some(m => m.name === 'house'));
    await ev(() => { app.cur = app.project.models.findIndex(m => m.name === 'house'); app.S.view.skin = false; app.refreshAll(); app.view.frame(); app.requestRender(); });
    await page.waitForTimeout(400);
    const png = await page.locator('#glc').screenshot();
    const magenta = await ev(async b64 => {
      const img = await BM.decodePNG(Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      let n = 0;
      for (let i = 0; i < img.px.length; i += 4) if (img.px[i] > 240 && img.px[i + 1] < 16 && img.px[i + 2] > 240) n++;
      return n;
    }, png.toString('base64'));
    check(magenta === 0, `the house of Studio Village with its textures (${magenta} magenta pixels)`);
    await shot('a4-textures');
  }

  check(!errors.length, 'no errors in the page: ' + errors.join('; '));
  await browser.close();
  console.log(`animator ui: ${checks - fails}/${checks} checks passed (screenshots in ${OUT})`);
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
