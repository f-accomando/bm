/*
 * bm Animator - skeletons, keyframe animation and pre-rendered sprites for
 * the 3D models of a .bm (made with bm Studio). Three pages:
 *   Rig      bones (head and tail, a tree) and skin: which bone each face
 *            (or corner) follows
 *   Animate  poses on a timeline: turn a bone with the rings, aim it by its
 *            tail, move it by its head; every change is a keyframe
 *   Sprites  the animation drawn from 1-8 directions into sprites for the
 *            sprite sheet
 * The skeletons and animations go in the .bm (section ANIM); in the game
 * model() brings them and animate() plays them. See sdk/README.md.
 */
(function () {
  'use strict';
  const BM = window.BM, R = BM.rig, Q = R.Q, M = R.M, E = BM.edit, v3 = BM.v3;
  const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
  const hex6 = c => '#' + (c & 0xFFFFFF).toString(16).padStart(6, '0');
  const slug = s => (s || 'game').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'game';
  const SNAP = 1 / 32;
  const snap = v => Math.round(v / SNAP) * SNAP;
  const AXIS_COL = [[1, 0.35, 0.35, 1], [0.4, 1, 0.4, 1], [0.4, 0.6, 1, 1]];

  const HINTS = {
    bones: 'Bones: click a bone to choose it · drag a joint to move it (joints in one place move together; Shift: only this bone) · N new bone · Del delete',
    skin: 'Skin: click / Shift+click / drag a box to choose faces · A: they follow the chosen bone · Alt+click: the bone of a face',
    anim: 'Animate: click a bone · drag a ring to turn it · drag its tail to aim it · drag its head to move it · Space play · K key · ←/→ frames',
  };

  function loadPrefs() { try { return JSON.parse(localStorage.getItem('bmanimator.prefs') || '{}'); } catch (e) { return {}; } }
  function savePrefs(S) { try { localStorage.setItem('bmanimator.prefs', JSON.stringify({ view: S.view, fps: S.fps, spr: S.spr })); } catch (e) { /* private */ } }

  function exampleProject() {
    return {
      title: 'Villager', author: '', res: '640x360', lua: BM.viewerLua(), sheet: BM.starterSheet(), map: null, cover: null,
      models: [BM.examples.villager()], uvInset: 0.25, extras: [],
    };
  }

  /* ================================================================ 3D view */

  class AnimView {
    constructor(app, canvas) {
      this.app = app;
      this.canvas = canvas;
      this.r = new BM.Renderer(canvas);
      this.cam = new BM.Camera();
      this.cam.target = [0, 1, 0];
      this.cam.dist = 6;
      this.drag = null;
      this.hover = null;
      canvas.addEventListener('pointerdown', e => this.down(e));
      canvas.addEventListener('pointermove', e => this.move(e));
      canvas.addEventListener('pointerup', e => this.up(e));
      canvas.addEventListener('pointercancel', e => this.up(e));
      canvas.addEventListener('wheel', e => {
        e.preventDefault();
        this.cam.dist = Math.max(0.3, Math.min(300, this.cam.dist * Math.exp(e.deltaY * 0.0012)));
        app.requestRender();
      }, { passive: false });
      canvas.addEventListener('contextmenu', e => e.preventDefault());
      new ResizeObserver(() => this.resize()).observe(canvas.parentElement);
      this.resize();
    }

    get S() { return this.app.S; }

    resize() {
      const p = this.canvas.parentElement, dpr = window.devicePixelRatio || 1;
      this.canvas.width = Math.max(1, Math.round(p.clientWidth * dpr));
      this.canvas.height = Math.max(1, Math.round(p.clientHeight * dpr));
      this.canvas.style.width = p.clientWidth + 'px';
      this.canvas.style.height = p.clientHeight + 'px';
      this.app.requestRender();
    }

    mouse(e) {
      const b = this.canvas.getBoundingClientRect();
      return { x: e.clientX - b.left, y: e.clientY - b.top, nx: ((e.clientX - b.left) / b.width) * 2 - 1, ny: 1 - ((e.clientY - b.top) / b.height) * 2 };
    }

    updateCam() { this.cam.matrix(this.canvas.width / Math.max(1, this.canvas.height)); }
    ray(m) { this.updateCam(); return this.cam.ray(m.nx, m.ny); }
    toScreen(p) { this.updateCam(); return this.cam.project(p, this.canvas.clientWidth, this.canvas.clientHeight); }

    /* the plane through p facing the camera, under the mouse */
    onViewPlane(m, p) {
      const ray = this.ray(m), f = this.cam.basis().f, d = v3.dot(ray.d, f);
      if (Math.abs(d) < 1e-6) return null;
      const t = v3.dot(v3.sub(p, ray.o), f) / d;
      return t > 0 ? v3.add(ray.o, v3.scale(ray.d, t)) : null;
    }

    /* bone ends where the view shows them (rest in Rig, posed in Animate) */
    bones() {
      const app = this.app, rig = app.rig();
      if (!rig) return [];
      return app.S.mode === 'rig' ? rig.bones.map(b => ({ head: b.head, tail: b.tail })) : R.posedBones(rig, app.currentPose());
    }

    pickBone(m) {
      let best = -1, bd = 9 * 9;
      this.bones().forEach((b, i) => {
        const a = this.toScreen(b.head), c = this.toScreen(b.tail);
        if (!a || !c) return;
        const ab = [c[0] - a[0], c[1] - a[1]], ap = [m.x - a[0], m.y - a[1]], l2 = ab[0] ** 2 + ab[1] ** 2;
        const u = l2 > 1e-9 ? Math.max(0, Math.min(1, (ap[0] * ab[0] + ap[1] * ab[1]) / l2)) : 0;
        const d = (ap[0] - ab[0] * u) ** 2 + (ap[1] - ab[1] * u) ** 2;
        if (d < bd) { bd = d; best = i; }
      });
      return best;
    }

    /* a joint (head or tail of a bone) under the mouse: { bone, end } */
    pickJoint(m, only) {
      let best = null, bd = 10 * 10;
      this.bones().forEach((b, i) => {
        if (only !== undefined && i !== only) return;
        for (const end of ['tail', 'head']) {
          const s = this.toScreen(b[end]);
          if (!s) continue;
          const d = (s[0] - m.x) ** 2 + (s[1] - m.y) ** 2;
          if (d < bd) { bd = d; best = { bone: i, end, p: b[end] }; }
        }
      });
      return best;
    }

    ringRadius() { return this.cam.dist * 0.09; }

    ringPoints(k, c, r) {
      const [a, b] = [0, 1, 2].filter(i => i !== k), pts = [];
      for (let i = 0; i <= 48; i++) {
        const th = i / 48 * Math.PI * 2, p = c.slice();
        p[a] += Math.cos(th) * r; p[b] += Math.sin(th) * r;
        pts.push(p);
      }
      return pts;
    }

    pickRing(m) {
      const app = this.app, i = app.bone, rig = app.rig();
      if (app.S.mode !== 'anim' || i < 0 || !rig) return -1;
      const c = this.bones()[i].head, r = this.ringRadius();
      let best = -1, bd = 7 * 7;
      for (let k = 0; k < 3; k++) {
        const pts = this.ringPoints(k, c, r).map(p => this.toScreen(p));
        for (let j = 0; j + 1 < pts.length; j++) {
          const a = pts[j], b = pts[j + 1];
          if (!a || !b) continue;
          const ab = [b[0] - a[0], b[1] - a[1]], ap = [m.x - a[0], m.y - a[1]], l2 = ab[0] ** 2 + ab[1] ** 2;
          const u = l2 > 1e-9 ? Math.max(0, Math.min(1, (ap[0] * ab[0] + ap[1] * ab[1]) / l2)) : 0;
          const d = (ap[0] - ab[0] * u) ** 2 + (ap[1] - ab[1] * u) ** 2;
          if (d < bd) { bd = d; best = k; }
        }
      }
      return best;
    }

    // ---------------------------------------------------------- mouse

    down(e) {
      this.canvas.setPointerCapture(e.pointerId);
      const app = this.app, S = this.S, m = this.mouse(e);
      app.blurInputs();
      if (e.button === 1 || (e.button === 2 && e.shiftKey)) { this.drag = { kind: 'pan', x: e.clientX, y: e.clientY }; return; }
      if (e.button === 2) { this.drag = { kind: 'orbit', x: e.clientX, y: e.clientY }; return; }
      if (e.button !== 0) return;
      const rig = app.rig();

      if (S.mode === 'rig' && S.rtool === 'skin') {
        const hit = E.raycast(app.model().faces, this.ray(m), S.view.cull);
        if (hit && e.altKey) { const b = (hit.face.b || [0])[0]; app.selectBone(b); return; }
        if (hit) {
          if (e.shiftKey) { if (app.selFaces.has(hit.face)) app.selFaces.delete(hit.face); else app.selFaces.add(hit.face); } else { app.selFaces.clear(); app.selFaces.add(hit.face); }
          app.requestRender();
          app.updateStatus();
          return;
        }
        this.drag = { kind: 'box', x0: m.x, y0: m.y, add: e.shiftKey };
        return;
      }
      if (!rig) return;

      if (S.mode === 'rig') {
        const j = this.pickJoint(m);
        if (j) {
          app.selectBone(j.bone);
          app.beginEdit();
          // joints in the same place move together (a chain stays joined), unless Shift
          const key = BM.posKey(j.p), group = [];
          rig.bones.forEach((b, i) => {
            for (const end of ['head', 'tail'])
              if ((i === j.bone && end === j.end) || (!e.shiftKey && BM.posKey(b[end]) === key)) group.push([i, end]);
          });
          this.drag = { kind: 'joint', start: j.p.slice(), group, moved: false };
          return;
        }
        const b = this.pickBone(m);
        app.selectBone(b);
        return;
      }

      // Animate
      const ring = this.pickRing(m);
      if (ring >= 0) { this.startTurn(m, ring); return; }
      const bi = app.bone;
      if (bi >= 0) {
        const j = this.pickJoint(m, bi);
        if (j && j.end === 'tail') { this.startAim(m); return; }
        if (j && j.end === 'head') { this.startMove(m); return; }
      }
      const b = this.pickBone(m);
      app.selectBone(b);
      if (b >= 0) {
        const j = this.pickJoint(m, b);
        if (j && j.end === 'tail') this.startAim(m);
      }
    }

    startTurn(m, k) {
      const app = this.app, i = app.bone, pose = app.editPose(), c = R.posedBones(app.rig(), pose)[i].head;
      const axis = E.unit(k);
      const at = this.onAxisPlane(m, c, axis);
      app.beginEdit();
      this.drag = { kind: 'turn', k, axis, c, q0: pose[i].q.slice(), at, sx: m.x, sy: m.y, moved: false };
    }

    onAxisPlane(m, c, axis) {
      const ray = this.ray(m), d = v3.dot(ray.d, axis);
      if (Math.abs(d) < 0.15) return null;            // the ring seen edge on: turn by the mouse around the centre
      const t = v3.dot(v3.sub(c, ray.o), axis) / d;
      return t > 0 ? v3.sub(v3.add(ray.o, v3.scale(ray.d, t)), c) : null;
    }

    startAim(m) {
      const app = this.app, i = app.bone, pose = app.editPose(), b = R.posedBones(app.rig(), pose)[i];
      app.beginEdit();
      this.drag = { kind: 'aim', head: b.head, v0: v3.sub(b.tail, b.head), q0: pose[i].q.slice(), moved: false };
    }

    startMove(m) {
      const app = this.app, i = app.bone, pose = app.editPose(), b = R.posedBones(app.rig(), pose)[i];
      app.beginEdit();
      this.drag = { kind: 'move', p0: b.head, t0: pose[i].t.slice(), start: this.onViewPlane(m, b.head), moved: false };
    }

    /* a turn in the world -> the bone's own turn: q = Wp^-1 * r * Wp * q0 */
    applyWorldTurn(r, q0) {
      const app = this.app, rig = app.rig(), i = app.bone, pose = app.editPose(), p = rig.bones[i].parent;
      const wp = p >= 0 ? R.worldRot(rig, pose, p) : Q.id();
      pose[i].q = Q.norm(Q.mul(Q.mul(Q.mul(Q.conj(wp), r), wp), q0));
      app.poseChanged(true);
    }

    move(e) {
      const app = this.app, d = this.drag, m = this.mouse(e);
      if (!d) {
        const S = this.S;
        let hover = null;
        if (S.mode === 'anim') { const ring = this.pickRing(m); hover = ring >= 0 ? { ring } : null; }
        if (S.mode === 'rig' && S.rtool === 'bones' && app.rig()) { const j = this.pickJoint(m); hover = j ? { joint: j } : null; }
        if (JSON.stringify(hover) !== JSON.stringify(this.hover)) { this.hover = hover; app.requestRender(); }
        return;
      }
      if (d.kind === 'orbit') {
        this.cam.yaw += (e.clientX - d.x) * 0.008;
        this.cam.pitch = Math.max(-1.5, Math.min(1.5, this.cam.pitch + (e.clientY - d.y) * 0.008));
        d.x = e.clientX; d.y = e.clientY;
        app.requestRender();
        return;
      }
      if (d.kind === 'pan') {
        const { r, u } = this.cam.basis(), k = this.cam.dist * 0.0018;
        this.cam.target = v3.add(this.cam.target, v3.add(v3.scale(r, -(e.clientX - d.x) * k), v3.scale(u, (e.clientY - d.y) * k)));
        d.x = e.clientX; d.y = e.clientY;
        app.requestRender();
        return;
      }
      if (d.kind === 'box') { d.x1 = m.x; d.y1 = m.y; app.showBox(d.x0, d.y0, m.x, m.y); return; }
      if (d.kind === 'joint') {
        const p = this.onViewPlane(m, d.start);
        if (!p) return;
        const np = e.altKey ? p : p.map(snap), rig = app.rig();
        for (const [i, end] of d.group) rig.bones[i][end] = np.slice();
        d.moved = true;
        app.rigChanged(true);
        app.status(`${end2(d.group)} at ${np.map(v => +v.toFixed(3)).join(', ')}`);
        return;
      }
      if (d.kind === 'turn') {
        let ang;
        const at = this.onAxisPlane(m, d.c, d.axis);
        if (d.at && at) {
          const a = v3.norm(d.at), b = v3.norm(at);
          ang = Math.atan2(v3.dot(v3.cross(a, b), d.axis), v3.dot(a, b));
        } else {
          const c = this.toScreen(d.c);
          if (!c) return;
          const a0 = Math.atan2(d.sy - c[1], d.sx - c[0]), a1 = Math.atan2(m.y - c[1], m.x - c[0]);
          const towards = v3.dot(this.cam.basis().f, d.axis) > 0 ? 1 : -1;
          ang = (a1 - a0) * towards;
        }
        if (e.shiftKey) ang = Math.round(ang / (Math.PI / 12)) * (Math.PI / 12);      // 15 degree steps
        this.applyWorldTurn(Q.axisAngle(d.axis, ang), d.q0);
        d.moved = true;
        app.status(`turn ${'XYZ'[d.k]} ${(ang * 180 / Math.PI).toFixed(1)}°`);
        return;
      }
      if (d.kind === 'aim') {
        const p = this.onViewPlane(m, v3.add(d.head, d.v0));
        if (!p) return;
        const a = v3.norm(d.v0), b = v3.norm(v3.sub(p, d.head)), ax = v3.cross(a, b), s = v3.len(ax);
        if (s < 1e-6) return;
        this.applyWorldTurn(Q.axisAngle(ax, Math.atan2(s, v3.dot(a, b))), d.q0);
        d.moved = true;
        return;
      }
      if (d.kind === 'move') {
        const p = this.onViewPlane(m, d.p0);
        if (!p || !d.start) return;
        const rig = app.rig(), i = app.bone, pose = app.editPose(), par = rig.bones[i].parent;
        let w = v3.sub(p, d.start);
        if (!e.altKey) w = w.map(snap);
        const local = par >= 0 ? Q.rotate(Q.conj(R.worldRot(rig, pose, par)), w) : w;
        pose[i].t = [0, 1, 2].map(k => d.t0[k] + local[k]);
        d.moved = true;
        app.poseChanged(true);
      }

      function end2(group) { return group.length > 1 ? `${group.length} joints` : 'joint'; }
    }

    up(e) {
      const app = this.app, d = this.drag;
      this.drag = null;
      if (!d) return;
      if (d.kind === 'box') {
        app.showBox(null);
        if (d.x1 === undefined) { if (!d.add) { app.selFaces.clear(); app.requestRender(); app.updateStatus(); } return; }
        const x0 = Math.min(d.x0, d.x1), x1 = Math.max(d.x0, d.x1), y0 = Math.min(d.y0, d.y1), y1 = Math.max(d.y0, d.y1);
        if (!d.add) app.selFaces.clear();
        const eye = this.cam.basis().eye;
        for (const f of app.model().faces) {
          if (this.S.view.cull && v3.dot(BM.faceNormal(f), v3.sub(BM.faceCenter(f), eye)) >= 0) continue;
          const s = this.toScreen(BM.faceCenter(f));
          if (s && s[0] >= x0 && s[0] <= x1 && s[1] >= y0 && s[1] <= y1) app.selFaces.add(f);
        }
        app.requestRender();
        app.updateStatus();
        return;
      }
      if (d.kind === 'joint') { if (d.moved) app.commitEdit('move joint'); else app.cancelEdit(); app.refreshBones(); return; }
      if (d.kind === 'turn' || d.kind === 'aim' || d.kind === 'move') {
        if (d.moved) app.poseDone({ turn: 'turn', aim: 'aim', move: 'move' }[d.kind]); else app.cancelEdit();
      }
    }

    frame() {
      const b = BM.modelBounds(this.app.model().faces);
      if (!b) { this.cam.reset(); this.app.requestRender(); return; }
      this.cam.target = [0, 1, 2].map(k => (b.lo[k] + b.hi[k]) / 2);
      this.cam.dist = Math.max(1, v3.len(v3.sub(b.hi, b.lo))) * 1.6 + 0.5;
      this.app.requestRender();
    }

    // ---------------------------------------------------------- drawing

    boneShape(head, tail, colour) {
      const d = v3.sub(tail, head), L = v3.len(d);
      if (L < 1e-6) return [];
      const n = v3.scale(d, 1 / L), up = Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
      const a = v3.norm(v3.cross(n, up)), b = v3.cross(n, a), w = Math.min(0.12, L * 0.12);
      const mid = v3.add(head, v3.scale(d, 0.2));
      const c = [0, 1, 2, 3].map(k => v3.add(mid, v3.add(v3.scale(a, Math.cos(k * Math.PI / 2) * w), v3.scale(b, Math.sin(k * Math.PI / 2) * w))));
      const faces = [];
      for (let k = 0; k < 4; k++) {
        const c0 = c[k], c1 = c[(k + 1) % 4];
        faces.push({ p: [head, c0, c1], uv: [[0, 0], [0, 0], [0, 0]], c: colour });
        faces.push({ p: [tail, c1, c0], uv: [[0, 0], [0, 0], [0, 0]], c: colour });
      }
      return faces;
    }

    grid() {
      const out = [], N = 8, c = [0.2, 0.22, 0.3];
      for (let i = -N; i <= N; i++) {
        const col = i === 0 ? [0.5, 0.3, 0.3, 0.8] : [1, 1, 1, i % 4 === 0 ? 0.14 : 0.06];
        out.push(i, 0, -N, ...col, i, 0, N, ...col);
        const col2 = i === 0 ? [0.3, 0.4, 0.7, 0.8] : [1, 1, 1, i % 4 === 0 ? 0.14 : 0.06];
        out.push(-N, 0, i, ...col2, N, 0, i, ...col2);
      }
      void c;
      return out;
    }

    render() {
      const app = this.app, S = this.S, Rr = this.r, model = app.model(), rig = app.rig();
      // the sheet is the texture of the faces with tiles: a new file, bm Studio's
      // project, the sprites put in the sheet or undone (else they show magenta)
      if (app.project.sheet && this.sheet !== app.project.sheet) {
        Rr.uploadSheet(app.project.sheet);
        this.sheet = app.project.sheet;
      }
      Rr.begin(S.view.bg, this.cam);
      Rr.drawLines(Rr.lineBuffer('grid', this.grid(), 'lines'), {});
      const anim = S.mode === 'anim' && rig;
      const pose = anim ? app.currentPose() : null;
      const faces = anim ? R.posedFaces(model, pose) : model.faces;
      Rr.faceBuffer('model', faces);
      Rr.drawFaces(Rr.buffers.get('model'), { lit: S.view.lit, cull: S.view.cull });
      if (S.view.skin && rig) {
        const tinted = faces.map((f, k) => {
          const src = model.faces[k], b = src.b ? majority(src.b) : 0;
          return { p: f.p, uv: f.uv, c: R.boneColour(b) };
        });
        Rr.faceBuffer('skin', tinted);
        Rr.drawFaces(Rr.buffers.get('skin'), { cull: S.view.cull, alpha: S.mode === 'rig' ? 0.55 : 0.3, offset: true });
      }
      if (app.selFaces.size && S.mode === 'rig') {
        Rr.faceBuffer('sel', [...app.selFaces]);
        Rr.drawFaces(Rr.buffers.get('sel'), { cull: S.view.cull, tint: [1, 0.72, 0.2, 0.6], offset: true });
      }
      if (anim && S.view.onion) {
        const clip = app.clipObj();
        if (clip) {
          const prev = [...clip.keys].reverse().find(k => k.t < app.time - 1e-4), next = clip.keys.find(k => k.t > app.time + 1e-4);
          for (const [key, tint, name] of [[prev, [0.3, 0.5, 1, 0.5], 'onionA'], [next, [0.3, 1, 0.5, 0.5], 'onionB']]) {
            if (!key) continue;
            Rr.faceBuffer(name, R.posedFaces(model, key.pose));
            Rr.drawFaces(Rr.buffers.get(name), { cull: S.view.cull, alpha: 0.22, tint });
          }
        }
      }
      if (rig && S.view.bones) {
        const bones = anim ? R.posedBones(rig, pose) : rig.bones;
        const shapes = [], pts = [];
        bones.forEach((b, i) => {
          shapes.push(...this.boneShape(b.head, b.tail, i === app.bone ? 0xFFC050 : R.boneColour(i)));
          const hj = this.hover && this.hover.joint;
          for (const end of ['head', 'tail']) {
            const hot = hj && hj.bone === i && hj.end === end;
            pts.push(...b[end], ...(hot ? [1, 1, 1, 1] : i === app.bone ? [1, 0.75, 0.3, 1] : [0.85, 0.9, 1, 0.9]));
          }
        });
        Rr.faceBuffer('bones', shapes);
        Rr.drawFaces(Rr.buffers.get('bones'), { cull: false, alpha: 0.85, noDepth: true });
        Rr.drawLines(Rr.lineBuffer('joints', pts, 'points'), { size: 7 * (window.devicePixelRatio || 1), onTop: true });
      }
      if (anim && app.bone >= 0) {
        const c = R.posedBones(rig, pose)[app.bone].head, r = this.ringRadius(), out = [];
        const hot = this.drag && this.drag.kind === 'turn' ? this.drag.k : this.hover && this.hover.ring !== undefined ? this.hover.ring : -1;
        for (let k = 0; k < 3; k++) {
          const col = k === hot ? [1, 1, 0.6, 1] : AXIS_COL[k], pts = this.ringPoints(k, c, r);
          for (let j = 0; j + 1 < pts.length; j++) out.push(...pts[j], ...col, ...pts[j + 1], ...col);
        }
        Rr.drawLines(Rr.lineBuffer('rings', out, 'lines'), { onTop: true });
      }
    }
  }

  function majority(b) {
    const n = new Map();
    let best = b[0], bn = 0;
    for (const x of b) { const c = (n.get(x) || 0) + 1; n.set(x, c); if (c > bn) { bn = c; best = x; } }
    return best;
  }

  /* ================================================================ timeline */

  class Timeline {
    constructor(app, canvas) {
      this.app = app;
      this.canvas = canvas;
      this.ctx = canvas.getContext('2d');
      this.drag = null;
      canvas.addEventListener('pointerdown', e => this.down(e));
      canvas.addEventListener('pointermove', e => this.move(e));
      canvas.addEventListener('pointerup', e => this.up(e));
      canvas.addEventListener('contextmenu', e => e.preventDefault());
      new ResizeObserver(() => this.resize()).observe(canvas);
      this.resize();
    }

    resize() {
      const dpr = window.devicePixelRatio || 1, w = this.canvas.clientWidth, h = this.canvas.clientHeight;
      this.canvas.width = Math.max(1, Math.round(w * dpr));
      this.canvas.height = Math.max(1, Math.round(h * dpr));
      this.dpr = dpr;
      this.app.requestRender();
    }

    geom() {
      const clip = this.app.clipObj(), w = this.canvas.width / this.dpr, pad = 14;
      const L = clip ? clip.length : 1;
      return { clip, L, pad, w, x: t => pad + t / L * (w - 2 * pad), t: x => Math.max(0, Math.min(L, (x - pad) / (w - 2 * pad) * L)) };
    }

    snapT(t) { const f = this.app.S.fps; return Math.round(t * f) / f; }

    down(e) {
      this.canvas.setPointerCapture(e.pointerId);
      const app = this.app, g = this.geom(), b = this.canvas.getBoundingClientRect(), x = e.clientX - b.left;
      if (!g.clip) return;
      app.stop();
      const k = g.clip.keys.findIndex(key => Math.abs(g.x(key.t) - x) < 7);
      if (e.button === 2 && k >= 0) { app.setTime(g.clip.keys[k].t); app.cmd('deleteKey'); return; }
      if (k >= 0) {
        app.setTime(g.clip.keys[k].t);
        app.beginEdit();
        this.drag = { kind: 'key', key: g.clip.keys[k], x0: x, moved: false };
        return;
      }
      this.drag = { kind: 'scrub' };
      app.setTime(this.snapT(g.t(x)));
    }

    move(e) {
      const d = this.drag;
      if (!d) return;
      const app = this.app, g = this.geom(), b = this.canvas.getBoundingClientRect(), x = e.clientX - b.left;
      if (d.kind === 'scrub') { app.setTime(this.snapT(g.t(x))); return; }
      if (Math.abs(x - d.x0) < 3 && !d.moved) return;
      const t = Math.min(g.L, this.snapT(g.t(x)));
      if (g.clip.keys.some(k => k !== d.key && Math.abs(k.t - t) < 1e-4)) return;    // another key there
      d.key.t = t;
      d.moved = true;
      g.clip.keys.sort((p, q) => p.t - q.t);
      app.setTime(t);
    }

    up() {
      const d = this.drag;
      this.drag = null;
      if (d && d.kind === 'key') { if (d.moved) this.app.commitEdit('move key'); else this.app.cancelEdit(); }
    }

    render() {
      const ctx = this.ctx, g = this.geom(), dpr = this.dpr, H = this.canvas.height / dpr, app = this.app;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = '#161922';
      ctx.fillRect(0, 0, g.w, H);
      if (!g.clip) {
        ctx.fillStyle = '#8088a0';
        ctx.font = '12px system-ui, sans-serif';
        ctx.fillText(app.rig() ? 'No animation: New (on the right), or turn a bone and one is made.' : 'No bones yet: make them in Rig.', 12, H / 2 + 4);
        return;
      }
      const fps = app.S.fps, n = Math.round(g.L * fps);
      ctx.fillStyle = '#1c2030';
      ctx.fillRect(g.x(0), 0, g.x(g.L) - g.x(0), H);
      for (let f = 0; f <= n; f++) {
        const x = Math.round(g.x(f / fps)) + 0.5, major = f % fps === 0;
        ctx.strokeStyle = major ? 'rgba(255,255,255,0.35)' : 'rgba(255,255,255,0.1)';
        ctx.beginPath(); ctx.moveTo(x, major ? 0 : 14); ctx.lineTo(x, H); ctx.stroke();
        if (major || (g.x(1 / fps) - g.x(0) > 26 && f % Math.max(1, Math.round(fps / 4)) === 0)) {
          ctx.fillStyle = '#8088a0';
          ctx.font = '10px ui-monospace, monospace';
          ctx.fillText(major ? (f / fps) + 's' : String(f), x + 3, 10);
        }
      }
      const y = H / 2 + 6;
      for (const key of g.clip.keys) {
        const x = g.x(key.t), on = Math.abs(key.t - app.time) < 1e-4;
        ctx.fillStyle = on ? '#ffc050' : '#c8d0e8';
        ctx.beginPath(); ctx.moveTo(x, y - 7); ctx.lineTo(x + 6, y); ctx.lineTo(x, y + 7); ctx.lineTo(x - 6, y); ctx.closePath(); ctx.fill();
        ctx.strokeStyle = '#11131a'; ctx.stroke();
      }
      const px = Math.round(g.x(Math.min(app.time, g.L))) + 0.5;
      ctx.strokeStyle = '#ff6060';
      ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, H); ctx.stroke();
      ctx.lineWidth = 1;
    }
  }

  /* ================================================================ app */

  class App {
    constructor() {
      const prefs = loadPrefs();
      this.S = {
        mode: 'rig', rtool: 'bones', fps: prefs.fps || 12, autoKey: true,
        view: Object.assign({ lit: true, cull: true, skin: true, onion: false, bones: true, bg: 0x1A1D28 }, prefs.view || {}),
        spr: Object.assign({}, BM.sprites.DEFAULTS, { dirs: 4, bands: 3, colours: 16, w: 48, h: 48, frames: 8, fps: 12 }, prefs.spr || {}),
      };
      this.project = exampleProject();
      this.cur = 0; this.bone = -1; this.clip = 0; this.time = 0; this.playing = false;
      this.pose = null;
      this.selFaces = new Set();
      this.undoStack = []; this.redoStack = [];
      this.dirty = false; this.handle = null; this.fileName = null;
      this.poseClip = null;
      this.needRender = true;
      this.view = new AnimView(this, $('#glc'));
      this.timeline = new Timeline(this, $('#tlc'));
      this.wire();
      this.setMode('rig');
      this.refreshAll();
      this.view.frame();
      let last = performance.now();
      const loop = now => {
        const dt = Math.min(0.1, (now - last) / 1000);
        last = now;
        if (this.playing) this.advance(dt);
        if (this.S.mode === 'sprites') this.spritesTick(dt);
        if (this.needRender) { this.needRender = false; this.render(); }
        requestAnimationFrame(loop);
      };
      requestAnimationFrame(loop);
      window.addEventListener('beforeunload', e => { if (this.dirty && !this.leaving) { e.preventDefault(); e.returnValue = ''; } });
      BM.handoff.take().then(async h => {
        if (!h || !h.bytes) return;
        try {
          await this.openBytes(new Uint8Array(h.bytes), h.name || 'cartridge.bm', h.handle || null, true);
          if (h.dirty) this.markDirty();
          this.status('from bm Studio: ' + (h.name || ''), 'good');
        } catch (e) { this.error(e); }
      });
    }

    // ------------------------------------------------------------ access

    model() { return this.project.models[this.cur]; }
    rig() { const m = this.model(); return m && m.rig && m.rig.bones.length ? m.rig : null; }
    clipObj() { const r = this.rig(); return r && r.clips[this.clip] ? r.clips[this.clip] : null; }
    requestRender() { this.needRender = true; }

    /* the pose on screen in Animate: the clip at this time, or the one being edited */
    currentPose() {
      const rig = this.rig();
      if (!rig) return [];
      if (this.pose && this.pose.length === rig.bones.length) return this.pose;
      return R.samplePose(rig, this.clipObj(), this.time);
    }

    /* the pose that the tools change (a copy of the one on screen) */
    editPose() {
      if (!this.pose || this.pose.length !== this.rig().bones.length) this.pose = R.clonePose(this.currentPose());
      return this.pose;
    }

    render() {
      if (this.S.mode === 'sprites') { this.renderSprites(); return; }
      this.view.render();
      if (this.S.mode === 'anim') this.timeline.render();
    }

    // ------------------------------------------------------------ undo

    snapOf(m) { return JSON.stringify({ rig: m.rig || null, b: m.faces.map(f => f.b || null) }); }

    restore(m, s) {
      const o = JSON.parse(s);
      if (o.rig) m.rig = o.rig; else delete m.rig;
      m.faces.forEach((f, i) => { if (o.b[i]) f.b = o.b[i]; else delete f.b; });
    }

    beginEdit() { this.before = { m: this.model(), s: this.snapOf(this.model()) }; }
    cancelEdit() { const b = this.before; this.before = null; if (b && this.snapOf(b.m) !== b.s) { this.restore(b.m, b.s); this.afterRestore(); } }

    commitEdit(label) {
      const b = this.before;
      this.before = null;
      if (!b) return;
      const after = this.snapOf(b.m);
      if (after === b.s) return;
      const set = s => () => { this.restore(b.m, s); const i = this.project.models.indexOf(b.m); if (i >= 0) this.cur = i; this.afterRestore(); };
      this.push({ label, undo: set(b.s), redo: set(after) });
      this.refreshAll();
    }

    edit(label, fn) { this.beginEdit(); fn(this.model()); this.commitEdit(label); }

    push(e) {
      this.undoStack.push(e);
      if (this.undoStack.length > 300) this.undoStack.shift();
      this.redoStack = [];
      this.markDirty();
    }

    undo() { const e = this.undoStack.pop(); if (!e) { this.status('nothing to undo'); return; } e.undo(); this.redoStack.push(e); this.markDirty(); this.status('undo: ' + e.label); }
    redo() { const e = this.redoStack.pop(); if (!e) { this.status('nothing to redo'); return; } e.redo(); this.undoStack.push(e); this.markDirty(); this.status('redo: ' + e.label); }

    afterRestore() {
      const rig = this.rig();
      if (!rig) { this.bone = -1; this.clip = 0; } else {
        if (this.bone >= rig.bones.length) this.bone = rig.bones.length - 1;
        if (this.clip >= rig.clips.length) this.clip = Math.max(0, rig.clips.length - 1);
      }
      this.pose = null;
      this.refreshAll();
    }

    markDirty(on = true) { this.dirty = on; $('#dirty').classList.toggle('on', on); }

    // ------------------------------------------------------------ changes

    rigChanged(live) { this.pose = null; this.markDirty(); this.requestRender(); if (!live) this.refreshAll(); }

    poseChanged() { this.markDirty(); this.requestRender(); this.refreshBoneProps(); }

    /* the end of a change of the pose: a keyframe at this time (auto key) */
    poseDone(label) {
      if (this.S.autoKey) this.keyHere(true);
      this.commitEdit(label);
      this.refreshBoneProps();
    }

    keyHere(inEdit) {
      const rig = this.rig();
      if (!rig) return;
      if (!inEdit) this.beginEdit();
      let clip = this.clipObj();
      if (!clip) {
        clip = { name: this.uniqueClip('anim'), length: 1, loop: true, mode: 'smooth', keys: [] };
        rig.clips.push(clip);
        this.clip = rig.clips.length - 1;
      }
      R.setKey(clip, Math.min(this.time, clip.length), this.currentPose());
      this.pose = null;
      if (!inEdit) this.commitEdit('key');
      this.refreshClips();
      this.requestRender();
    }

    setTime(t) {
      const clip = this.clipObj();
      this.time = Math.max(0, clip ? Math.min(clip.length, t) : t);
      this.pose = null;
      $('#tlTime').textContent = this.time.toFixed(2) + ' s';
      this.refreshBoneProps();
      this.updateStatus();
      this.requestRender();
    }

    advance(dt) {
      const clip = this.clipObj();
      if (!clip) { this.stop(); return; }
      let t = this.time + dt;
      if (t > clip.length) { if (clip.loop) t %= clip.length; else { t = clip.length; this.stop(); } }
      this.time = t;
      this.pose = null;
      $('#tlTime').textContent = t.toFixed(2) + ' s';
      this.requestRender();
    }

    play() {
      if (!this.clipObj()) return;
      this.playing = !this.playing;
      $('#playBtn').textContent = this.playing ? '⏸' : '▶';
      if (this.playing && this.time >= this.clipObj().length - 1e-4 && !this.clipObj().loop) this.time = 0;
    }

    stop() { if (this.playing) { this.playing = false; $('#playBtn').textContent = '▶'; this.setTime(Math.round(this.time * this.S.fps) / this.S.fps); } }

    selectBone(i) {
      this.bone = i;
      this.refreshBones();
      this.requestRender();
      this.updateStatus();
    }

    setMode(mode) {
      this.stop();
      if (mode === 'anim' && !this.rig()) this.status('no bones yet: make them in Rig first', 'warn');
      this.S.mode = mode;
      this.pose = null;
      $$('[data-mode]').forEach(b => b.classList.toggle('on', b.dataset.mode === mode));
      $('#ws-view').classList.toggle('on', mode !== 'sprites');
      $('#ws-sprites').classList.toggle('on', mode === 'sprites');
      $('#rigtools').hidden = mode !== 'rig';
      $('#animtools').hidden = mode !== 'anim';
      $('#timeline').hidden = mode !== 'anim';
      $('#clipSect').hidden = mode === 'rig';
      $('#hint').textContent = mode === 'rig' ? HINTS[this.S.rtool] : HINTS.anim;
      if (mode === 'sprites') this.refreshSprites();
      else { this.view.resize(); if (mode === 'anim') this.timeline.resize(); }
      this.refreshAll();
    }

    setRigTool(t) {
      this.S.rtool = t;
      $$('[data-rtool]').forEach(b => b.classList.toggle('on', b.dataset.rtool === t));
      $('#hint').textContent = HINTS[t];
      if (t !== 'skin') this.selFaces.clear();
      this.requestRender();
    }

    // ------------------------------------------------------------ bones

    uniqueBone(base) {
      const names = new Set((this.rig() || { bones: [] }).bones.map(b => b.name));
      base = (base || 'bone').slice(0, 15);
      if (!names.has(base)) return base;
      const stem = base.replace(/\d+$/, '').slice(0, 12) || 'b';
      for (let i = 2; ; i++) if (!names.has(stem + i)) return stem + i;
    }

    uniqueClip(base) {
      const names = new Set((this.rig() || { clips: [] }).clips.map(c => c.name));
      base = (base || 'anim').slice(0, 15);
      if (!names.has(base)) return base;
      const stem = base.replace(/\d+$/, '').slice(0, 12) || 'a';
      for (let i = 2; ; i++) if (!names.has(stem + i)) return stem + i;
    }

    addBone() {
      const m = this.model();
      this.edit('new bone', () => {
        if (!m.rig) m.rig = { bones: [], clips: [] };
        const rig = m.rig, b = BM.modelBounds(m.faces) || { lo: [0, 0, 0], hi: [0, 1, 0] };
        const c = [(b.lo[0] + b.hi[0]) / 2, b.lo[1], (b.lo[2] + b.hi[2]) / 2].map(snap), h = b.hi[1] - b.lo[1];
        let bone;
        if (!rig.bones.length) bone = { name: 'root', parent: -1, head: c, tail: [c[0], snap(c[1] + h / 2), c[2]] };
        else {
          const p = this.bone >= 0 ? this.bone : 0, pb = rig.bones[p], dir = v3.sub(pb.tail, pb.head);
          const len = Math.max(0.25, v3.len(dir) * 0.8), n = v3.len(dir) > 1e-6 ? v3.norm(dir) : [0, 1, 0];
          bone = { name: this.uniqueBone('bone'), parent: p, head: pb.tail.slice(), tail: v3.add(pb.tail, v3.scale(n, len)).map(snap) };
        }
        rig.bones.push(bone);
        for (const clip of rig.clips) for (const k of clip.keys) k.pose.push({ q: [0, 0, 0, 1], t: [0, 0, 0] });
        this.bone = rig.bones.length - 1;
      });
      this.status('a new bone: drag its joints to place it, rename it on the right');
    }

    deleteBone() {
      const rig = this.rig(), i = this.bone;
      if (!rig || i < 0) return;
      this.edit('delete bone', m => {
        const parent = rig.bones[i].parent;
        rig.bones.forEach(b => { if (b.parent === i) b.parent = parent; });
        rig.bones.splice(i, 1);
        rig.bones.forEach(b => { if (b.parent > i) b.parent--; });
        for (const f of m.faces) if (f.b) f.b = f.b.map(x => x === i ? Math.max(0, parent) : x > i ? x - 1 : x);
        for (const clip of rig.clips) for (const k of clip.keys) k.pose.splice(i, 1);
        if (!rig.bones.length) { delete m.rig; m.faces.forEach(f => delete f.b); }
        this.bone = -1;
      });
    }

    /* the chosen bone and its children, mirrored left <-> right (x) */
    mirrorBones() {
      const rig = this.rig(), i = this.bone;
      if (!rig || i < 0) { this.status('choose a bone first'); return; }
      const chain = [i];
      rig.bones.forEach((b, k) => { if (chain.includes(b.parent)) chain.push(k); });
      this.edit('mirror bones', () => {
        const map = new Map();
        for (const k of chain) {
          const b = rig.bones[k];
          let name = R.mirrorName(b.name);
          if (!name) {                                   // no side yet: .L on +x (the left of a model that looks at you)
            const left = (b.head[0] + b.tail[0]) / 2 >= 0;
            b.name = this.uniqueBone(b.name.slice(0, 13) + (left ? '.L' : '.R'));
            name = R.mirrorName(b.name);
          }
          const copy = { name: this.uniqueBone(name), parent: map.has(b.parent) ? map.get(b.parent) : b.parent,
            head: [-b.head[0], b.head[1], b.head[2]], tail: [-b.tail[0], b.tail[1], b.tail[2]] };
          rig.bones.push(copy);
          map.set(k, rig.bones.length - 1);
          for (const clip of rig.clips) for (const key of clip.keys) {
            const p = key.pose[k] || { q: [0, 0, 0, 1], t: [0, 0, 0] };
            key.pose.push({ q: [p.q[0], -p.q[1], -p.q[2], p.q[3]], t: [-p.t[0], p.t[1], p.t[2]] });
          }
        }
      });
      this.status('mirrored: the faces on the other side follow them after Auto or Assign');
    }

    assign() {
      if (this.bone < 0 || !this.selFaces.size) { this.status('choose a bone and some faces (Skin)'); return; }
      const b = this.bone;
      this.edit('assign', () => { for (const f of this.selFaces) f.b = f.p.map(() => b); });
      this.status(`${this.selFaces.size} faces follow "${this.rig().bones[b].name}"`);
    }

    autoSkin(byCorner) {
      if (!this.rig()) { this.status('make the bones first'); return; }
      const faces = this.selFaces.size ? [...this.selFaces] : null;
      this.edit(byCorner ? 'auto skin (smooth)' : 'auto skin', m => R.autoSkin(m, faces, byCorner));
      this.status(byCorner ? 'every corner follows the nearest bone' : 'every face follows the nearest bone');
    }

    // ------------------------------------------------------------ poses

    resetBone() {
      if (this.bone < 0 || !this.rig()) return;
      this.beginEdit();
      const p = this.editPose()[this.bone];
      p.q = [0, 0, 0, 1]; p.t = [0, 0, 0];
      this.poseChanged();
      this.poseDone('rest bone');
    }

    resetPose() {
      if (!this.rig()) return;
      this.beginEdit();
      this.pose = R.restPose(this.rig().bones.length);
      this.poseChanged();
      this.poseDone('rest pose');
    }

    mirrorPose() {
      const rig = this.rig();
      if (!rig) return;
      this.beginEdit();
      const src = R.clonePose(this.currentPose()), out = R.clonePose(src);
      rig.bones.forEach((b, i) => {
        const other = R.mirrorName(b.name), j = other ? rig.bones.findIndex(x => x.name === other) : i;
        const p = src[j >= 0 ? j : i];
        out[i] = { q: [p.q[0], -p.q[1], -p.q[2], p.q[3]], t: [-p.t[0], p.t[1], p.t[2]] };
      });
      this.pose = out;
      this.poseChanged();
      this.poseDone('mirror pose');
    }

    copyPose() { if (this.rig()) { this.clipboard = R.clonePose(this.currentPose()); this.status('pose copied'); } }

    pastePose() {
      const rig = this.rig();
      if (!rig || !this.clipboard || this.clipboard.length !== rig.bones.length) return;
      this.beginEdit();
      this.pose = R.clonePose(this.clipboard);
      this.poseChanged();
      this.poseDone('paste pose');
    }

    // ------------------------------------------------------------ commands

    cmd(name) {
      const f = this.commands[name];
      if (!f) return;
      Promise.resolve(f()).catch(e => this.error(e));
    }

    get commands() {
      const A = this;
      return {
        open: () => A.openFile(), save: () => A.save(false), saveAs: () => A.save(true),
        exportGlb: () => A.exportGlb(), exportSprites: () => A.exportSprites(), toStudio: () => A.toStudio(),
        undo: () => A.undo(), redo: () => A.redo(),
        copyPose: () => A.copyPose(), pastePose: () => A.pastePose(), mirrorPose: () => A.mirrorPose(), resetPose: () => A.resetPose(),
        toggleLit: () => A.toggleView('lit'), toggleCull: () => A.toggleView('cull'), toggleSkin: () => A.toggleView('skin'),
        toggleOnion: () => A.toggleView('onion'), toggleBones: () => A.toggleView('bones'),
        frame: () => A.view.frame(), resetCam: () => { A.view.cam.reset(); A.view.frame(); },
        help: () => A.helpDialog(), about: () => A.aboutDialog(),
        addBone: () => A.addBone(), mirrorBones: () => A.mirrorBones(), deleteBone: () => A.deleteBone(),
        assign: () => A.assign(), autoFace: () => A.autoSkin(false), autoCorner: () => A.autoSkin(true),
        resetBone: () => A.resetBone(),
        first: () => { A.stop(); A.setTime(0); },
        play: () => A.play(),
        prevKey: () => { const c = A.clipObj(); if (!c) return; A.stop(); const k = [...c.keys].reverse().find(x => x.t < A.time - 1e-4); A.setTime(k ? k.t : 0); },
        nextKey: () => { const c = A.clipObj(); if (!c) return; A.stop(); const k = c.keys.find(x => x.t > A.time + 1e-4); A.setTime(k ? k.t : c.length); },
        key: () => { A.stop(); A.keyHere(false); A.status('keyframe at ' + A.time.toFixed(2) + ' s'); },
        deleteKey: () => {
          const c = A.clipObj();
          if (!c) return;
          const i = R.keyAt(c, A.time);
          if (i < 0) { A.status('no keyframe here'); return; }
          if (c.keys.length === 1) { A.status('the last keyframe stays (delete the animation instead)'); return; }
          A.edit('delete key', () => c.keys.splice(i, 1));
          A.pose = null;
        },
        newClip: async () => {
          if (!A.rig()) { A.status('make the bones first (Rig)'); return; }
          const name = await A.askName('New animation', A.uniqueClip('anim'), A.rig().clips.map(c => c.name));
          if (!name) return;
          A.edit('new animation', () => {
            A.rig().clips.push({ name, length: 1, loop: true, mode: 'smooth', keys: [{ t: 0, pose: R.restPose(A.rig().bones.length) }] });
            A.clip = A.rig().clips.length - 1;
          });
          A.setTime(0);
        },
        dupClip: () => {
          const c = A.clipObj();
          if (!c) return;
          A.edit('duplicate animation', () => {
            const copy = JSON.parse(JSON.stringify(c));
            copy.name = A.uniqueClip(c.name);
            A.rig().clips.splice(A.clip + 1, 0, copy);
            A.clip++;
          });
        },
        renameClip: async () => {
          const c = A.clipObj();
          if (!c) return;
          const name = await A.askName('Rename the animation', c.name, A.rig().clips.filter(x => x !== c).map(x => x.name));
          if (name && name !== c.name) A.edit('rename animation', () => { c.name = name; });
        },
        deleteClip: async () => {
          const c = A.clipObj();
          if (!c || !(await A.confirm(`Delete the animation "${c.name}"?`, 'Delete'))) return;
          A.edit('delete animation', () => { A.rig().clips.splice(A.clip, 1); A.clip = Math.max(0, A.clip - 1); });
          A.setTime(0);
        },
        spritesToSheet: () => A.spritesToSheet(), spritesLua: () => A.spritesLua(),
      };
    }

    toggleView(k) { this.S.view[k] = !this.S.view[k]; savePrefs(this.S); this.refreshChecks(); this.requestRender(); }

    refreshChecks() {
      $$('[data-check]').forEach(b => {
        const on = !!this.S.view[b.dataset.check];
        b.classList.toggle('checked', on);
        if (b.classList.contains('tog')) b.classList.toggle('on', on);
      });
    }

    // ------------------------------------------------------------ files

    async confirmLose() { return !this.dirty || this.confirm('The project has changes that are not saved. Lose them?', 'Lose them'); }

    loadProject(p, handle, name, warnings) {
      this.project = p;
      this.cur = Math.max(0, p.models.findIndex(m => m.rig && m.rig.bones.length));
      this.bone = -1; this.clip = 0; this.time = 0; this.pose = null;
      this.handle = handle; this.fileName = name;
      this.undoStack = []; this.redoStack = [];
      this.selFaces.clear();
      this.stop();
      this.refreshAll();
      this.view.frame();
      this.markDirty(false);
      if (!p.models.length) this.alert('No 3D models', 'This cartridge has no 3D models yet: make them in bm Studio, then animate them here.');
      if (warnings && warnings.length) this.alert('Opened with warnings', warnings.map(esc).join('<br>'));
    }

    async openFile() {
      if (window.showOpenFilePicker) {
        let h;
        try { [h] = await window.showOpenFilePicker({ types: [{ description: 'bm cartridge', accept: { 'application/octet-stream': ['.bm'] } }] }); } catch (e) { return; }
        const file = await h.getFile();
        await this.openBytes(new Uint8Array(await file.arrayBuffer()), file.name, h);
        return;
      }
      this.pick('.bm', (b, name) => this.openBytes(b, name, null));
    }

    async openBytes(b, name, handle, fromStudio) {
      if (!fromStudio && !(await this.confirmLose())) return;
      if (b.length < 8 || String.fromCharCode(...b.subarray(0, 6)) !== 'BMCART') {
        throw new Error(`${name}: not a .bm cartridge (models and pictures go through bm Studio)`);
      }
      const { project, warnings } = BM.parseCart(b);
      this.loadProject(project, handle, name, warnings);
      this.status(`opened ${name}: ${project.models.length} models`);
    }

    async save(as) {
      const problems = BM.checkProject(this.project);
      if (problems.length) { await this.alert('The cartridge cannot be saved like this', problems.map(esc).join('<br>')); return; }
      const bytes = BM.buildCart(this.project), suggested = this.fileName || slug(this.project.title) + '.bm';
      if (!as && this.handle) {
        try {
          const w = await this.handle.createWritable();
          await w.write(bytes); await w.close();
          this.saved(this.handle.name, bytes);
          return;
        } catch (e) { if (e.name === 'AbortError') return; this.status('cannot write the file there: ' + e.message, 'bad'); }
      }
      if (window.showSaveFilePicker) {
        try {
          const h = await window.showSaveFilePicker({ suggestedName: suggested, types: [{ description: 'bm cartridge', accept: { 'application/octet-stream': ['.bm'] } }] });
          const w = await h.createWritable();
          await w.write(bytes); await w.close();
          this.handle = h;
          this.saved(h.name, bytes);
        } catch (e) { if (e.name !== 'AbortError') throw e; }
        return;
      }
      this.download(bytes, suggested);
      this.saved(suggested, bytes);
    }

    saved(name, bytes) {
      this.fileName = name;
      this.markDirty(false);
      this.refreshDoc();
      this.status(`saved ${name} (${(bytes.length / 1024).toFixed(1)} KiB): copy it to carts/ on the SD card`, 'good');
    }

    download(bytes, name) {
      const url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' })), a = document.createElement('a');
      a.href = url; a.download = name;
      document.body.appendChild(a); a.click(); a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 5000);
    }

    pick(accept, fn) {
      const inp = $('#fileInput');
      inp.accept = accept; inp.value = '';
      inp.onchange = async () => {
        const file = inp.files[0];
        if (!file) return;
        try { await fn(new Uint8Array(await file.arrayBuffer()), file.name); } catch (e) { this.error(e); }
      };
      inp.click();
    }

    async toStudio() {
      const problems = BM.checkProject(this.project);
      if (problems.length) { await this.alert('First fix this', problems.map(esc).join('<br>')); return; }
      this.leaving = true;
      await BM.handoff.go('../studio/index.html', { bytes: BM.buildCart(this.project), name: this.fileName || slug(this.project.title) + '.bm', handle: this.handle, dirty: this.dirty });
    }

    async exportGlb() {
      const m = this.model();
      if (!BM.exportAnimatedGLB) { this.status('not available'); return; }
      const bytes = await BM.exportAnimatedGLB(this.project, m);
      this.download(bytes, slug(m.name) + '.glb');
    }

    // ------------------------------------------------------------ sprites

    sprClip() {
      const m = this.project.models[this.S.spr.model | 0] || this.model();
      return { m, clip: m.rig && m.rig.clips[this.S.spr.clip | 0] || null };
    }

    refreshSprites() {
      const S = this.S.spr, models = this.project.models;
      if (!(S.model >= 0 && S.model < models.length)) S.model = this.cur;
      $('#sprModel').innerHTML = models.map((m, i) => `<option value="${i}" ${i === S.model ? 'selected' : ''}>${esc(m.name)}</option>`).join('');
      const m = models[S.model], clips = m && m.rig ? m.rig.clips : [];
      if (!(S.clip >= 0 && S.clip < clips.length)) S.clip = 0;
      $('#sprClip').innerHTML = clips.length ? clips.map((c, i) => `<option value="${i}" ${i === S.clip ? 'selected' : ''}>${esc(c.name)} (${c.length} s)</option>`).join('')
        : '<option>(no animation: one still frame)</option>';
      for (const [id, k] of [['#sprFrames', 'frames'], ['#sprFps', 'fps'], ['#sprW', 'w'], ['#sprH', 'h'], ['#sprPitch', 'pitch'], ['#sprScale', 'scale'], ['#sprAmb', 'ambient']])
        $(id).value = S[k];
      $('#sprDirs').value = S.dirs; $('#sprProj').value = S.projection; $('#sprLit').checked = S.lit;
      $('#sprBands').value = S.bands; $('#sprOutline').checked = S.outline != null; $('#sprCols').value = S.colours;
      $('#sprSmooth').checked = S.supersample > 1;
      if (S.outline != null) $('#sprOutCol').value = hex6(S.outline);
      this.sprDirty = true;
      this.requestRender();
    }

    readSprites() {
      const S = this.S.spr, num = (id, lo, hi) => Math.max(lo, Math.min(hi, +$(id).value || 0));
      S.model = +$('#sprModel').value || 0;
      S.clip = +$('#sprClip').value || 0;
      S.frames = num('#sprFrames', 1, 64); S.fps = num('#sprFps', 1, 60);
      S.w = num('#sprW', 8, 256); S.h = num('#sprH', 8, 256);
      S.dirs = +$('#sprDirs').value; S.pitch = num('#sprPitch', -30, 90); S.projection = $('#sprProj').value;
      S.scale = num('#sprScale', 0, 1000); S.lit = $('#sprLit').checked; S.ambient = num('#sprAmb', 0, 1);
      S.bands = +$('#sprBands').value; S.outline = $('#sprOutline').checked ? parseInt($('#sprOutCol').value.slice(1), 16) : null;
      S.colours = +$('#sprCols').value; S.supersample = $('#sprSmooth').checked ? 2 : 1;
      savePrefs(this.S);
      this.sprDirty = true;
      this.requestRender();
    }

    makeSprites() {
      const { m, clip } = this.sprClip();
      if (!m || !m.faces.length) return null;
      return BM.sprites.render(m, this.project.sheet, clip, this.S.spr);
    }

    renderSprites() {
      if (this.sprDirty || !this.spr) {
        this.sprDirty = false;
        this.spr = this.makeSprites();
        this.sprFrame = 0; this.sprT = 0;
        const g = $('#sprGrid');
        if (!this.spr) { g.width = g.height = 1; $('#sprInfo').textContent = 'no model'; return; }
        const r = this.spr, k = Math.max(1, Math.min(4, Math.floor(Math.min(900 / r.img.w, 520 / r.img.h))));
        g.width = r.img.w; g.height = r.img.h;
        g.style.width = r.img.w * k + 'px'; g.style.height = r.img.h * k + 'px';
        g.getContext('2d').putImageData(new ImageData(r.img.px, r.img.w, r.img.h), 0, 0);
        g.onclick = e => { const b = g.getBoundingClientRect(); this.sprDir = Math.floor((e.clientY - b.top) / b.height * r.rows); };
        $('#sprInfo').textContent = `${r.cols} frames × ${r.rows} directions of ${r.w}×${r.h} = ${r.img.w}×${r.img.h} pixels · ` +
          `${r.scale.toFixed(1)} px per unit · origin at ${r.anchor.join(', ')} · click a row to see that direction`;
      }
      const r = this.spr;
      if (!r) return;
      const a = $('#sprAnim'), k = Math.max(2, Math.min(6, Math.floor(220 / Math.max(r.w, r.h))));
      a.width = r.w; a.height = r.h;
      a.style.width = r.w * k + 'px'; a.style.height = r.h * k + 'px';
      const d = Math.min(this.sprDir || 0, r.rows - 1), f = this.sprFrame % r.cols, ctx = a.getContext('2d');
      const one = new ImageData(r.w, r.h);
      for (let y = 0; y < r.h; y++) one.data.set(r.img.px.subarray(((d * r.h + y) * r.img.w + f * r.w) * 4, ((d * r.h + y) * r.img.w + (f + 1) * r.w) * 4), y * r.w * 4);
      ctx.clearRect(0, 0, r.w, r.h);
      ctx.putImageData(one, 0, 0);
    }

    spritesTick(dt) {
      if (!this.spr) return;
      this.sprT = (this.sprT || 0) + dt;
      const step = 1 / (this.S.spr.fps || 12);
      if (this.sprT >= step) { this.sprT %= step; this.sprFrame = (this.sprFrame + 1) % this.spr.cols; this.requestRender(); }
    }

    spriteName() {
      const { m, clip } = this.sprClip();
      return (m ? m.name : 'model') + (clip ? '_' + clip.name : '');
    }

    async spritesToSheet() {
      const r = this.makeSprites();
      if (!r) return;
      const placed = BM.placeInSheet(this.project.sheet, r.img, 8);
      if (!placed) { await this.alert('No room', 'The sprites do not fit in the sheet (at most 4096×4096): make them smaller or fewer.'); return; }
      const before = this.project.sheet, after = placed.sheet;
      const set = s => () => { this.project.sheet = s; this.markDirty(); };
      set(after)();
      this.push({ label: 'sprites in the sheet', undo: set(before), redo: set(after) });
      this.lastSprites = { name: this.spriteName(), res: r, at: placed.at };
      this.status(`sprites in the sheet at ${placed.at.join(', ')} (${r.img.w}×${r.img.h}); the sheet is ${after.w}×${after.h}`, 'good');
      await this.spritesLua();
    }

    async spritesLua() {
      const L = this.lastSprites, r = L ? L.res : this.makeSprites();
      if (!r) return;
      const code = BM.sprites.snippet(L ? L.name : this.spriteName(), r, L ? L.at : ['X', 'Y'], this.S.spr.fps);
      const res = await this.dialog('Lua code for the sprites', `<p class="dim small">${L ? 'Where they are in the sheet and how to draw them:' :
        'Put them in the sheet first: X and Y become their place.'}</p><pre id="luaCode">${esc(code)}</pre>`,
      [['copy', 'Copy'], ['ok', 'OK', true]]);
      if (res.button === 'copy') { try { await navigator.clipboard.writeText(code); this.status('copied'); } catch (e) { this.status('select the text and copy it'); } }
    }

    async exportSprites() {
      const r = this.makeSprites();
      if (r) this.download(await BM.encodePNG(r.img), slug(this.spriteName()) + '-sprites.png');
    }

    // ------------------------------------------------------------ dialogs

    dialog(title, html, buttons, read) {
      return new Promise(resolve => {
        $('#modalTitle').textContent = title;
        $('#modalBody').innerHTML = html;
        const bar = $('#modalButtons');
        bar.innerHTML = '';
        const close = button => {
          const value = read && button !== 'cancel' ? read() : undefined;
          $('#modal').hidden = true;
          document.removeEventListener('keydown', key, true);
          resolve({ button, value });
        };
        const key = e => {
          if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); close('cancel'); }
          if (e.key === 'Enter') { const prim = buttons.find(b => b[2]); if (prim) { e.preventDefault(); e.stopPropagation(); close(prim[0]); } }
        };
        for (const [id, label, primary] of buttons) {
          const b = document.createElement('button');
          b.textContent = label;
          if (primary) b.className = 'primary';
          b.onclick = () => close(id);
          bar.appendChild(b);
        }
        document.addEventListener('keydown', key, true);
        $('#modal').hidden = false;
        const first = $('#modalBody input') || bar.querySelector('.primary');
        if (first) { first.focus(); if (first.select) first.select(); }
      });
    }

    async alert(title, html) { await this.dialog(title, `<div>${html}</div>`, [['ok', 'OK', true]]); }
    async confirm(text, yes = 'OK', no = 'Cancel') { return (await this.dialog('bm Animator', `<p>${esc(text)}</p>`, [['cancel', no], ['ok', yes, true]])).button === 'ok'; }

    async askName(title, value, taken) {
      for (;;) {
        const r = await this.dialog(title, `<label class="field">name <input id="nm" maxlength="15" value="${esc(value)}" spellcheck="false"></label>`,
          [['cancel', 'Cancel'], ['ok', 'OK', true]], () => $('#nm').value.trim());
        if (r.button !== 'ok' || !r.value) return null;
        if (BM.utf8(r.value).length > 15) { await this.alert('Name too long', 'At most 15 bytes.'); value = r.value; continue; }
        if (taken.includes(r.value)) { await this.alert('Name taken', `"${esc(r.value)}" is already there.`); value = r.value; continue; }
        return r.value;
      }
    }

    helpDialog() {
      const rows = [
        ['1 2 3, Tab', 'Rig, Animate, Sprites'], ['Right drag · Shift+right · wheel', 'turn, move, zoom the camera'], ['Z', 'frame the model'],
        ['Rig — click', 'choose a bone; drag a joint to move it (joints in one place move together; Shift: only this bone; Alt: off the grid)'],
        ['Rig — N · Del', 'a new bone (child of the chosen one) · delete the chosen bone'],
        ['Rig — Skin: click, Shift, box · A', 'choose faces · they follow the chosen bone'], ['Rig — Alt+click (Skin)', 'the bone of a face'],
        ['Animate — rings', 'drag to turn the chosen bone around x (red), y (green), z (blue); Shift: 15° steps'],
        ['Animate — tail · head', 'drag the tail to aim the bone · the head to move it'],
        ['Space · ← → · Shift+← →', 'play / stop · a frame back, forward · previous, next keyframe'],
        ['K · Del', 'keyframe here · delete the keyframe here'], ['R · M', 'the bone at rest · mirror the pose (left ↔ right)'],
        ['Ctrl+C Ctrl+V', 'copy, paste the pose'], ['Ctrl+Z Ctrl+Y', 'undo, redo'], ['L C O B', 'light · bone colours · onion skin · bones'],
        ['Ctrl+S · Ctrl+O', 'save · open'],
      ];
      this.alert('Keys and mouse', '<table>' + rows.map(r => `<tr><td>${esc(r[0])}</td><td>${esc(r[1])}</td></tr>`).join('') + '</table>');
    }

    aboutDialog() {
      this.alert('Using the animations in a game', `
        <p>The skeletons and animations are saved in the <b>.bm</b> (section ANIM), with the models. In the game:</p>
        <pre>local hero, t = nil, 0
function _init()
  hero = model("villager")            -- with its skeleton
end
function _update() t = t + 1 / 60 end
function _draw()
  cls(0x1c2030)
  zclear()
  camera3d(0, 2, -6, 0, -0.25)
  animate(hero, "walk", t)            -- the pose of "walk" at time t
  draw3d(hero, 0, 0, 0)
end</pre>
        <p><code>animate(m, "walk", t, "idle", t, k)</code> mixes two animations (k from 0 to 1);
        <code>animate(m)</code> is the rest pose; <code>clips(m)</code> lists the animations;
        <code>bone3d(m, "arm.R")</code> gives where a bone is (to hold a sword, a lamp...).</p>
        <p><b>Sprites</b>: the Sprites page draws an animation from 1 to 8 directions and puts the frames in the
        sprite sheet; its Lua code draws them with <code>sspr()</code>.</p>`);
    }

    error(e) { console.error(e); this.alert('Something went wrong', esc(e && e.message ? e.message : String(e))); }

    // ------------------------------------------------------------ panels

    refreshAll() {
      this.refreshModels();
      this.refreshBones();
      this.refreshClips();
      this.refreshChecks();
      this.refreshDoc();
      if (this.S.mode === 'sprites') this.refreshSprites();
      this.updateStatus();
      this.requestRender();
    }

    refreshDoc() {
      $('#docName').textContent = this.fileName || (this.project.title ? slug(this.project.title) + '.bm' : 'no file');
      document.title = (this.fileName || this.project.title || 'bm') + ' — bm Animator';
    }

    refreshModels() {
      $('#modelSel').innerHTML = this.project.models.map((m, i) =>
        `<option value="${i}" ${i === this.cur ? 'selected' : ''}>${esc(m.name)}${m.rig && m.rig.bones.length ? ` (${m.rig.bones.length} bones)` : ''}</option>`).join('');
      const e = $('#empty');
      if (!this.project.models.length) { e.textContent = 'No 3D models in this cartridge: make them in bm Studio.'; e.classList.add('on'); } else if (!this.rig() && this.S.mode !== 'sprites') {
        e.innerHTML = '<b>No bones yet.</b> ＋ Bone makes the first one; then drag its joints, add more, and Auto gives the faces to them.';
        e.classList.add('on');
      } else e.classList.remove('on');
    }

    refreshBones() {
      const rig = this.rig(), ul = $('#boneList');
      ul.innerHTML = '';
      if (rig) {
        const depth = i => { let d = 0; for (let p = rig.bones[i].parent; p >= 0; p = rig.bones[p].parent) d++; return d; };
        const order = [];
        const visit = p => rig.bones.forEach((b, i) => { if (b.parent === p) { order.push(i); visit(i); } });
        visit(-1);
        for (const i of order) {
          const b = rig.bones[i], li = document.createElement('li');
          li.className = i === this.bone ? 'on' : '';
          li.style.paddingLeft = 8 + depth(i) * 14 + 'px';
          const n = this.model().faces.filter(f => f.b && majority(f.b) === i || (!f.b && i === 0)).length;
          li.innerHTML = `<span class="sw" style="background:${hex6(R.boneColour(i))}"></span><span class="n">${esc(b.name)}</span><span class="i">${n} faces</span>`;
          li.onclick = () => this.selectBone(i);
          ul.appendChild(li);
        }
      }
      this.refreshBoneProps();
    }

    refreshBoneProps() {
      const el = $('#boneProps'), rig = this.rig(), i = this.bone;
      if (!rig || i < 0 || !rig.bones[i]) { el.innerHTML = '<span class="dim small">' + (rig ? 'Choose a bone.' : '') + '</span>'; return; }
      const b = rig.bones[i], f3 = v => +v.toFixed(3);
      const xyz = (id, v, step, f = f3) => `<span class="xyz">${[0, 1, 2].map(k => `<input type="number" step="${step}" data-v="${id}" data-k="${k}" value="${f(v[k])}">`).join('')}</span>`;
      if (this.S.mode === 'rig') {
        const parents = ['<option value="-1">(none)</option>'].concat(rig.bones.slice(0, i).map((p, k) =>
          `<option value="${k}" ${k === b.parent ? 'selected' : ''}>${esc(p.name)}</option>`));
        el.innerHTML = `<span>name</span><input id="bpName" maxlength="15" value="${esc(b.name)}" spellcheck="false">
          <span>parent</span><select id="bpParent">${parents.join('')}</select>
          <span>head</span>${xyz('head', b.head, 0.03125)}<span>tail</span>${xyz('tail', b.tail, 0.03125)}`;
        $('#bpName').onchange = e => {
          const v = e.target.value.trim();
          if (!v || BM.utf8(v).length > 15 || rig.bones.some((x, k) => k !== i && x.name === v)) { e.target.value = b.name; this.status('bone names: 1 to 15 letters, all different'); return; }
          this.edit('rename bone', () => { b.name = v; });
        };
        $('#bpParent').onchange = e => this.edit('parent', () => { b.parent = +e.target.value; });
      } else {
        const p = this.currentPose()[i] || { q: [0, 0, 0, 1], t: [0, 0, 0] };
        el.innerHTML = `<span>turn °</span>${xyz('rot', Q.toEuler(p.q), 5, v => +v.toFixed(1))}<span>move</span>${xyz('move', p.t, 0.03125)}`;
      }
      el.querySelectorAll('input[data-v]').forEach(inp => inp.onchange = () => this.boneField(inp.dataset.v, +inp.dataset.k, +inp.value));
    }

    boneField(what, k, v) {
      const rig = this.rig(), i = this.bone;
      if (!rig || i < 0 || !isFinite(v)) return;
      if (what === 'head' || what === 'tail') { this.edit('move joint', () => { rig.bones[i][what][k] = v; }); return; }
      this.beginEdit();
      const p = this.editPose()[i];
      if (what === 'rot') { const e = Q.toEuler(p.q); e[k] = v; p.q = Q.fromEuler(e); } else p.t[k] = v;
      this.poseChanged();
      this.poseDone(what === 'rot' ? 'turn' : 'move');
    }

    refreshClips() {
      const rig = this.rig(), ul = $('#clipList');
      ul.innerHTML = '';
      if (rig) rig.clips.forEach((c, i) => {
        const li = document.createElement('li');
        li.className = i === this.clip ? 'on' : '';
        li.innerHTML = `<span class="n">${esc(c.name)}</span><span class="i">${c.keys.length} keys · ${c.length} s${c.loop ? ' · loop' : ''}</span>`;
        li.onclick = () => { this.stop(); this.clip = i; this.refreshClips(); this.setTime(0); };
        li.ondblclick = () => this.cmd('renameClip');
        ul.appendChild(li);
      });
      const c = this.clipObj();
      for (const id of ['#clipLen', '#clipLoop', '#clipMode']) $(id).disabled = !c;
      if (c) {
        if (document.activeElement !== $('#clipLen')) $('#clipLen').value = c.length;
        $('#clipLoop').checked = c.loop;
        $('#clipMode').value = c.mode;
      }
      $('#tlFps').value = this.S.fps;
      this.requestRender();
    }

    updateStatus() {
      const m = this.model(), rig = this.rig(), c = this.clipObj();
      let s = m ? `<b>${esc(m.name)}</b>` : 'no model';
      if (m) s += rig ? ` · ${rig.bones.length} bones · ${rig.clips.length} animations` : ' · no bones';
      if (this.S.mode === 'anim' && c) s += ` · ${esc(c.name)}: frame ${Math.round(this.time * this.S.fps)} of ${Math.round(c.length * this.S.fps)}`;
      if (this.S.mode === 'rig' && this.selFaces.size) s += ` · ${this.selFaces.size} faces chosen`;
      if (rig && this.bone >= 0) s += ` · bone <b>${esc(rig.bones[this.bone].name)}</b>`;
      $('#stLeft').innerHTML = s;
    }

    status(msg, cls) {
      const el = $('#stRight');
      el.textContent = msg;
      el.className = cls || '';
    }

    showBox(x0, y0, x1, y1) {
      const b = $('#boxsel');
      if (x0 == null) { b.style.display = 'none'; return; }
      Object.assign(b.style, { display: 'block', left: Math.min(x0, x1) + 'px', top: Math.min(y0, y1) + 'px', width: Math.abs(x1 - x0) + 'px', height: Math.abs(y1 - y0) + 'px' });
    }

    blurInputs() {
      const a = document.activeElement;
      if (a && (a.tagName === 'INPUT' || a.tagName === 'SELECT' || a.tagName === 'TEXTAREA')) a.blur();
    }

    // ------------------------------------------------------------ wiring

    wire() {
      $$('.menu').forEach(m => {
        m.querySelector('.mbtn').addEventListener('click', e => {
          e.stopPropagation();
          const open = !m.classList.contains('open');
          $$('.menu').forEach(x => x.classList.remove('open'));
          m.classList.toggle('open', open);
        });
        m.addEventListener('mouseenter', () => { if ($$('.menu.open').length && !m.classList.contains('open')) { $$('.menu').forEach(x => x.classList.remove('open')); m.classList.add('open'); } });
      });
      document.addEventListener('click', () => $$('.menu').forEach(x => x.classList.remove('open')));
      $$('[data-cmd]').forEach(b => b.addEventListener('click', e => { e.preventDefault(); e.stopPropagation(); $$('.menu').forEach(x => x.classList.remove('open')); this.cmd(b.dataset.cmd); }));
      $$('[data-mode]').forEach(b => b.addEventListener('click', () => this.setMode(b.dataset.mode)));
      $$('[data-rtool]').forEach(b => b.addEventListener('click', () => this.setRigTool(b.dataset.rtool)));
      $('#modelSel').addEventListener('change', e => { this.cur = +e.target.value; this.bone = -1; this.clip = 0; this.selFaces.clear(); this.stop(); this.setTime(0); this.refreshAll(); this.view.frame(); e.target.blur(); });
      $('#autoKey').addEventListener('change', e => { this.S.autoKey = e.target.checked; });
      $('#tlFps').addEventListener('change', e => { this.S.fps = +e.target.value; savePrefs(this.S); this.requestRender(); this.updateStatus(); e.target.blur(); });
      $('#clipLen').addEventListener('change', e => {
        const c = this.clipObj(), v = +e.target.value;
        if (!c || !(v > 0)) return;
        const last = c.keys.length ? c.keys[c.keys.length - 1].t : 0;
        const len = Math.max(v, last, 1 / this.S.fps);
        this.edit('length', () => { c.length = +len.toFixed(4); });
        if (len !== v) this.status(`the last keyframe is at ${last.toFixed(2)} s`);
        this.setTime(this.time);
      });
      $('#clipLoop').addEventListener('change', e => { const c = this.clipObj(); if (c) this.edit('loop', () => { c.loop = e.target.checked; }); });
      $('#clipMode').addEventListener('change', e => { const c = this.clipObj(); if (c) this.edit('in-between', () => { c.mode = e.target.value; }); this.pose = null; e.target.blur(); });
      for (const id of ['#sprModel', '#sprClip', '#sprFrames', '#sprFps', '#sprW', '#sprH', '#sprDirs', '#sprPitch', '#sprProj', '#sprScale',
        '#sprLit', '#sprAmb', '#sprBands', '#sprOutline', '#sprOutCol', '#sprCols', '#sprSmooth'])
        $(id).addEventListener('change', () => {
          if (id === '#sprModel') { this.S.spr.model = +$(id).value; this.S.spr.clip = 0; this.refreshSprites(); }
          if (id === '#sprClip') { const { clip } = this.sprClipOf(+$('#sprModel').value, +$(id).value); if (clip) $('#sprFrames').value = Math.max(1, Math.round(clip.length * (+$('#sprFps').value || 12))); }
          this.readSprites();
        });
      document.addEventListener('keydown', e => this.key(e));
      document.addEventListener('dragover', e => { e.preventDefault(); document.body.classList.add('dropping'); });
      document.addEventListener('dragleave', e => { if (!e.relatedTarget) document.body.classList.remove('dropping'); });
      document.addEventListener('drop', async e => {
        e.preventDefault();
        document.body.classList.remove('dropping');
        const file = e.dataTransfer.files[0];
        if (!file) return;
        const item = e.dataTransfer.items && e.dataTransfer.items[0];
        const handleP = item && item.getAsFileSystemHandle ? item.getAsFileSystemHandle().catch(() => null) : Promise.resolve(null);
        try { await this.openBytes(new Uint8Array(await file.arrayBuffer()), file.name, await handleP); } catch (err) { this.error(err); }
      });
    }

    sprClipOf(mi, ci) { const m = this.project.models[mi]; return { m, clip: m && m.rig ? m.rig.clips[ci] : null }; }

    key(e) {
      const t = e.target, typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT');
      const ctrl = e.ctrlKey || e.metaKey, k = e.key, lk = k.length === 1 ? k.toLowerCase() : k, S = this.S;
      if (!$('#modal').hidden) return;
      if (ctrl) {
        const map = { s: e.shiftKey ? 'saveAs' : 'save', o: 'open', z: e.shiftKey ? 'redo' : 'undo', y: 'redo', c: 'copyPose', v: 'pastePose' };
        if (map[lk] && !(typing && 'zycv'.includes(lk))) { e.preventDefault(); this.cmd(map[lk]); }
        return;
      }
      if (typing) return;
      if (k === 'F1') { e.preventDefault(); this.cmd('help'); return; }
      if (k === 'Tab') { e.preventDefault(); const order = ['rig', 'anim', 'sprites']; this.setMode(order[(order.indexOf(S.mode) + 1) % 3]); return; }
      const modes = { 1: 'rig', 2: 'anim', 3: 'sprites' };
      if (modes[k]) { this.setMode(modes[k]); return; }
      const toggles = { l: 'toggleLit', c: 'toggleSkin', o: 'toggleOnion', b: 'toggleBones' };
      if (toggles[lk] && S.mode !== 'sprites') { this.cmd(toggles[lk]); return; }
      if (lk === 'z') { this.cmd('frame'); return; }
      if (k === 'Escape') { this.selFaces.clear(); this.selectBone(-1); return; }
      if (S.mode === 'rig') {
        if (lk === 'n') this.cmd('addBone');
        else if (lk === 'a') this.cmd('assign');
        else if (k === 'Delete' || k === 'Backspace') this.cmd('deleteBone');
        else if (k === 'Home') this.cmd('resetCam');
        return;
      }
      if (S.mode === 'anim') {
        const f = 1 / S.fps;
        if (k === ' ') { e.preventDefault(); this.cmd('play'); }
        else if (k === 'ArrowLeft') { e.preventDefault(); if (e.shiftKey) this.cmd('prevKey'); else { this.stop(); this.setTime(Math.max(0, Math.round(this.time / f - 1) * f)); } }
        else if (k === 'ArrowRight') { e.preventDefault(); if (e.shiftKey) this.cmd('nextKey'); else { this.stop(); this.setTime(Math.round(this.time / f + 1) * f); } }
        else if (k === 'Home') this.cmd('first');
        else if (lk === 'k') this.cmd('key');
        else if (k === 'Delete' || k === 'Backspace') this.cmd('deleteKey');
        else if (lk === 'r') this.cmd('resetBone');
        else if (lk === 'm') this.cmd('mirrorPose');
      }
    }
  }

  window.addEventListener('DOMContentLoaded', () => {
    try { window.app = new App(); } catch (e) {
      document.body.innerHTML = `<p style="padding:20px">bm Animator cannot start: ${esc(e.message)}</p>`;
      throw e;
    }
  });
})();
