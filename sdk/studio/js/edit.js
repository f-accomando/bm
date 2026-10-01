/*
 * bm Studio - geometry of the editing tools, without the DOM: tiles on
 * grid planes, blocks, picking with rays, moving, turning and merging.
 * A tile is 1 x 1 unit of the grid; its corners are bottom left, top
 * left, top right, bottom right, clockwise seen from the side that shows.
 */
(function (root) {
  'use strict';
  const BM = root.BM, v3 = BM.v3;
  const AXES = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
  const EPS = 1e-6;

  const snap = (v, step) => Math.round(v / step) * step;
  const unit = (k, s = 1) => { const a = [0, 0, 0]; a[k] = s; return a; };
  const dominant = n => {
    const a = n.map(Math.abs);
    return a[0] >= a[1] && a[0] >= a[2] ? 0 : a[1] >= a[2] ? 1 : 2;
  };

  /* "up" on the floor: the horizontal axis nearest to where the camera
   * looks, so a tile shows the right way up */
  function floorUp(f) {
    return Math.abs(f[0]) > Math.abs(f[2]) ? [Math.sign(f[0]) || 1, 0, 0] : [0, 0, Math.sign(f[2]) || 1];
  }

  /* the plane the tools draw on when the camera looks along f */
  function autoAxis(f) {
    if (Math.abs(f[1]) > 0.4) return 1;
    return Math.abs(f[0]) > Math.abs(f[2]) ? 0 : 2;
  }

  /* n: the side that shows; U: up of the texture; R: right */
  function basis(axis, side, camF) {
    const n = unit(axis, side), U = axis === 1 ? floorUp(camF) : [0, 1, 0];
    return { axis, side, n, U, R: v3.cross(n, U) };
  }

  /* texture corners of a tile rect [x, y, w, h] in corner order, flipped
   * (left-right) and then turned `rot` quarter turns clockwise */
  function rectUV(rect, rot = 0, flip = false) {
    const [x, y, w, h] = rect;
    let b = [[x, y + h], [x, y], [x + w, y], [x + w, y + h]];
    if (flip) b = [b[3], b[2], b[1], b[0]];
    const r = ((rot % 4) + 4) % 4;
    return [0, 1, 2, 3].map(k => b[(k - r + 4) % 4].slice());
  }

  /* the tile on cell `o` (its lowest corner; o[axis] = the plane) */
  function tileFace(o, B, tile) {
    const c = o.slice();
    for (let k = 0; k < 3; k++) if (k !== B.axis) c[k] += 0.5;
    const h = (a, b) => [c[0] + (a * B.R[0] + b * B.U[0]) / 2, c[1] + (a * B.R[1] + b * B.U[1]) / 2, c[2] + (a * B.R[2] + b * B.U[2]) / 2];
    return {
      p: [h(-1, -1), h(-1, 1), h(1, 1), h(1, -1)],
      uv: tile.c == null ? rectUV(tile.rect, tile.rot, tile.flip) : [[0, 0], [0, 0], [0, 0], [0, 0]],
      c: tile.c == null ? null : tile.c,
    };
  }

  /* the tiles of a stamp: a selection of w x h tiles of the sheet (x, y
   * the top left, in tiles), flipped, turned; dx right, dy up */
  function makeStamp(sel, T, rot = 0, flip = false, colour = null) {
    if (colour != null) return { w: 1, h: 1, tiles: [{ dx: 0, dy: 0, c: colour }] };
    let W = sel.w, H = sel.h, tiles = [];
    for (let j = 0; j < H; j++)
      for (let i = 0; i < W; i++)
        tiles.push({ dx: i, dy: H - 1 - j, rect: [(sel.x + i) * T, (sel.y + j) * T, T, T], rot: 0, flip: false, c: null });
    if (flip) tiles = tiles.map(t => ({ ...t, dx: W - 1 - t.dx, flip: !t.flip }));
    for (let r = 0; r < (((rot % 4) + 4) % 4); r++) {
      tiles = tiles.map(t => ({ ...t, dx: t.dy, dy: W - 1 - t.dx, rot: t.rot + 1 }));
      [W, H] = [H, W];
    }
    return { w: W, h: H, tiles };
  }

  function stampFaces(stamp, cell, B) {
    return stamp.tiles.map(t => {
      const o = cell.slice();
      for (let k = 0; k < 3; k++) o[k] += B.R[k] * t.dx + B.U[k] * t.dy;
      return tileFace(o, B, t);
    });
  }

  const spotKey = f => f.p.map(BM.posKey).sort().join(';');

  /* adds faces: one on the same spot facing the same way is replaced (a
   * new texture); with `cancel`, one facing the other way disappears with
   * the new one (two blocks side by side have no wall between them) */
  function placeFaces(faces, added, cancel) {
    const index = new Map();
    faces.forEach((f, i) => {
      const k = spotKey(f);
      if (!index.has(k)) index.set(k, []);
      index.get(k).push(i);
    });
    const remove = new Set(), out = [];
    for (const nf of added) {
      const k = spotKey(nf), n = BM.faceNormal(nf);
      const same = (index.get(k) || []).filter(i => !remove.has(i));
      const facing = same.find(i => v3.dot(BM.faceNormal(faces[i]), n) > 0.9);
      const opposite = same.find(i => v3.dot(BM.faceNormal(faces[i]), n) < -0.9);
      if (cancel && opposite !== undefined) { remove.add(opposite); continue; }
      if (facing !== undefined) { faces[facing].p = nf.p; faces[facing].uv = nf.uv; faces[facing].c = nf.c; out.push(faces[facing]); continue; }
      faces.push(nf);
      out.push(nf);
      const i = faces.length - 1;
      if (!index.has(k)) index.set(k, []);
      index.get(k).push(i);
    }
    if (remove.size) {
      const keep = faces.filter((_, i) => !remove.has(i));
      faces.length = 0;
      faces.push(...keep);
    }
    return out;
  }

  /* the six faces of the unit block with lowest corner `min` */
  function blockFaces(min, tile, camF) {
    const out = [];
    for (let k = 0; k < 3; k++)
      for (const s of [-1, 1]) {
        const B = basis(k, s, camF), o = min.slice();
        o[k] += s > 0 ? 1 : 0;
        out.push(tileFace(o, B, tile));
      }
    return out;
  }

  // ------------------------------------------------------------ rays

  function rayPlane(ray, axis, level) {
    const d = ray.d[axis];
    if (Math.abs(d) < EPS) return null;
    const t = (level - ray.o[axis]) / d;
    if (t <= 0) return null;
    return { t, point: v3.add(ray.o, v3.scale(ray.d, t)) };
  }

  function rayTri(ray, a, b, c) {
    const e1 = v3.sub(b, a), e2 = v3.sub(c, a), pv = v3.cross(ray.d, e2), det = v3.dot(e1, pv);
    if (Math.abs(det) < 1e-12) return null;
    const inv = 1 / det, tv = v3.sub(ray.o, a), u = v3.dot(tv, pv) * inv;
    if (u < -1e-7 || u > 1 + 1e-7) return null;
    const qv = v3.cross(tv, e1), v = v3.dot(ray.d, qv) * inv;
    if (v < -1e-7 || u + v > 1 + 1e-7) return null;
    const t = v3.dot(e2, qv) * inv;
    return t > 1e-6 ? { t, u, v } : null;
  }

  /* the nearest face under the ray; with `cull`, faces seen from behind
   * are not there (as on the console) */
  function raycast(faces, ray, cull) {
    let best = null;
    faces.forEach((f, index) => {
      const n = BM.faceNormal(f);
      if (cull && v3.dot(n, ray.d) >= 0) return;
      const tris = f.p.length === 4 ? [[0, 1, 2], [0, 2, 3]] : [[0, 1, 2]];
      for (const tri of tris) {
        const h = rayTri(ray, f.p[tri[0]], f.p[tri[1]], f.p[tri[2]]);
        if (h && (!best || h.t < best.t)) best = { face: f, index, t: h.t, tri, u: h.u, v: h.v, n };
      }
    });
    if (best) best.point = v3.add(ray.o, v3.scale(ray.d, best.t));
    return best;
  }

  /* texture pixel under a hit */
  function hitUV(hit) {
    const f = hit.face, [a, b, c] = hit.tri, w = 1 - hit.u - hit.v;
    return [f.uv[a][0] * w + f.uv[b][0] * hit.u + f.uv[c][0] * hit.v, f.uv[a][1] * w + f.uv[b][1] * hit.u + f.uv[c][1] * hit.v];
  }

  /* a face lying on a grid plane: its axis, level and side */
  function facePlane(f) {
    const n = BM.faceNormal(f), axis = dominant(n);
    if (Math.abs(n[axis]) < 0.999) return null;
    return { axis, level: f.p[0][axis], side: Math.sign(n[axis]) };
  }

  /* the cell of a plane under a point */
  function cellAt(point, axis, level) {
    const o = point.map(v => Math.floor(v + 1e-6));
    o[axis] = level;
    return o;
  }

  // ------------------------------------------------------------ changes

  function vertices(faces) {
    const map = new Map();
    for (const f of faces)
      f.p.forEach((p, k) => {
        const key = BM.posKey(p);
        let v = map.get(key);
        if (!v) { v = { key, p: p.slice(), refs: [] }; map.set(key, v); }
        v.refs.push([f, k]);
      });
    return map;
  }

  function centerOf(points) {
    const b = { lo: [Infinity, Infinity, Infinity], hi: [-Infinity, -Infinity, -Infinity] };
    for (const p of points) for (let k = 0; k < 3; k++) { b.lo[k] = Math.min(b.lo[k], p[k]); b.hi[k] = Math.max(b.hi[k], p[k]); }
    return [0, 1, 2].map(k => (b.lo[k] + b.hi[k]) / 2);
  }

  const facePoints = faces => faces.flatMap(f => f.p);

  function translate(points, d) {
    for (const p of points) { p[0] += d[0]; p[1] += d[1]; p[2] += d[2]; }
  }

  /* quarter turns around the vertical axis through c (clockwise seen
   * from above) */
  function turnY(points, c, q) {
    q = ((q % 4) + 4) % 4;
    for (const p of points)
      for (let i = 0; i < q; i++) {
        const x = p[0] - c[0], z = p[2] - c[2];
        p[0] = c[0] + z; p[2] = c[2] - x;
      }
  }

  /* quarter turns around any axis (0 x, 1 y, 2 z) through c */
  function turnAxis(points, c, axis, q) {
    if (axis === 1) return turnY(points, c, q);
    const [a, b] = axis === 0 ? [1, 2] : [0, 1];
    q = ((q % 4) + 4) % 4;
    for (const p of points)
      for (let i = 0; i < q; i++) {
        const x = p[a] - c[a], y = p[b] - c[b];
        p[a] = c[a] - y; p[b] = c[b] + x;
      }
  }

  function scalePoints(points, c, k) {
    for (const p of points) for (let i = 0; i < 3; i++) p[i] = c[i] + (p[i] - c[i]) * k;
  }

  /* mirror on an axis through c: the winding turns, so the faces are
   * reversed too (they keep showing outwards) */
  function mirror(faces, c, axis) {
    for (const f of faces) {
      for (const p of f.p) p[axis] = 2 * c[axis] - p[axis];
      flipFace(f);
    }
  }

  /* the other side shows */
  function flipFace(f) {
    f.p.reverse();
    f.uv.reverse();
  }

  function turnUV(f, q = 1) {
    const n = f.uv.length;
    for (let i = 0; i < (((q % n) + n) % n); i++) f.uv.unshift(f.uv.pop());
  }

  function mirrorUV(f) {
    if (f.uv.length !== 4) return;
    const us = f.uv.map(t => t[0]), lo = Math.min(...us), hi = Math.max(...us);
    for (const t of f.uv) t[0] = lo + hi - t[0];
  }

  function retexture(f, tile) {
    if (tile.c != null) { f.c = tile.c; return; }
    f.c = null;
    const uv = rectUV(tile.rect, tile.rot, tile.flip);
    f.uv = f.p.length === 4 ? uv : uv.slice(0, 3);
  }

  /* the corners at `keys` become one point, the middle of them; faces
   * that lose their area go, tiles with two corners in one become
   * triangles */
  function mergeVertices(faces, keys) {
    const pts = [];
    for (const f of faces) for (const p of f.p) if (keys.has(BM.posKey(p))) pts.push(p);
    if (pts.length < 2) return null;
    const c = [0, 1, 2].map(k => pts.reduce((s, p) => s + p[k], 0) / pts.length);
    for (const p of pts) { p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; }
    cleanFaces(faces);
    return c;
  }

  function cleanFaces(faces) {
    const keep = [];
    for (const f of faces) {
      const seen = [], p = [], uv = [];
      f.p.forEach((q, k) => {
        const key = BM.posKey(q);
        if (!seen.includes(key)) { seen.push(key); p.push(q); uv.push(f.uv[k]); }
      });
      if (p.length < 3) continue;
      f.p = p; f.uv = uv;
      if (v3.len(v3.cross(v3.sub(p[1], p[0]), v3.sub(p[2], p[0]))) < 1e-9) continue;
      keep.push(f);
    }
    faces.length = 0;
    faces.push(...keep);
  }

  /* the tile of the sheet a face shows (for the eyedropper) */
  function faceRect(f, T) {
    if (f.c != null) return null;
    const us = f.uv.map(t => t[0]), vs = f.uv.map(t => t[1]);
    const x0 = Math.floor(Math.min(...us) / T), y0 = Math.floor(Math.min(...vs) / T);
    const x1 = Math.max(x0 + 1, Math.ceil(Math.max(...us) / T)), y1 = Math.max(y0 + 1, Math.ceil(Math.max(...vs) / T));
    return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
  }

  /* triangles from a .glb: the two halves of a tile, one after the
   * other, become a tile again */
  function trisToQuads(faces) {
    const out = [], same = (a, b) => Math.abs(a[0] - b[0]) < 1e-6 && Math.abs(a[1] - b[1]) < 1e-6;
    for (let i = 0; i < faces.length; i++) {
      const a = faces[i], b = faces[i + 1];
      if (b && a.p.length === 3 && b.p.length === 3 && a.c === b.c && a.img === b.img &&
          BM.posKey(a.p[0]) === BM.posKey(b.p[0]) && BM.posKey(a.p[2]) === BM.posKey(b.p[1]) &&
          (a.c != null || (same(a.uv[0], b.uv[0]) && same(a.uv[2], b.uv[1]))) &&
          v3.dot(BM.faceNormal(a), BM.faceNormal(b)) > 0.999) {
        out.push({ ...a, p: [a.p[0], a.p[1], a.p[2], b.p[2]], uv: [a.uv[0], a.uv[1], a.uv[2], b.uv[2]] });
        i++;
      } else out.push(a);
    }
    return out;
  }

  BM.edit = {
    trisToQuads,
    AXES, snap, unit, dominant, floorUp, autoAxis, basis, rectUV, tileFace, makeStamp, stampFaces, spotKey,
    placeFaces, blockFaces, rayPlane, rayTri, raycast, hitUV, facePlane, cellAt, vertices, centerOf, facePoints,
    translate, turnY, turnAxis, scalePoints, mirror, flipFace, turnUV, mirrorUV, retexture, mergeVertices,
    cleanFaces, faceRect,
  };
})(typeof window !== 'undefined' ? window : globalThis);
