/*
 * bm Studio - the sprite sheet on a canvas: the tile picker beside the 3D
 * view and the pixel editor (the same widget, bigger, with more tools).
 * Wheel: zoom; right or middle drag (or Space + drag): move the view;
 * right click: take the colour under the mouse.
 */
(function (root) {
  'use strict';
  const BM = root.BM;
  const ZOOMS = [1, 1.25, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48];

  class SheetView {
    constructor(app, canvas, opts) {
      this.app = app;
      this.canvas = canvas;
      this.ctx = canvas.getContext('2d');
      this.full = !!opts.full;              // the Pixel page (else the tile picker)
      this.zoom = this.full ? 4 : 2;
      this.px = 8; this.py = 8;             // where sheet pixel (0, 0) is, in canvas pixels
      this.drag = null;
      this.mouse = null;
      this.fitted = false;
      canvas.addEventListener('pointerdown', e => this.down(e));
      canvas.addEventListener('pointermove', e => this.move(e));
      canvas.addEventListener('pointerup', e => this.up(e));
      canvas.addEventListener('pointercancel', e => this.up(e));
      canvas.addEventListener('pointerleave', () => { this.mouse = null; this.app.requestRender(); });
      canvas.addEventListener('wheel', e => this.wheel(e), { passive: false });
      canvas.addEventListener('contextmenu', e => e.preventDefault());
      new ResizeObserver(() => this.resize()).observe(canvas.parentElement);
      this.resize();
    }

    get S() { return this.app.S; }
    tool() { return this.full ? this.S.pxTool : this.S.sideTool; }
    sheet() { return this.app.project.sheet; }

    resize() {
      const p = this.canvas.parentElement, dpr = window.devicePixelRatio || 1;
      const w = Math.max(1, p.clientWidth), h = Math.max(1, p.clientHeight);
      this.canvas.width = Math.round(w * dpr);
      this.canvas.height = Math.round(h * dpr);
      this.canvas.style.width = w + 'px';
      this.canvas.style.height = h + 'px';
      this.dpr = dpr;
      if (!this.fitted && w > 20) { this.fit(); this.fitted = true; }
      this.app.requestRender();
    }

    fit() {
      const s = this.sheet(), w = this.canvas.width / this.dpr - 16, h = this.canvas.height / this.dpr - 16;
      let z = ZOOMS[0];
      // the pixel editor keeps whole pixels (an integer zoom); the tile picker fills its width
      for (const k of ZOOMS)
        if (s.w * k <= Math.max(w, s.w) && (!this.full || (k % 1 === 0 && s.h * k <= Math.max(h, s.h)))) z = k;
      this.zoom = z;
      this.px = 8; this.py = 8;
      if (this.full) {
        this.px = Math.max(8, Math.round((w + 16 - s.w * z) / 2));
        this.py = Math.max(8, Math.round((h + 16 - s.h * z) / 2));
      }
      this.app.requestRender();
    }

    at(e) {
      const b = this.canvas.getBoundingClientRect(), x = e.clientX - b.left, y = e.clientY - b.top;
      return { x, y, sx: Math.floor((x - this.px) / this.zoom), sy: Math.floor((y - this.py) / this.zoom) };
    }

    inside(sx, sy) { const s = this.sheet(); return sx >= 0 && sy >= 0 && sx < s.w && sy < s.h; }

    wheel(e) {
      e.preventDefault();
      const m = this.at(e), i = ZOOMS.indexOf(this.zoom);
      const nz = ZOOMS[Math.max(0, Math.min(ZOOMS.length - 1, (i < 0 ? 3 : i) + (e.deltaY < 0 ? 1 : -1)))];
      if (nz === this.zoom) return;
      const fx = (m.x - this.px) / this.zoom, fy = (m.y - this.py) / this.zoom;
      this.zoom = nz;
      this.px = Math.round(m.x - fx * nz);
      this.py = Math.round(m.y - fy * nz);
      this.app.requestRender();
    }

    zoomBy(k) {
      const i = ZOOMS.indexOf(this.zoom), c = this.canvas.width / this.dpr / 2, d = this.canvas.height / this.dpr / 2;
      const nz = ZOOMS[Math.max(0, Math.min(ZOOMS.length - 1, (i < 0 ? 3 : i) + k))];
      const fx = (c - this.px) / this.zoom, fy = (d - this.py) / this.zoom;
      this.zoom = nz;
      this.px = Math.round(c - fx * nz);
      this.py = Math.round(d - fy * nz);
      this.app.requestRender();
    }

    tileAt(sx, sy) {
      const T = this.S.tileSize;
      return [Math.floor(sx / T), Math.floor(sy / T)];
    }

    down(e) {
      this.canvas.setPointerCapture(e.pointerId);
      const m = this.at(e), tool = this.tool();
      this.mouse = m;
      if (e.button === 1 || e.button === 2 || this.app.spaceHeld) {
        this.drag = { kind: 'pan', x: e.clientX, y: e.clientY, moved: false, button: e.button, m };
        return;
      }
      if (e.button !== 0) return;
      if (this.S.floating && this.full) { this.app.dropFloating(m.sx, m.sy); return; }
      if (tool === 'tiles') {
        const [tx, ty] = this.tileAt(m.sx, m.sy);
        if (!this.inside(m.sx, m.sy)) return;
        this.drag = { kind: 'tiles', tx, ty };
        this.app.setTileSel({ x: tx, y: ty, w: 1, h: 1 });
        return;
      }
      if (tool === 'picker') { this.app.pickFromSheet(m.sx, m.sy); return; }
      if (tool === 'fill') { if (this.inside(m.sx, m.sy)) this.app.floodFill(m.sx, m.sy, e.shiftKey ? null : this.app.colour()); return; }
      if (tool === 'pencil' || tool === 'eraser') {
        this.app.beginStroke();
        this.drag = { kind: 'draw', last: [m.sx, m.sy], erase: tool === 'eraser' };
        this.dot(m.sx, m.sy, tool === 'eraser');
        return;
      }
      if (tool === 'line' || tool === 'rect') { this.drag = { kind: tool, x0: m.sx, y0: m.sy, x1: m.sx, y1: m.sy, fill: e.shiftKey }; this.app.requestRender(); return; }
      if (tool === 'select') {
        const r = this.S.region;
        if (r && m.sx >= r.x && m.sy >= r.y && m.sx < r.x + r.w && m.sy < r.y + r.h) {
          this.app.liftRegion(m.sx - r.x, m.sy - r.y, e.altKey);    // drag the pixels away (Alt: a copy)
          return;
        }
        this.drag = { kind: 'region', x0: m.sx, y0: m.sy };
        this.S.region = null;
        this.app.requestRender();
      }
    }

    move(e) {
      const m = this.at(e), d = this.drag;
      this.mouse = m;
      this.app.sheetHover(this.inside(m.sx, m.sy) ? [m.sx, m.sy] : null);
      if (!d) { this.app.requestRender(); return; }
      if (d.kind === 'pan') {
        if (Math.abs(e.clientX - d.x) + Math.abs(e.clientY - d.y) > 2) d.moved = true;
        this.px += e.clientX - d.x; this.py += e.clientY - d.y;
        d.x = e.clientX; d.y = e.clientY;
      } else if (d.kind === 'tiles') {
        const [tx, ty] = this.tileAt(m.sx, m.sy), s = this.sheet(), T = this.S.tileSize;
        const cx = Math.max(0, Math.min(Math.floor(s.w / T) - 1, tx)), cy = Math.max(0, Math.min(Math.floor(s.h / T) - 1, ty));
        this.app.setTileSel({ x: Math.min(d.tx, cx), y: Math.min(d.ty, cy), w: Math.abs(cx - d.tx) + 1, h: Math.abs(cy - d.ty) + 1 });
      } else if (d.kind === 'draw') {
        this.lineTo(d.last[0], d.last[1], m.sx, m.sy, p => this.dot(p[0], p[1], d.erase));
        d.last = [m.sx, m.sy];
      } else if (d.kind === 'line' || d.kind === 'rect') {
        d.x1 = m.sx; d.y1 = m.sy;
        if (d.kind === 'line' && e.shiftKey) {            // straight or 45 degrees
          const dx = d.x1 - d.x0, dy = d.y1 - d.y0;
          if (Math.abs(dx) > 2 * Math.abs(dy)) d.y1 = d.y0;
          else if (Math.abs(dy) > 2 * Math.abs(dx)) d.x1 = d.x0;
          else { const k = Math.max(Math.abs(dx), Math.abs(dy)); d.x1 = d.x0 + Math.sign(dx) * k; d.y1 = d.y0 + Math.sign(dy) * k; }
        }
      } else if (d.kind === 'region') {
        const s = this.sheet(), c = (v, n) => Math.max(0, Math.min(n - 1, v));
        const x1 = c(m.sx, s.w), y1 = c(m.sy, s.h), x0 = c(d.x0, s.w), y0 = c(d.y0, s.h);
        this.S.region = { x: Math.min(x0, x1), y: Math.min(y0, y1), w: Math.abs(x1 - x0) + 1, h: Math.abs(y1 - y0) + 1 };
      }
      this.app.requestRender();
    }

    up(e) {
      const d = this.drag;
      this.drag = null;
      if (!d) return;
      if (d.kind === 'pan' && !d.moved && d.button === 2) { this.app.pickFromSheet(d.m.sx, d.m.sy); return; }
      if (d.kind === 'draw') this.app.endStroke(d.erase ? 'erase' : 'pencil');
      if (d.kind === 'line' || d.kind === 'rect') {
        this.app.beginStroke();
        const c = this.app.colour();
        if (d.kind === 'line') this.lineTo(d.x0, d.y0, d.x1, d.y1, p => this.dot(p[0], p[1], false));
        else {
          const x0 = Math.min(d.x0, d.x1), x1 = Math.max(d.x0, d.x1), y0 = Math.min(d.y0, d.y1), y1 = Math.max(d.y0, d.y1);
          for (let y = y0; y <= y1; y++)
            for (let x = x0; x <= x1; x++)
              if (d.fill || e.shiftKey || x === x0 || x === x1 || y === y0 || y === y1) this.app.paintPixel(x, y, c);
        }
        this.app.endStroke(d.kind);
      }
      this.app.requestRender();
    }

    dot(x, y, erase) {
      const b = this.S.brush || 1, o = Math.floor((b - 1) / 2), c = erase ? null : this.app.colour();
      for (let j = 0; j < b; j++) for (let i = 0; i < b; i++) this.app.paintPixel(x - o + i, y - o + j, c);
    }

    lineTo(x0, y0, x1, y1, fn) {
      const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
      let err = dx + dy;
      for (;;) {
        fn([x0, y0]);
        if (x0 === x1 && y0 === y1) break;
        const e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
    }

    render() {
      const ctx = this.ctx, s = this.sheet(), z = this.zoom, dpr = this.dpr;
      const W = this.canvas.width / dpr, H = this.canvas.height / dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = '#12141b';
      ctx.fillRect(0, 0, W, H);
      const x0 = this.px, y0 = this.py, sw = s.w * z, sh = s.h * z;
      // transparent pixels: a checker board
      if (!this.checker) {
        const c = document.createElement('canvas');
        c.width = c.height = 16;
        const g = c.getContext('2d');
        g.fillStyle = '#2a2d3a'; g.fillRect(0, 0, 16, 16);
        g.fillStyle = '#343848'; g.fillRect(0, 0, 8, 8); g.fillRect(8, 8, 8, 8);
        this.checker = ctx.createPattern(c, 'repeat');
      }
      ctx.save();
      ctx.translate(x0, y0);
      ctx.fillStyle = this.checker;
      ctx.fillRect(0, 0, sw, sh);
      ctx.restore();
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(this.app.sheetCanvas, x0, y0, sw, sh);
      const T = this.S.tileSize;
      const lines = (step, colour) => {
        ctx.beginPath();
        for (let x = 0; x <= s.w; x += step) { ctx.moveTo(x0 + x * z + 0.5, y0); ctx.lineTo(x0 + x * z + 0.5, y0 + sh); }
        for (let y = 0; y <= s.h; y += step) { ctx.moveTo(x0, y0 + y * z + 0.5); ctx.lineTo(x0 + sw, y0 + y * z + 0.5); }
        ctx.strokeStyle = colour;
        ctx.lineWidth = 1;
        ctx.stroke();
      };
      if (this.S.pixelGrid && z >= 8) lines(1, 'rgba(255,255,255,0.07)');
      if (this.S.tileGrid && T * z >= 6) lines(T, 'rgba(255,255,255,0.22)');
      ctx.strokeStyle = 'rgba(255,255,255,0.35)';
      ctx.strokeRect(x0 - 0.5, y0 - 0.5, sw + 1, sh + 1);

      const box = (x, y, w, h, colour, width = 2, dash) => {
        ctx.save();
        ctx.strokeStyle = colour;
        ctx.lineWidth = width;
        if (dash) ctx.setLineDash(dash);
        ctx.strokeRect(x0 + x * z, y0 + y * z, w * z, h * z);
        ctx.restore();
      };
      const sel = this.S.tileSel, tool = this.tool();
      if (sel && (!this.full || tool === 'tiles')) {
        box(sel.x * T - 1 / z, sel.y * T - 1 / z, sel.w * T + 2 / z, sel.h * T + 2 / z, '#11131a', 4);
        box(sel.x * T, sel.y * T, sel.w * T, sel.h * T, this.S.plainColour ? '#8890a8' : '#ffc050', 2);
      }
      const m = this.mouse, d = this.drag;
      if (this.S.region && this.full) box(this.S.region.x, this.S.region.y, this.S.region.w, this.S.region.h, '#7ac8ff', 1, [4, 3]);
      if (this.S.floating && this.full && m) {
        const f = this.S.floating;
        const fx = m.sx - f.hx, fy = m.sy - f.hy;
        if (!f.canvas) {
          f.canvas = document.createElement('canvas');
          f.canvas.width = f.img.w; f.canvas.height = f.img.h;
          f.canvas.getContext('2d').putImageData(new ImageData(f.img.px, f.img.w, f.img.h), 0, 0);
        }
        ctx.globalAlpha = 0.85;
        ctx.drawImage(f.canvas, x0 + fx * z, y0 + fy * z, f.img.w * z, f.img.h * z);
        ctx.globalAlpha = 1;
        box(fx, fy, f.img.w, f.img.h, '#ffc050', 1, [4, 3]);
      }
      if (d && (d.kind === 'line' || d.kind === 'rect')) {
        ctx.fillStyle = this.app.colourCss();
        if (d.kind === 'line') this.lineTo(d.x0, d.y0, d.x1, d.y1, p => ctx.fillRect(x0 + p[0] * z, y0 + p[1] * z, z, z));
        else {
          const ax = Math.min(d.x0, d.x1), ay = Math.min(d.y0, d.y1), bx = Math.max(d.x0, d.x1), by = Math.max(d.y0, d.y1);
          if (d.fill) ctx.fillRect(x0 + ax * z, y0 + ay * z, (bx - ax + 1) * z, (by - ay + 1) * z);
          else box(ax, ay, bx - ax + 1, by - ay + 1, this.app.colourCss(), Math.max(1, z));
        }
      }
      if (m && this.inside(m.sx, m.sy) && !(d && d.kind === 'pan')) {
        if (tool === 'tiles') {
          const [tx, ty] = this.tileAt(m.sx, m.sy);
          box(tx * T, ty * T, T, T, 'rgba(255,255,255,0.6)', 1);
        } else if (!this.S.floating) {
          const b = tool === 'pencil' || tool === 'eraser' ? this.S.brush || 1 : 1, o = Math.floor((b - 1) / 2);
          box(m.sx - o, m.sy - o, b, b, 'rgba(255,255,255,0.8)', 1);
        }
      }
    }
  }

  BM.SheetView = SheetView;
})(typeof window !== 'undefined' ? window : globalThis);
