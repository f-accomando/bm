#!/usr/bin/env node
/*
 * The cover of bm Pixel (carts/pixel): a little sprite drawn big on its
 * grid, the pointer on a pixel, and a strip of the palette. The PNG is in
 * git: `make` does not need Node. To change it, change this script and run
 * it again:
 *
 *   node carts/pixel/mkcover.js
 */
'use strict';
const fs = require('fs');
const path = require('path');

require(path.join(__dirname, '..', '..', 'sdk', 'studio', 'js', 'core.js'));
const BM = globalThis.BM;

const CW = 128, CH = 80;
const img = BM.newImage(CW, CH);
const put = (x, y, c) => {
  if (x < 0 || y < 0 || x >= CW || y >= CH) return;
  const o = (y * CW + x) * 4;
  img.px[o] = c >> 16 & 255; img.px[o + 1] = c >> 8 & 255; img.px[o + 2] = c & 255; img.px[o + 3] = 255;
};
const box = (x, y, w, h, c) => { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) put(x + i, y + j, c); };
for (let y = 0; y < CH; y++) {
  const k = y / CH;
  box(0, y, CW, 1, (0x14 + 10 * k) << 16 | (0x16 + 14 * k) << 8 | (0x26 + 30 * k));
}

// a slime of 10x8 pixels, 6 screen pixels each
const S = [
  '...####...',
  '..#oooo#..',
  '.#oooooo#.',
  '#oowooowo#',
  '#oobooobo#',
  '#oooooooo#',
  '#oooommoo#',
  '.########.',
];
const COL = { '#': 0x1D3A2A, o: 0x5CB048, w: 0xFFFFFF, b: 0x101018, m: 0x2E6A3A };
const Z = 6, X0 = 40, Y0 = 12;
// the checkerboard of the transparent pixels, then the grid
for (let j = 0; j < 8; j++)
  for (let i = 0; i < 10; i++) {
    const c = S[j][i];
    box(X0 + i * Z, Y0 + j * Z, Z, Z, c === '.' ? ((i + j) % 2 ? 0x2A2E3A : 0x343846) : COL[c]);
  }
for (let i = 0; i <= 10; i++) box(X0 + i * Z, Y0, 1, 8 * Z, 0x3C4254);
for (let j = 0; j <= 8; j++) box(X0, Y0 + j * Z, 10 * Z + 1, 1, 0x3C4254);
// the pointer on a pixel
const px = X0 + 7 * Z, py = Y0 + 5 * Z;
for (let i = -1; i <= Z; i++) { put(px + i, py - 1, 0xFFE070); put(px + i, py + Z, 0xFFE070); }
for (let j = -1; j <= Z; j++) { put(px - 1, py + j, 0xFFE070); put(px + Z, py + j, 0xFFE070); }
// the sprite at its own size, beside
for (let j = 0; j < 8; j++) for (let i = 0; i < 10; i++) if (S[j][i] !== '.') put(108 + i, 14 + j, COL[S[j][i]]);
// the palette
const PAL = [0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
             0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA];
PAL.forEach((c, i) => box(8 + i * 7, 68, 6, 6, c));
// "P" in the corner, in the orange of the tools
const P = ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'];
for (let y = 0; y < 7; y++)
  for (let x = 0; x < 5; x++)
    if (P[y][x] === '#') box(6 + x * 3, 6 + y * 3, 3, 3, 0xFFC050);

(async () => {
  fs.writeFileSync(path.join(__dirname, 'cover.png'), await BM.encodePNG(img));
  console.log('carts/pixel: cover.png (128x80)');
})();
