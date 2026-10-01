/*
 * bm Animator - 3D to sprites: draws the frames of an animation, seen from
 * one or more directions, into a grid of sprites for the sprite sheet
 * (frames left to right, directions top to bottom). A small software
 * rasterizer (z-buffer, textures from the sheet, light per face, light in
 * bands, outline, fewer colours), so it runs in Node too and the same
 * input always gives the same pixels.
 */
(function (root) {
  'use strict';
  const BM = root.BM, v3 = BM.v3, R = BM.rig;

  const DEFAULTS = {
    frames: 8, w: 48, h: 48, dirs: 1, pitch: 30, projection: 'ortho', fov: 40, scale: 0, margin: 1,
    lit: true, ambient: 0.45, bands: 0, outline: 0x101018, colours: 0, supersample: 1, fps: 12,
  };

  /* the times of the frames: a loop does not repeat its first frame at the end */
  function frameTimes(clip, n) {
    if (!clip) return [0];
    const L = clip.length > 0 ? clip.length : 1;
    if (n <= 1) return [0];
    return Array.from({ length: n }, (_, i) => clip.loop ? i * L / n : i * L / (n - 1));
  }

  /* camera axes: looking towards +z, down by `pitch` degrees */
  function viewAxes(pitch) {
    const p = pitch * Math.PI / 180;
    return { r: [1, 0, 0], u: [0, Math.cos(p), Math.sin(p)], f: [0, -Math.sin(p), Math.cos(p)] };
  }

  /* direction d of n: the model turned d/n of a full turn (clockwise seen
   * from above, as Select's R); 0 = its front (-z) towards the viewer */
  function turn(p, d, n) {
    const a = -2 * Math.PI * d / n, c = Math.cos(a), s = Math.sin(a);
    return [c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2]];
  }

  /* all the posed, turned faces of every frame and direction */
  function scenes(model, clip, o) {
    const times = frameTimes(clip, o.frames), out = [];
    for (let d = 0; d < o.dirs; d++)
      for (let i = 0; i < times.length; i++) {
        const pose = model.rig && model.rig.bones.length ? R.samplePose(model.rig, clip, times[i]) : null;
        const faces = pose ? R.posedFaces(model, pose) : model.faces;
        out.push({ d, i, faces: faces.map(f => ({ p: f.p.map(p => turn(p, d, o.dirs)), uv: f.uv, c: f.c })) });
      }
    return { times, out };
  }

  /* where the camera is and how big things are, the same for every frame */
  function frameCamera(all, o) {
    const ax = viewAxes(o.pitch);
    let maxX = 0, maxY = -Infinity, minY = Infinity, far = 0;
    const pts = [];
    for (const sc of all) for (const f of sc.faces) for (const p of f.p) pts.push(p);
    for (const p of pts) far = Math.max(far, v3.len(p));
    const dist = Math.max(1, far) * 3.5;
    const eye = v3.scale(ax.f, -dist);
    const proj = p => {
      const X = v3.dot(p, ax.r), Y = v3.dot(p, ax.u);
      if (o.projection !== 'persp') return [X, Y];
      const Z = v3.dot(v3.sub(p, eye), ax.f);
      return [X / Z, Y / Z];
    };
    for (const p of pts) {
      const [X, Y] = proj(p);
      maxX = Math.max(maxX, Math.abs(X));
      maxY = Math.max(maxY, Y);
      minY = Math.min(minY, Y);
    }
    if (!pts.length) { maxY = 1; minY = 0; maxX = 1; }
    const m = o.margin + (o.outline != null ? 1 : 0);
    let scale = o.scale > 0 ? o.scale : Math.min((o.w - 2 * m) / Math.max(1e-6, 2 * maxX), (o.h - 2 * m) / Math.max(1e-6, maxY - minY));
    if (o.projection === 'persp' && o.scale > 0) scale = o.scale * dist;       // px per unit at the origin
    const anchor = [Math.floor(o.w / 2), Math.round(m + maxY * scale)];
    return { ax, eye, dist, scale, anchor, persp: o.projection === 'persp' };
  }

  /* -> screen x, y, and the depth along the view */
  function project(cam, p, k) {
    const X = v3.dot(p, cam.ax.r), Y = v3.dot(p, cam.ax.u), D = v3.dot(v3.sub(p, cam.eye), cam.ax.f);
    const w = cam.persp ? D : 1;
    return [cam.anchor[0] * k + X * cam.scale * k / w, cam.anchor[1] * k - Y * cam.scale * k / w, D];
  }

  /* one frame, at k times the size (supersampling) */
  function rasterize(faces, sheet, cam, o, k) {
    // depth test on "nearness": -depth with a flat camera (linear on screen), 1/depth with perspective
    const W = o.w * k, H = o.h * k, img = new Uint8ClampedArray(W * H * 4), zb = new Float32Array(W * H).fill(-Infinity);
    const L = v3.norm([-0.5, 0.75, -0.55]);    // towards the light, in view space: up, left, towards the viewer
    for (const f of faces) {
      const tris = f.p.length === 4 ? [[0, 1, 2], [0, 2, 3]] : [[0, 1, 2]];
      const n = BM.faceNormal(f);
      const nv = [v3.dot(n, cam.ax.r), v3.dot(n, cam.ax.u), v3.dot(n, cam.ax.f)];
      let kl = 1;
      if (o.lit) {
        kl = Math.min(1, o.ambient + (1 - o.ambient) * Math.max(0, nv[0] * L[0] + nv[1] * L[1] + nv[2] * L[2]));
        if (o.bands > 0) kl = Math.min(1, Math.ceil(kl * o.bands - 1e-6) / o.bands);   // cel shading
      }
      for (const t of tris) {
        const P = t.map(i => project(cam, f.p[i], k)), UV = t.map(i => f.uv[i]);
        // the side that shows is clockwise on screen (y down), as on bm
        const area = (P[1][0] - P[0][0]) * (P[2][1] - P[0][1]) - (P[1][1] - P[0][1]) * (P[2][0] - P[0][0]);
        if (area <= 1e-9) continue;
        const x0 = Math.max(0, Math.floor(Math.min(P[0][0], P[1][0], P[2][0]))), x1 = Math.min(W - 1, Math.ceil(Math.max(P[0][0], P[1][0], P[2][0])));
        const y0 = Math.max(0, Math.floor(Math.min(P[0][1], P[1][1], P[2][1]))), y1 = Math.min(H - 1, Math.ceil(Math.max(P[0][1], P[1][1], P[2][1])));
        const iz = P.map(q => cam.persp ? 1 / q[2] : 1), near = P.map(q => cam.persp ? 1 / q[2] : -q[2]);
        for (let y = y0; y <= y1; y++)
          for (let x = x0; x <= x1; x++) {
            const px = x + 0.5, py = y + 0.5;
            const w0 = (P[1][0] - px) * (P[2][1] - py) - (P[1][1] - py) * (P[2][0] - px);
            const w1 = (P[2][0] - px) * (P[0][1] - py) - (P[2][1] - py) * (P[0][0] - px);
            const w2 = (P[0][0] - px) * (P[1][1] - py) - (P[0][1] - py) * (P[1][0] - px);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const b0 = w0 / area, b1 = w1 / area, b2 = w2 / area;
            const z = b0 * near[0] + b1 * near[1] + b2 * near[2];
            const i = y * W + x;
            if (z <= zb[i]) continue;
            let rgb;
            if (f.c == null) {
              // perspective correct (with an orthographic camera 1/z is the same everywhere)
              const q0 = b0 * iz[0], q1 = b1 * iz[1], q2 = b2 * iz[2], s = q0 + q1 + q2;
              const u = (q0 * UV[0][0] + q1 * UV[1][0] + q2 * UV[2][0]) / s, v = (q0 * UV[0][1] + q1 * UV[1][1] + q2 * UV[2][1]) / s;
              const tx = Math.max(0, Math.min(sheet.w - 1, Math.floor(u))), ty = Math.max(0, Math.min(sheet.h - 1, Math.floor(v)));
              const ti = (ty * sheet.w + tx) * 4;
              if (sheet.px[ti + 3] < 128) continue;
              rgb = [sheet.px[ti], sheet.px[ti + 1], sheet.px[ti + 2]];
            } else rgb = [f.c >> 16 & 255, f.c >> 8 & 255, f.c & 255];
            zb[i] = z;
            img[i * 4] = rgb[0] * kl; img[i * 4 + 1] = rgb[1] * kl; img[i * 4 + 2] = rgb[2] * kl; img[i * 4 + 3] = 255;
          }
      }
    }
    return img;
  }

  /* k x k samples -> one pixel: solid if half of them are */
  function downsample(src, w, h, k) {
    if (k === 1) return src;
    const out = new Uint8ClampedArray(w * h * 4), W = w * k;
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        let n = 0;
        const acc = [0, 0, 0];
        for (let j = 0; j < k; j++)
          for (let i = 0; i < k; i++) {
            const s = ((y * k + j) * W + x * k + i) * 4;
            if (src[s + 3] < 128) continue;
            acc[0] += src[s]; acc[1] += src[s + 1]; acc[2] += src[s + 2]; n++;
          }
        if (n * 2 < k * k) continue;
        const d = (y * w + x) * 4;
        out[d] = acc[0] / n; out[d + 1] = acc[1] / n; out[d + 2] = acc[2] / n; out[d + 3] = 255;
      }
    return out;
  }

  /* a line of `colour` around the sprite (the empty pixels next to it) */
  function outline(img, w, h, colour) {
    const solid = i => img[i * 4 + 3] >= 128, add = [];
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        const i = y * w + x;
        if (solid(i)) continue;
        if ((x > 0 && solid(i - 1)) || (x < w - 1 && solid(i + 1)) || (y > 0 && solid(i - w)) || (y < h - 1 && solid(i + w))) add.push(i);
      }
    for (const i of add) { img[i * 4] = colour >> 16 & 255; img[i * 4 + 1] = colour >> 8 & 255; img[i * 4 + 2] = colour & 255; img[i * 4 + 3] = 255; }
  }

  /* at most n colours for all the frames (median cut), the same palette
   * everywhere so the animation does not flicker */
  function reduceColours(imgs, n) {
    const count = new Map();
    for (const img of imgs)
      for (let i = 0; i < img.length; i += 4)
        if (img[i + 3] >= 128) { const c = (img[i] << 16) | (img[i + 1] << 8) | img[i + 2]; count.set(c, (count.get(c) || 0) + 1); }
    if (count.size <= n) return [...count.keys()];
    let boxes = [[...count.entries()].map(([c, k]) => [c >> 16 & 255, c >> 8 & 255, c & 255, k])];
    while (boxes.length < n) {
      let bi = -1, bc = 0, br = -1;
      boxes.forEach((b, i) => {
        if (b.length < 2) return;
        for (let ch = 0; ch < 3; ch++) {
          let lo = 255, hi = 0;
          for (const e of b) { lo = Math.min(lo, e[ch]); hi = Math.max(hi, e[ch]); }
          if (hi - lo > br) { br = hi - lo; bi = i; bc = ch; }
        }
      });
      if (bi < 0 || br <= 0) break;
      const b = boxes[bi].sort((x, y) => x[bc] - y[bc]);
      let total = 0;
      for (const e of b) total += e[3];
      let acc = 0, cut = 1;
      for (; cut < b.length - 1; cut++) { acc += b[cut - 1][3]; if (acc * 2 >= total) break; }
      boxes.splice(bi, 1, b.slice(0, cut), b.slice(cut));
    }
    const pal = boxes.map(b => {
      let k = 0;
      const s = [0, 0, 0];
      for (const e of b) { s[0] += e[0] * e[3]; s[1] += e[1] * e[3]; s[2] += e[2] * e[3]; k += e[3]; }
      return (Math.round(s[0] / k) << 16) | (Math.round(s[1] / k) << 8) | Math.round(s[2] / k);
    });
    const near = new Map();
    for (const img of imgs)
      for (let i = 0; i < img.length; i += 4) {
        if (img[i + 3] < 128) continue;
        const c = (img[i] << 16) | (img[i + 1] << 8) | img[i + 2];
        let best = near.get(c);
        if (best === undefined) {
          let bd = Infinity;
          for (const p of pal) {
            const d = ((p >> 16 & 255) - img[i]) ** 2 * 3 + ((p >> 8 & 255) - img[i + 1]) ** 2 * 4 + ((p & 255) - img[i + 2]) ** 2 * 2;
            if (d < bd) { bd = d; best = p; }
          }
          near.set(c, best);
        }
        img[i] = best >> 16 & 255; img[i + 1] = best >> 8 & 255; img[i + 2] = best & 255;
      }
    return pal;
  }

  /* model + animation -> { img (grid), cols, rows, w, h, anchor, scale, times, frames } */
  function render(model, sheet, clip, opts) {
    const o = Object.assign({}, DEFAULTS, opts || {});
    o.w = Math.max(4, Math.min(512, o.w | 0)); o.h = Math.max(4, Math.min(512, o.h | 0));
    o.dirs = Math.max(1, Math.min(16, o.dirs | 0)); o.frames = clip ? Math.max(1, Math.min(256, o.frames | 0)) : 1;
    const { times, out } = scenes(model, clip, o);
    const cam = frameCamera(out, o), k = Math.max(1, Math.min(4, o.supersample | 0));
    const frames = out.map(sc => {
      const img = downsample(rasterize(sc.faces, sheet, cam, o, k), o.w, o.h, k);
      return { d: sc.d, i: sc.i, img };
    });
    if (o.colours > 0) reduceColours(frames.map(f => f.img), o.colours);
    if (o.outline != null) for (const f of frames) outline(f.img, o.w, o.h, o.outline);
    const cols = times.length, rows = o.dirs, grid = BM.newImage(cols * o.w, rows * o.h);
    for (const f of frames)
      for (let y = 0; y < o.h; y++)
        grid.px.set(f.img.subarray(y * o.w * 4, (y + 1) * o.w * 4), ((f.d * o.h + y) * grid.w + f.i * o.w) * 4);
    return { img: grid, cols, rows, w: o.w, h: o.h, anchor: cam.anchor, scale: cam.scale, times, frames, opts: o };
  }

  /* Lua for a game: where the frames are in the sheet and how to draw them */
  function snippet(name, res, at, fps) {
    const id = (name || 'anim').replace(/[^A-Za-z0-9_]/g, '_').toUpperCase().replace(/^(\d)/, '_$1');
    const fn = id.toLowerCase();
    return `-- ${name}: ${res.cols} frame${res.cols === 1 ? '' : 's'} of ${res.w}x${res.h}` +
      (res.rows > 1 ? `, ${res.rows} directions (rows: 0 = front, then turning clockwise)` : '') +
      `, made with bm Animator; in the sprite sheet at ${at[0]}, ${at[1]}
local ${id} = { x = ${at[0]}, y = ${at[1]}, w = ${res.w}, h = ${res.h}, frames = ${res.cols}, dirs = ${res.rows},
  ax = ${res.anchor[0]}, ay = ${res.anchor[1]}, fps = ${fps} }

-- draws ${name} at time t (seconds), facing dir (0..${res.rows - 1}), with the
-- model's origin (its feet) at x, y
local function draw_${fn}(t, x, y, dir)
  local s = ${id}
  local f = math.floor(t * s.fps) % s.frames
  sspr(s.x + f * s.w, s.y + ((dir or 0) % s.dirs) * s.h, s.w, s.h, x - s.ax, y - s.ay)
end
`;
  }

  BM.sprites = { DEFAULTS, frameTimes, viewAxes, turn, render, outline, reduceColours, downsample, snippet };
})(typeof window !== 'undefined' ? window : globalThis);
