#!/usr/bin/env node
/*
 * The cover of bm Mesh (carts/mesh): a faceted crystal as the editor shows
 * it, flat-shaded, with its edges, its vertices (three chosen, in yellow)
 * and the pointer. The PNG is in git: `make` does not need Node. To change
 * it, change this script and run it again:
 *
 *   node carts/mesh/mkcover.js
 */
'use strict';
const fs = require('fs');
const path = require('path');

require(path.join(__dirname, '..', '..', 'sdk', 'studio', 'js', 'core.js'));
const BM = globalThis.BM;

const CW = 128, CH = 80;
const img = BM.newImage(CW, CH);
const put = (x, y, c) => {
  x = Math.round(x); y = Math.round(y);
  if (x < 0 || y < 0 || x >= CW || y >= CH) return;
  const o = (y * CW + x) * 4;
  img.px[o] = c >> 16 & 255; img.px[o + 1] = c >> 8 & 255; img.px[o + 2] = c & 255; img.px[o + 3] = 255;
};
for (let y = 0; y < CH; y++) {
  const k = y / CH;
  for (let x = 0; x < CW; x++) put(x, y, (0x14 + 10 * k) << 16 | (0x16 + 14 * k) << 8 | (0x26 + 30 * k));
}

// a hexagonal crystal: a ring in the middle, a point above and below
const verts = [[0, 1.35, 0]];
for (let i = 0; i < 6; i++) {
  const a = i / 6 * Math.PI * 2;
  verts.push([Math.cos(a) * 0.75, 0.15 * (i % 2 ? 1 : -1), Math.sin(a) * 0.75]);
}
verts.push([0, -1.1, 0]);
const faces = [];
for (let i = 0; i < 6; i++) {
  const a = 1 + i, b = 1 + (i + 1) % 6;
  faces.push([0, b, a], [7, a, b]);
}

// turned and tilted, then orthographic
const yaw = 0.5, pitch = 0.35, S = 30, CX = 72, CY = 42;
const proj = verts.map(([x, y, z]) => {
  const x1 = Math.cos(yaw) * x - Math.sin(yaw) * z, z1 = Math.sin(yaw) * x + Math.cos(yaw) * z;
  const y2 = Math.cos(pitch) * y - Math.sin(pitch) * z1, z2 = Math.sin(pitch) * y + Math.cos(pitch) * z1;
  return [CX + x1 * S, CY - y2 * S, z2];
});
const light = [-0.4, 0.8, -0.5];
const ll = Math.hypot(...light);
const tris = faces.map(f => {
  const [a, b, c] = f.map(i => verts[i]);
  const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
  const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
  const nl = Math.hypot(...n) || 1;
  const lit = Math.max(0, (n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / nl / ll);
  return { f, z: f.reduce((s, i) => s + proj[i][2], 0) / 3, lit };
}).sort((p, q) => q.z - p.z);

const area = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
for (const t of tris) {
  const [a, b, c] = t.f.map(i => proj[i]);
  if (area(a, b, c) <= 0) continue;                 // seen from behind
  const k = 0.45 + 0.55 * t.lit;
  const col = Math.round(0x50 * k) << 16 | Math.round(0x90 * k) << 8 | Math.round(0xE8 * k);
  const x0 = Math.floor(Math.min(a[0], b[0], c[0])), x1 = Math.ceil(Math.max(a[0], b[0], c[0]));
  const y0 = Math.floor(Math.min(a[1], b[1], c[1])), y1 = Math.ceil(Math.max(a[1], b[1], c[1]));
  for (let y = y0; y <= y1; y++)
    for (let x = x0; x <= x1; x++) {
      const p = [x + 0.5, y + 0.5];
      if (area(a, b, p) >= 0 && area(b, c, p) >= 0 && area(c, a, p) >= 0) put(x, y, col);
    }
}
const line = (a, b, c) => {
  const n = Math.ceil(Math.max(Math.abs(b[0] - a[0]), Math.abs(b[1] - a[1]))) || 1;
  for (let i = 0; i <= n; i++) put(a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n, c);
};
for (const t of tris) {
  const [a, b, c] = t.f.map(i => proj[i]);
  if (area(a, b, c) <= 0) continue;
  line(a, b, 0x9AA8D0); line(b, c, 0x9AA8D0); line(c, a, 0x9AA8D0);
}
proj.forEach((p, i) => {
  const chosen = i === 0 || i === 2 || i === 3;
  const r = chosen ? 2 : 1;
  for (let y = -r; y <= r; y++) for (let x = -r; x <= r; x++) put(p[0] + x, p[1] + y, chosen ? 0xFFE070 : 0xD0D8F0);
});
// the pointer on a chosen vertex
const [px, py] = proj[2];
for (let d = 4; d <= 8; d++) {
  put(px + d, py, 0xFFFFFF); put(px - d, py, 0xFFFFFF); put(px, py + d, 0xFFFFFF); put(px, py - d, 0xFFFFFF);
}
// "M" in the corner, in the orange of the tools
const M = ['#...#', '##.##', '#.#.#', '#.#.#', '#...#', '#...#', '#...#'];
for (let y = 0; y < 7; y++)
  for (let x = 0; x < 5; x++)
    if (M[y][x] === '#')
      for (let yy = 0; yy < 3; yy++) for (let xx = 0; xx < 3; xx++) put(6 + x * 3 + xx, 6 + y * 3 + yy, 0xFFC050);

(async () => {
  fs.writeFileSync(path.join(__dirname, 'cover.png'), await BM.encodePNG(img));
  console.log('carts/mesh: cover.png (128x80)');
})();
