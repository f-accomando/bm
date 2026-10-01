/*
 * bm Studio - WebGL renderer of the 3D view. The camera works like bm's
 * (src/bm/r3d.c): x right, y up, z away from the camera, the side of a
 * face that shows is clockwise on screen; so what the view hides is what
 * the console hides.
 */
(function (root) {
  'use strict';
  const BM = root.BM;

  const MESH_VS = `
attribute vec3 a_pos; attribute vec3 a_nrm; attribute vec2 a_uv; attribute vec4 a_col;
uniform mat4 u_mvp;
varying vec3 v_nrm; varying vec2 v_uv; varying vec4 v_col;
void main() { v_nrm = a_nrm; v_uv = a_uv; v_col = a_col; gl_Position = u_mvp * vec4(a_pos, 1.0); }`;
  const MESH_FS = `
precision mediump float;
uniform sampler2D u_tex; uniform vec2 u_texSize;
uniform vec3 u_light; uniform float u_ambient; uniform float u_lit; uniform float u_565;
uniform vec4 u_tint; uniform float u_alpha;
varying vec3 v_nrm; varying vec2 v_uv; varying vec4 v_col;
void main() {
  vec3 c;
  if (v_col.a > 0.5) {
    vec4 t = texture2D(u_tex, v_uv / u_texSize);
    if (t.a < 0.5) discard;
    c = t.rgb;
  } else c = v_col.rgb;
  float k = 1.0;
  if (u_lit > 0.5) k = min(1.0, u_ambient + (1.0 - u_ambient) * max(0.0, dot(normalize(v_nrm), u_light)));
  c *= k;
  if (u_565 > 0.5) c = vec3(floor(c.r * 31.0 + 0.5) / 31.0, floor(c.g * 63.0 + 0.5) / 63.0, floor(c.b * 31.0 + 0.5) / 31.0);
  c = mix(c, u_tint.rgb, u_tint.a);
  gl_FragColor = vec4(c, u_alpha);
}`;
  const LINE_VS = `
attribute vec3 a_pos; attribute vec4 a_col;
uniform mat4 u_mvp; uniform float u_size;
varying vec4 v_col;
void main() { v_col = a_col; gl_Position = u_mvp * vec4(a_pos, 1.0); gl_PointSize = u_size; }`;
  const LINE_FS = `
precision mediump float;
varying vec4 v_col;
void main() { gl_FragColor = v_col; }`;

  function compile(gl, vs, fs) {
    const p = gl.createProgram();
    for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      gl.attachShader(p, s);
    }
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    const info = { p, a: {}, u: {} };
    const na = gl.getProgramParameter(p, gl.ACTIVE_ATTRIBUTES), nu = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < na; i++) { const n = gl.getActiveAttrib(p, i).name; info.a[n] = gl.getAttribLocation(p, n); }
    for (let i = 0; i < nu; i++) { const n = gl.getActiveUniform(p, i).name; info.u[n] = gl.getUniformLocation(p, n); }
    return info;
  }

  /* camera: orbit around a target. yaw 0 looks along +z, pitch > 0 looks
   * down (the opposite of camera3d's sign) */
  class Camera {
    constructor() { this.reset(); }
    reset() { this.target = [0, 0.5, 0]; this.yaw = 0.65; this.pitch = 0.55; this.dist = 9; this.fov = 50; }
    basis() {
      const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch), cy = Math.cos(this.yaw), sy = Math.sin(this.yaw);
      const f = [cp * sy, -sp, cp * cy];
      const r = BM.v3.norm(BM.v3.cross([0, 1, 0], f));
      const u = BM.v3.cross(f, r);
      const eye = BM.v3.sub(this.target, BM.v3.scale(f, this.dist));
      return { f, r, u, eye };
    }
    /* column-major matrix for gl: clip = P * V * p */
    matrix(aspect) {
      const { f, r, u, eye } = this.basis();
      const t = 1 / Math.tan(this.fov * Math.PI / 360), near = Math.max(0.02, this.dist * 0.005), far = this.dist * 60 + 200;
      const fx = t / aspect, fy = t, a = (far + near) / (far - near), b = -2 * far * near / (far - near);
      const tx = -BM.v3.dot(r, eye), ty = -BM.v3.dot(u, eye), tz = -BM.v3.dot(f, eye);
      this.last = { f, r, u, eye, fx, fy, aspect };
      return new Float32Array([
        fx * r[0], fy * u[0], a * f[0], f[0],
        fx * r[1], fy * u[1], a * f[1], f[1],
        fx * r[2], fy * u[2], a * f[2], f[2],
        fx * tx, fy * ty, a * tz + b, tz,
      ]);
    }
    /* ray through a point of the view (-1..1, y up) */
    ray(nx, ny) {
      const L = this.last;
      return { o: L.eye, d: BM.v3.norm(BM.v3.add(L.f, BM.v3.add(BM.v3.scale(L.r, nx / L.fx), BM.v3.scale(L.u, ny / L.fy)))) };
    }
    /* world -> view pixels, or null behind the camera */
    project(p, w, h) {
      const L = this.last, d = BM.v3.sub(p, L.eye), z = BM.v3.dot(d, L.f);
      if (z < 1e-4) return null;
      return [(BM.v3.dot(d, L.r) * L.fx / z * 0.5 + 0.5) * w, (0.5 - BM.v3.dot(d, L.u) * L.fy / z * 0.5) * h, z];
    }
  }

  class Renderer {
    constructor(canvas) {
      const opts = { antialias: false, preserveDrawingBuffer: true, alpha: false };
      const gl = canvas.getContext('webgl', opts) || canvas.getContext('experimental-webgl', opts);
      if (!gl) throw new Error('WebGL is not available in this browser');
      this.gl = gl;
      this.canvas = canvas;
      this.mesh = compile(gl, MESH_VS, MESH_FS);
      this.line = compile(gl, LINE_VS, LINE_FS);
      this.tex = gl.createTexture();
      this.texW = 1; this.texH = 1;
      gl.bindTexture(gl.TEXTURE_2D, this.tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 0, 255, 255]));
      for (const [k, v] of [[gl.TEXTURE_MIN_FILTER, gl.NEAREST], [gl.TEXTURE_MAG_FILTER, gl.NEAREST],
        [gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE], [gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE]]) gl.texParameteri(gl.TEXTURE_2D, k, v);
      this.buffers = new Map();
      this.light = BM.v3.norm([-0.4, 0.8, -0.5]);
    }

    /* the sheet, whole or the rectangle that changed */
    uploadSheet(img, rect) {
      const gl = this.gl;
      gl.bindTexture(gl.TEXTURE_2D, this.tex);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      const px = new Uint8Array(img.px.buffer, img.px.byteOffset, img.px.byteLength);
      if (!rect || img.w !== this.texW || img.h !== this.texH) {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, img.w, img.h, 0, gl.RGBA, gl.UNSIGNED_BYTE, px);
        this.texW = img.w; this.texH = img.h;
        return;
      }
      const [x0, y0, w, h] = rect;
      if (w <= 0 || h <= 0) return;
      const sub = new Uint8Array(w * h * 4);
      for (let y = 0; y < h; y++) sub.set(px.subarray(((y0 + y) * img.w + x0) * 4, ((y0 + y) * img.w + x0 + w) * 4), y * w * 4);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, x0, y0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, sub);
    }

    /* faces -> a vertex buffer: 12 floats per vertex (pos, normal, uv, rgba) */
    faceBuffer(key, faces, extra) {
      const gl = this.gl;
      let n = 0;
      for (const f of faces) n += f.p.length === 4 ? 6 : 3;
      const data = new Float32Array(n * 12);
      let o = 0;
      for (const f of faces) {
        const nr = BM.faceNormal(f), tris = f.p.length === 4 ? [0, 1, 2, 0, 2, 3] : [0, 1, 2];
        const tex = f.c == null ? 1 : 0, c = f.c == null ? 0xFFFFFF : f.c;
        const r = (c >> 16 & 255) / 255, g = (c >> 8 & 255) / 255, b = (c & 255) / 255;
        for (const k of tris) {
          const p = f.p[k], uv = f.uv[k];
          data.set([p[0], p[1], p[2], nr[0], nr[1], nr[2], uv[0], uv[1], r, g, b, tex], o);
          o += 12;
        }
      }
      let buf = this.buffers.get(key);
      if (!buf) { buf = { vbo: gl.createBuffer() }; this.buffers.set(key, buf); }
      gl.bindBuffer(gl.ARRAY_BUFFER, buf.vbo);
      gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
      buf.count = n;
      Object.assign(buf, extra || {});
      return buf;
    }

    lineBuffer(key, verts, mode) {
      const gl = this.gl;
      let buf = this.buffers.get(key);
      if (!buf) { buf = { vbo: gl.createBuffer() }; this.buffers.set(key, buf); }
      gl.bindBuffer(gl.ARRAY_BUFFER, buf.vbo);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.DYNAMIC_DRAW);
      buf.count = verts.length / 7;
      buf.mode = mode;
      return buf;
    }

    begin(bg, camera) {
      const gl = this.gl, c = this.canvas;
      gl.viewport(0, 0, c.width, c.height);
      gl.clearColor((bg >> 16 & 255) / 255, (bg >> 8 & 255) / 255, (bg & 255) / 255, 1);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LEQUAL);
      gl.frontFace(gl.CW);
      this.mvp = camera.matrix(c.width / Math.max(1, c.height));
    }

    drawFaces(buf, o) {
      if (!buf || !buf.count) return;
      const gl = this.gl, P = this.mesh;
      gl.useProgram(P.p);
      gl.bindBuffer(gl.ARRAY_BUFFER, buf.vbo);
      const attr = (name, size, off) => {
        const loc = P.a[name];
        if (loc === undefined || loc < 0) return;
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 48, off * 4);
      };
      attr('a_pos', 3, 0); attr('a_nrm', 3, 3); attr('a_uv', 2, 6); attr('a_col', 4, 8);
      gl.uniformMatrix4fv(P.u.u_mvp, false, this.mvp);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, this.tex);
      gl.uniform1i(P.u.u_tex, 0);
      gl.uniform2f(P.u.u_texSize, this.texW, this.texH);
      gl.uniform3fv(P.u.u_light, this.light);
      gl.uniform1f(P.u.u_ambient, o.ambient === undefined ? 0.4 : o.ambient);
      gl.uniform1f(P.u.u_lit, o.lit ? 1 : 0);
      gl.uniform1f(P.u.u_565, o.rgb565 ? 1 : 0);
      gl.uniform4fv(P.u.u_tint, o.tint || [0, 0, 0, 0]);
      gl.uniform1f(P.u.u_alpha, o.alpha === undefined ? 1 : o.alpha);
      if (o.cull) { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); } else gl.disable(gl.CULL_FACE);
      if (o.alpha !== undefined && o.alpha < 1) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false); }
      if (o.offset) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(-1, -2); }
      if (o.noDepth) gl.disable(gl.DEPTH_TEST);
      gl.drawArrays(gl.TRIANGLES, 0, buf.count);
      gl.enable(gl.DEPTH_TEST);
      gl.disable(gl.POLYGON_OFFSET_FILL);
      gl.disable(gl.BLEND);
      gl.depthMask(true);
      gl.disableVertexAttribArray(P.a.a_nrm);
      gl.disableVertexAttribArray(P.a.a_uv);
    }

    /* verts: x, y, z, r, g, b, a per vertex */
    drawLines(buf, o = {}) {
      if (!buf || !buf.count) return;
      const gl = this.gl, P = this.line;
      gl.useProgram(P.p);
      gl.bindBuffer(gl.ARRAY_BUFFER, buf.vbo);
      gl.enableVertexAttribArray(P.a.a_pos);
      gl.vertexAttribPointer(P.a.a_pos, 3, gl.FLOAT, false, 28, 0);
      gl.enableVertexAttribArray(P.a.a_col);
      gl.vertexAttribPointer(P.a.a_col, 4, gl.FLOAT, false, 28, 12);
      gl.uniformMatrix4fv(P.u.u_mvp, false, this.mvp);
      gl.uniform1f(P.u.u_size, o.size || 1);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      if (o.onTop) gl.disable(gl.DEPTH_TEST);
      gl.depthMask(false);
      gl.drawArrays(buf.mode === 'points' ? gl.POINTS : gl.LINES, 0, buf.count);
      gl.depthMask(true);
      gl.enable(gl.DEPTH_TEST);
      gl.disable(gl.BLEND);
    }
  }

  BM.Camera = Camera;
  BM.Renderer = Renderer;
})(typeof window !== 'undefined' ? window : globalThis);
