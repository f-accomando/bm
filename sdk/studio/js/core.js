/*
 * bm Studio - core: the .bm cartridge format (src/bm/bm.h), its sections
 * (code, sprite sheet, map, cover, 3D models), PNG and glTF (.glb) files.
 * No DOM here: the same file runs in the browser and in Node (tests).
 *
 * The project, as the editor holds it:
 *   { title, author, res: "640x360" | "320x180" | "256x256", lua: string,
 *     sheet: { w, h, px: Uint8ClampedArray RGBA (alpha 0 or 255) },
 *     map: Uint8Array | null        MAP section, kept as it came
 *     cover: { w, h, px } | null    the picture on the cartridge in the menu
 *     models: [{ name, faces: [face], rig? }],
 *     uvInset: pixels,              see MESH in bm.h
 *     extras: [{ type, data }] }    sections this editor does not know
 * A face is a tile (4 corners) or a triangle (3): { p: [[x, y, z], ...],
 * uv: [[u, v], ...] in sheet pixels, c: null (textured) or 0xRRGGBB,
 * b?: [bone of each corner] (bm Animator) }.
 * A rig (bm Animator; section ANIM): { bones: [{ name, parent, head, tail }],
 * clips: [{ name, length, loop, mode: 'linear' | 'smooth' | 'step',
 * keys: [{ t, pose: [{ q: [x, y, z, w], t: [x, y, z] }, ...one per bone] }] }] }.
 * Corners go clockwise seen from the side that shows (as for mesh()),
 * y up; a tile is bottom left, top left, top right, bottom right.
 */
(function (root) {
  'use strict';
  const BM = root.BM || (root.BM = {});

  /* src/bm/bm.h; 6 is the sound bank of the console's Sound editor (kept as
   * it is). The first files of bm Studio had MESH 6 and ANIM 7: still read. */
  const SEC = { LUA: 1, SHEET: 2, MAP: 3, COVER: 4, SHEET8: 5, AUDIO: 6, MESH: 8, ANIM: 9, OLD_ANIM: 7 };
  const TEXTURED = 0x80000000;
  const LIMITS = { verts: 65535, faces: 65535, models: 256, name: 15, sheet: 4096, tris60: 1200, bones: 64, clips: 255, keys: 1024 };
  const MODES = ['linear', 'smooth', 'step'];

  // ------------------------------------------------------------ bytes

  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();

  function crc32(b, start = 0, end = b.length) {
    let c = 0xFFFFFFFF;
    for (let i = start; i < end; i++) c = CRC_TABLE[(c ^ b[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }

  class Writer {
    constructor(n = 4096) { this.buf = new Uint8Array(n); this.n = 0; this.dv = new DataView(this.buf.buffer); }
    ensure(k) {
      if (this.n + k <= this.buf.length) return;
      let size = this.buf.length * 2;
      while (size < this.n + k) size *= 2;
      const nb = new Uint8Array(size);
      nb.set(this.buf.subarray(0, this.n));
      this.buf = nb;
      this.dv = new DataView(nb.buffer);
    }
    u8(v) { this.ensure(1); this.buf[this.n++] = v & 255; }
    u16(v) { this.ensure(2); this.dv.setUint16(this.n, v, true); this.n += 2; }
    u32(v) { this.ensure(4); this.dv.setUint32(this.n, v >>> 0, true); this.n += 4; }
    f32(v) { this.ensure(4); this.dv.setFloat32(this.n, v, true); this.n += 4; }
    bytes(a) { this.ensure(a.length); this.buf.set(a, this.n); this.n += a.length; }
    pad4(v = 0) { while (this.n & 3) this.u8(v); }
    result() { return this.buf.slice(0, this.n); }
  }

  const utf8 = s => new TextEncoder().encode(s);
  const fromUtf8 = b => new TextDecoder().decode(b);

  /* a C string of at most `max` bytes, cut on a character boundary */
  function cstr(s, max) {
    let b = utf8(s);
    while (b.length > max) { s = s.slice(0, -1); b = utf8(s); }
    return b;
  }

  function cstrRead(b) {
    const z = b.indexOf(0);
    return fromUtf8(z < 0 ? b : b.subarray(0, z));
  }

  // ------------------------------------------------------------ zlib

  async function streamBytes(bytes, stream) {
    const s = new Blob([bytes]).stream().pipeThrough(stream);
    return new Uint8Array(await new Response(s).arrayBuffer());
  }
  const inflate = b => streamBytes(b, new DecompressionStream('deflate'));
  const deflate = b => streamBytes(b, new CompressionStream('deflate'));

  // ------------------------------------------------------------ images

  function newImage(w, h) { return { w, h, px: new Uint8ClampedArray(w * h * 4) }; }

  /* alpha is on or off for bm (alpha < 128 = transparent) */
  function binarizeAlpha(img) {
    const p = img.px;
    for (let i = 3; i < p.length; i += 4) {
      if (p[i] >= 128) p[i] = 255;
      else { p[i - 3] = p[i - 2] = p[i - 1] = 0; p[i] = 0; }
    }
    return img;
  }

  function isPNG(b) {
    return b.length > 8 && b[0] === 0x89 && b[1] === 0x50 && b[2] === 0x4E && b[3] === 0x47;
  }

  /* PNG -> RGBA. Every colour type and depth, not interlaced (those go
   * through the browser's decoder, see app.js). */
  async function decodePNG(b) {
    if (!isPNG(b)) throw new Error('not a PNG file');
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    let pos = 8, w = 0, h = 0, depth = 0, ctype = 0, interlace = 0, pal = null, trns = null;
    const idat = [];
    while (pos + 8 <= b.length) {
      const n = dv.getUint32(pos), type = String.fromCharCode(b[pos + 4], b[pos + 5], b[pos + 6], b[pos + 7]);
      const body = b.subarray(pos + 8, pos + 8 + n);
      if (type === 'IHDR') {
        w = dv.getUint32(pos + 8); h = dv.getUint32(pos + 12);
        depth = body[8]; ctype = body[9]; interlace = body[12];
      } else if (type === 'PLTE') pal = body;
      else if (type === 'tRNS') trns = body;
      else if (type === 'IDAT') idat.push(body);
      else if (type === 'IEND') break;
      pos += 12 + n;
    }
    if (!w || !h) throw new Error('PNG without a header');
    if (interlace) throw new Error('interlaced PNG');
    let total = 0;
    for (const c of idat) total += c.length;
    const z = new Uint8Array(total);
    let o = 0;
    for (const c of idat) { z.set(c, o); o += c.length; }
    const raw = await inflate(z);
    const chans = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[ctype];
    if (!chans) throw new Error('PNG colour type ' + ctype);
    const bpp = Math.max(1, (chans * depth) >> 3), stride = (w * chans * depth + 7) >> 3;
    const img = newImage(w, h), out = img.px;
    let prev = new Uint8Array(stride), line = new Uint8Array(stride);
    const max = (1 << depth) - 1;
    for (let y = 0; y < h; y++) {
      const f = raw[y * (stride + 1)];
      line.set(raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)));
      for (let i = 0; i < stride; i++) {
        const a = i >= bpp ? line[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
        let v = line[i];
        if (f === 1) v += a;
        else if (f === 2) v += up;
        else if (f === 3) v += (a + up) >> 1;
        else if (f === 4) {
          const p = a + up - c, pa = Math.abs(p - a), pb = Math.abs(p - up), pc = Math.abs(p - c);
          v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c;
        }
        line[i] = v & 255;
      }
      const sample = (x, k) => {                       // channel k of pixel x, 0..255
        if (depth === 8) return line[x * chans + k];
        if (depth === 16) return line[(x * chans + k) * 2];
        const bit = (x * chans + k) * depth, v = (line[bit >> 3] >> (8 - depth - (bit & 7))) & max;
        return ctype === 3 ? v : Math.round(v * 255 / max);
      };
      const raw16 = (x, k) => depth === 16 ? (line[(x * chans + k) * 2] << 8) | line[(x * chans + k) * 2 + 1] : -1;
      for (let x = 0; x < w; x++) {
        const d = (y * w + x) * 4;
        if (ctype === 3) {
          const i = sample(x, 0);
          out[d] = pal ? pal[i * 3] : 0; out[d + 1] = pal ? pal[i * 3 + 1] : 0; out[d + 2] = pal ? pal[i * 3 + 2] : 0;
          out[d + 3] = trns && i < trns.length ? trns[i] : 255;
        } else if (ctype === 0 || ctype === 4) {
          const g = sample(x, 0);
          out[d] = out[d + 1] = out[d + 2] = g;
          out[d + 3] = ctype === 4 ? sample(x, 1) : 255;
          if (ctype === 0 && trns && trns.length >= 2) {
            const t = (trns[0] << 8) | trns[1];
            const v = depth === 16 ? raw16(x, 0) : (depth === 8 ? g : Math.round(g * max / 255));
            if (v === t) out[d + 3] = 0;
          }
        } else {
          out[d] = sample(x, 0); out[d + 1] = sample(x, 1); out[d + 2] = sample(x, 2);
          out[d + 3] = ctype === 6 ? sample(x, 3) : 255;
          if (ctype === 2 && trns && trns.length >= 6) {
            const t = [0, 1, 2].map(k => (trns[k * 2] << 8) | trns[k * 2 + 1]);
            const v = depth === 16 ? [0, 1, 2].map(k => raw16(x, k)) : [out[d], out[d + 1], out[d + 2]];
            if (v[0] === t[0] && v[1] === t[1] && v[2] === t[2]) out[d + 3] = 0;
          }
        }
      }
      const t = prev; prev = line; line = t;
    }
    return img;
  }

  /* RGBA -> PNG (8-bit RGBA; each row filtered with None or Sub, whichever
   * looks smaller) */
  async function encodePNG(img) {
    const { w, h, px } = img, stride = w * 4;
    const raw = new Uint8Array(h * (stride + 1));
    for (let y = 0; y < h; y++) {
      const row = px.subarray(y * stride, (y + 1) * stride), o = y * (stride + 1);
      let sNone = 0, sSub = 0;
      for (let i = 0; i < stride; i++) {
        const v = row[i], s = (v - (i >= 4 ? row[i - 4] : 0)) & 255;
        sNone += v < 128 ? v : 256 - v;
        sSub += s < 128 ? s : 256 - s;
      }
      if (sSub < sNone) {
        raw[o] = 1;
        for (let i = 0; i < stride; i++) raw[o + 1 + i] = (row[i] - (i >= 4 ? row[i - 4] : 0)) & 255;
      } else {
        raw[o] = 0;
        raw.set(row, o + 1);
      }
    }
    const z = await deflate(raw);
    const wr = new Writer(z.length + 128);
    wr.bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
    const chunk = (type, body) => {
      const t = utf8(type), all = new Uint8Array(4 + body.length);
      all.set(t); all.set(body, 4);
      const be = new DataView(new ArrayBuffer(4));
      be.setUint32(0, body.length); wr.bytes(new Uint8Array(be.buffer));
      wr.bytes(all);
      be.setUint32(0, crc32(all)); wr.bytes(new Uint8Array(be.buffer));
    };
    const ih = new DataView(new ArrayBuffer(13));
    ih.setUint32(0, w); ih.setUint32(4, h);
    ih.setUint8(8, 8); ih.setUint8(9, 6);
    chunk('IHDR', new Uint8Array(ih.buffer));
    chunk('IDAT', z);
    chunk('IEND', new Uint8Array(0));
    return wr.result();
  }

  /* the picture on the cartridge: centre crop to 16:10, box filter to
   * 128x80 (as mkbm.py make_cover) */
  function makeCover(img, CW = 128, CH = 80) {
    const { w, h, px } = img;
    let cw, ch;
    if (w * CH > h * CW) { cw = Math.floor(h * CW / CH); ch = h; } else { cw = w; ch = Math.floor(w * CH / CW); }
    const x0 = (w - cw) >> 1, y0 = (h - ch) >> 1, out = newImage(CW, CH);
    for (let y = 0; y < CH; y++) {
      const sy0 = y0 + Math.floor(y * ch / CH), sy1 = y0 + Math.max(Math.floor((y + 1) * ch / CH), Math.floor(y * ch / CH) + 1);
      for (let x = 0; x < CW; x++) {
        const sx0 = x0 + Math.floor(x * cw / CW), sx1 = x0 + Math.max(Math.floor((x + 1) * cw / CW), Math.floor(x * cw / CW) + 1);
        const acc = [0, 0, 0, 0];
        let n = 0;
        for (let sy = sy0; sy < sy1; sy++)
          for (let sx = sx0; sx < sx1; sx++) {
            const i = (sy * w + sx) * 4;
            acc[0] += px[i]; acc[1] += px[i + 1]; acc[2] += px[i + 2]; acc[3] += px[i + 3]; n++;
          }
        const d = (y * CW + x) * 4;
        for (let k = 0; k < 4; k++) out.px[d + k] = Math.floor(acc[k] / n);
      }
    }
    return out;
  }

  function countColours(img, limit = 257) {
    const seen = new Set(), p = img.px;
    for (let i = 0; i < p.length; i += 4) {
      seen.add(p[i + 3] >= 128 ? ((p[i] << 16) | (p[i + 1] << 8) | p[i + 2]) : -1);
      if (seen.size >= limit) break;
    }
    return seen.size;
  }

  function imageIsEmpty(img) {
    for (let i = 3; i < img.px.length; i += 4) if (img.px[i] >= 128) return false;
    return true;
  }

  /* the first place of the sheet (on a grid of `step`) where w x h pixels
   * are all transparent, or null */
  function freeSpot(sheet, w, h, step = 8) {
    const W = sheet.w, H = sheet.h, sat = new Int32Array((W + 1) * (H + 1));
    for (let y = 0; y < H; y++) {
      let row = 0;
      for (let x = 0; x < W; x++) {
        row += sheet.px[(y * W + x) * 4 + 3] >= 128 ? 1 : 0;
        sat[(y + 1) * (W + 1) + x + 1] = sat[y * (W + 1) + x + 1] + row;
      }
    }
    for (let y = 0; y + h <= H; y += step)
      for (let x = 0; x + w <= W; x += step) {
        const s = sat[(y + h) * (W + 1) + x + w] - sat[y * (W + 1) + x + w] - sat[(y + h) * (W + 1) + x] + sat[y * (W + 1) + x];
        if (s === 0) return [x, y];
      }
    return null;
  }

  function resizeImage(img, w, h) {
    const out = newImage(w, h);
    for (let y = 0; y < Math.min(h, img.h); y++)
      out.px.set(img.px.subarray(y * img.w * 4, (y * img.w + Math.min(w, img.w)) * 4), y * w * 4);
    return out;
  }

  /* a copy of the sheet with img in it: where there is room, else below
   * everything (the sheet grows, up to 4096); -> { sheet, at } or null */
  function placeInSheet(sheet, img, step = 8) {
    let at = freeSpot(sheet, img.w, img.h, step), out;
    if (at) out = { w: sheet.w, h: sheet.h, px: sheet.px.slice() };
    else {
      let lowest = 0;
      for (let y = sheet.h - 1; y >= 0 && !lowest; y--)
        for (let x = 0; x < sheet.w; x++) if (sheet.px[(y * sheet.w + x) * 4 + 3] >= 128) { lowest = y + 1; break; }
      const top = Math.ceil(lowest / step) * step;
      const nw = Math.min(LIMITS.sheet, Math.max(sheet.w, Math.ceil(img.w / 8) * 8));
      const nh = Math.min(LIMITS.sheet, Math.max(sheet.h, Math.ceil((top + img.h) / 8) * 8));
      if (img.w > nw || top + img.h > nh) return null;
      out = resizeImage(sheet, nw, nh);
      at = [0, top];
    }
    for (let y = 0; y < img.h; y++)
      out.px.set(img.px.subarray(y * img.w * 4, (y + 1) * img.w * 4), ((at[1] + y) * out.w + at[0]) * 4);
    return { sheet: out, at };
  }

  // ------------------------------------------------------------ SHEET8

  function sheet8Encode(img) {
    const { w, h, px } = img, n = w * h, idx = new Uint8Array(n), pal = [], index = new Map();
    for (let i = 0; i < n; i++) {
      const o = i * 4, key = px[o + 3] >= 128 ? ((px[o] << 16) | (px[o + 1] << 8) | px[o + 2]) : -1;
      let k = index.get(key);
      if (k === undefined) {
        if (pal.length === 256) return null;
        k = pal.length;
        index.set(key, k);
        pal.push(key);
      }
      idx[i] = k;
    }
    const wr = new Writer(n / 4 + 1024);
    wr.u16(w); wr.u16(h); wr.u16(pal.length); wr.u16(0);
    for (const c of pal) {
      if (c < 0) wr.u32(0);
      else { wr.u8(c >> 16); wr.u8(c >> 8); wr.u8(c); wr.u8(255); }
    }
    const lit = [];
    const flush = () => {
      while (lit.length) {
        const chunk = lit.splice(0, 128);
        wr.u8(chunk.length - 1);
        wr.bytes(chunk);
      }
    };
    let i = 0;
    while (i < n) {
      let j = i;
      while (j < n && j - i < 129 && idx[j] === idx[i]) j++;
      if (j - i >= 3) { flush(); wr.u8(j - i + 126); wr.u8(idx[i]); i = j; } else { lit.push(idx[i]); i++; }
    }
    flush();
    return wr.result();
  }

  function sheet8Decode(b) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const w = dv.getUint16(0, true), h = dv.getUint16(2, true), ncol = dv.getUint16(4, true);
    const pal = b.subarray(8, 8 + ncol * 4), img = newImage(w, h), n = w * h;
    let q = 8 + ncol * 4, i = 0;
    while (i < n) {
      if (q >= b.length) throw new Error('bad SHEET8 section');
      const t = b[q++], isLit = t < 128, run = isLit ? t + 1 : t - 126;
      for (let k = 0; k < run && i < n; k++, i++) {
        const c = isLit ? b[q + k] : b[q];
        img.px.set(pal.subarray(c * 4, c * 4 + 4), i * 4);
      }
      q += isLit ? run : 1;
    }
    return binarizeAlpha(img);
  }

  // ------------------------------------------------------------ geometry

  const v3 = {
    sub: (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]],
    add: (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]],
    scale: (a, s) => [a[0] * s, a[1] * s, a[2] * s],
    dot: (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2],
    cross: (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]],
    len: a => Math.hypot(a[0], a[1], a[2]),
    norm: a => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; },
  };

  /* the normal of the side that shows (as r3d_mesh_normals: u x v) */
  function faceNormal(f) {
    const p = f.p;
    let n = v3.cross(v3.sub(p[1], p[0]), v3.sub(p[2], p[0]));
    if (p.length === 4) n = v3.add(n, v3.cross(v3.sub(p[2], p[0]), v3.sub(p[3], p[0])));
    return v3.norm(n);
  }

  function faceCenter(f) {
    const c = [0, 0, 0];
    for (const p of f.p) { c[0] += p[0]; c[1] += p[1]; c[2] += p[2]; }
    return v3.scale(c, 1 / f.p.length);
  }

  function cloneFace(f) {
    const g = { p: f.p.map(q => q.slice()), uv: f.uv.map(q => q.slice()), c: f.c };
    if (f.b) g.b = f.b.slice();
    return g;
  }

  const cloneRig = r => r ? JSON.parse(JSON.stringify(r)) : undefined;

  function modelBounds(faces) {
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const f of faces)
      for (const p of f.p)
        for (let k = 0; k < 3; k++) { if (p[k] < lo[k]) lo[k] = p[k]; if (p[k] > hi[k]) hi[k] = p[k]; }
    return lo[0] <= hi[0] ? { lo, hi } : null;
  }

  const posKey = p => Math.round(p[0] * 1e4) + ',' + Math.round(p[1] * 1e4) + ',' + Math.round(p[2] * 1e4);

  /* a model as the kernel gets it: shared vertices, triangles; with a rig
   * corners of different bones stay apart (the bone of each vertex in
   * `bones`) */
  function modelToMesh(model) {
    const verts = [], index = new Map(), faces = [], bones = [], rigged = !!model.rig;
    const nb = rigged ? model.rig.bones.length : 1;
    const vid = (p, bone) => {
      bone = rigged && bone > 0 && bone < nb ? bone : 0;
      const key = rigged ? posKey(p) + '#' + bone : posKey(p);
      let i = index.get(key);
      if (i === undefined) { i = verts.length; index.set(key, i); verts.push(p); bones.push(bone); }
      return i;
    };
    for (const f of model.faces) {
      const ids = f.p.map((p, k) => vid(p, f.b ? f.b[k] : 0));
      const tris = f.p.length === 4 ? [[0, 1, 2], [0, 2, 3]] : [[0, 1, 2]];
      for (const t of tris) {
        const [a, b, c] = t.map(k => ids[k]);
        if (a === b || b === c || a === c) continue;       // a corner merged away
        faces.push({
          idx: [a, b, c],
          colour: f.c == null ? TEXTURED : f.c & 0xFFFFFF,
          uv: f.c == null ? t.flatMap(k => f.uv[k]) : [0, 0, 0, 0, 0, 0],
        });
      }
    }
    return { verts, faces, bones };
  }

  /* back from triangles: the two halves of each tile, as modelToMesh
   * wrote them, become a tile again */
  function meshToFaces(verts, faces, vbones) {
    const out = [];
    const tri = t => {
      const f = {
        p: t.idx.map(i => verts[i].slice()),
        uv: t.colour & TEXTURED ? [[t.uv[0], t.uv[1]], [t.uv[2], t.uv[3]], [t.uv[4], t.uv[5]]] : [[0, 0], [0, 0], [0, 0]],
        c: t.colour & TEXTURED ? null : t.colour & 0xFFFFFF,
      };
      if (vbones) f.b = t.idx.map(i => vbones[i]);
      return f;
    };
    for (let i = 0; i < faces.length; i++) {
      const t = faces[i], n = faces[i + 1], a = tri(t);
      if (n && n.idx[0] === t.idx[0] && n.idx[1] === t.idx[2] && n.colour === t.colour &&
          (!(t.colour & TEXTURED) || (n.uv[0] === t.uv[0] && n.uv[1] === t.uv[1] && n.uv[2] === t.uv[4] && n.uv[3] === t.uv[5]))) {
        const b = tri(n);
        if (v3.dot(faceNormal(a), faceNormal(b)) > 0) {
          const q = { p: [a.p[0], a.p[1], a.p[2], b.p[2]], uv: [a.uv[0], a.uv[1], a.uv[2], b.uv[2]], c: a.c };
          if (vbones) q.b = [a.b[0], a.b[1], a.b[2], b.b[2]];
          out.push(q);
          i++;
          continue;
        }
      }
      out.push(a);
    }
    return out;
  }

  function modelStats(model) {
    const m = modelToMesh(model);
    return { verts: m.verts.length, tris: m.faces.length, faces: model.faces.length };
  }

  // ------------------------------------------------------------ MESH

  /* entries: [{ model, mesh: modelToMesh(model) }] */
  function meshEncode(entries, inset) {
    const wr = new Writer(65536);
    wr.u16(entries.length); wr.u16(Math.max(0, Math.min(65535, Math.round(inset * 256)))); wr.u32(0);
    for (const { model: m, mesh } of entries) {
      if (!mesh.faces.length) throw new Error(`model "${m.name}" is empty`);
      if (mesh.verts.length > LIMITS.verts) throw new Error(`model "${m.name}": ${mesh.verts.length} vertices, at most ${LIMITS.verts}`);
      if (mesh.faces.length > LIMITS.faces) throw new Error(`model "${m.name}": ${mesh.faces.length} triangles, at most ${LIMITS.faces}`);
      const name = new Uint8Array(16);
      name.set(cstr(m.name, LIMITS.name));
      wr.bytes(name);
      wr.u16(mesh.verts.length); wr.u16(mesh.faces.length); wr.u32(0);
      for (const p of mesh.verts) { wr.f32(p[0]); wr.f32(p[1]); wr.f32(p[2]); }
      for (const f of mesh.faces) {
        wr.u16(f.idx[0]); wr.u16(f.idx[1]); wr.u16(f.idx[2]); wr.u16(0);
        wr.u32(f.colour);
        for (const t of f.uv) wr.u16(Math.max(0, Math.min(65535, Math.round(t * 8))));
      }
    }
    return wr.result();
  }

  /* -> { raw: [{ name, verts, tris }], inset }: meshToFaces makes them faces */
  function meshDecode(b) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const count = dv.getUint16(0, true), inset = dv.getUint16(2, true) / 256, models = [];
    let o = 8;
    for (let m = 0; m < count; m++) {
      const name = cstrRead(b.subarray(o, o + 16));
      const nv = dv.getUint16(o + 16, true), nf = dv.getUint16(o + 18, true);
      o += 24;
      const verts = [];
      for (let i = 0; i < nv; i++, o += 12)
        verts.push([dv.getFloat32(o, true), dv.getFloat32(o + 4, true), dv.getFloat32(o + 8, true)]);
      const faces = [];
      for (let i = 0; i < nf; i++, o += 24) {
        const idx = [dv.getUint16(o, true), dv.getUint16(o + 2, true), dv.getUint16(o + 4, true)];
        if (idx.some(k => k >= nv)) throw new Error(`model "${name}": bad vertex index`);
        const uv = [];
        for (let k = 0; k < 6; k++) uv.push(dv.getUint16(o + 12 + k * 2, true) / 8);
        faces.push({ idx, colour: dv.getUint32(o + 8, true), uv });
      }
      models.push({ name, verts, tris: faces });
    }
    return { raw: models, inset };
  }

  // ------------------------------------------------------------ ANIM

  /* entries: [{ model (with a rig), mesh: modelToMesh(model) }] */
  function animEncode(entries) {
    const wr = new Writer(65536), name16 = s => { const n = new Uint8Array(16); n.set(cstr(s, LIMITS.name)); return n; };
    wr.u16(entries.length); wr.u16(0); wr.u32(0);
    for (const { model, mesh } of entries) {
      const rig = model.rig, nb = rig.bones.length;
      wr.bytes(name16(model.name));
      wr.u16(nb); wr.u16(rig.clips.length); wr.u16(mesh.verts.length); wr.u16(0);
      for (const bone of rig.bones) {
        wr.bytes(name16(bone.name));
        wr.u16(bone.parent < 0 ? 0xFFFF : bone.parent); wr.u16(0);
        for (const v of bone.head) wr.f32(v);
        for (const v of bone.tail) wr.f32(v);
      }
      for (const b of mesh.bones) wr.u8(b);
      wr.pad4();
      for (const clip of rig.clips) {
        wr.bytes(name16(clip.name));
        wr.u16(clip.keys.length); wr.u8(Math.max(0, MODES.indexOf(clip.mode))); wr.u8(clip.loop ? 1 : 0);
        wr.f32(clip.length);
        for (const key of clip.keys) {
          wr.f32(key.t);
          for (let i = 0; i < nb; i++) {
            const p = key.pose[i] || { q: [0, 0, 0, 1], t: [0, 0, 0] };
            for (const v of p.q) wr.f32(v);
            for (const v of p.t) wr.f32(v);
          }
        }
      }
    }
    return wr.result();
  }

  function animDecode(b) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), out = [];
    const count = dv.getUint16(0, true);
    let o = 8;
    const f32 = () => { const v = dv.getFloat32(o, true); o += 4; return v; };
    for (let r = 0; r < count; r++) {
      const name = cstrRead(b.subarray(o, o + 16));
      const nb = dv.getUint16(o + 16, true), nc = dv.getUint16(o + 18, true), nv = dv.getUint16(o + 20, true);
      o += 24;
      const bones = [];
      for (let i = 0; i < nb; i++) {
        const bn = cstrRead(b.subarray(o, o + 16)), parent = dv.getInt16(o + 16, true);
        o += 20;
        bones.push({ name: bn, parent, head: [f32(), f32(), f32()], tail: [f32(), f32(), f32()] });
      }
      const vbones = Array.from(b.subarray(o, o + nv));
      o += (nv + 3) & ~3;
      const clips = [];
      for (let c = 0; c < nc; c++) {
        const cn = cstrRead(b.subarray(o, o + 16)), nk = dv.getUint16(o + 16, true);
        const mode = MODES[b[o + 18]] || 'linear', loop = !!(b[o + 19] & 1);
        o += 20;
        const length = f32(), keys = [];
        for (let k = 0; k < nk; k++) {
          const t = f32(), pose = [];
          for (let i = 0; i < nb; i++) pose.push({ q: [f32(), f32(), f32(), f32()], t: [f32(), f32(), f32()] });
          keys.push({ t, pose });
        }
        clips.push({ name: cn, length, loop, mode, keys });
      }
      out.push({ name, bones, vbones, clips });
    }
    return out;
  }

  // ------------------------------------------------------------ .bm

  function parseCart(bytes) {
    const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const magic = String.fromCharCode(...b.subarray(0, 8));
    if (b.length < 128 || (magic !== 'BMCART\0\0' && magic !== 'BM33CART')) throw new Error('not a .bm cartridge');
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    if (dv.getUint16(8, true) !== 1 || dv.getUint16(10, true) !== 128) throw new Error('unsupported .bm version');
    const warnings = [];
    let rawModels = [], rigs = [];
    if (crc32(b, 128) !== dv.getUint32(20, true)) warnings.push('the CRC is wrong: the file was damaged (bm refuses it until it is saved again)');
    const w = dv.getUint16(12, true);
    const project = {
      title: cstrRead(b.subarray(24, 72)), author: cstrRead(b.subarray(72, 104)),
      res: w === 320 ? '320x180' : w === 256 ? '256x256' : '640x360', lua: '', sheet: null, map: null, cover: null,
      models: [], uvInset: 0.25, extras: [],
    };
    for (let i = 0; i < b[17]; i++) {
      const e = 128 + i * 16, type = dv.getUint32(e, true), off = dv.getUint32(e + 4, true), size = dv.getUint32(e + 8, true);
      if (off + size > b.length) throw new Error('section out of bounds');
      const body = b.slice(off, off + size), bd = new DataView(body.buffer);
      const bank = type === SEC.AUDIO && size >= 4 && String.fromCharCode(...body.subarray(0, 4)) === 'BMAU';
      switch (type === SEC.AUDIO && !bank ? SEC.MESH : type === SEC.OLD_ANIM ? SEC.ANIM : type) {
      case SEC.LUA: project.lua = fromUtf8(body); break;
      case SEC.SHEET: {
        const sw = bd.getUint16(0, true), sh = bd.getUint16(2, true);
        project.sheet = binarizeAlpha({ w: sw, h: sh, px: new Uint8ClampedArray(body.buffer, 4, sw * sh * 4).slice() });
        break;
      }
      case SEC.SHEET8: project.sheet = sheet8Decode(body); break;
      case SEC.MAP: project.map = body; break;
      case SEC.COVER: {
        const cw = bd.getUint16(0, true), ch = bd.getUint16(2, true);
        project.cover = { w: cw, h: ch, px: new Uint8ClampedArray(body.buffer, 4, cw * ch * 4).slice() };
        break;
      }
      case SEC.MESH: {
        const m = meshDecode(body);
        rawModels = m.raw;
        project.uvInset = m.inset;
        break;
      }
      case SEC.ANIM: rigs = animDecode(body); break;
      default: project.extras.push({ type, data: body });
      }
    }
    for (const r of rawModels) {
      const a = rigs.find(x => x.name === r.name);
      const ok = a && a.vbones.length === r.verts.length;
      if (a && !ok) warnings.push(`the skeleton of "${r.name}" does not fit the model any more: left out`);
      const model = { name: r.name, faces: meshToFaces(r.verts, r.tris, ok ? a.vbones : null) };
      if (ok) model.rig = { bones: a.bones, clips: a.clips };
      project.models.push(model);
    }
    if (!project.sheet) project.sheet = newImage(256, 256);
    return { project, warnings };
  }

  function buildCart(project) {
    const sections = [];
    if (project.cover) {
      const c = project.cover, wr = new Writer(4 + c.px.length);
      wr.u16(c.w); wr.u16(c.h); wr.bytes(c.px);
      sections.push([SEC.COVER, wr.result()]);
    }
    sections.push([SEC.LUA, utf8(project.lua || '')]);
    const sh = project.sheet;
    if (sh && !imageIsEmpty(sh)) {
      const packed = sheet8Encode(sh);
      if (packed && packed.length < 4 + sh.px.length) sections.push([SEC.SHEET8, packed]);
      else {
        const wr = new Writer(4 + sh.px.length);
        wr.u16(sh.w); wr.u16(sh.h); wr.bytes(sh.px);
        sections.push([SEC.SHEET, wr.result()]);
      }
    }
    if (project.map) sections.push([SEC.MAP, project.map]);
    const entries = project.models.filter(m => m.faces.length).map(model => ({ model, mesh: modelToMesh(model) }));
    if (entries.length) sections.push([SEC.MESH, meshEncode(entries, project.uvInset)]);
    const rigged = entries.filter(e => e.model.rig && e.model.rig.bones.length);
    if (rigged.length) sections.push([SEC.ANIM, animEncode(rigged)]);
    for (const e of project.extras || []) sections.push([e.type, e.data]);
    if (sections.length > 255) throw new Error('too many sections');

    const wr = new Writer(1 << 16);
    wr.bytes(new Uint8Array(128 + 16 * sections.length));
    const table = [];
    for (const [type, data] of sections) {
      table.push([type, wr.n, data.length]);
      wr.bytes(data);
      wr.pad4();
    }
    const out = wr.result(), dv = new DataView(out.buffer);
    out.set(utf8('BMCART'), 0);
    dv.setUint16(8, 1, true); dv.setUint16(10, 128, true);
    const [rw, rh] = project.res === '320x180' ? [320, 180] : project.res === '256x256' ? [256, 256] : [640, 360];
    dv.setUint16(12, rw, true); dv.setUint16(14, rh, true);
    out[16] = 1; out[17] = sections.length;
    out.set(cstr(project.title || '', 47), 24);
    out.set(cstr(project.author || '', 31), 72);
    table.forEach(([type, off, size], i) => {
      dv.setUint32(128 + i * 16, type, true);
      dv.setUint32(128 + i * 16 + 4, off, true);
      dv.setUint32(128 + i * 16 + 8, size, true);
    });
    dv.setUint32(20, crc32(out, 128), true);
    return out;
  }

  /* what would stop the kernel from loading the project */
  function checkProject(project) {
    const problems = [], names = new Set();
    for (const m of project.models) {
      if (!m.faces.length) continue;
      const bytes = utf8(m.name).length;
      if (!m.name || bytes > LIMITS.name) problems.push(`model name "${m.name}": 1 to ${LIMITS.name} bytes`);
      if (names.has(m.name)) problems.push(`two models called "${m.name}"`);
      names.add(m.name);
      const s = modelStats(m);
      if (s.verts > LIMITS.verts) problems.push(`"${m.name}": ${s.verts} vertices (at most ${LIMITS.verts})`);
      if (s.tris > LIMITS.faces) problems.push(`"${m.name}": ${s.tris} triangles (at most ${LIMITS.faces})`);
      if (m.rig) problems.push(...checkRig(m));
    }
    if (project.models.filter(m => m.faces.length).length > LIMITS.models) problems.push(`at most ${LIMITS.models} models`);
    return problems;
  }

  function checkRig(m) {
    const r = m.rig, out = [], bn = new Set(), cn = new Set();
    if (!r.bones.length) return out;
    if (r.bones.length > LIMITS.bones) out.push(`"${m.name}": ${r.bones.length} bones (at most ${LIMITS.bones})`);
    r.bones.forEach((b, i) => {
      if (!b.name || utf8(b.name).length > LIMITS.name) out.push(`"${m.name}": bone name "${b.name}": 1 to ${LIMITS.name} bytes`);
      if (bn.has(b.name)) out.push(`"${m.name}": two bones called "${b.name}"`);
      bn.add(b.name);
      if (b.parent >= i) out.push(`"${m.name}": bone "${b.name}" comes before its parent`);
    });
    if (r.clips.length > LIMITS.clips) out.push(`"${m.name}": at most ${LIMITS.clips} animations`);
    for (const c of r.clips) {
      if (!c.name || utf8(c.name).length > LIMITS.name) out.push(`"${m.name}": animation name "${c.name}": 1 to ${LIMITS.name} bytes`);
      if (cn.has(c.name)) out.push(`"${m.name}": two animations called "${c.name}"`);
      cn.add(c.name);
      if (!c.keys.length) out.push(`"${m.name}": the animation "${c.name}" has no keyframes`);
      if (c.keys.length > LIMITS.keys) out.push(`"${m.name}": "${c.name}" has more than ${LIMITS.keys} keyframes`);
      if (!(c.length > 0)) out.push(`"${m.name}": "${c.name}" lasts no time`);
      for (let k = 1; k < c.keys.length; k++) if (c.keys[k].t < c.keys[k - 1].t) out.push(`"${m.name}": "${c.name}": keyframes out of order`);
    }
    return out;
  }

  // ------------------------------------------------------------ glTF

  const srgbToLinear = c => { c /= 255; return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
  const linearToSrgb = v => {
    v = Math.max(0, Math.min(1, v));
    return Math.max(0, Math.min(255, Math.round((v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055) * 255)));
  };

  /* Models -> .glb. glTF is right handed: z flips, and so each triangle
   * goes the other way round (both compute the outward normal as
   * (b - a) x (c - a) in their own axes). Texture = the whole sprite sheet. */
  async function exportGLB(project, models) {
    const bin = new Writer(1 << 16), js = {
      asset: { version: '2.0', generator: 'bm Studio', extras: { bm: { sheet_w: project.sheet.w, sheet_h: project.sheet.h, uv_inset: project.uvInset } } },
      scene: 0, scenes: [{ name: project.title || 'bm', nodes: [] }], nodes: [], meshes: [], accessors: [], bufferViews: [],
      materials: [
        { name: 'sheet', pbrMetallicRoughness: { baseColorTexture: { index: 0 }, metallicFactor: 0, roughnessFactor: 1 }, alphaMode: 'MASK', alphaCutoff: 0.5 },
        { name: 'colour', pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1], metallicFactor: 0, roughnessFactor: 1 } },
      ],
      textures: [{ sampler: 0, source: 0 }],
      samplers: [{ magFilter: 9728, minFilter: 9728, wrapS: 33071, wrapT: 33071 }],
      images: [], buffers: [],
    };
    const view = (data, target) => {
      bin.pad4();
      const v = { buffer: 0, byteOffset: bin.n, byteLength: data.byteLength };
      if (target) v.target = target;
      bin.bytes(new Uint8Array(data.buffer, data.byteOffset, data.byteLength));
      js.bufferViews.push(v);
      return js.bufferViews.length - 1;
    };
    const accessor = (arr, type, comp, target, minmax) => {
      const a = { bufferView: view(arr, target), componentType: comp, count: arr.length / { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }[type], type };
      if (minmax) {
        const n = { VEC3: 3 }[type], lo = new Array(n).fill(Infinity), hi = new Array(n).fill(-Infinity);
        for (let i = 0; i < arr.length; i++) { const k = i % n; lo[k] = Math.min(lo[k], arr[i]); hi[k] = Math.max(hi[k], arr[i]); }
        a.min = lo; a.max = hi;
      }
      js.accessors.push(a);
      return js.accessors.length - 1;
    };
    for (const m of models) {
      const prims = [];
      for (const textured of [true, false]) {
        const pos = [], nrm = [], uv = [], col = [], idx = [], index = new Map();
        for (const f of m.faces) {
          if ((f.c == null) !== textured) continue;
          const n = faceNormal(f), tris = f.p.length === 4 ? [[0, 1, 2], [0, 2, 3]] : [[0, 1, 2]];
          const lin = textured ? null : [srgbToLinear(f.c >> 16 & 255), srgbToLinear(f.c >> 8 & 255), srgbToLinear(f.c & 255)];
          for (const t of tris) {
            if (posKey(f.p[t[0]]) === posKey(f.p[t[1]]) || posKey(f.p[t[1]]) === posKey(f.p[t[2]]) || posKey(f.p[t[0]]) === posKey(f.p[t[2]])) continue;
            for (const k of [t[0], t[2], t[1]]) {
              const p = f.p[k], key = posKey(p) + '|' + n.map(v => v.toFixed(4)).join(',') + '|' +
                (textured ? f.uv[k][0] + ',' + f.uv[k][1] : f.c);
              let i = index.get(key);
              if (i === undefined) {
                i = pos.length / 3;
                index.set(key, i);
                pos.push(p[0], p[1], -p[2]);
                nrm.push(n[0], n[1], -n[2]);
                if (textured) uv.push(f.uv[k][0] / project.sheet.w, f.uv[k][1] / project.sheet.h);
                else col.push(lin[0], lin[1], lin[2]);
              }
              idx.push(i);
            }
          }
        }
        if (!idx.length) continue;
        const attributes = {
          POSITION: accessor(new Float32Array(pos), 'VEC3', 5126, 34962, true),
          NORMAL: accessor(new Float32Array(nrm), 'VEC3', 5126, 34962),
        };
        if (textured) attributes.TEXCOORD_0 = accessor(new Float32Array(uv), 'VEC2', 5126, 34962);
        else attributes.COLOR_0 = accessor(new Float32Array(col), 'VEC3', 5126, 34962);
        const big = pos.length / 3 > 65535;
        prims.push({ attributes, indices: accessor(big ? new Uint32Array(idx) : new Uint16Array(idx), 'SCALAR', big ? 5125 : 5123, 34963), material: textured ? 0 : 1 });
      }
      if (!prims.length) continue;
      js.meshes.push({ name: m.name, primitives: prims });
      js.nodes.push({ name: m.name, mesh: js.meshes.length - 1 });
      js.scenes[0].nodes.push(js.nodes.length - 1);
    }
    const png = await encodePNG(project.sheet);
    js.images.push({ name: 'sheet', mimeType: 'image/png', bufferView: view(png) });
    bin.pad4();
    js.buffers.push({ byteLength: bin.n });
    let json = utf8(JSON.stringify(js));
    const jpad = (4 - (json.length & 3)) & 3;
    const jb = new Uint8Array(json.length + jpad).fill(0x20);
    jb.set(json);
    json = jb;
    const body = bin.result(), out = new Writer(28 + json.length + body.length);
    out.u32(0x46546C67); out.u32(2); out.u32(12 + 8 + json.length + 8 + body.length);
    out.u32(json.length); out.u32(0x4E4F534A); out.bytes(json);
    out.u32(body.length); out.u32(0x004E4942); out.bytes(body);
    return out.result();
  }

  function glbParse(bytes) {
    const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    if (dv.getUint32(0, true) !== 0x46546C67 || dv.getUint32(4, true) !== 2) throw new Error('not a glTF 2 binary file (.glb)');
    let pos = 12, js = null, bin = new Uint8Array(0);
    while (pos + 8 <= Math.min(b.length, dv.getUint32(8, true))) {
      const n = dv.getUint32(pos, true), type = dv.getUint32(pos + 4, true);
      if (type === 0x4E4F534A) js = JSON.parse(fromUtf8(b.subarray(pos + 8, pos + 8 + n)));
      else if (type === 0x004E4942) bin = b.subarray(pos + 8, pos + 8 + n);
      pos += 8 + n;
    }
    if (!js) throw new Error('.glb without JSON');
    return { js, bin };
  }

  function glbAccessor(js, bin, i) {
    const a = js.accessors[i], comps = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }[a.type];
    const info = { 5126: ['getFloat32', 4], 5125: ['getUint32', 4], 5123: ['getUint16', 2], 5121: ['getUint8', 1], 5122: ['getInt16', 2], 5120: ['getInt8', 1] }[a.componentType];
    const out = [];
    if (a.bufferView === undefined) { for (let k = 0; k < a.count; k++) out.push(new Array(comps).fill(0)); return out; }
    const bv = js.bufferViews[a.bufferView], base = (bv.byteOffset || 0) + (a.byteOffset || 0);
    const stride = bv.byteStride || comps * info[1];
    const dv = new DataView(bin.buffer, bin.byteOffset, bin.byteLength);
    const norm = a.normalized ? { 5121: 255, 5123: 65535, 5120: 127, 5122: 32767 }[a.componentType] : 0;
    for (let k = 0; k < a.count; k++) {
      const t = [];
      for (let c = 0; c < comps; c++) {
        const v = dv[info[0]](base + k * stride + c * info[1], true);
        t.push(norm ? v / norm : v);
      }
      out.push(t);
    }
    return out;
  }

  function mat4Mul(a, b) {
    const o = new Array(16);
    for (let c = 0; c < 4; c++)
      for (let r = 0; r < 4; r++) {
        let s = 0;
        for (let k = 0; k < 4; k++) s += a[r + k * 4] * b[k + c * 4];
        o[r + c * 4] = s;
      }
    return o;
  }

  function nodeMatrix(n) {
    if (n.matrix) return n.matrix.slice();
    const [tx, ty, tz] = n.translation || [0, 0, 0], [qx, qy, qz, qw] = n.rotation || [0, 0, 0, 1], [sx, sy, sz] = n.scale || [1, 1, 1];
    return [
      (1 - 2 * (qy * qy + qz * qz)) * sx, 2 * (qx * qy + qz * qw) * sx, 2 * (qx * qz - qy * qw) * sx, 0,
      2 * (qx * qy - qz * qw) * sy, (1 - 2 * (qx * qx + qz * qz)) * sy, 2 * (qy * qz + qx * qw) * sy, 0,
      2 * (qx * qz + qy * qw) * sz, 2 * (qy * qz - qx * qw) * sz, (1 - 2 * (qx * qx + qy * qy)) * sz, 0,
      tx, ty, tz, 1,
    ];
  }

  /* .glb -> { models, images, bm }. Faces with a texture keep uv in 0..1
   * of their image (`img`: index in images, PNG/JPEG bytes and type); the
   * caller places the images in the sheet (bm Studio files: the image is
   * the sheet itself, bm = asset.extras.bm). */
  function importGLB(bytes) {
    const { js, bin } = glbParse(bytes);
    const bm = (js.asset && js.asset.extras && js.asset.extras.bm) || null;
    const images = (js.images || []).map(im => {
      if (im.bufferView === undefined) return null;
      const bv = js.bufferViews[im.bufferView];
      return { bytes: bin.slice(bv.byteOffset || 0, (bv.byteOffset || 0) + bv.byteLength), mime: im.mimeType || 'image/png' };
    });
    const models = [], names = new Set(), warnings = new Set();
    const visit = (ni, parent) => {
      const node = js.nodes[ni], m = mat4Mul(parent, nodeMatrix(node));
      if (node.mesh !== undefined) {
        const mesh = js.meshes[node.mesh];
        let name = (node.name || mesh.name || `model${models.length + 1}`).replace(/[^\x20-\x7e]/g, '').slice(0, LIMITS.name) || `model${models.length + 1}`;
        for (let k = 2, base = name; names.has(name); k++) name = base.slice(0, LIMITS.name - 2) + k;
        names.add(name);
        models.push({ name, faces: glbMeshFaces(js, bin, mesh, m, warnings) });
      }
      for (const c of node.children || []) visit(c, m);
    };
    const ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const scenes = js.scenes || [{ nodes: (js.nodes || []).map((_, i) => i) }];
    for (const ni of (scenes[js.scene || 0] || scenes[0]).nodes || []) visit(ni, ident);
    return { models: models.filter(m => m.faces.length), images, bm, warnings: [...warnings] };
  }

  function glbMeshFaces(js, bin, mesh, m, warnings) {
    const det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
    const xf = p => [
      m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
      m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
      -(m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]),
    ];
    const faces = [];
    for (const prim of mesh.primitives || []) {
      if ((prim.mode === undefined ? 4 : prim.mode) !== 4) { warnings.add('only triangles are imported (points and lines are left out)'); continue; }
      const at = prim.attributes, pos = glbAccessor(js, bin, at.POSITION).map(xf);
      const idx = prim.indices !== undefined ? glbAccessor(js, bin, prim.indices).map(t => t[0]) : pos.map((_, i) => i);
      const mat = prim.material !== undefined && js.materials ? js.materials[prim.material] : {};
      const pbr = mat.pbrMetallicRoughness || {}, factor = pbr.baseColorFactor || [1, 1, 1, 1];
      const texInfo = pbr.baseColorTexture, tex = texInfo && js.textures ? js.textures[texInfo.index] : null;
      const img = tex ? tex.source : undefined;
      const uvs = img !== undefined && at['TEXCOORD_' + (texInfo.texCoord || 0)] !== undefined
        ? glbAccessor(js, bin, at['TEXCOORD_' + (texInfo.texCoord || 0)]) : null;
      const cols = at.COLOR_0 !== undefined ? glbAccessor(js, bin, at.COLOR_0) : null;
      for (let t = 0; t + 2 < idx.length; t += 3) {
        // z flips (glTF is right handed): the winding turns, unless the
        // node is a mirror image itself
        let tri = [idx[t], idx[t + 1], idx[t + 2]];
        if (det > 0) tri = [tri[0], tri[2], tri[1]];
        const p = tri.map(i => pos[i]);
        if (posKey(p[0]) === posKey(p[1]) || posKey(p[1]) === posKey(p[2]) || posKey(p[0]) === posKey(p[2])) continue;
        if (uvs) {
          const uv = tri.map(i => uvs[i].slice(0, 2));
          if (uv.some(q => q[0] < -0.001 || q[0] > 1.001 || q[1] < -0.001 || q[1] > 1.001))
            warnings.add('some textures repeat outside their image: bm stretches the edge instead');
          faces.push({ p, uv: uv.map(q => [Math.min(1, Math.max(0, q[0])), Math.min(1, Math.max(0, q[1]))]), c: null, img });
        } else {
          let rgb = [factor[0], factor[1], factor[2]];
          if (cols) rgb = rgb.map((v, k) => v * (cols[tri[0]][k] + cols[tri[1]][k] + cols[tri[2]][k]) / 3);
          faces.push({ p, uv: [[0, 0], [0, 0], [0, 0]], c: (linearToSrgb(rgb[0]) << 16) | (linearToSrgb(rgb[1]) << 8) | linearToSrgb(rgb[2]) });
        }
      }
    }
    return faces;
  }

  // ------------------------------------------------------------ Lua

  const VIEWER_MARK = '-- bm Studio: viewer of the 3D models';

  function viewerLua() {
    return `${VIEWER_MARK} of this cartridge.
-- Left / right: model; up / down: closer / farther; A: light; B: spin;
-- X: the next animation (models with a skeleton, from bm Animator).
-- Replace it with your game: model("name") gives a model as a mesh for
-- draw3d(), models() the list of names, bounds3d(mesh) its size,
-- animate(mesh, "walk", t) the pose of an animation at time t.

local names, meshes, boxes, anims = {}, {}, {}, {}
local cur, zoom, spin, lit, angle, clip, t = 1, 1, true, true, 0.6, 1, 0

function _init()
  names = models()
  for i, n in ipairs(names) do
    meshes[i] = model(n)
    anims[i] = clips(meshes[i])
    local x0, y0, z0, x1, y1, z1 = bounds3d(meshes[i])
    boxes[i] = { (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2,
                 math.max(x1 - x0, y1 - y0, z1 - z0, 0.5) }
  end
end

function _update()
  t = t + 1 / 60
  if #names == 0 then return end
  if btnp(0) then cur = (cur - 2) % #names + 1; clip = 1 end
  if btnp(1) then cur = cur % #names + 1; clip = 1 end
  if btn(2) then zoom = math.max(0.3, zoom - 0.02) end
  if btn(3) then zoom = math.min(4, zoom + 0.02) end
  if btnp(4) then lit = not lit end
  if btnp(5) then spin = not spin end
  if btnp(6) and #anims[cur] > 0 then clip = clip % #anims[cur] + 1; t = 0 end
  if spin then angle = angle + 0.01 end
end

function _draw()
  cls(0x1C2030)
  if #names == 0 then
    print("no 3D models yet: make them with bm Studio", 16, 16, 0xFFFFFF)
    return
  end
  local b, a = boxes[cur], anims[cur]
  if #a > 0 then animate(meshes[cur], a[clip].name, t) end
  local d = b[4] * 1.6 * zoom
  zclear()
  camera3d(0, d * 0.55, -d, 0, -0.5, 60)
  light3d(-0.4, 0.8, -0.5, lit and 0.4 or 1)
  -- turn around the middle of the model: move it there, then spin
  local c, s = math.cos(angle), math.sin(angle)
  local x = -(b[1] * c + b[3] * s)
  local z = -(-b[1] * s + b[3] * c)
  draw3d(meshes[cur], x, -b[2], z, 0, angle, 0, 1, lit and 0 or 2)
  print(names[cur] .. "  " .. cur .. "/" .. #names .. (#a > 0 and ("  " .. a[clip].name) or ""), 8, 6, 0xFFFFFF)
  print(stat(4) .. " triangles  " .. stat(2) .. " fps", 8, 24, 0x8890A8)
  print("left/right model  up/down zoom  A light  B spin" .. (#a > 0 and "  X animation" or ""), 8, SCREEN_H - 20, 0x8890A8)
end
`;
  }

  const luaNum = v => {
    const r = Math.round(v * 1e4) / 1e4;
    return Object.is(r, -0) ? '0' : String(r);
  };

  /* a model as Lua code for mesh() (for games that build meshes in code) */
  // bm Mesh (the console's mesh editor) keeps its meshes as functions at the
  // end of main.lua, between these two lines: { code without them, block }
  const MESH_BEGIN = '-- [bm Mesh begin]', MESH_END = '-- [bm Mesh end]';
  function meshBlock(lua) {
    const s = lua.indexOf(MESH_BEGIN), e = s < 0 ? -1 : lua.indexOf(MESH_END, s);
    if (e < 0) return { code: lua, block: '' };
    const nl = lua.indexOf('\n', e), end = nl < 0 ? lua.length : nl + 1;
    return { code: lua.slice(0, s).replace(/\n+$/, '\n') + lua.slice(end), block: lua.slice(s, end) };
  }

  function modelToLua(model, varName) {
    const mesh = modelToMesh(model), name = varName || model.name.replace(/[^A-Za-z0-9_]/g, '_');
    const lines = [`-- ${model.name}: ${mesh.verts.length} vertices, ${mesh.faces.length} triangles (bm Studio)`];
    const wrap = (arr, per) => {
      const out = [];
      for (let i = 0; i < arr.length; i += per) out.push('  ' + arr.slice(i, i + per).join(', ') + ',');
      return out.join('\n');
    };
    lines.push(`local ${name} = mesh({`);
    lines.push(wrap(mesh.verts.flatMap(p => p.map(luaNum)), 12));
    lines.push('}, {');
    lines.push(wrap(mesh.faces.flatMap(f => [f.idx[0] + 1, f.idx[1] + 1, f.idx[2] + 1, f.colour === TEXTURED ? -1 : '0x' + f.colour.toString(16).padStart(6, '0').toUpperCase()]), 16));
    lines.push('}, {');
    lines.push(wrap(mesh.faces.flatMap(f => f.uv.map(luaNum)), 12));
    lines.push('})');
    return lines.join('\n') + '\n';
  }

  Object.assign(BM, {
    SEC, TEXTURED, LIMITS, crc32, Writer, utf8, fromUtf8, inflate, deflate,
    newImage, binarizeAlpha, isPNG, decodePNG, encodePNG, makeCover, countColours, imageIsEmpty, freeSpot, resizeImage,
    placeInSheet,
    sheet8Encode, sheet8Decode, v3, faceNormal, faceCenter, cloneFace, cloneRig, modelBounds, posKey, MODES,
    modelToMesh, meshToFaces, modelStats, meshEncode, meshDecode, animEncode, animDecode, parseCart, buildCart,
    checkProject, checkRig,
    srgbToLinear, linearToSrgb, exportGLB, importGLB, glbParse, VIEWER_MARK, viewerLua, modelToLua, meshBlock,
  });
  if (typeof module !== 'undefined' && module.exports) module.exports = BM;
})(typeof window !== 'undefined' ? window : globalThis);
