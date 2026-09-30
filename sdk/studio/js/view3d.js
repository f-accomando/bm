/*
 * bm Studio - the 3D view: camera, grid, the model, and the tools.
 *   Tile    click / drag: tiles of the sheet on the grid plane or on a face
 *   Block   click / drag: unit blocks, the tile on every side
 *   Select  click, Shift+click, drag a box; drag a face to move
 *   Vertex  the same with corners: drag them to make slopes and roofs
 *   Paint   paint pixels of the sheet right on the model
 * Right drag turns the camera, Shift+right (or middle) drag moves it, the
 * wheel zooms; a right click (without dragging) erases the face under it.
 */
(function (root) {
  'use strict';
  const BM = root.BM, E = BM.edit, v3 = BM.v3;

  const TOOL_HINTS = {
    tile: 'Tile: click or drag to lay tiles · on a face: new texture · Alt+click: take its tile · R turn, F flip · [ ] move the grid · right click: erase',
    block: 'Block: click or drag to stack blocks · on a face: a block beside it · [ ] move the grid · right click: erase a face',
    select: 'Select: click / Shift+click / drag a box · drag to move · arrows, PgUp/PgDn move · R turn · Del erase · Ctrl+D copy',
    vertex: 'Vertex: click / Shift+click / drag a box · drag to move (Shift: 1 pixel) · arrows, PgUp/PgDn · M merge',
    paint: 'Paint: draw on the model with the colour · right click: take a colour · the sheet changes with it',
  };

  class View3D {
    constructor(app, canvas) {
      this.app = app;
      this.canvas = canvas;
      this.r = new BM.Renderer(canvas);
      this.cam = new BM.Camera();
      this.drag = null;
      this.hover = null;
      this.mouse = null;
      this.version = -1;
      this.selVersion = -1;
      const c = canvas;
      c.addEventListener('pointerdown', e => this.down(e));
      c.addEventListener('pointermove', e => this.move(e));
      c.addEventListener('pointerup', e => this.up(e));
      c.addEventListener('pointercancel', e => this.up(e));
      c.addEventListener('pointerleave', () => { if (!this.drag) { this.hover = null; this.mouse = null; app.requestRender(); } });
      c.addEventListener('wheel', e => this.wheel(e), { passive: false });
      c.addEventListener('contextmenu', e => e.preventDefault());
      new ResizeObserver(() => this.resize()).observe(c.parentElement);
      this.resize();
    }

    get S() { return this.app.S; }
    faces() { return this.app.model().faces; }

    resize() {
      const p = this.canvas.parentElement, dpr = window.devicePixelRatio || 1;
      this.canvas.width = Math.max(1, Math.round(p.clientWidth * dpr));
      this.canvas.height = Math.max(1, Math.round(p.clientHeight * dpr));
      this.canvas.style.width = p.clientWidth + 'px';
      this.canvas.style.height = p.clientHeight + 'px';
      this.app.requestRender();
    }

    // ---------------------------------------------------------- helpers

    ndc(e) {
      const b = this.canvas.getBoundingClientRect();
      return [((e.clientX - b.left) / b.width) * 2 - 1, 1 - ((e.clientY - b.top) / b.height) * 2, e.clientX - b.left, e.clientY - b.top];
    }

    ray(e) {
      const [x, y] = this.ndc(e);
      this.cam.matrix(this.canvas.width / Math.max(1, this.canvas.height));
      return this.cam.ray(x, y);
    }

    camF() { return this.cam.basis().f; }

    workingPlane() {
      const S = this.S, axis = S.plane === 'auto' ? E.autoAxis(this.camF()) : { floor: 1, x: 0, z: 2 }[S.plane];
      return { axis, level: S.planeLevel[axis] };
    }

    /* where a tile would go under the mouse */
    target(ray) {
      const S = this.S, hit = E.raycast(this.faces(), ray, S.view.cull), wp = this.workingPlane();
      const ph = E.rayPlane(ray, wp.axis, wp.level), camF = this.camF();
      if (hit && (!ph || hit.t <= ph.t + 1e-4)) {
        const fp = E.facePlane(hit.face);
        if (!fp) return { onFace: hit, sloped: true };
        const inside = v3.sub(hit.point, v3.scale(hit.n, 1e-4));
        return { onFace: hit, axis: fp.axis, level: fp.level, side: fp.side, B: E.basis(fp.axis, fp.side, camF),
          cell: E.cellAt(inside, fp.axis, fp.level) };
      }
      if (!ph) return null;
      const eye = this.cam.basis().eye, side = Math.sign(eye[wp.axis] - wp.level) || 1;
      return { axis: wp.axis, level: wp.level, side, B: E.basis(wp.axis, side, camF), cell: E.cellAt(ph.point, wp.axis, wp.level) };
    }

    blockMin(t) {
      const m = t.cell.slice();
      m[t.axis] = t.side > 0 ? t.level : t.level - 1;
      return m;
    }

    previewFaces(t) {
      if (!t || t.sloped) return [];
      const tool = this.S.tool, stamp = this.app.stamp();
      if (tool === 'tile') return E.stampFaces(stamp, t.cell, t.B);
      if (tool === 'block') return E.blockFaces(this.blockMin(t), stamp.tiles[0], this.camF());
      return [];
    }

    step(fine) { return fine ? 1 / this.S.tileSize : this.S.snap; }

    /* camera-relative axis moves for the arrow keys */
    keyAxes() {
      const { r, f } = this.cam.basis(), rk = Math.abs(r[0]) > Math.abs(r[2]) ? 0 : 2;
      const right = E.unit(rk, Math.sign(r[rk]) || 1), fwd = E.floorUp(f);
      return { right, fwd, up: [0, 1, 0] };
    }

    // ---------------------------------------------------------- mouse

    down(e) {
      this.canvas.setPointerCapture(e.pointerId);
      this.app.focusView();
      const S = this.S, [, , mx, my] = this.ndc(e);
      this.mouse = [mx, my];
      if (e.button === 1 || (e.button === 2 && e.shiftKey)) { this.drag = { kind: 'pan', x: e.clientX, y: e.clientY }; return; }
      if (e.button === 2) { this.drag = { kind: 'orbit', x: e.clientX, y: e.clientY, moved: false, e }; return; }
      if (e.button !== 0) return;
      const ray = this.ray(e), faces = this.faces();

      if (S.tool === 'tile' || S.tool === 'block') {
        const t = this.target(ray);
        if (!t) return;
        if (e.altKey) {                                  // eyedropper: the tile of that face
          if (t.onFace) this.app.takeFace(t.onFace.face);
          return;
        }
        this.app.beginEdit();
        if (t.sloped) {
          E.retexture(t.onFace.face, this.app.stamp().tiles[0]);
          this.app.commitEdit('new texture');
          return;
        }
        this.drag = { kind: S.tool, plane: t, last: null, start: t.cell };
        this.placeAt(t);
        return;
      }

      if (S.tool === 'paint') {
        const hit = E.raycast(faces, ray, S.view.cull);
        this.app.beginStroke();
        this.drag = { kind: 'paint' };
        if (hit) this.paintHit(hit);
        return;
      }

      if (S.tool === 'select') {
        const hit = E.raycast(faces, ray, S.view.cull), sel = this.app.selFaces;
        if (hit) {
          if (e.shiftKey) {
            if (sel.has(hit.face)) sel.delete(hit.face); else sel.add(hit.face);
            this.app.selectionChanged();
            return;
          }
          if (!sel.has(hit.face)) { sel.clear(); sel.add(hit.face); this.app.selectionChanged(); }
          this.startMove(e, hit.point, E.facePoints([...sel]));
        } else {
          this.drag = { kind: 'box', x0: mx, y0: my, add: e.shiftKey };
        }
        return;
      }

      if (S.tool === 'vertex') {
        const v = this.nearestVertex(mx, my), sel = this.app.selVerts;
        if (v) {
          if (e.shiftKey) {
            if (sel.has(v.key)) sel.delete(v.key); else sel.add(v.key);
            this.app.selectionChanged();
            return;
          }
          if (!sel.has(v.key)) { sel.clear(); sel.add(v.key); this.app.selectionChanged(); }
          const verts = E.vertices(faces), pts = [];
          for (const k of sel) { const vv = verts.get(k); if (vv) for (const [f, i] of vv.refs) pts.push(f.p[i]); }
          this.startMove(e, v.p, pts, true);
        } else {
          this.drag = { kind: 'box', x0: mx, y0: my, add: e.shiftKey };
        }
      }
    }

    startMove(e, at, points, verts) {
      const axis = this.S.plane === 'auto' ? E.autoAxis(this.camF()) : this.workingPlane().axis;
      this.app.beginEdit();
      this.drag = { kind: 'move', axis, level: at[axis], start: at.slice(), points, orig: points.map(p => p.slice()), verts, moved: false };
    }

    move(e) {
      const [, , mx, my] = this.ndc(e), d = this.drag, S = this.S;
      this.mouse = [mx, my];
      if (!d) {
        if (S.tool === 'tile' || S.tool === 'block') this.hover = this.target(this.ray(e));
        else if (S.tool === 'select' || S.tool === 'paint') {
          const hit = E.raycast(this.faces(), this.ray(e), S.view.cull);
          this.hover = hit ? { onFace: hit } : null;
        } else if (S.tool === 'vertex') this.hover = { vertex: this.nearestVertex(mx, my) };
        this.app.requestRender();
        this.app.hoverInfo(this.hover);
        return;
      }
      if (d.kind === 'orbit') {
        const dx = e.clientX - d.x, dy = e.clientY - d.y;
        if (Math.abs(dx) + Math.abs(dy) > 3) d.moved = true;
        if (!d.moved) return;
        this.cam.yaw += dx * 0.008;
        this.cam.pitch = Math.max(-1.5, Math.min(1.5, this.cam.pitch + dy * 0.008));
        d.x = e.clientX; d.y = e.clientY;
        this.app.requestRender();
        return;
      }
      if (d.kind === 'pan') {
        const { r, u } = this.cam.basis(), k = this.cam.dist * 0.0018 * (window.devicePixelRatio || 1);
        this.cam.target = v3.add(this.cam.target, v3.add(v3.scale(r, -(e.clientX - d.x) * k), v3.scale(u, (e.clientY - d.y) * k)));
        d.x = e.clientX; d.y = e.clientY;
        this.app.requestRender();
        return;
      }
      if (d.kind === 'tile' || d.kind === 'block') {
        const P = d.plane, ph = E.rayPlane(this.ray(e), P.axis, P.level);
        if (!ph) return;
        const cell = E.cellAt(ph.point, P.axis, P.level);
        const t = { ...P, cell, onFace: null };
        if (d.kind === 'tile') {
          const stamp = this.app.stamp(), dd = v3.sub(cell, d.start);
          const dr = Math.round(v3.dot(dd, P.B.R)), du = Math.round(v3.dot(dd, P.B.U));
          if (dr % stamp.w !== 0 || du % stamp.h !== 0) { this.hover = t; this.app.requestRender(); return; }
        }
        this.hover = t;
        this.placeAt(t);
        return;
      }
      if (d.kind === 'paint') {
        const hit = E.raycast(this.faces(), this.ray(e), S.view.cull);
        this.hover = hit ? { onFace: hit } : null;
        if (hit) this.paintHit(hit);
        this.app.requestRender();
        return;
      }
      if (d.kind === 'box') {
        d.x1 = mx; d.y1 = my;
        this.app.showBox(d.x0, d.y0, mx, my);
        return;
      }
      if (d.kind === 'move') {
        const ph = E.rayPlane(this.ray(e), d.axis, d.level);
        if (!ph) return;
        const st = this.step(e.shiftKey), delta = v3.sub(ph.point, d.start).map(v => E.snap(v, st));
        delta[d.axis] = 0;
        d.points.forEach((p, i) => { p[0] = d.orig[i][0] + delta[0]; p[1] = d.orig[i][1] + delta[1]; p[2] = d.orig[i][2] + delta[2]; });
        d.moved = delta.some(v => v !== 0);
        d.delta = delta;
        this.app.modelChanged(true);
        this.app.status(`move ${delta.map(v => +v.toFixed(3)).join(', ')}`);
      }
    }

    up(e) {
      const d = this.drag;
      this.drag = null;
      if (!d) return;
      const S = this.S;
      if (d.kind === 'orbit' && !d.moved) {          // right click: erase
        const hit = E.raycast(this.faces(), this.ray(e), S.view.cull);
        if (S.tool === 'paint') { if (hit) this.app.eyedrop(hit); return; }
        if (hit) this.app.editModel('erase', faces => { const i = faces.indexOf(hit.face); if (i >= 0) faces.splice(i, 1); });
        return;
      }
      if (d.kind === 'tile' || d.kind === 'block') { this.app.commitEdit(d.kind === 'tile' ? 'tiles' : 'blocks'); return; }
      if (d.kind === 'paint') { this.app.endStroke('paint'); return; }
      if (d.kind === 'box') {
        this.app.showBox(null);
        if (d.x1 === undefined) {                     // a click on nothing
          if (!d.add) { this.app.selFaces.clear(); this.app.selVerts.clear(); this.app.selectionChanged(); }
          return;
        }
        this.boxSelect(d);
        return;
      }
      if (d.kind === 'move') {
        if (d.moved) this.app.commitEdit(d.verts ? 'move corners' : 'move'); else this.app.cancelEdit();
        if (d.verts) {                                 // the selected corners are elsewhere now
          const keys = new Set();
          for (const p of d.points) keys.add(BM.posKey(p));
          this.app.selVerts.clear();
          for (const k of keys) this.app.selVerts.add(k);
        }
        this.app.selectionChanged();
      }
    }

    wheel(e) {
      e.preventDefault();
      if (e.ctrlKey || e.metaKey) { this.app.movePlane(e.deltaY < 0 ? 1 : -1); return; }
      this.cam.dist = Math.max(0.5, Math.min(500, this.cam.dist * Math.exp(e.deltaY * 0.0012)));
      this.app.requestRender();
    }

    placeAt(t) {
      const d = this.drag;
      const key = t.cell.join(',');
      if (d && d.last === key) return;
      if (d) d.last = key;
      const faces = this.faces();
      if (this.S.tool === 'tile') E.placeFaces(faces, E.stampFaces(this.app.stamp(), t.cell, t.B), false);
      else E.placeFaces(faces, E.blockFaces(this.blockMin(t), this.app.stamp().tiles[0], this.camF()), true);
      this.app.modelChanged(true);
    }

    paintHit(hit) {
      const f = hit.face;
      if (f.c != null) {
        if (f.c !== this.app.colour()) { f.c = this.app.colour(); this.app.modelChanged(true); this.app.stroke.faces = true; }
        return;
      }
      const [u, v] = E.hitUV(hit);
      this.app.paintPixel(Math.floor(u), Math.floor(v), this.app.colour());
    }

    nearestVertex(mx, my) {
      const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
      this.cam.matrix(this.canvas.width / Math.max(1, this.canvas.height));
      let best = null, bd = 12 * 12;
      for (const v of E.vertices(this.faces()).values()) {
        const s = this.cam.project(v.p, w, h);
        if (!s) continue;
        const d = (s[0] - mx) ** 2 + (s[1] - my) ** 2;
        if (d < bd || (best && Math.abs(d - bd) < 1 && s[2] < best.z)) { bd = d; best = { key: v.key, p: v.p, z: s[2] }; }
      }
      return best;
    }

    boxSelect(d) {
      const x0 = Math.min(d.x0, d.x1), x1 = Math.max(d.x0, d.x1), y0 = Math.min(d.y0, d.y1), y1 = Math.max(d.y0, d.y1);
      const w = this.canvas.clientWidth, h = this.canvas.clientHeight, inside = p => {
        const s = this.cam.project(p, w, h);
        return s && s[0] >= x0 && s[0] <= x1 && s[1] >= y0 && s[1] <= y1;
      };
      this.cam.matrix(this.canvas.width / Math.max(1, this.canvas.height));
      const eye = this.cam.basis().eye;
      if (this.S.tool === 'vertex') {
        if (!d.add) this.app.selVerts.clear();
        for (const v of E.vertices(this.faces()).values()) if (inside(v.p)) this.app.selVerts.add(v.key);
      } else {
        if (!d.add) this.app.selFaces.clear();
        for (const f of this.faces()) {
          if (this.S.view.cull && v3.dot(BM.faceNormal(f), v3.sub(BM.faceCenter(f), eye)) >= 0) continue;
          if (inside(BM.faceCenter(f))) this.app.selFaces.add(f);
        }
      }
      this.app.selectionChanged();
    }

    frame(faces) {
      const b = BM.modelBounds(faces && faces.length ? faces : this.faces());
      if (!b) { this.cam.reset(); this.app.requestRender(); return; }
      this.cam.target = [0, 1, 2].map(k => (b.lo[k] + b.hi[k]) / 2);
      const size = Math.max(1, v3.len(v3.sub(b.hi, b.lo)));
      this.cam.dist = size / (2 * Math.tan(this.cam.fov * Math.PI / 360)) * 1.3 + 0.5;
      this.app.requestRender();
    }

    // ---------------------------------------------------------- drawing

    render() {
      const S = this.S, R = this.r, app = this.app, model = app.model();
      R.begin(S.view.bg, this.cam);
      if (app.modelVersion !== this.version) {
        R.faceBuffer('model', model.faces);
        this.version = app.modelVersion;
        this.edgesDirty = true;
      }
      if (app.selVersion !== this.selVersion || this.edgesDirty) {
        R.faceBuffer('sel', [...app.selFaces]);
        this.selVersion = app.selVersion;
        this.buildEdges();
      }
      const lit = S.view.lit;
      if (this.clean) {                                  // for the cover: the model only
        R.drawFaces(R.buffers.get('model'), { lit, cull: S.view.cull, rgb565: S.view.rgb565 });
        return;
      }
      if (S.view.grid) this.drawGrid();
      R.drawFaces(R.buffers.get('model'), { lit, cull: S.view.cull, rgb565: S.view.rgb565 });
      if (app.selFaces.size) R.drawFaces(R.buffers.get('sel'), { cull: S.view.cull, tint: [1, 0.72, 0.2, 0.35], offset: true });
      if (S.view.wire || S.tool === 'vertex') R.drawLines(R.buffers.get('edges'), {});
      if (app.selFaces.size) R.drawLines(R.buffers.get('seledges'), {});

      const h = this.hover;
      if (h && (S.tool === 'tile' || S.tool === 'block') && !(this.drag && this.drag.kind === 'orbit')) {
        const pf = this.previewFaces(h);
        if (pf.length) {
          R.faceBuffer('preview', pf);
          R.drawFaces(R.buffers.get('preview'), { cull: false, alpha: 0.6, offset: true });
          R.drawLines(R.lineBuffer('previewEdges', this.edgeVerts(pf, [1, 0.85, 0.3, 0.95]), 'lines'), { onTop: false });
        } else if (h.sloped) {
          R.drawLines(R.lineBuffer('hoverEdges', this.edgeVerts([h.onFace.face], [1, 0.85, 0.3, 1]), 'lines'), { onTop: true });
        }
      } else if (h && h.onFace && (S.tool === 'select' || S.tool === 'paint')) {
        R.drawLines(R.lineBuffer('hoverEdges', this.edgeVerts([h.onFace.face], [0.6, 0.85, 1, 1]), 'lines'), { onTop: true });
      }
      if (S.tool === 'vertex') {
        const pts = [], sel = app.selVerts, hv = h && h.vertex ? h.vertex.key : null;
        for (const v of E.vertices(model.faces).values()) {
          const c = sel.has(v.key) ? [1, 0.6, 0.1, 1] : v.key === hv ? [0.6, 0.9, 1, 1] : [0.92, 0.94, 1, 0.9];
          pts.push(...v.p, ...c);
        }
        R.drawLines(R.lineBuffer('points', pts, 'points'), { size: 7 * (window.devicePixelRatio || 1), onTop: true });
      }
      this.edgesDirty = false;
    }

    edgeVerts(faces, col) {
      const out = [];
      for (const f of faces)
        for (let i = 0; i < f.p.length; i++) out.push(...f.p[i], ...col, ...f.p[(i + 1) % f.p.length], ...col);
      return out;
    }

    buildEdges() {
      const R = this.r, faces = this.app.model().faces;
      R.lineBuffer('edges', this.edgeVerts(faces, [0.85, 0.9, 1, 0.35]), 'lines');
      R.lineBuffer('seledges', this.edgeVerts([...this.app.selFaces], [1, 0.75, 0.25, 1]), 'lines');
    }

    drawGrid() {
      const wp = this.workingPlane(), out = [];
      const plane = (axis, level, ca, cb, N, fade) => {
        const [a, b] = [0, 1, 2].filter(k => k !== axis);
        const P = (u, v) => { const p = [0, 0, 0]; p[axis] = level; p[a] = u; p[b] = v; return p; };
        for (let i = -N; i <= N; i++) {
          const ua = ca + i, ub = cb + i;
          const colA = ua === 0 && level === 0 ? this.axisColour(b) : [1, 1, 1, (ua % 4 === 0 ? 0.16 : 0.07) * fade];
          const colB = ub === 0 && level === 0 ? this.axisColour(a) : [1, 1, 1, (ub % 4 === 0 ? 0.16 : 0.07) * fade];
          out.push(...P(ua, cb - N), ...colA, ...P(ua, cb + N), ...colA);
          out.push(...P(ca - N, ub), ...colB, ...P(ca + N, ub), ...colB);
        }
        return [a, b];
      };
      // the working plane, around what the camera looks at
      const c = this.cam.target, N = Math.min(64, Math.max(12, Math.ceil(this.cam.dist * 1.6)));
      const [a0, b0] = [0, 1, 2].filter(k => k !== wp.axis);
      plane(wp.axis, wp.level, Math.round(c[a0]), Math.round(c[b0]), N, 1);
      // a plane the tools use now (a face under the mouse, or a drag): a
      // small grid around the cell, brighter
      const t = this.drag && this.drag.plane ? { ...this.drag.plane, cell: this.hover && this.hover.cell || this.drag.plane.cell }
        : this.hover && this.hover.axis !== undefined ? this.hover : null;
      if (t && t.cell && (t.axis !== wp.axis || t.level !== wp.level)) {
        const [a, b] = [0, 1, 2].filter(k => k !== t.axis);
        plane(t.axis, t.level, t.cell[a], t.cell[b], 3, 3.5);
      }
      // the origin: where draw3d() puts x, y, z
      out.push(0, 0, 0, 1, 0.3, 0.3, 1, 0.6, 0, 0, 1, 0.3, 0.3, 1);
      out.push(0, 0, 0, 0.3, 1, 0.3, 1, 0, 0.6, 0, 0.3, 1, 0.3, 1);
      out.push(0, 0, 0, 0.3, 0.5, 1, 1, 0, 0, 0.6, 0.3, 0.5, 1, 1);
      this.r.drawLines(this.r.lineBuffer('grid', out, 'lines'), {});
    }

    axisColour(k) { return [[1, 0.35, 0.35, 0.55], [0.35, 1, 0.35, 0.55], [0.4, 0.55, 1, 0.55]][k]; }
  }

  BM.View3D = View3D;
  BM.TOOL_HINTS = TOOL_HINTS;
})(typeof window !== 'undefined' ? window : globalThis);
