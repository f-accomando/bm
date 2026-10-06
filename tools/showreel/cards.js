#!/usr/bin/env node
/*
 * The showreel's cards, drawn in HTML by Chromium: the title, the end and a
 * caption for each step (transparent PNGs laid over the video by
 * tools/showreel/assemble.py).
 *
 *   node tools/showreel/cards.js OUTDIR
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

const OUT = path.resolve(process.argv[2] || 'build/showreel/cards');
const STEPS = [
  ['1', 'Map', 'SDK, on the console'],
  ['2', 'Code with the AI assistant', 'bm Code, on the console'],
  ['3', 'Play', 'on the console'],
];

const CSS = `
  html, body { margin: 0; width: 1280px; height: 720px; background: transparent; overflow: hidden;
    font-family: 'DejaVu Sans', 'Liberation Sans', sans-serif; }
  .card { position: absolute; inset: 0; background: radial-gradient(ellipse at 50% 40%, #232838 0%, #12141c 70%);
    color: #e8eaf2; display: flex; flex-direction: column; align-items: center; justify-content: center; }
  .logo { display: flex; align-items: center; gap: 18px; margin-bottom: 30px; }
  .badge { background: #f5b82e; color: #12141c; font-weight: 700; font-size: 64px; padding: 2px 22px 8px;
    border-radius: 14px; letter-spacing: 1px; }
  .name { font-size: 46px; font-weight: 700; letter-spacing: 1px; }
  .big { font-size: 50px; font-weight: 700; margin: 0 0 18px; }
  .sub { font-size: 25px; color: #a9b0c6; max-width: 980px; text-align: center; line-height: 1.45; }
  .url { margin-top: 36px; font-size: 26px; color: #f5b82e; font-family: 'DejaVu Sans Mono', monospace; }
  .steps { margin-top: 34px; display: flex; gap: 12px; flex-wrap: wrap; justify-content: center; max-width: 1100px; }
  .steps span { font-size: 18px; color: #c8cde0; background: #262b3b; border: 1px solid #3a4157; border-radius: 20px; padding: 6px 14px; }
  .cap { position: absolute; left: 28px; bottom: 26px; display: flex; align-items: center; gap: 14px;
    background: rgba(18, 20, 28, 0.88); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 16px;
    padding: 12px 22px 12px 12px; box-shadow: 0 6px 24px rgba(0, 0, 0, 0.45); }
  .cap.tr { left: auto; right: 28px; bottom: auto; top: 26px; }
  .num { width: 44px; height: 44px; border-radius: 12px; background: #f5b82e; color: #12141c; font-weight: 700;
    font-size: 26px; display: flex; align-items: center; justify-content: center; }
  .t1 { color: #f2f3f8; font-size: 27px; font-weight: 700; line-height: 1.1; }
  .t2 { color: #a9b0c6; font-size: 18px; margin-top: 3px; }
`;

(async () => {
  const { chromium } = loadPlaywright();
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const shot = async (html, name) => {
    await page.setContent(`<style>${CSS}</style>${html}`);
    await page.evaluate(() => document.fonts.ready);
    await page.screenshot({ path: path.join(OUT, name), omitBackground: true });
  };
  await shot(`<div class="card">
      <div class="logo"><span class="badge">bm</span><span class="name">BareMetal</span></div>
      <div class="big">A game from scratch, with bm alone</div>
      <div class="sub">a map painted and a game programmed for a villager<br>with bm's own tools, on the console itself</div>
      <div class="steps">${STEPS.map(s => `<span>${s[0]} · ${s[1]}</span>`).join('')}</div>
    </div>`, 'title.png');
  await shot(`<div class="card">
      <div class="logo"><span class="badge">bm</span><span class="name">BareMetal</span></div>
      <div class="sub">a bare-metal fantasy console for the Raspberry Pi Zero W:<br>games in Lua, 2D and 3D graphics, the editors on the console itself</div>
      <div class="url">github.com/f-accomando/bm</div>
    </div>`, 'end.png');
  // two places: bottom left, top right (the console's scenes)
  for (const [n, t1, t2] of STEPS)
    for (const [cls, suffix] of [['', ''], [' tr', '-tr']])
      await shot(`<div class="cap${cls}"><div class="num">${n}</div><div><div class="t1">${t1}</div><div class="t2">${t2}</div></div></div>`, `step${n}${suffix}.png`);
  await browser.close();
  console.log('cards: ' + OUT);
})().catch(e => { console.error(e); process.exit(1); });
