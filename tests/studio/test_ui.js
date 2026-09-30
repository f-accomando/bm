#!/usr/bin/env node
/*
 * bm Studio in a real browser (Playwright + Chromium): lays tiles and
 * blocks with the mouse, selects, moves, undoes, paints the sheet, opens
 * a .bm, adds a .glb, saves. Screenshots go to OUTDIR.
 *
 *   node tests/studio/test_ui.js OUTDIR          (make test-studio-ui)
 *
 * Needs the playwright package (npm install playwright; then
 * npx playwright install chromium).
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
  // the file pickers of Chrome cannot be driven: the fallbacks (file input, download)
  await page.addInitScript(() => { delete window.showSaveFilePicker; delete window.showOpenFilePicker; });
  await page.goto('file://' + path.join(ROOT, 'sdk', 'studio', 'index.html'));
  await page.waitForFunction(() => window.app);
  const state = () => page.evaluate(() => ({
    faces: app.model().faces.length, sel: app.selFaces.size, undo: app.undoStack.length, models: app.project.models.length,
    name: app.model().name,
  }));
  const shot = name => page.screenshot({ path: path.join(OUT, name + '.png') });

  const vb = await page.locator('#glc').boundingBox(), cx = vb.x + vb.width / 2, cy = vb.y + vb.height / 2;
  const tb = await page.locator('#tilesc').boundingBox();
  const tileAt = async (tx, ty) => {
    const z = await page.evaluate(() => app.tiles.zoom);
    await page.mouse.click(tb.x + 8 + (tx * 16 + 8) * z, tb.y + 8 + (ty * 16 + 8) * z);
  };

  // tiles: a strip of grass, dragged
  await tileAt(0, 0);
  await page.mouse.move(cx - 120, cy + 20);
  await page.mouse.down();
  await page.mouse.move(cx + 120, cy + 20, { steps: 12 });
  await page.mouse.up();
  let s = await state();
  check(s.faces >= 4 && s.undo === 1, `a drag lays tiles in one step: ${s.faces} faces, ${s.undo} undo`);
  const floor = await page.evaluate(() => app.model().faces.every(f => BM.faceNormal(f)[1] > 0.99));
  check(floor, 'the tiles lie on the floor, facing up');

  // blocks: one, then one on top of it (clicking its top face)
  await tileAt(5, 0);
  await page.keyboard.press('2');
  await page.mouse.click(cx, cy - 40);
  const f1 = (await state()).faces;
  const top = await page.evaluate(() => {
    const top = app.model().faces.find(f => f.p.every(p => p[1] === 1) && BM.faceNormal(f)[1] > 0.99);
    const c = BM.faceCenter(top), w = app.view.canvas.clientWidth, h = app.view.canvas.clientHeight;
    app.view.cam.matrix(app.view.canvas.width / app.view.canvas.height);
    return app.view.cam.project(c, w, h);
  });
  await page.mouse.click(vb.x + top[0], vb.y + top[1]);
  s = await state();
  check(s.faces === f1 + 4, `a block on a block: 4 more faces (${f1} -> ${s.faces})`);
  await shot('1-tiles-blocks');

  // undo, redo
  await page.keyboard.press('Control+z');
  check((await state()).faces === f1, 'undo');
  await page.keyboard.press('Control+y');
  check((await state()).faces === f1 + 4, 'redo');

  // select everything with a box, move it up with PgUp, undo
  await page.keyboard.press('3');
  await page.mouse.move(vb.x + 10, vb.y + 110);
  await page.mouse.down();
  await page.mouse.move(vb.x + vb.width - 10, vb.y + vb.height - 50, { steps: 5 });
  await page.mouse.up();
  s = await state();
  const facing = await page.evaluate(() => {
    const eye = app.view.cam.basis().eye;
    return app.model().faces.filter(f => BM.v3.dot(BM.faceNormal(f), BM.v3.sub(BM.faceCenter(f), eye)) < 0).length;
  });
  check(s.sel === facing, `box selection: the ${facing} faces that show (${s.sel} of ${s.faces})`);
  check(await page.locator('#selbar').isVisible(), 'the selection bar shows');
  await page.keyboard.press('Control+a');
  check((await state()).sel === s.faces, 'Ctrl+A selects all');
  const y0 = await page.evaluate(() => Math.min(...app.model().faces.flatMap(f => f.p.map(p => p[1]))));
  await page.keyboard.press('PageUp');
  const y1 = await page.evaluate(() => Math.min(...app.model().faces.flatMap(f => f.p.map(p => p[1]))));
  check(y1 === y0 + 1, `PgUp moves one square up (${y0} -> ${y1})`);
  await shot('2-select');
  await page.keyboard.press('Control+z');
  await page.keyboard.press('Escape');

  // corners: drag the corners of the top of the second block up into a point
  await page.keyboard.press('4');
  const merged = await page.evaluate(() => {
    const keys = new Set([...BM.edit.vertices(app.model().faces).values()].filter(v => v.p[1] === 2).map(v => v.key));
    app.selVerts = keys;
    app.cmd('merge');
    return app.selVerts.size;
  });
  check(merged === 1, 'four corners merged into one');
  await shot('3-vertex');

  // paint: a stroke on the sheet in the Pixel page
  await page.keyboard.press('Tab');
  const pb = await page.locator('#pxc').boundingBox();
  await page.evaluate(() => app.setColour(0x123456));
  await page.mouse.move(pb.x + pb.width / 2 - 40, pb.y + pb.height / 2 + 60);
  await page.mouse.down();
  await page.mouse.move(pb.x + pb.width / 2 + 40, pb.y + pb.height / 2 + 60, { steps: 8 });
  await page.mouse.up();
  const painted = await page.evaluate(() => {
    const p = app.project.sheet.px;
    let n = 0;
    for (let i = 0; i < p.length; i += 4) if (p[i] === 0x12 && p[i + 1] === 0x34 && p[i + 2] === 0x56) n++;
    return n;
  });
  check(painted > 10, `pencil stroke on the sheet: ${painted} pixels`);
  await shot('4-pixel');
  await page.keyboard.press('Tab');

  // save (download), read it back
  const [dl] = await Promise.all([page.waitForEvent('download'), page.keyboard.press('Control+s')]);
  const saved = path.join(OUT, 'saved.bm');
  await dl.saveAs(saved);
  const back = await page.evaluate(b64 => {
    const b = Uint8Array.from(atob(b64), c => c.charCodeAt(0));
    const { project, warnings } = BM.parseCart(b);
    return { warnings, models: project.models.map(m => m.faces.length), dirty: app.dirty };
  }, fs.readFileSync(saved).toString('base64'));
  check(!back.warnings.length && back.models[0] === (await state()).faces, 'saved and read back');
  check(!back.dirty, 'saved: nothing left to save');

  // open a game of the repository, add a .glb from Chaos Kitchen
  const cart = path.join(ROOT, 'build', 'carts', 'village.bm');
  if (fs.existsSync(cart)) {
    const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate(() => app.cmd('open'))]);
    await fc.setFiles(cart);
    await page.waitForFunction(() => app.project.title === 'Studio Village');
    s = await state();
    check(s.models === 7 && s.name === 'ground', `Studio Village: ${s.models} models`);
    await page.evaluate(() => { app.cur = 1; app.S.view.lit = true; app.modelChanged(true); app.refreshModels(); app.view.frame(); });
    await shot('5-village');
    const [fc2] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate(() => app.cmd('importGlb'))]);
    await fc2.setFiles(path.join(ROOT, 'carts', 'kitchen', 'models', 'chef1.glb'));
    await page.waitForSelector('#modal:not([hidden])');
    await page.keyboard.press('Enter');                     // textures: at most 256
    await page.waitForFunction(() => app.project.models.length === 8);
    const chef = await page.evaluate(() => {
      const faces = app.model().faces, b = BM.modelBounds(faces);
      // it faces the camera of bm (towards -z): more of it shows from the front than from the back
      const eye = [0, 0.5, -5], back = [0, 0.5, 5];
      const seen = e => faces.filter(f => BM.v3.dot(BM.faceNormal(f), BM.v3.sub(e, BM.faceCenter(f))) > 0).length;
      return { faces: faces.length, h: b.hi[1] - b.lo[1], sheet: [app.project.sheet.w, app.project.sheet.h], front: seen(eye), back: seen(back) };
    });
    check(chef.faces > 100 && chef.h > 0.9 && chef.h < 1.1, `the chef from Chaos Kitchen: ${chef.faces} faces, ${chef.h.toFixed(2)} tall`);
    check(chef.sheet[0] <= 256 * 2, `its texture fits in the sheet: ${chef.sheet}`);
    check(chef.front > chef.back, `the chef looks at the camera (front ${chef.front}, back ${chef.back})`);
    await shot('6-chef');
  }
  check(!errors.length, 'no errors in the page: ' + errors.join('; '));
  await browser.close();
  console.log(`studio ui: ${checks - fails}/${checks} checks passed (screenshots in ${OUT})`);
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
