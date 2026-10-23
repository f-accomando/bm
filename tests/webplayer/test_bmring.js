// Test of web/bmring.js: the 60 Hz clock and the sound queue of the web player, with a fake clock.
// Run: node tests/webplayer/test_bmring.js
"use strict";
const assert = require("assert");
const path = require("path");
const { BmRing, BmPacer } = require(path.join(__dirname, "..", "..", "web", "bmring.js"));

// small deterministic random numbers
let seed = 12345;
const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;

// run n frames with a timer that fires 0..jit ms late; the frame itself takes work ms
function run(p, n, jit, work, pauseAt, pauseMs) {
  let t = 1000;
  const times = [];
  for (let i = 0; i < n; i++) {
    const d = p.delay(t);
    times.push(t);
    t += d + rnd() * jit + 1;       /* the timer: late by 0..jit ms, and the 1 ms the pacer keeps */
    t += work;
    if (i === pauseAt) t += pauseMs;
  }
  return times;
}

// 1. a normal run: 600 frames at 60 Hz, a timer that is up to 4 ms late
{
  const p = new BmPacer(1000 / 60, 250);
  const times = run(p, 600, 4, 1);
  const rate = 1000 * (times.length - 1) / (times[times.length - 1] - times[0]);
  assert.ok(Math.abs(rate - 60) < 0.6, "rate " + rate);
  assert.ok(p.worst < 1000 / 60 + 6, "worst " + p.worst);
  assert.strictEqual(p.resets, 0);
  console.log("pacer: " + rate.toFixed(2) + " Hz, worst frame " + p.worst.toFixed(1) + " ms");
}

// 2. a slow frame is paid back by the next frames (no drift)
{
  const p = new BmPacer(1000 / 60, 250);
  const times = run(p, 300, 0, 0);
  assert.ok(Math.abs((times[299] - times[0]) - 299 * 1000 / 60) < 4, "span " + (times[299] - times[0]));
  p.reset();
  const t2 = [];
  let t = 5000;
  for (let i = 0; i < 120; i++) {
    const d = p.delay(t);
    t += i === 10 ? 60 : d + 1;      /* frame 10 takes 60 ms: 3 frames late */
    t2.push(t);
  }
  const rate = 1000 * 119 / (t2[119] - t2[0]);
  assert.ok(Math.abs(rate - 60) < 1.2, "rate after a slow frame " + rate);
}

// 3. a long pause (hidden tab): no burst of frames afterwards, the clock restarts
{
  const p = new BmPacer(1000 / 60, 250);
  const times = run(p, 200, 1, 0, 99, 5000);
  assert.strictEqual(p.resets, 1);
  for (let i = 101; i < 120; i++) assert.ok(times[i] - times[i - 1] > 12, "burst at " + i);
  assert.ok(p.worst > 5000);
}

// 4. the ring gives back the samples in order, and silence when it is empty
{
  const r = new BmRing(14400);
  const chunk = (a, n) => { const c = new Float32Array(n * 2); for (let i = 0; i < n; i++) { c[2 * i] = a + i; c[2 * i + 1] = -(a + i); } return c; };
  r.push(chunk(0, 800)); r.push(chunk(800, 800));
  const l = new Float32Array(128), rr = new Float32Array(128);
  let got = [];
  for (let b = 0; b < 13; b++) { r.pull(l, rr); got = got.concat(Array.from(l)); assert.strictEqual(rr[5], -l[5]); }
  for (let i = 0; i < 1600; i++) assert.strictEqual(got[i], i, "sample " + i);
  for (let i = 1600; i < got.length; i++) assert.strictEqual(got[i], 0);
  assert.strictEqual(r.under, 1);
  assert.strictEqual(r.n, 0);
}

// 5. the queue is capped: a producer that is too fast loses the oldest sound, never the newest
{
  const r = new BmRing(2400);
  const chunk = (a) => { const c = new Float32Array(1600); c.fill(a); return c; };
  for (let i = 1; i <= 10; i++) r.push(chunk(i));       /* 10 x 800 frames */
  assert.ok(r.n <= 2400 + 800 && r.n >= 800, "n " + r.n);
  assert.ok(r.drops > 0);
  const l = new Float32Array(128), rr = new Float32Array(128);
  let last = 0;
  while (r.n > 0) { r.pull(l, rr); last = l[0]; }
  assert.strictEqual(last, 10, "the newest chunk survives");
  assert.strictEqual(r.n, 0);
}

// 6. steady state: a 60 Hz producer (800 frames) and a 128 frame consumer at 48 kHz never underrun
{
  const r = new BmRing(14400);
  let produced = 0, pulled = 0, t = 0;
  const l = new Float32Array(128), rr = new Float32Array(128);
  r.push(new Float32Array(1600));                         /* one frame of lead */
  for (let step = 0; step < 48000 / 128 * 5; step++) {    /* 5 s */
    t = step * 128 / 48000 * 1000;
    while (produced < Math.floor(t / (1000 / 60)) + 1) { r.push(new Float32Array(1600)); produced++; }
    r.pull(l, rr); pulled += 128;
  }
  assert.strictEqual(r.under, 0, "underruns " + r.under);
  assert.strictEqual(r.drops, 0);
}

// 7. the worklet gets the class as text: it must stand alone
{
  const src = BmRing.toString();
  const Copy = new Function("return (" + src + ")")();
  const r = new Copy(100);
  r.push(new Float32Array([1, 2, 3, 4]));
  const l = new Float32Array(2), rr = new Float32Array(2);
  r.pull(l, rr);
  assert.deepStrictEqual([l[0], rr[0], l[1], rr[1]], [1, 2, 3, 4]);
}
console.log("bmring: ok");
