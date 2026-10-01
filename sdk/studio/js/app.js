/*
 * bm Studio - the application: the project, undo and redo, files (.bm
 * opened and saved in place, .glb and .png in and out), the panels and
 * the keys. See sdk/README.md.
 */
(function () {
  'use strict';
  const BM = window.BM, E = BM.edit, v3 = BM.v3;
  const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];

  const DEFAULT_PALETTE = [
    0x000000, 0x1C2030, 0x404450, 0x808490, 0xC0C4D0, 0xFFFFFF, 0x5A2A22, 0xA84632, 0xE84A5A, 0xE890B0,
    0xF09030, 0xF0D040, 0xFFF4C0, 0x8A5A30, 0xC49A5E, 0xE0C888, 0x285A28, 0x3E8A3A, 0x5CB048, 0xA8E070,
    0x1E4E6E, 0x2E6EB8, 0x3478C4, 0x7ABCE8, 0x2E8A70, 0x60D0C0, 0x3A2A6A, 0x6A50C8, 0xB060D8, 0xFFC050,
  ];

  function loadPrefs() {
    try { return JSON.parse(localStorage.getItem('bmstudio.prefs') || '{}'); } catch (e) { return {}; }
  }
  function savePrefs(S) {
    try { localStorage.setItem('bmstudio.prefs', JSON.stringify({ tileSize: S.tileSize, view: S.view, brush: S.brush })); } catch (e) { /* private mode */ }
  }

  function newProject() {
    return {
      title: 'New game', author: '', res: '640x360', lua: BM.viewerLua(), sheet: BM.starterSheet(), map: null,
      cover: null, models: [{ name: 'model', faces: [] }], uvInset: 0.25, extras: [],
    };
  }

  function slug(s) { return (s || 'game').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'game'; }
  const hex6 = c => '#' + (c & 0xFFFFFF).toString(16).padStart(6, '0');
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

  class App {
    constructor() {
      const prefs = loadPrefs();
      this.S = {
        tool: 'tile', tileSize: prefs.tileSize || 16, tileSel: { x: 0, y: 0, w: 1, h: 1 }, stampRot: 0, stampFlip: false,
        plainColour: false, colour: 0xE84A5A, plane: 'auto', planeLevel: [0, 0, 0], snap: 1,
        view: Object.assign({ lit: false, cull: true, wire: false, grid: true, rgb565: false, bg: 0x1A1D28 }, prefs.view || {}),
        sideTool: 'tiles', pxTool: 'pencil', brush: prefs.brush || 1, tileGrid: true, pixelGrid: true,
        region: null, floating: null, palette: DEFAULT_PALETTE.slice(), ws: '3d',
      };
      this.project = newProject();
      this.cur = 0;
      this.selFaces = new Set();
      this.selVerts = new Set();
      this.modelVersion = 0;
      this.selVersion = 0;
      this.undoStack = [];
      this.redoStack = [];
      this.dirty = false;
      this.handle = null;
      this.fileName = null;
      this.clipboard = null;
      this.pixelClip = null;
      this.stroke = null;
      this.pendingRect = null;
      this.needRender = true;
      this.sheetCanvas = document.createElement('canvas');
      this.spaceHeld = false;

      this.view = new BM.View3D(this, $('#glc'));
      this.tiles = new BM.SheetView(this, $('#tilesc'), { full: false });
      this.pixel = new BM.SheetView(this, $('#pxc'), { full: true });
      this.sheetReplaced();
      this.wire();
      this.refreshAll();
      this.setTool('tile');
      const loop = () => { this.frame(); requestAnimationFrame(loop); };
      requestAnimationFrame(loop);
      window.addEventListener('beforeunload', e => { if (this.dirty) { e.preventDefault(); e.returnValue = ''; } });
    }

    // ------------------------------------------------------------ access

    model() { return this.project.models[this.cur]; }
    colour() { return this.S.colour; }
    colourCss() { return hex6(this.S.colour); }
    stamp() {
      const S = this.S;
      return E.makeStamp(S.tileSel, S.tileSize, S.stampRot, S.stampFlip, S.plainColour ? S.colour : null);
    }

    requestRender() { this.needRender = true; }

    frame() {
      if (this.pendingRect) {
        const [x0, y0, x1, y1] = this.pendingRect, sh = this.project.sheet;
        const rect = [x0, y0, x1 - x0 + 1, y1 - y0 + 1];
        this.view.r.uploadSheet(sh, rect);
        this.sheetCtx.putImageData(this.sheetImage, 0, 0, rect[0], rect[1], rect[2], rect[3]);
        this.pendingRect = null;
        this.needRender = true;
      }
      if (!this.needRender) return;
      this.needRender = false;
      if (this.S.ws === '3d') { this.view.render(); this.tiles.render(); } else this.pixel.render();
    }

    // ------------------------------------------------------------ changes

    markDirty(on = true) {
      this.dirty = on;
      $('#dirty').classList.toggle('on', on);
    }

    push(entry) {
      this.undoStack.push(entry);
      if (this.undoStack.length > 300) this.undoStack.shift();
      this.redoStack = [];
      this.markDirty();
    }

    undo() {
      const e = this.undoStack.pop();
      if (!e) { this.status('nothing to undo'); return; }
      e.undo();
      this.redoStack.push(e);
      this.markDirty();
      this.status('undo: ' + e.label);
    }

    redo() {
      const e = this.redoStack.pop();
      if (!e) { this.status('nothing to redo'); return; }
      e.redo();
      this.undoStack.push(e);
      this.markDirty();
      this.status('redo: ' + e.label);
    }

    snapFaces(faces) { return faces.map(BM.cloneFace); }

    beginEdit() { this.editBefore = { model: this.model(), faces: this.snapFaces(this.model().faces) }; }

    cancelEdit() {
      const b = this.editBefore;
      this.editBefore = null;
      if (!b) return;
      if (JSON.stringify(b.faces) !== JSON.stringify(b.model.faces)) { b.model.faces = b.faces; this.clearSel(); this.modelChanged(); }
    }

    commitEdit(label) {
      const b = this.editBefore;
      this.editBefore = null;
      if (!b) return;
      const m = b.model, after = this.snapFaces(m.faces);
      if (JSON.stringify(after) === JSON.stringify(b.faces)) return;
      const before = b.faces;
      const set = faces => () => { m.faces = this.snapFaces(faces); this.showModel(m); this.clearSel(); this.modelChanged(); };
      this.push({ label, undo: set(before), redo: set(after) });
      this.modelChanged();
    }

    editModel(label, fn) {
      this.beginEdit();
      fn(this.model().faces);
      this.commitEdit(label);
    }

    showModel(m) {
      const i = this.project.models.indexOf(m);
      if (i >= 0 && i !== this.cur) { this.cur = i; this.refreshModels(); }
    }

    modelChanged(live) {
      this.modelVersion++;
      this.markDirty();
      if (!live) {
        // selections that point to faces no longer there go
        const faces = new Set(this.model().faces);
        for (const f of [...this.selFaces]) if (!faces.has(f)) this.selFaces.delete(f);
        this.selVersion++;
        this.refreshModels();
      }
      this.updateStatus();
      this.requestRender();
    }

    clearSel() {
      this.selFaces.clear();
      this.selVerts.clear();
      this.selectionChanged();
    }

    selectionChanged() {
      this.selVersion++;
      const nf = this.selFaces.size, nv = this.selVerts.size;
      $('#selbar').hidden = !(this.S.tool === 'select' && nf);
      $('#vertbar').hidden = !(this.S.tool === 'vertex' && nv);
      $('#selCount').textContent = `${nf} face${nf === 1 ? '' : 's'}`;
      $('#vertCount').textContent = `${nv} corner${nv === 1 ? '' : 's'}`;
      this.requestRender();
    }

    // ------------------------------------------------------------ sheet

    sheetReplaced() {
      const sh = this.project.sheet;
      this.sheetCanvas.width = sh.w;
      this.sheetCanvas.height = sh.h;
      this.sheetCtx = this.sheetCanvas.getContext('2d');
      this.sheetImage = new ImageData(sh.px, sh.w, sh.h);
      this.sheetCtx.putImageData(this.sheetImage, 0, 0);
      this.view.r.uploadSheet(sh);
      const T = this.S.tileSize, s = this.S.tileSel;
      if ((s.x + s.w) * T > sh.w || (s.y + s.h) * T > sh.h) this.S.tileSel = { x: 0, y: 0, w: 1, h: 1 };
      $('#sheetSize').textContent = `${sh.w} × ${sh.h}`;
      this.tiles.fitted = false; this.tiles.resize();
      this.pixel.fitted = false; this.pixel.resize();
      this.requestRender();
    }

    sheetDirty(x, y) {
      const r = this.pendingRect;
      if (!r) this.pendingRect = [x, y, x, y];
      else { r[0] = Math.min(r[0], x); r[1] = Math.min(r[1], y); r[2] = Math.max(r[2], x); r[3] = Math.max(r[3], y); }
    }

    beginStroke() {
      this.stroke = { px: new Map(), faces: false };
      this.beginEdit();
    }

    /* c: 0xRRGGBB, or null = transparent */
    paintPixel(x, y, c) {
      const sh = this.project.sheet;
      if (x < 0 || y < 0 || x >= sh.w || y >= sh.h) return;
      if (!this.stroke) this.beginStroke();
      const i = (y * sh.w + x) * 4, p = sh.px;
      const old = ((p[i] << 24) | (p[i + 1] << 16) | (p[i + 2] << 8) | p[i + 3]) >>> 0;
      const nw = c == null ? 0 : (((c & 0xFFFFFF) << 8) | 255) >>> 0;
      if (old === nw) return;
      if (!this.stroke.px.has(i)) this.stroke.px.set(i, old);
      p[i] = nw >>> 24; p[i + 1] = nw >>> 16 & 255; p[i + 2] = nw >>> 8 & 255; p[i + 3] = nw & 255;
      this.sheetDirty(x, y);
      this.markDirty();
    }

    endStroke(label) {
      const st = this.stroke;
      this.stroke = null;
      if (!st) return;
      if (st.faces) this.commitEdit('paint'); else this.editBefore = null;
      if (!st.px.size) return;
      const sh = this.project.sheet, idx = [...st.px.keys()], olds = idx.map(i => st.px.get(i));
      const news = idx.map(i => ((sh.px[i] << 24) | (sh.px[i + 1] << 16) | (sh.px[i + 2] << 8) | sh.px[i + 3]) >>> 0);
      const apply = vals => () => {
        const s = this.project.sheet;
        if (s !== sh) return;
        idx.forEach((i, k) => {
          const v = vals[k];
          s.px[i] = v >>> 24; s.px[i + 1] = v >>> 16 & 255; s.px[i + 2] = v >>> 8 & 255; s.px[i + 3] = v & 255;
          this.sheetDirty((i / 4) % s.w, Math.floor(i / 4 / s.w));
        });
      };
      this.push({ label, undo: apply(olds), redo: apply(news) });
    }

    floodFill(x, y, c) {
      const sh = this.project.sheet, p = sh.px, w = sh.w, h = sh.h, at = (x, y) => (y * w + x) * 4;
      const i0 = at(x, y), tr = p[i0 + 3] < 128, target = [p[i0], p[i0 + 1], p[i0 + 2]];
      const match = i => tr ? p[i + 3] < 128 : p[i + 3] >= 128 && p[i] === target[0] && p[i + 1] === target[1] && p[i + 2] === target[2];
      const nw = c == null ? null : c & 0xFFFFFF;
      if (nw == null ? tr : (!tr && ((target[0] << 16) | (target[1] << 8) | target[2]) === nw)) return;
      this.beginStroke();
      const stack = [[x, y]], seen = new Uint8Array(w * h);
      while (stack.length) {
        const [cx, cy] = stack.pop();
        if (cx < 0 || cy < 0 || cx >= w || cy >= h || seen[cy * w + cx]) continue;
        seen[cy * w + cx] = 1;
        if (!match(at(cx, cy))) continue;
        this.paintPixel(cx, cy, nw);
        stack.push([cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]);
      }
      this.endStroke('fill');
    }

    pickFromSheet(x, y) {
      const sh = this.project.sheet;
      if (x < 0 || y < 0 || x >= sh.w || y >= sh.h) return;
      const i = (y * sh.w + x) * 4;
      if (sh.px[i + 3] < 128) { this.status('transparent pixel'); return; }
      this.setColour((sh.px[i] << 16) | (sh.px[i + 1] << 8) | sh.px[i + 2]);
    }

    eyedrop(hit) {
      if (hit.face.c != null) { this.setColour(hit.face.c); return; }
      const [u, v] = E.hitUV(hit);
      this.pickFromSheet(Math.floor(u), Math.floor(v));
    }

    /* replaces the whole sheet, with undo */
    setSheet(img, label) {
      const before = this.project.sheet;
      const set = s => () => { this.project.sheet = s; this.sheetReplaced(); };
      this.project.sheet = img;
      this.sheetReplaced();
      this.push({ label, undo: set(before), redo: set(img) });
    }

    /* the first place of the sheet where w x h pixels are all transparent,
     * on the tile grid, or null */
    freeSpot(w, h, sheet = this.project.sheet) {
      const W = sheet.w, H = sheet.h, sat = new Int32Array((W + 1) * (H + 1));
      for (let y = 0; y < H; y++) {
        let row = 0;
        for (let x = 0; x < W; x++) {
          row += sheet.px[(y * W + x) * 4 + 3] >= 128 ? 1 : 0;
          sat[(y + 1) * (W + 1) + x + 1] = sat[y * (W + 1) + x + 1] + row;
        }
      }
      const step = Math.min(8, this.S.tileSize);
      for (let y = 0; y + h <= H; y += step)
        for (let x = 0; x + w <= W; x += step) {
          const s = sat[(y + h) * (W + 1) + x + w] - sat[y * (W + 1) + x + w] - sat[(y + h) * (W + 1) + x] + sat[y * (W + 1) + x];
          if (s === 0) return [x, y];
        }
      return null;
    }

    /* puts a picture in the sheet (growing it if there is no room);
     * returns where, in one undo step with the sheet */
    placePicture(img, at) {
      let sheet = this.project.sheet, pos = at || this.freeSpot(img.w, img.h);
      if (!pos) {
        let lowest = 0;
        for (let y = sheet.h - 1; y >= 0 && !lowest; y--)
          for (let x = 0; x < sheet.w; x++) if (sheet.px[(y * sheet.w + x) * 4 + 3] >= 128) { lowest = y + 1; break; }
        const T = this.S.tileSize, top = Math.ceil(lowest / T) * T;
        const nw = Math.min(BM.LIMITS.sheet, Math.max(sheet.w, Math.ceil(img.w / 8) * 8));
        const nh = Math.min(BM.LIMITS.sheet, Math.ceil((top + img.h) / 8) * 8);
        if (img.w > nw || top + img.h > nh) return null;
        sheet = this.resized(sheet, nw, nh);
        pos = [0, top];
      } else sheet = { w: sheet.w, h: sheet.h, px: sheet.px.slice() };
      for (let y = 0; y < img.h; y++)
        for (let x = 0; x < img.w; x++) {
          const tx = pos[0] + x, ty = pos[1] + y;
          if (tx >= sheet.w || ty >= sheet.h) continue;
          const s = (y * img.w + x) * 4, d = (ty * sheet.w + tx) * 4;
          sheet.px[d] = img.px[s]; sheet.px[d + 1] = img.px[s + 1]; sheet.px[d + 2] = img.px[s + 2]; sheet.px[d + 3] = img.px[s + 3];
        }
      this.setSheet(sheet, 'picture');
      return pos;
    }

    resized(sheet, w, h) {
      const out = BM.newImage(w, h);
      for (let y = 0; y < Math.min(h, sheet.h); y++)
        out.px.set(sheet.px.subarray(y * sheet.w * 4, (y * sheet.w + Math.min(w, sheet.w)) * 4), y * w * 4);
      return out;
    }

    sheetHover(p) {
      $('#pxAt').textContent = p ? `${p[0]}, ${p[1]}` : '';
      if (p) {
        const T = this.S.tileSize, per = Math.floor(this.project.sheet.w / 8);
        this.statusRight(`pixel ${p[0]}, ${p[1]} · tile ${Math.floor(p[0] / T)}, ${Math.floor(p[1] / T)} · spr ${Math.floor(p[1] / 8) * per + Math.floor(p[0] / 8)}`);
      }
    }

    // ------------------------------------------------------------ pixel selection

    regionImage(r) {
      const sh = this.project.sheet, img = BM.newImage(r.w, r.h);
      for (let y = 0; y < r.h; y++) img.px.set(sh.px.subarray(((r.y + y) * sh.w + r.x) * 4, ((r.y + y) * sh.w + r.x + r.w) * 4), y * r.w * 4);
      return img;
    }

    clearRegion(r, label) {
      this.beginStroke();
      for (let y = r.y; y < r.y + r.h; y++) for (let x = r.x; x < r.x + r.w; x++) this.paintPixel(x, y, null);
      this.endStroke(label);
    }

    liftRegion(hx, hy, copy) {
      const r = this.S.region;
      this.S.floating = { img: this.regionImage(r), hx, hy };
      if (!copy) this.clearRegion(r, 'move');
      this.S.region = null;
      this.requestRender();
    }

    dropFloating(sx, sy) {
      const f = this.S.floating;
      this.S.floating = null;
      const x0 = sx - f.hx, y0 = sy - f.hy;
      this.beginStroke();
      for (let y = 0; y < f.img.h; y++)
        for (let x = 0; x < f.img.w; x++) {
          const i = (y * f.img.w + x) * 4;
          this.paintPixel(x0 + x, y0 + y, f.img.px[i + 3] >= 128 ? (f.img.px[i] << 16) | (f.img.px[i + 1] << 8) | f.img.px[i + 2] : null);
        }
      this.endStroke('paste');
      this.S.region = { x: x0, y: y0, w: f.img.w, h: f.img.h };
      this.requestRender();
    }

    transformRegion(kind) {
      const flipImg = (img, fn) => {
        const out = BM.newImage(kind === 'rot' ? img.h : img.w, kind === 'rot' ? img.w : img.h);
        for (let y = 0; y < img.h; y++)
          for (let x = 0; x < img.w; x++) {
            const [nx, ny] = fn(x, y);
            out.px.set(img.px.subarray((y * img.w + x) * 4, (y * img.w + x) * 4 + 4), (ny * out.w + nx) * 4);
          }
        return out;
      };
      const fns = { h: (w, h) => (x, y) => [w - 1 - x, y], v: (w, h) => (x, y) => [x, h - 1 - y], rot: (w, h) => (x, y) => [h - 1 - y, x] };
      if (this.S.floating) {
        const f = this.S.floating;
        f.img = flipImg(f.img, fns[kind](f.img.w, f.img.h));
        f.canvas = null;
        this.requestRender();
        return;
      }
      const r = this.S.region;
      if (!r) { this.status('select an area first (M)'); return; }
      if (kind === 'rot' && r.w !== r.h) { this.status('only a square selection turns'); return; }
      const img = flipImg(this.regionImage(r), fns[kind](r.w, r.h));
      this.beginStroke();
      for (let y = 0; y < r.h; y++)
        for (let x = 0; x < r.w; x++) {
          const i = (y * img.w + x) * 4;
          this.paintPixel(r.x + x, r.y + y, img.px[i + 3] >= 128 ? (img.px[i] << 16) | (img.px[i + 1] << 8) | img.px[i + 2] : null);
        }
      this.endStroke(kind === 'rot' ? 'turn' : 'mirror');
    }

    // ------------------------------------------------------------ tiles and colours

    setTileSel(sel) {
      this.S.tileSel = sel;
      this.S.plainColour = false;
      $('#plainColour').checked = false;
      this.updateStampInfo();
      this.requestRender();
    }

    updateStampInfo() {
      const s = this.S.tileSel, st = this.stamp();
      $('#stampInfo').textContent = this.S.plainColour ? 'colour' : `${s.w}×${s.h}` + (this.S.stampRot ? ` ⟳${this.S.stampRot * 90}°` : '') + (this.S.stampFlip ? ' ⇋' : '');
      return st;
    }

    takeFace(face) {
      if (face.c != null) {
        this.setColour(face.c);
        this.S.plainColour = true;
        $('#plainColour').checked = true;
        this.updateStampInfo();
        return;
      }
      const r = E.faceRect(face, this.S.tileSize);
      if (r) { this.setTileSel(r); this.S.stampRot = 0; this.S.stampFlip = false; this.updateStampInfo(); this.status('tile taken from the face'); }
    }

    setColour(c) {
      this.S.colour = c & 0xFFFFFF;
      const h = hex6(this.S.colour);
      for (const id of ['#colourPick', '#colourPick2']) $(id).value = h;
      for (const id of ['#colourHex', '#colourHex2']) if (document.activeElement !== $(id)) $(id).value = h;
      $$('.palette button').forEach(b => b.classList.toggle('cur', +b.dataset.c === this.S.colour));
      if (!this.S.palette.includes(this.S.colour)) { this.S.palette.push(this.S.colour); this.refreshPalette(); }
      this.requestRender();
    }

    refreshPalette() {
      for (const id of ['#palette', '#palette2']) {
        const el = $(id);
        el.innerHTML = '';
        for (const c of this.S.palette.slice(-64)) {
          const b = document.createElement('button');
          b.style.background = hex6(c);
          b.dataset.c = c;
          b.title = hex6(c);
          b.classList.toggle('cur', c === this.S.colour);
          b.onclick = () => this.setColour(c);
          el.appendChild(b);
        }
      }
    }

    paletteFromSheet() {
      const sh = this.project.sheet, count = new Map();
      for (let i = 0; i < sh.px.length; i += 4) {
        if (sh.px[i + 3] < 128) continue;
        const c = (sh.px[i] << 16) | (sh.px[i + 1] << 8) | sh.px[i + 2];
        count.set(c, (count.get(c) || 0) + 1);
      }
      const list = [...count.entries()].sort((a, b) => b[1] - a[1]).slice(0, 64).map(e => e[0]);
      list.sort((a, b) => this.hue(a) - this.hue(b));
      this.S.palette = list.length ? list : DEFAULT_PALETTE.slice();
      this.refreshPalette();
    }

    hue(c) {
      const r = (c >> 16 & 255) / 255, g = (c >> 8 & 255) / 255, b = (c & 255) / 255, mx = Math.max(r, g, b), mn = Math.min(r, g, b);
      if (mx - mn < 0.08) return -1 + mx;
      const h = mx === r ? (g - b) / (mx - mn) : mx === g ? 2 + (b - r) / (mx - mn) : 4 + (r - g) / (mx - mn);
      return ((h + 6) % 6) * 10 + mx;
    }

    // ------------------------------------------------------------ tools

    setTool(t) {
      this.S.tool = t;
      $$('#tools3d button').forEach(b => b.classList.toggle('on', b.dataset.tool === t));
      $('#hint').textContent = BM.TOOL_HINTS[t];
      this.view.hover = null;
      if (t !== 'select') this.selFaces.clear();
      if (t !== 'vertex') this.selVerts.clear();
      this.selectionChanged();
    }

    setWorkspace(ws) {
      this.S.ws = ws;
      $$('.wstabs button').forEach(b => b.classList.toggle('on', b.dataset.ws === ws));
      $('#ws-3d').classList.toggle('on', ws === '3d');
      $('#ws-pixel').classList.toggle('on', ws === 'pixel');
      if (ws === '3d') { this.view.resize(); this.tiles.resize(); } else this.pixel.resize();
      this.requestRender();
    }

    movePlane(d) {
      const axis = this.view.workingPlane().axis;
      this.S.planeLevel[axis] += d;
      this.refreshPlane();
      this.view.hover = null;
      this.requestRender();
    }

    refreshPlane() {
      const axis = this.view.workingPlane().axis;
      $('#planeLevel').textContent = this.S.planeLevel[axis];
      $('#planeSel').value = this.S.plane;
    }

    hoverInfo(h) {
      if (!h) { this.statusRight(''); return; }
      if (h.cell) {
        const names = ['x', 'y', 'z'];
        this.statusRight(`${h.onFace ? 'on a face' : 'grid'} ${names[h.axis]} = ${+h.level.toFixed(3)} · cell ${h.cell.map(v => +v.toFixed(2)).join(', ')}`);
      } else if (h.onFace) {
        const f = h.onFace.face;
        this.statusRight(f.c != null ? `face in ${hex6(f.c)}` : `face · texture at ${E.hitUV(h.onFace).map(Math.floor).join(', ')}`);
      } else if (h.vertex) this.statusRight(`corner ${h.vertex.p.map(v => +v.toFixed(3)).join(', ')}`);
      else this.statusRight('');
      this.refreshPlane();
    }

    selectionPoints() {
      if (this.S.tool === 'vertex') {
        const verts = E.vertices(this.model().faces), pts = [];
        for (const k of this.selVerts) { const v = verts.get(k); if (v) for (const [f, i] of v.refs) pts.push(f.p[i]); }
        return pts;
      }
      return E.facePoints([...this.selFaces]);
    }

    /* after moving corners, the selection follows them */
    reselectVerts(pts) {
      if (this.S.tool !== 'vertex') return;
      this.selVerts.clear();
      for (const p of pts) this.selVerts.add(BM.posKey(p));
      this.selectionChanged();
    }

    moveSel(d, label = 'move') {
      const pts = this.selectionPoints();
      if (!pts.length) return false;
      this.beginEdit();
      E.translate(pts, d);
      this.commitEdit(label);
      this.reselectVerts(pts);
      return true;
    }

    transformSel(label, fn) {
      const faces = [...this.selFaces];
      if (!faces.length) { this.status('select some faces first'); return; }
      this.beginEdit();
      fn(faces, E.facePoints(faces));
      this.commitEdit(label);
      this.selVersion++;
      this.requestRender();
    }

    selCenter(pts, half = true) {
      const c = E.centerOf(pts);
      return half ? c.map(v => E.snap(v, 0.5)) : c;
    }

    // ------------------------------------------------------------ commands

    cmd(name, arg) {
      const f = this.commands[name];
      if (!f) { console.warn('no command', name); return; }
      Promise.resolve(f.call(this, arg)).catch(e => this.error(e));
    }

    get commands() {
      const A = this;
      return {
        new: async () => { if (await A.confirmLose()) A.loadProject(newProject(), null, null); },
        open: () => A.openFile(),
        save: () => A.save(false),
        saveAs: () => A.save(true),
        importGlb: () => A.pick('.glb', b => A.importGlb(b)),
        importPng: () => A.pick('.png', (b, name) => A.importPngDialog(b, name)),
        exportGlb: () => A.exportGlb([A.model()]),
        exportAllGlb: () => A.exportGlb(A.project.models.filter(m => m.faces.length)),
        exportPng: async () => A.download(await BM.encodePNG(A.project.sheet), slug(A.project.title) + '-sheet.png'),
        exportRegion: async () => {
          const r = A.S.region;
          if (!r) { A.status('select an area first (M)'); return; }
          A.download(await BM.encodePNG(A.regionImage(r)), slug(A.project.title) + `-${r.x}-${r.y}.png`);
        },
        exportLua: () => A.download(BM.utf8(BM.modelToLua(A.model())), slug(A.model().name) + '.lua'),
        undo: () => A.undo(),
        redo: () => A.redo(),
        selectAll: () => {
          if (A.S.ws === 'pixel') { const s = A.project.sheet; A.S.region = { x: 0, y: 0, w: s.w, h: s.h }; A.requestRender(); return; }
          if (A.S.tool === 'vertex') for (const v of E.vertices(A.model().faces).keys()) A.selVerts.add(v);
          else { if (A.S.tool !== 'select') A.setTool('select'); for (const f of A.model().faces) A.selFaces.add(f); }
          A.selectionChanged();
        },
        copy: () => {
          if (A.S.ws === 'pixel') { if (A.S.region) { A.pixelClip = A.regionImage(A.S.region); A.status('copied'); } return; }
          if (!A.selFaces.size) return;
          A.clipboard = [...A.selFaces].map(BM.cloneFace);
          A.status(`${A.clipboard.length} faces copied`);
        },
        cut: () => {
          if (A.S.ws !== 'pixel' || !A.S.region) return;
          A.pixelClip = A.regionImage(A.S.region);
          A.clearRegion(A.S.region, 'cut');
        },
        paste: () => {
          if (A.S.ws === 'pixel') {
            if (!A.pixelClip) return;
            A.S.pxTool = 'select'; A.refreshPixelTools();
            A.S.floating = { img: A.pixelClip, hx: 0, hy: 0 };
            A.status('click where it goes (Esc: cancel)');
            A.requestRender();
            return;
          }
          if (!A.clipboard) return;
          const copies = A.clipboard.map(BM.cloneFace);
          A.editModel('paste', faces => faces.push(...copies));
          A.setTool('select');
          for (const f of copies) A.selFaces.add(f);
          A.selectionChanged();
        },
        duplicate: () => {
          if (!A.selFaces.size) return;
          const copies = [...A.selFaces].map(BM.cloneFace);
          A.editModel('duplicate', faces => faces.push(...copies));
          A.selFaces.clear();
          for (const f of copies) A.selFaces.add(f);
          A.selectionChanged();
          A.status('duplicated: move the copy with the arrows or the mouse');
        },
        delete: () => {
          if (A.S.ws === 'pixel') { if (A.S.region) A.clearRegion(A.S.region, 'clear'); return; }
          if (A.S.tool === 'vertex' && A.selVerts.size) {
            const keys = A.selVerts;
            A.editModel('delete', faces => { const keep = faces.filter(f => !f.p.some(p => keys.has(BM.posKey(p)))); faces.length = 0; faces.push(...keep); });
            A.clearSel();
            return;
          }
          if (!A.selFaces.size) return;
          const sel = A.selFaces;
          A.editModel('delete', faces => { const keep = faces.filter(f => !sel.has(f)); faces.length = 0; faces.push(...keep); });
          A.clearSel();
        },
        turnRight: () => A.transformSel('turn', (f, pts) => E.turnY(pts, A.selCenter(pts), 1)),
        turnLeft: () => A.transformSel('turn', (f, pts) => E.turnY(pts, A.selCenter(pts), -1)),
        tip: () => A.transformSel('tip over', (f, pts) => {
          const r = A.view.cam.basis().r, axis = Math.abs(r[0]) > Math.abs(r[2]) ? 0 : 2;
          E.turnAxis(pts, A.selCenter(pts), axis, (axis === 0 ? 1 : -1) * (Math.sign(r[axis]) || 1));
        }),
        mirror: () => A.transformSel('mirror', (faces, pts) => {
          const r = A.view.cam.basis().r, axis = Math.abs(r[0]) > Math.abs(r[2]) ? 0 : 2;
          E.mirror(faces, A.selCenter(pts, false), axis);
        }),
        flipSide: () => A.transformSel('other side', faces => faces.forEach(E.flipFace)),
        retexture: () => A.transformSel('new texture', faces => faces.forEach(f => E.retexture(f, A.stamp().tiles[0]))),
        turnUV: () => A.transformSel('turn texture', faces => faces.forEach(f => E.turnUV(f, 1))),
        grow: () => A.transformSel('bigger', (f, pts) => E.scalePoints(pts, A.selCenter(pts, false), 2)),
        shrink: () => A.transformSel('smaller', (f, pts) => E.scalePoints(pts, A.selCenter(pts, false), 0.5)),
        merge: () => {
          if (A.selVerts.size < 2) { A.status('select two or more corners (Shift+click)'); return; }
          let c = null;
          A.editModel('merge', faces => { c = E.mergeVertices(faces, A.selVerts); });
          A.selVerts.clear();
          if (c) A.selVerts.add(BM.posKey(c));
          A.selectionChanged();
        },
        toModel: async () => {
          if (!A.selFaces.size) return;
          const name = await A.askName('Name of the new model', A.uniqueName(A.model().name + '2'));
          if (!name) return;
          const moved = [...A.selFaces], from = A.model(), before = from.faces.slice(), models = A.project.models.slice();
          const m = { name, faces: moved.map(BM.cloneFace) };
          const after = from.faces.filter(f => !A.selFaces.has(f));
          const doIt = () => { from.faces = after.slice(); A.project.models = models.concat([m]); A.cur = A.project.models.length - 1; A.clearSel(); A.modelChanged(); };
          const undoIt = () => { from.faces = before.slice(); A.project.models = models.slice(); A.cur = A.project.models.indexOf(from); A.clearSel(); A.modelChanged(); };
          doIt();
          A.push({ label: 'new model', undo: undoIt, redo: doIt });
        },
        toggleLit: () => A.toggleView('lit'),
        toggleCull: () => A.toggleView('cull'),
        toggleWire: () => A.toggleView('wire'),
        toggleGrid: () => A.toggleView('grid'),
        toggle565: () => A.toggleView('rgb565'),
        frame: () => A.view.frame(A.selFaces.size ? [...A.selFaces] : null),
        resetCam: () => { A.view.cam.reset(); A.requestRender(); },
        help: () => A.helpDialog(),
        about: () => A.aboutDialog(),
        newModel: async () => {
          const name = await A.askName('Name of the new model', A.uniqueName('model'));
          if (name) A.modelsEdit('new model', ms => { ms.push({ name, faces: [] }); return ms.length - 1; });
        },
        dupModel: () => A.modelsEdit('duplicate model', ms => {
          const copy = { name: A.uniqueName(A.model().name), faces: A.model().faces.map(BM.cloneFace) };
          if (A.model().rig) copy.rig = BM.cloneRig(A.model().rig);
          ms.splice(A.cur + 1, 0, copy);
          return A.cur + 1;
        }),
        renameModel: () => A.renameModel(A.cur),
        deleteModel: async () => {
          if (A.model().faces.length && !(await A.confirm(`Delete the model "${A.model().name}"?`, 'Delete'))) return;
          A.modelsEdit('delete model', ms => {
            ms.splice(A.cur, 1);
            if (!ms.length) ms.push({ name: 'model', faces: [] });
            return Math.min(A.cur, ms.length - 1);
          });
        },
        modelUp: () => A.cur > 0 && A.modelsEdit('order', ms => { ms.splice(A.cur - 1, 0, ms.splice(A.cur, 1)[0]); return A.cur - 1; }),
        modelDown: () => A.cur < A.project.models.length - 1 && A.modelsEdit('order', ms => { ms.splice(A.cur + 1, 0, ms.splice(A.cur, 1)[0]); return A.cur + 1; }),
        coverFromView: () => A.coverFromView(),
        coverFromPng: () => A.pick('.png', async b => A.setCover(BM.makeCover(await A.decodeImage(b)))),
        coverRemove: () => A.setCover(null),
        luaViewer: async () => {
          if (A.project.lua.trim() && !A.project.lua.startsWith(BM.VIEWER_MARK) &&
              !(await A.confirm('Replace the code of the cartridge with the model viewer?', 'Replace'))) return;
          A.project.lua = BM.viewerLua(); $('#cartLua').value = A.project.lua; A.markDirty(); A.refreshCart();
        },
        luaImport: () => A.pick('.lua,.txt', b => { A.project.lua = BM.fromUtf8(b); $('#cartLua').value = A.project.lua; A.markDirty(); A.refreshCart(); }),
        luaExport: () => A.download(BM.utf8(A.project.lua), 'main.lua'),
        zoomIn: () => A.pixel.zoomBy(1),
        zoomOut: () => A.pixel.zoomBy(-1),
        zoomFit: () => A.pixel.fit(),
        paletteFromSheet: () => A.paletteFromSheet(),
        paletteDefault: () => { A.S.palette = DEFAULT_PALETTE.slice(); A.refreshPalette(); },
        flipH: () => A.transformRegion('h'),
        flipV: () => A.transformRegion('v'),
        rotRegion: () => A.transformRegion('rot'),
        resizeSheet: () => A.resizeDialog(),
      };
    }

    toggleView(k) {
      this.S.view[k] = !this.S.view[k];
      savePrefs(this.S);
      this.refreshChecks();
      this.view.hover = null;
      this.requestRender();
    }

    refreshChecks() {
      $$('[data-check]').forEach(b => {
        const on = !!this.S.view[b.dataset.check];
        b.classList.toggle('checked', on);
        if (b.classList.contains('tog')) b.classList.toggle('on', on);
      });
    }

    uniqueName(base) {
      base = base.slice(0, BM.LIMITS.name) || 'model';
      const names = new Set(this.project.models.map(m => m.name));
      if (!names.has(base)) return base;
      const stem = base.replace(/\d+$/, '').slice(0, BM.LIMITS.name - 3) || 'm';
      for (let i = 2; ; i++) if (!names.has(stem + i)) return stem + i;
    }

    modelsEdit(label, fn) {
      const before = this.project.models.slice(), cur0 = this.cur, after = before.slice();
      const cur1 = fn(after);
      const set = (ms, c) => () => { this.project.models = ms.slice(); this.cur = c; this.clearSel(); this.modelChanged(); };
      set(after, cur1)();
      this.push({ label, undo: set(before, cur0), redo: set(after, cur1) });
    }

    async renameModel(i) {
      const m = this.project.models[i], old = m.name;
      const name = await this.askName('Rename the model', old, old);
      if (!name || name === old) return;
      const set = n => () => { m.name = n; this.refreshModels(); };
      set(name)();
      this.push({ label: 'rename', undo: set(old), redo: set(name) });
    }

    // ------------------------------------------------------------ files

    async confirmLose() {
      return !this.dirty || this.confirm('The project has changes that are not saved. Lose them?', 'Lose them');
    }

    loadProject(p, handle, name, warnings) {
      this.project = p;
      this.cur = 0;
      if (!p.models.length) p.models.push({ name: 'model', faces: [] });
      this.handle = handle;
      this.fileName = name;
      this.pngFile = null;
      this.undoStack = [];
      this.redoStack = [];
      this.S.region = null; this.S.floating = null;
      this.clearSel();
      this.sheetReplaced();
      this.modelVersion++;
      this.refreshAll();
      this.view.frame();
      this.markDirty(false);
      if (warnings && warnings.length) this.alert('Opened with warnings', warnings.map(esc).join('<br>'));
    }

    async openFile() {
      if (window.showOpenFilePicker) {
        let h;
        try {
          [h] = await window.showOpenFilePicker({ types: [{ description: 'bm cartridge, glTF model, picture', accept: { 'application/octet-stream': ['.bm', '.glb'], 'image/png': ['.png'] } }] });
        } catch (e) { return; }
        const file = await h.getFile();
        await this.openBytes(new Uint8Array(await file.arrayBuffer()), file.name, h);
        return;
      }
      this.pick('.bm,.glb,.png', (b, name) => this.openBytes(b, name, null));
    }

    async openBytes(b, name, handle) {
      const ext = (name.match(/\.[^.]+$/) || [''])[0].toLowerCase();
      if (b[0] === 0x67 && b[1] === 0x6C && b[2] === 0x54 && b[3] === 0x46) {       // glTF
        if (!(await this.confirmLose())) return;
        const p = newProject();
        p.sheet = BM.newImage(256, 256);
        p.models = [];
        p.title = name.replace(/\.[^.]+$/, '').slice(0, 47);
        this.loadProject(p, null, null);
        await this.importGlb(b, true);
        this.undoStack = []; this.redoStack = [];
        return;
      }
      if (BM.isPNG(b)) {
        if (!(await this.confirmLose())) return;
        const img = await this.decodeImage(b);
        if (img.w > BM.LIMITS.sheet || img.h > BM.LIMITS.sheet) throw new Error(`the picture is ${img.w}×${img.h}: a sheet is at most 4096×4096`);
        const p = newProject();
        p.sheet = img;
        p.title = name.replace(/\.[^.]+$/, '').slice(0, 47);
        this.loadProject(p, null, null);
        // a picture opened alone: Ctrl+S writes the picture back (Save as… makes a .bm)
        this.pngFile = { name, handle };
        this.refreshDoc();
        this.setWorkspace('pixel');
        return;
      }
      if (ext !== '.bm' && !(b.length > 8 && String.fromCharCode(...b.subarray(0, 6)) === 'BMCART')) throw new Error(`${name}: not a .bm, .glb or .png file`);
      if (!(await this.confirmLose())) return;
      const { project, warnings } = BM.parseCart(b);
      this.loadProject(project, handle, name, warnings);
      this.status(`opened ${name}: ${project.models.length} models, sheet ${project.sheet.w}×${project.sheet.h}`);
    }

    async save(as) {
      this.syncCart();
      if (!as && this.pngFile) { await this.savePng(); return; }
      const problems = BM.checkProject(this.project);
      if (problems.length) { await this.alert('The cartridge cannot be saved like this', problems.map(esc).join('<br>')); return; }
      const bytes = BM.buildCart(this.project);
      const suggested = this.fileName || slug(this.project.title) + '.bm';
      if (!as && this.handle) {
        try {
          const w = await this.handle.createWritable();
          await w.write(bytes);
          await w.close();
          this.saved(this.handle.name, bytes);
          return;
        } catch (e) {
          if (e.name === 'AbortError') return;
          this.status('cannot write the file there: ' + e.message, 'bad');
        }
      }
      if (window.showSaveFilePicker) {
        try {
          const h = await window.showSaveFilePicker({ suggestedName: suggested, types: [{ description: 'bm cartridge', accept: { 'application/octet-stream': ['.bm'] } }] });
          const w = await h.createWritable();
          await w.write(bytes);
          await w.close();
          this.handle = h;
          this.saved(h.name, bytes);
        } catch (e) { if (e.name !== 'AbortError') throw e; }
        return;
      }
      this.download(bytes, suggested);
      this.fileName = suggested;
      this.saved(suggested, bytes);
    }

    async savePng() {
      const f = this.pngFile, bytes = await BM.encodePNG(this.project.sheet);
      if (f.handle && f.handle.createWritable) {
        try {
          const w = await f.handle.createWritable();
          await w.write(bytes);
          await w.close();
          this.markDirty(false);
          this.status(`saved ${f.name}`, 'good');
          return;
        } catch (e) {
          if (e.name === 'AbortError') return;
          this.status('cannot write the picture there: ' + e.message, 'bad');
        }
      }
      this.download(bytes, f.name);
      this.markDirty(false);
      this.status(`saved ${f.name}`, 'good');
    }

    saved(name, bytes) {
      this.pngFile = null;
      this.fileName = name;
      this.markDirty(false);
      this.refreshDoc();
      this.status(`saved ${name} (${(bytes.length / 1024).toFixed(1)} KiB): copy it to carts/ on the SD card`, 'good');
    }

    download(bytes, name) {
      const url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
      const a = document.createElement('a');
      a.href = url; a.download = name;
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 5000);
    }

    pick(accept, fn) {
      const inp = $('#fileInput');
      inp.accept = accept;
      inp.value = '';
      inp.onchange = async () => {
        const file = inp.files[0];
        if (!file) return;
        try { await fn(new Uint8Array(await file.arrayBuffer()), file.name); } catch (e) { this.error(e); }
      };
      inp.click();
    }

    async decodeImage(b, mime) {
      try {
        if (BM.isPNG(b)) return BM.binarizeAlpha(await BM.decodePNG(b));
      } catch (e) { /* the browser's decoder below */ }
      const bmp = await createImageBitmap(new Blob([b], { type: mime || 'image/png' }), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
      const c = document.createElement('canvas');
      c.width = bmp.width; c.height = bmp.height;
      const g = c.getContext('2d');
      g.drawImage(bmp, 0, 0);
      const d = g.getImageData(0, 0, c.width, c.height);
      return BM.binarizeAlpha({ w: c.width, h: c.height, px: d.data });
    }

    async exportGlb(models) {
      const ms = models.filter(m => m.faces.length);
      if (!ms.length) { this.status('the model is empty', 'warn'); return; }
      const bytes = await BM.exportGLB(this.project, ms);
      this.download(bytes, (ms.length === 1 ? slug(ms[0].name) : slug(this.project.title)) + '.glb');
    }

    async importGlb(b, asProject) {
      const g = BM.importGLB(b);
      if (!g.models.length) throw new Error('no triangles in this .glb');
      let placed = new Map();
      if (g.bm && g.images[0]) {
        const img = await this.decodeImage(g.images[0].bytes, g.images[0].mime);
        const sh = this.project.sheet;
        const same = img.w === sh.w && img.h === sh.h && img.px.every((v, i) => v === sh.px[i]);
        if (!same) {
          const replace = asProject || BM.imageIsEmpty(sh) ||
            await this.confirm('This .glb comes from bm Studio with its own sprite sheet. Replace the sheet of this project with it? (Otherwise its sheet is added as a picture.)', 'Replace the sheet', 'Add as a picture');
          if (replace) { this.setSheet(img, 'sheet from .glb'); placed.set(0, { x: 0, y: 0, w: img.w, h: img.h }); }
        } else placed.set(0, { x: 0, y: 0, w: img.w, h: img.h });
        if (g.bm.uv_inset !== undefined && asProject) this.project.uvInset = g.bm.uv_inset;
      }
      const used = new Set(), imgs = new Map();
      for (const m of g.models) for (const f of m.faces) if (f.img !== undefined && f.c == null) used.add(f.img);
      for (const i of used) {
        if (placed.has(i)) continue;
        if (!g.images[i]) throw new Error('a texture of the .glb is not inside the file');
        imgs.set(i, await this.decodeImage(g.images[i].bytes, g.images[i].mime));
      }
      let limit = 4096;
      const biggest = Math.max(0, ...[...imgs.values()].map(im => Math.max(im.w, im.h)));
      if (biggest > 256) {
        const r = await this.dialog('Big textures',
          `<p>The .glb has textures up to ${biggest} pixels. The sprite sheet of a bm cartridge is loaded in memory
           and big textures make the .bm large: bm Studio can make them smaller (pixel art looks at home at 128–256).</p>
           <label class="field"><input type="radio" name="lim" value="256" checked> at most 256 pixels</label>
           <label class="field"><input type="radio" name="lim" value="512"> at most 512 pixels</label>
           <label class="field"><input type="radio" name="lim" value="4096"> as they are</label>`,
          [['ok', 'Import', true]], () => +$('input[name=lim]:checked').value);
        limit = r.value || 256;
      }
      for (const i of used) {
        if (placed.has(i)) continue;
        let img = imgs.get(i);
        while (img.w > limit || img.h > limit) img = this.half(img);
        const pos = this.placePicture(img);
        if (!pos) throw new Error(`no room in the sheet for a ${img.w}×${img.h} texture`);
        placed.set(i, { x: pos[0], y: pos[1], w: img.w, h: img.h });
      }
      const models = g.models.map(m => ({
        name: this.uniqueName(m.name),
        faces: E.trisToQuads(m.faces).map(f => {
          const r = f.c == null ? placed.get(f.img) : null;
          return { p: f.p, uv: r ? f.uv.map(t => [r.x + t[0] * r.w, r.y + t[1] * r.h]) : f.uv.map(() => [0, 0]), c: f.c == null ? (r ? null : 0xFF00FF) : f.c };
        }),
      }));
      if (asProject) { this.project.models = models; this.cur = 0; this.modelChanged(); this.refreshAll(); this.view.frame(); this.markDirty(false); }
      else {
        const start = this.project.models.length;
        this.modelsEdit('add models', ms => { ms.push(...models); return start; });
        this.view.frame();
      }
      const b3 = BM.modelBounds(models.flatMap(m => m.faces)), size = b3 ? v3.sub(b3.hi, b3.lo).map(v => +v.toFixed(2)).join(' × ') : '';
      this.status(`${models.length} model${models.length === 1 ? '' : 's'} from the .glb, ${size} units` + (g.warnings.length ? ' — ' + g.warnings.join('; ') : ''), g.warnings.length ? 'warn' : 'good');
    }

    /* half the size: each pixel the average of the solid ones of 2 x 2 */
    half(img) {
      const out = BM.newImage(Math.max(1, img.w >> 1), Math.max(1, img.h >> 1));
      for (let y = 0; y < out.h; y++)
        for (let x = 0; x < out.w; x++) {
          const acc = [0, 0, 0];
          let n = 0;
          for (let j = 0; j < 2; j++)
            for (let i = 0; i < 2; i++) {
              const sx = Math.min(img.w - 1, x * 2 + i), sy = Math.min(img.h - 1, y * 2 + j), s = (sy * img.w + sx) * 4;
              if (img.px[s + 3] < 128) continue;
              acc[0] += img.px[s]; acc[1] += img.px[s + 1]; acc[2] += img.px[s + 2]; n++;
            }
          const d = (y * out.w + x) * 4;
          if (n >= 2) { out.px[d] = acc[0] / n; out.px[d + 1] = acc[1] / n; out.px[d + 2] = acc[2] / n; out.px[d + 3] = 255; }
        }
      return out;
    }

    async importPngDialog(b, name) {
      const img = await this.decodeImage(b);
      const spot = this.freeSpot(img.w, img.h) || [0, 0];
      const choice = await this.dialog(`Add ${esc(name)} (${img.w}×${img.h})`,
        `<label class="field"><input type="radio" name="how" value="paste" checked> into the sheet at
          x <input id="dx" type="number" value="${spot[0]}"> y <input id="dy" type="number" value="${spot[1]}"></label>
         <label class="field"><input type="radio" name="how" value="sheet"> as the whole sheet (it replaces it)</label>
         <label class="field"><input type="radio" name="how" value="cover"> as the cover of the cartridge</label>`,
        [['cancel', 'Cancel'], ['ok', 'Add', true]], () => ({ how: $('input[name=how]:checked').value, x: +$('#dx').value, y: +$('#dy').value }));
      if (!choice || choice.button !== 'ok') return;
      const how = choice.value.how;
      if (how === 'cover') { this.setCover(BM.makeCover(img)); return; }
      if (how === 'sheet') {
        if (img.w > BM.LIMITS.sheet || img.h > BM.LIMITS.sheet) throw new Error('a sheet is at most 4096×4096');
        this.setSheet(img, 'new sheet');
        return;
      }
      const pos = this.placePicture(img, [Math.max(0, choice.value.x | 0), Math.max(0, choice.value.y | 0)]);
      this.S.region = { x: pos[0], y: pos[1], w: img.w, h: img.h };
      this.status(`added at ${pos[0]}, ${pos[1]}`);
    }

    setCover(img) {
      const before = this.project.cover;
      const set = c => () => { this.project.cover = c; this.refreshCart(); this.markDirty(); };
      set(img)();
      this.push({ label: 'cover', undo: set(before), redo: set(img) });
    }

    coverFromView() {
      this.view.clean = true;
      this.view.render();
      this.view.clean = false;
      const c = document.createElement('canvas'), gc = $('#glc');
      c.width = gc.width; c.height = gc.height;
      const g = c.getContext('2d');
      g.drawImage(gc, 0, 0);
      const d = g.getImageData(0, 0, c.width, c.height);
      this.setCover(BM.makeCover({ w: c.width, h: c.height, px: d.data }));
      this.requestRender();
      this.status('cover taken from the 3D view');
    }

    async resizeDialog() {
      const sh = this.project.sheet;
      const r = await this.dialog('Size of the sheet',
        `<p class="dim">The pixels stay where they are (top left); multiples of 8, up to 4096.</p>
         <label class="field">width <input id="nw" type="number" step="8" min="8" max="4096" value="${sh.w}"></label>
         <label class="field">height <input id="nh" type="number" step="8" min="8" max="4096" value="${sh.h}"></label>`,
        [['cancel', 'Cancel'], ['ok', 'Resize', true]], () => ({ w: +$('#nw').value, h: +$('#nh').value }));
      if (!r || r.button !== 'ok') return;
      const w = Math.max(8, Math.min(4096, Math.round(r.value.w / 8) * 8)), h = Math.max(8, Math.min(4096, Math.round(r.value.h / 8) * 8));
      if (w === sh.w && h === sh.h) return;
      this.setSheet(this.resized(sh, w, h), 'sheet size');
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
          if (e.key === 'Enter' && e.target.tagName !== 'TEXTAREA') {
            const prim = buttons.find(b => b[2]);
            if (prim) { e.preventDefault(); e.stopPropagation(); close(prim[0]); }
          }
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

    async confirm(text, yes = 'OK', no = 'Cancel') {
      const r = await this.dialog('bm Studio', `<p>${esc(text)}</p>`, [['cancel', no], ['ok', yes, true]]);
      return r.button === 'ok';
    }

    async askName(title, value, current) {
      for (;;) {
        const r = await this.dialog(title, `<label class="field">name <input id="nm" maxlength="15" value="${esc(value)}" spellcheck="false"></label>
          <p class="dim small">Up to 15 letters; in the game: <code>model("${esc(value)}")</code></p>`, [['cancel', 'Cancel'], ['ok', 'OK', true]], () => $('#nm').value.trim());
        if (r.button !== 'ok' || !r.value) return null;
        const name = r.value;
        if (BM.utf8(name).length > BM.LIMITS.name) { await this.alert('Name too long', 'At most 15 bytes.'); value = name; continue; }
        if (name !== current && this.project.models.some(m => m.name === name)) { await this.alert('Name taken', `There is already a model called "${esc(name)}".`); value = name; continue; }
        return name;
      }
    }

    helpDialog() {
      const rows = [
        ['Right drag', 'turn the camera'], ['Shift + right drag, middle drag', 'move the camera'], ['Wheel', 'zoom'],
        ['Ctrl + wheel, [ ], PgUp/PgDn', 'move the grid (Tile and Block)'], ['P', 'grid: auto → floor → wall ⟂ z → wall ⟂ x'],
        ['Right click', 'erase the face under the mouse (Paint: take its colour)'],
        ['1 2 3 4 5', 'Tile, Block, Select, Vertex, Paint'],
        ['R, Shift+R · F', 'Tile and Block: turn the tile · mirror it'], ['Alt + click', 'Tile and Block: take the tile of a face'],
        ['Arrows, PgUp/PgDn', 'Select and Vertex: move by one square (Shift: one pixel)'],
        ['R, Shift+R · T · M · N', 'Select: turn · tip over · mirror · show the other side'],
        ['Enter · U · + −', 'Select: the chosen tile on them · turn their texture · bigger, smaller'],
        ['M', 'Vertex: merge the corners into one'],
        ['Ctrl+C Ctrl+V Ctrl+D Del', 'copy, paste, duplicate, delete'], ['Ctrl+Z, Ctrl+Y', 'undo, redo'],
        ['Z · Home', 'frame the model · reset the camera'], ['L · K · W · G', 'light · back faces · wireframe · grid'],
        ['Tab', 'switch 3D ↔ Pixel'],
        ['Pixel: B E G I L U M T', 'pencil, eraser, fill, colour, line, rectangle, select, tiles'],
        ['Pixel: H V R · [ ]', 'mirror, flip, turn the selection · brush size'], ['Space + drag', 'move the sheet'],
        ['Ctrl+S · Ctrl+O', 'save the .bm · open'],
      ];
      this.alert('Keys and mouse', '<table>' + rows.map(r => `<tr><td>${esc(r[0])}</td><td>${esc(r[1])}</td></tr>`).join('') + '</table>');
    }

    aboutDialog() {
      this.alert('Using the models in a game', `
        <p>The models are saved in the <b>.bm</b> itself (section MESH), with the sprite sheet as their texture.
        In the cartridge's code:</p>
        <pre>local house
function _init()
  house = model("house")           -- a mesh, as mesh() makes
end
function _draw()
  cls(0x1c2030)
  zclear()
  camera3d(0, 4, -8, 0, -0.4)
  light3d(-0.4, 0.8, -0.5, 0.4)
  draw3d(house, 0, 0, 0, 0, time() * 0.5)
end</pre>
        <p><code>models()</code> lists the names, <code>bounds3d(m)</code> gives the box around a model.
        The origin of the grid goes to the x, y, z of <code>draw3d</code>; one square is one unit.</p>
        <p>bm draws about ${BM.LIMITS.tris60} triangles at 60 fps; a model has at most ${BM.LIMITS.verts} corners.</p>
        <p>To the Pi: copy the .bm to <code>carts/</code> on the SD card (or send it with
        <code>tools/bm_net.py</code>). A new project comes with a model viewer as its code.</p>
        <p>For a game in the repository: export the models (.glb) into <code>carts/&lt;game&gt;/models.glb</code>
        and the sheet into <code>carts/&lt;game&gt;/sheet.png</code>; <code>make</code> packs them.</p>`);
    }

    error(e) {
      console.error(e);
      this.alert('Something went wrong', esc(e && e.message ? e.message : String(e)));
    }

    // ------------------------------------------------------------ panels

    refreshAll() {
      this.refreshModels();
      this.refreshCart();
      this.refreshPalette();
      this.refreshChecks();
      this.refreshPlane();
      this.refreshDoc();
      this.refreshPixelTools();
      this.updateStampInfo();
      this.setColour(this.S.colour);
      $('#tileSize').value = this.S.tileSize;
      $('#brush').value = this.S.brush;
      this.updateStatus();
    }

    refreshDoc() {
      if (this.pngFile) {
        $('#docName').textContent = this.pngFile.name;
        document.title = this.pngFile.name + ' — bm Studio';
        return;
      }
      $('#docName').textContent = this.fileName || (this.project.title ? slug(this.project.title) + '.bm' : 'new project');
      document.title = (this.fileName || this.project.title || 'new project') + ' — bm Studio';
    }

    refreshModels() {
      const ul = $('#modelList');
      ul.innerHTML = '';
      this.project.models.forEach((m, i) => {
        const st = BM.modelStats(m), li = document.createElement('li');
        const bad = st.verts > BM.LIMITS.verts || st.tris > BM.LIMITS.faces;
        li.className = (i === this.cur ? 'on' : '') + (bad ? ' bad' : '');
        li.innerHTML = `<span class="n">${esc(m.name)}</span><span class="i">${st.tris} tri · ${st.verts} vert</span>`;
        li.onclick = () => { if (this.cur !== i) { this.cur = i; this.clearSel(); this.modelChanged(true); this.refreshModels(); } };
        li.ondblclick = () => this.renameModel(i);
        ul.appendChild(li);
      });
    }

    refreshCart() {
      const p = this.project;
      if (document.activeElement !== $('#cartTitle')) $('#cartTitle').value = p.title;
      if (document.activeElement !== $('#cartAuthor')) $('#cartAuthor').value = p.author;
      $('#cartRes').value = p.res;
      if (document.activeElement !== $('#cartLua')) $('#cartLua').value = p.lua;
      if (document.activeElement !== $('#uvInset')) $('#uvInset').value = p.uvInset;
      const g = $('#coverc').getContext('2d');
      g.fillStyle = '#0e1016';
      g.fillRect(0, 0, 128, 80);
      if (p.cover) {
        const c = document.createElement('canvas');
        c.width = p.cover.w; c.height = p.cover.h;
        c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(p.cover.px), p.cover.w, p.cover.h), 0, 0);
        g.drawImage(c, 0, 0, 128, 80);
      } else {
        g.fillStyle = '#707890';
        g.font = '11px sans-serif';
        g.fillText('no cover', 40, 44);
      }
      const secs = ['code ' + (BM.utf8(p.lua).length / 1024).toFixed(1) + ' KiB', `sheet ${p.sheet.w}×${p.sheet.h}`,
        `${p.models.filter(m => m.faces.length).length} models`];
      if (p.map) secs.push('map (kept as it is)');
      if (p.extras.length) secs.push(`${p.extras.length} other sections (kept)`);
      $('#cartInfo').textContent = secs.join(' · ');
      this.refreshDoc();
    }

    syncCart() {
      const p = this.project;
      p.title = $('#cartTitle').value;
      p.author = $('#cartAuthor').value;
      p.res = $('#cartRes').value;
      p.lua = $('#cartLua').value;
      const inset = parseFloat($('#uvInset').value);
      if (inset >= 0 && inset <= 2) p.uvInset = inset;
    }

    refreshPixelTools() {
      $$('#pxtools [data-ptool]').forEach(b => b.classList.toggle('on', b.dataset.ptool === this.S.pxTool));
      $$('#sideTools [data-stool]').forEach(b => b.classList.toggle('on', b.dataset.stool === this.S.sideTool));
      this.requestRender();
    }

    updateStatus() {
      const m = this.model(), st = BM.modelStats(m);
      $('#empty').classList.toggle('on', !m.faces.length && this.S.ws === '3d');
      let cls = '', note = '';
      if (st.verts > BM.LIMITS.verts) { cls = 'bad'; note = ` · too many corners: bm draws at most ${BM.LIMITS.verts}`; }
      else if (st.tris > BM.LIMITS.tris60) { cls = 'warn'; note = ' · heavy for 60 fps on the Pi'; }
      $('#stLeft').innerHTML = `<b>${esc(m.name)}</b> · ${m.faces.length} faces · ${st.tris} triangles · ${st.verts} corners<span class="${cls}">${note}</span>`;
    }

    status(msg, cls) {
      const el = $('#stRight');
      el.textContent = msg;
      el.className = cls || '';
      clearTimeout(this.statusTimer);
      this.statusHold = true;
      this.statusTimer = setTimeout(() => { this.statusHold = false; }, 4000);
    }

    statusRight(msg) {
      if (this.statusHold) return;
      const el = $('#stRight');
      el.textContent = msg;
      el.className = '';
    }

    showBox(x0, y0, x1, y1) {
      const b = $('#boxsel');
      if (x0 == null) { b.style.display = 'none'; return; }
      Object.assign(b.style, { display: 'block', left: Math.min(x0, x1) + 'px', top: Math.min(y0, y1) + 'px',
        width: Math.abs(x1 - x0) + 'px', height: Math.abs(y1 - y0) + 'px' });
    }

    focusView() {
      const a = document.activeElement;
      if (a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' || a.tagName === 'SELECT')) a.blur();
    }

    // ------------------------------------------------------------ wiring

    wire() {
      // menus
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
      $$('[data-cmd]').forEach(b => b.addEventListener('click', e => { e.stopPropagation(); $$('.menu').forEach(x => x.classList.remove('open')); this.cmd(b.dataset.cmd); }));
      $$('[data-tool]').forEach(b => b.addEventListener('click', () => this.setTool(b.dataset.tool)));
      $$('[data-ws]').forEach(b => b.addEventListener('click', () => this.setWorkspace(b.dataset.ws)));
      $$('[data-side]').forEach(b => b.addEventListener('click', () => {
        $$('[data-side]').forEach(x => x.classList.toggle('on', x === b));
        $$('.sidepage').forEach(p => p.classList.toggle('on', p.id === 'side-' + b.dataset.side));
        if (b.dataset.side === 'tiles') this.tiles.resize();
      }));
      $$('[data-stool]').forEach(b => b.addEventListener('click', () => { this.S.sideTool = b.dataset.stool; this.refreshPixelTools(); }));
      $$('[data-ptool]').forEach(b => b.addEventListener('click', () => { this.S.pxTool = b.dataset.ptool; this.refreshPixelTools(); }));
      // grid plane
      $('#planeSel').addEventListener('change', e => { this.S.plane = e.target.value; this.refreshPlane(); this.requestRender(); e.target.blur(); });
      $('#planeUp').addEventListener('click', () => this.movePlane(1));
      $('#planeDown').addEventListener('click', () => this.movePlane(-1));
      // tiles
      $('#tileSize').addEventListener('change', e => {
        this.S.tileSize = +e.target.value;
        this.S.tileSel = { x: 0, y: 0, w: 1, h: 1 };
        savePrefs(this.S);
        this.updateStampInfo();
        this.requestRender();
        e.target.blur();
      });
      $('#stampRot').addEventListener('click', () => { this.S.stampRot = (this.S.stampRot + 1) % 4; this.updateStampInfo(); this.requestRender(); });
      $('#stampFlip').addEventListener('click', () => { this.S.stampFlip = !this.S.stampFlip; this.updateStampInfo(); this.requestRender(); });
      $('#plainColour').addEventListener('change', e => { this.S.plainColour = e.target.checked; this.updateStampInfo(); this.requestRender(); });
      for (const [pick, hex] of [['#colourPick', '#colourHex'], ['#colourPick2', '#colourHex2']]) {
        $(pick).addEventListener('input', e => this.setColour(parseInt(e.target.value.slice(1), 16)));
        $(hex).addEventListener('change', e => {
          const m = e.target.value.trim().match(/^#?([0-9a-f]{6})$/i);
          if (m) this.setColour(parseInt(m[1], 16)); else e.target.value = hex6(this.S.colour);
        });
      }
      $('#brush').addEventListener('change', e => { this.S.brush = +e.target.value; savePrefs(this.S); e.target.blur(); });
      $('#tileGrid').addEventListener('change', e => { this.S.tileGrid = e.target.checked; this.requestRender(); });
      $('#pixelGrid').addEventListener('change', e => { this.S.pixelGrid = e.target.checked; this.requestRender(); });
      // cartridge
      for (const id of ['#cartTitle', '#cartAuthor', '#cartRes', '#cartLua', '#uvInset'])
        $(id).addEventListener('input', () => { this.syncCart(); this.markDirty(); this.refreshDoc(); });
      $('#cartLua').addEventListener('change', () => this.refreshCart());
      $('#cartLua').addEventListener('keydown', e => {
        if (e.key === 'Tab') {
          e.preventDefault();
          const t = e.target, s = t.selectionStart;
          t.value = t.value.slice(0, s) + '  ' + t.value.slice(t.selectionEnd);
          t.selectionStart = t.selectionEnd = s + 2;
          this.syncCart();
        }
      });
      // keys
      document.addEventListener('keydown', e => this.key(e));
      document.addEventListener('keyup', e => { if (e.code === 'Space') this.spaceHeld = false; });
      // drop files
      document.addEventListener('dragover', e => { e.preventDefault(); document.body.classList.add('dropping'); });
      document.addEventListener('dragleave', e => { if (!e.relatedTarget) document.body.classList.remove('dropping'); });
      document.addEventListener('drop', async e => {
        e.preventDefault();
        document.body.classList.remove('dropping');
        const file = e.dataTransfer.files[0];
        if (!file) return;
        // the handle (to save the .bm where it is) only while the event lasts
        const item = e.dataTransfer.items && e.dataTransfer.items[0];
        const handleP = item && item.getAsFileSystemHandle ? item.getAsFileSystemHandle().catch(() => null) : Promise.resolve(null);
        const b = new Uint8Array(await file.arrayBuffer()), ext = (file.name.match(/\.[^.]+$/) || [''])[0].toLowerCase();
        try {
          if (ext === '.glb') {
            if (await this.confirmNewOrAdd()) await this.openBytes(b, file.name, null);
            else await this.importGlb(b);
          } else if (ext === '.png') await this.importPngDialog(b, file.name);
          else await this.openBytes(b, file.name, await handleP);
        } catch (err) { this.error(err); }
      });
    }

    async confirmNewOrAdd() {
      return this.confirm('Open this .glb as a new project, or add its models to this one?', 'New project', 'Add to this one');
    }

    key(e) {
      const t = e.target, typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT');
      const ctrl = e.ctrlKey || e.metaKey, k = e.key, S = this.S;
      if (!$('#modal').hidden) return;
      if (ctrl && !e.altKey) {
        const map = { s: e.shiftKey ? 'saveAs' : 'save', o: 'open', z: e.shiftKey ? 'redo' : 'undo', y: 'redo' };
        const lk = k.toLowerCase();
        if (map[lk] && !(typing && (lk === 'z' || lk === 'y'))) { e.preventDefault(); this.cmd(map[lk]); return; }
        if (typing) return;
        const more = { a: 'selectAll', c: 'copy', v: 'paste', d: 'duplicate', x: 'cut' };
        if (more[lk]) { e.preventDefault(); this.cmd(more[lk]); }
        return;
      }
      if (ctrl && e.altKey && k.toLowerCase() === 'n') { e.preventDefault(); this.cmd('new'); return; }
      if (typing) return;
      if (k === 'F1') { e.preventDefault(); this.cmd('help'); return; }
      if (k === 'Tab') { e.preventDefault(); this.setWorkspace(S.ws === '3d' ? 'pixel' : '3d'); return; }
      if (e.code === 'Space') { this.spaceHeld = true; e.preventDefault(); return; }
      if (S.ws === 'pixel') { this.pixelKey(e); return; }

      const lower = k.length === 1 ? k.toLowerCase() : k, tool = S.tool;
      const tools = { 1: 'tile', 2: 'block', 3: 'select', 4: 'vertex', 5: 'paint' };
      if (tools[k]) { this.setTool(tools[k]); return; }
      if (k === 'Escape') { this.clearSel(); return; }
      if (k === 'Delete' || k === 'Backspace') { e.preventDefault(); this.cmd('delete'); return; }
      if (k === 'Home') { this.cmd('resetCam'); return; }
      if (lower === 'z' || k === '.') { this.cmd('frame'); return; }
      if (lower === 'l') { this.cmd('toggleLit'); return; }
      if (lower === 'k') { this.cmd('toggleCull'); return; }
      if (lower === 'w') { this.cmd('toggleWire'); return; }
      if (lower === 'g') { this.cmd('toggleGrid'); return; }
      if (lower === 'p') {
        const order = ['auto', 'floor', 'z', 'x'];
        S.plane = order[(order.indexOf(S.plane) + 1) % 4];
        this.refreshPlane();
        this.status('grid: ' + { auto: 'auto (follows the camera)', floor: 'floor', z: 'wall ⟂ z', x: 'wall ⟂ x' }[S.plane]);
        this.view.hover = null;
        this.requestRender();
        return;
      }
      if (tool === 'tile' || tool === 'block' || tool === 'paint') {
        if (k === '[' || k === 'PageDown') { e.preventDefault(); this.movePlane(-1); return; }
        if (k === ']' || k === 'PageUp') { e.preventDefault(); this.movePlane(1); return; }
        if (lower === 'r') { S.stampRot = (S.stampRot + (e.shiftKey ? 3 : 1)) % 4; this.updateStampInfo(); this.requestRender(); return; }
        if (lower === 'f') { S.stampFlip = !S.stampFlip; this.updateStampInfo(); this.requestRender(); return; }
        return;
      }
      // Select and Vertex
      const ax = this.view.keyAxes(), st = e.shiftKey ? 1 / S.tileSize : S.snap;
      const moves = { ArrowRight: ax.right, ArrowLeft: v3.scale(ax.right, -1), ArrowUp: ax.fwd, ArrowDown: v3.scale(ax.fwd, -1), PageUp: ax.up, PageDown: v3.scale(ax.up, -1) };
      if (moves[k]) { e.preventDefault(); this.moveSel(v3.scale(moves[k], st)); return; }
      if (tool === 'vertex') { if (lower === 'm') this.cmd('merge'); return; }
      const cmds = { r: e.shiftKey ? 'turnLeft' : 'turnRight', t: 'tip', m: 'mirror', n: 'flipSide', u: 'turnUV', '+': 'grow', '=': 'grow', '-': 'shrink', Enter: 'retexture' };
      if (cmds[lower]) { e.preventDefault(); this.cmd(cmds[lower]); }
    }

    pixelKey(e) {
      const S = this.S, lower = e.key.length === 1 ? e.key.toLowerCase() : e.key;
      const tools = { b: 'pencil', e: 'eraser', g: 'fill', i: 'picker', l: 'line', u: 'rect', m: 'select', t: 'tiles' };
      if (tools[lower]) { S.pxTool = tools[lower]; this.refreshPixelTools(); return; }
      if (lower === 'Escape') { S.floating = null; S.region = null; this.requestRender(); return; }
      if (lower === 'Delete' || lower === 'Backspace') { this.cmd('delete'); return; }
      if (lower === '+' || lower === '=') { this.pixel.zoomBy(1); return; }
      if (lower === '-') { this.pixel.zoomBy(-1); return; }
      if (lower === '0') { this.pixel.fit(); return; }
      if (lower === 'h') { this.transformRegion('h'); return; }
      if (lower === 'v') { this.transformRegion('v'); return; }
      if (lower === 'r') { this.transformRegion('rot'); return; }
      if (lower === '[' || lower === ']') {
        S.brush = Math.max(1, Math.min(4, S.brush + (lower === ']' ? 1 : -1)));
        $('#brush').value = S.brush;
        this.requestRender();
      }
    }
  }

  window.addEventListener('DOMContentLoaded', () => {
    try {
      window.app = new App();
    } catch (e) {
      document.body.innerHTML = `<p style="padding:20px">bm Studio cannot start: ${esc(e.message)}</p>`;
      throw e;
    }
  });
})();
