/*
 * bm Animator - a model with its skeleton and animations as glTF (.glb):
 * joints, skin (each vertex with one joint, weight 1) and one animation per
 * clip, for Blender or the Windows 3D viewer.
 *
 * bm moves a vertex by M = M_parent T(head + t) R(q) T(-head); in glTF a
 * joint's local transform is then translation head - head_parent + t and
 * rotation q, with inverse bind matrices T(-head). glTF is right handed:
 * z flips, so (x, y, z) -> (x, y, -z), q -> (-qx, -qy, qz, qw) and every
 * triangle goes the other way round.
 */
(function (root) {
  'use strict';
  const BM = root.BM, R = BM.rig;

  const P = p => [p[0], p[1], -p[2]];
  const QC = q => [-q[0], -q[1], q[2], q[3]];

  async function exportAnimatedGLB(project, model) {
    const rig = model.rig && model.rig.bones.length ? model.rig : null;
    if (!rig) return BM.exportGLB(project, [model]);
    const nb = rig.bones.length, bin = new BM.Writer(1 << 16);
    const js = {
      asset: { version: '2.0', generator: 'bm Animator', extras: { bm: { sheet_w: project.sheet.w, sheet_h: project.sheet.h, uv_inset: project.uvInset } } },
      scene: 0, scenes: [{ name: model.name, nodes: [] }], nodes: [], meshes: [], accessors: [], bufferViews: [], skins: [], animations: [],
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
    const SIZE = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
    const accessor = (arr, type, comp, target, minmax) => {
      const a = { bufferView: view(arr, target), componentType: comp, count: arr.length / SIZE[type], type };
      if (minmax) {
        const n = SIZE[type], lo = new Array(n).fill(Infinity), hi = new Array(n).fill(-Infinity);
        for (let i = 0; i < arr.length; i++) { const k = i % n; lo[k] = Math.min(lo[k], arr[i]); hi[k] = Math.max(hi[k], arr[i]); }
        a.min = lo; a.max = hi;
      }
      js.accessors.push(a);
      return js.accessors.length - 1;
    };

    // the mesh: one primitive with the sheet, one with plain colours
    const prims = [];
    for (const textured of [true, false]) {
      const pos = [], nrm = [], uv = [], col = [], jnt = [], wgt = [], idx = [], index = new Map();
      for (const f of model.faces) {
        if ((f.c == null) !== textured) continue;
        const n = BM.faceNormal(f), tris = f.p.length === 4 ? [[0, 1, 2], [0, 2, 3]] : [[0, 1, 2]];
        const lin = textured ? null : [BM.srgbToLinear(f.c >> 16 & 255), BM.srgbToLinear(f.c >> 8 & 255), BM.srgbToLinear(f.c & 255)];
        for (const t of tris) {
          if (new Set(t.map(k => BM.posKey(f.p[k]))).size < 3) continue;
          for (const k of [t[0], t[2], t[1]]) {
            const p = f.p[k], b = f.b && f.b[k] > 0 && f.b[k] < nb ? f.b[k] : 0;
            const key = BM.posKey(p) + '|' + n.map(v => v.toFixed(4)).join(',') + '|' + (textured ? f.uv[k].join(',') : f.c) + '|' + b;
            let i = index.get(key);
            if (i === undefined) {
              i = pos.length / 3;
              index.set(key, i);
              pos.push(...P(p)); nrm.push(...P(n));
              if (textured) uv.push(f.uv[k][0] / project.sheet.w, f.uv[k][1] / project.sheet.h); else col.push(...lin);
              jnt.push(b, 0, 0, 0); wgt.push(1, 0, 0, 0);
            }
            idx.push(i);
          }
        }
      }
      if (!idx.length) continue;
      const attributes = {
        POSITION: accessor(new Float32Array(pos), 'VEC3', 5126, 34962, true),
        NORMAL: accessor(new Float32Array(nrm), 'VEC3', 5126, 34962),
        JOINTS_0: accessor(new Uint8Array(jnt), 'VEC4', 5121, 34962),
        WEIGHTS_0: accessor(new Float32Array(wgt), 'VEC4', 5126, 34962),
      };
      if (textured) attributes.TEXCOORD_0 = accessor(new Float32Array(uv), 'VEC2', 5126, 34962);
      else attributes.COLOR_0 = accessor(new Float32Array(col), 'VEC3', 5126, 34962);
      const big = pos.length / 3 > 65535;
      prims.push({ attributes, indices: accessor(big ? new Uint32Array(idx) : new Uint16Array(idx), 'SCALAR', big ? 5125 : 5123, 34963), material: textured ? 0 : 1 });
    }
    js.meshes.push({ name: model.name, primitives: prims });

    // joints: a node per bone, children under their parent
    const jointBase = 1;
    js.nodes.push({ name: model.name, mesh: 0, skin: 0 });
    rig.bones.forEach(b => {
      const h = b.parent >= 0 ? rig.bones[b.parent].head : [0, 0, 0];
      js.nodes.push({ name: b.name, translation: P([b.head[0] - h[0], b.head[1] - h[1], b.head[2] - h[2]]) });
    });
    rig.bones.forEach((b, i) => {
      if (b.parent >= 0) (js.nodes[jointBase + b.parent].children = js.nodes[jointBase + b.parent].children || []).push(jointBase + i);
    });
    const roots = rig.bones.map((b, i) => b.parent < 0 ? jointBase + i : -1).filter(i => i >= 0);
    js.scenes[0].nodes.push(0, ...roots);
    const ibm = [];
    for (const b of rig.bones) { const h = P(b.head); ibm.push(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -h[0], -h[1], -h[2], 1); }
    js.skins.push({ name: model.name, joints: rig.bones.map((_, i) => jointBase + i), skeleton: roots[0], inverseBindMatrices: accessor(new Float32Array(ibm), 'MAT4', 5126) });

    // an animation per clip: 'smooth' is sampled (glTF has no such curve), 'step' stays step
    for (const clip of rig.clips) {
      if (!clip.keys.length) continue;
      let times, poses, interp = clip.mode === 'step' ? 'STEP' : 'LINEAR';
      if (clip.mode === 'smooth') {
        const n = Math.max(2, Math.round(clip.length * 30));
        times = Array.from({ length: n + 1 }, (_, i) => i * clip.length / n);
        poses = times.map(t => R.samplePose(rig, clip, Math.min(t, clip.length - 1e-6 * !clip.loop)));
        if (clip.loop) poses[n] = R.samplePose(rig, clip, 0);
      } else {
        times = clip.keys.map(k => k.t);
        poses = clip.keys.map(k => k.pose);
        if (clip.loop && times[times.length - 1] < clip.length - 1e-6) { times = times.concat(clip.length); poses = poses.concat([clip.keys[0].pose]); }
        if (times[0] > 1e-6) { times = [0].concat(times); poses = [clip.loop ? poses[poses.length - 1] : poses[0]].concat(poses); }
      }
      const input = accessor(new Float32Array(times), 'SCALAR', 5126, undefined, true);
      const anim = { name: clip.name, channels: [], samplers: [] };
      rig.bones.forEach((b, i) => {
        const h = b.parent >= 0 ? rig.bones[b.parent].head : [0, 0, 0];
        const rot = [], tr = [];
        for (const pose of poses) {
          const p = pose[i] || { q: [0, 0, 0, 1], t: [0, 0, 0] };
          rot.push(...QC(R.Q.norm(p.q)));
          tr.push(...P([b.head[0] - h[0] + p.t[0], b.head[1] - h[1] + p.t[1], b.head[2] - h[2] + p.t[2]]));
        }
        anim.samplers.push({ input, output: accessor(new Float32Array(rot), 'VEC4', 5126), interpolation: interp });
        anim.channels.push({ sampler: anim.samplers.length - 1, target: { node: jointBase + i, path: 'rotation' } });
        anim.samplers.push({ input, output: accessor(new Float32Array(tr), 'VEC3', 5126), interpolation: interp });
        anim.channels.push({ sampler: anim.samplers.length - 1, target: { node: jointBase + i, path: 'translation' } });
      });
      js.animations.push(anim);
    }
    if (!js.animations.length) delete js.animations;

    const png = await BM.encodePNG(project.sheet);
    js.images.push({ name: 'sheet', mimeType: 'image/png', bufferView: view(png) });
    bin.pad4();
    js.buffers.push({ byteLength: bin.n });
    let json = BM.utf8(JSON.stringify(js));
    const jb = new Uint8Array(json.length + ((4 - (json.length & 3)) & 3)).fill(0x20);
    jb.set(json);
    json = jb;
    const body = bin.result(), out = new BM.Writer(28 + json.length + body.length);
    out.u32(0x46546C67); out.u32(2); out.u32(12 + 8 + json.length + 8 + body.length);
    out.u32(json.length); out.u32(0x4E4F534A); out.bytes(json);
    out.u32(body.length); out.u32(0x004E4942); out.bytes(body);
    return out.result();
  }

  BM.exportAnimatedGLB = exportAnimatedGLB;
})(typeof window !== 'undefined' ? window : globalThis);
