/*
 * bm Studio / bm Animator - skeletons and animation: quaternions, poses,
 * keyframes, skinning. The same arithmetic as the kernel (src/bm/runtime.c,
 * animate()), so what the Animator shows is what the console draws.
 *
 * A bone turns around its head. In the rest pose every bone has no turn;
 * a pose gives each bone a turn q (relative to its parent) and a move t:
 *   M[i] = M[parent] * T(head + t) * R(q) * T(-head)
 * maps a point of the model at rest to where the pose puts it. Every
 * corner of a face follows one bone (face.b): rigid parts, as on the PS1;
 * corners of two bones that share a place stretch between them.
 */
(function (root) {
  'use strict';
  const BM = root.BM, v3 = BM.v3;

  // ------------------------------------------------------------ quaternions [x, y, z, w]

  const Q = {
    id: () => [0, 0, 0, 1],
    mul(a, b) {
      return [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
      ];
    },
    conj: q => [-q[0], -q[1], -q[2], q[3]],
    norm(q) { const l = Math.hypot(q[0], q[1], q[2], q[3]) || 1; return [q[0] / l, q[1] / l, q[2] / l, q[3] / l]; },
    axisAngle(axis, a) {
      const n = v3.norm(axis), s = Math.sin(a / 2);
      return [n[0] * s, n[1] * s, n[2] * s, Math.cos(a / 2)];
    },
    /* the shorter way from a to b; the same as the kernel */
    slerp(a, b, u) {
      let d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
      let bb = b;
      if (d < 0) { d = -d; bb = [-b[0], -b[1], -b[2], -b[3]]; }
      let k0, k1;
      if (d > 0.9995) { k0 = 1 - u; k1 = u; } else {
        const th = Math.acos(d), s = Math.sin(th);
        k0 = Math.sin((1 - u) * th) / s; k1 = Math.sin(u * th) / s;
      }
      return Q.norm([a[0] * k0 + bb[0] * k1, a[1] * k0 + bb[1] * k1, a[2] * k0 + bb[2] * k1, a[3] * k0 + bb[3] * k1]);
    },
    /* rotation matrix, row major 3x3 */
    mat(q) {
      const [x, y, z, w] = q;
      return [
        1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
        2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
        2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y),
      ];
    },
    rotate(q, v) { const m = Q.mat(q); return [m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]]; },
    /* degrees around x, then y, then z (R = Rz Ry Rx, as draw3d turns) */
    fromEuler(e) {
      const r = Math.PI / 180;
      return Q.mul(Q.mul(Q.axisAngle([0, 0, 1], e[2] * r), Q.axisAngle([0, 1, 0], e[1] * r)), Q.axisAngle([1, 0, 0], e[0] * r));
    },
    toEuler(q) {
      const m = Q.mat(q), d = 180 / Math.PI;
      const sy = Math.max(-1, Math.min(1, -m[6]));
      const y = Math.asin(sy);
      let x, z;
      if (Math.abs(sy) < 0.9999) { x = Math.atan2(m[7], m[8]); z = Math.atan2(m[3], m[0]); } else { x = Math.atan2(-m[5], m[4]); z = 0; }
      return [x * d, y * d, z * d];
    },
  };

  // ------------------------------------------------------------ affine 3x4, row major

  const M = {
    id: () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0],
    mul(a, b) {
      const o = new Array(12);
      for (let r = 0; r < 3; r++) {
        for (let c = 0; c < 3; c++) o[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
        o[r * 4 + 3] = a[r * 4] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
      }
      return o;
    },
    apply: (m, p) => [m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
      m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]],
  };

  /* T(h + t) R(q) T(-h) */
  function boneLocal(h, q, t) {
    const r = Q.mat(q);
    const c = [h[0] + t[0], h[1] + t[1], h[2] + t[2]];
    const rh = [r[0] * h[0] + r[1] * h[1] + r[2] * h[2], r[3] * h[0] + r[4] * h[1] + r[5] * h[2], r[6] * h[0] + r[7] * h[1] + r[8] * h[2]];
    return [r[0], r[1], r[2], c[0] - rh[0], r[3], r[4], r[5], c[1] - rh[1], r[6], r[7], r[8], c[2] - rh[2]];
  }

  const restPose = n => Array.from({ length: n }, () => ({ q: [0, 0, 0, 1], t: [0, 0, 0] }));
  const clonePose = p => p.map(b => ({ q: b.q.slice(), t: b.t.slice() }));

  /* every bone's matrix for a pose (parents come before their children) */
  function boneMatrices(rig, pose) {
    const out = [];
    rig.bones.forEach((b, i) => {
      const p = pose[i] || { q: [0, 0, 0, 1], t: [0, 0, 0] };
      const local = boneLocal(b.head, p.q, p.t);
      out.push(b.parent >= 0 ? M.mul(out[b.parent], local) : local);
    });
    return out;
  }

  /* a bone's turn in the world for a pose */
  function worldRot(rig, pose, i) {
    let q = (pose[i] || {}).q || [0, 0, 0, 1];
    for (let p = rig.bones[i].parent; p >= 0; p = rig.bones[p].parent) q = Q.mul((pose[p] || {}).q || [0, 0, 0, 1], q);
    return Q.norm(q);
  }

  // ------------------------------------------------------------ keyframes

  function ease(mode, u) {
    if (mode === 'step') return 0;
    if (mode === 'smooth') return u * u * (3 - 2 * u);
    return u;
  }

  function mixPose(a, b, u) {
    return a.map((pa, i) => {
      const pb = b[i] || pa;
      return { q: Q.slerp(pa.q, pb.q, u), t: [0, 1, 2].map(k => pa.t[k] + (pb.t[k] - pa.t[k]) * u) };
    });
  }

  /* the pose of a clip at time t (seconds; a looping clip goes round, the
   * last keyframe going back to the first) */
  function samplePose(rig, clip, t) {
    const n = rig.bones.length, keys = clip && clip.keys;
    if (!keys || !keys.length) return restPose(n);
    const L = clip.length > 0 ? clip.length : 1;
    if (clip.loop) t = ((t % L) + L) % L; else t = Math.max(0, Math.min(L, t));
    const fill = p => p.length === n ? p : p.concat(restPose(n - p.length)).slice(0, n);
    if (keys.length === 1) return fill(clonePose(keys[0].pose));
    let a, b, ta, tb;
    if (t < keys[0].t) {
      if (!clip.loop) return fill(clonePose(keys[0].pose));
      a = keys[keys.length - 1]; b = keys[0]; ta = a.t - L; tb = b.t;
    } else if (t >= keys[keys.length - 1].t) {
      if (!clip.loop) return fill(clonePose(keys[keys.length - 1].pose));
      a = keys[keys.length - 1]; b = keys[0]; ta = a.t; tb = b.t + L;
    } else {
      let k = 0;
      while (k + 1 < keys.length && keys[k + 1].t <= t) k++;
      a = keys[k]; b = keys[k + 1]; ta = a.t; tb = b.t;
    }
    const u = tb > ta ? ease(clip.mode, (t - ta) / (tb - ta)) : 0;
    return fill(mixPose(a.pose, b.pose, u));
  }

  /* the key at time t (within half a frame), or -1 */
  function keyAt(clip, t, eps = 1e-4) {
    return clip.keys.findIndex(k => Math.abs(k.t - t) <= eps);
  }

  /* puts a pose at time t: a new keyframe, or the one already there */
  function setKey(clip, t, pose) {
    const i = keyAt(clip, t);
    if (i >= 0) { clip.keys[i].pose = clonePose(pose); return i; }
    clip.keys.push({ t, pose: clonePose(pose) });
    clip.keys.sort((x, y) => x.t - y.t);
    return keyAt(clip, t);
  }

  // ------------------------------------------------------------ skinning

  /* the faces of a model in a pose: corners moved by their bones */
  function posedFaces(model, pose, mats) {
    const rig = model.rig;
    if (!rig || !rig.bones.length) return model.faces;
    mats = mats || boneMatrices(rig, pose);
    const nb = mats.length;
    return model.faces.map(f => ({
      p: f.p.map((p, k) => {
        const b = f.b ? f.b[k] : 0;
        return M.apply(mats[b > 0 && b < nb ? b : 0], p);
      }),
      uv: f.uv, c: f.c, src: f,
    }));
  }

  /* where the head and tail of every bone are in a pose */
  function posedBones(rig, pose, mats) {
    mats = mats || boneMatrices(rig, pose);
    return rig.bones.map((b, i) => ({ head: M.apply(mats[i], b.head), tail: M.apply(mats[i], b.tail) }));
  }

  function segDist2(p, a, b) {
    const ab = v3.sub(b, a), ap = v3.sub(p, a), l2 = v3.dot(ab, ab);
    const u = l2 > 1e-12 ? Math.max(0, Math.min(1, v3.dot(ap, ab) / l2)) : 0;
    const d = v3.sub(ap, v3.scale(ab, u));
    return v3.dot(d, d);
  }

  function nearestBone(rig, p) {
    let best = 0, bd = Infinity;
    rig.bones.forEach((b, i) => {
      const d = segDist2(p, b.head, b.tail);
      if (d < bd - 1e-9) { bd = d; best = i; }
    });
    return best;
  }

  /* gives the corners to the nearest bones: by face (each face whole to one
   * bone: rigid parts) or by corner (shared corners: the mesh stretches) */
  function autoSkin(model, faces, byCorner) {
    const rig = model.rig;
    for (const f of faces || model.faces) {
      if (byCorner) f.b = f.p.map(p => nearestBone(rig, p));
      else { const b = nearestBone(rig, BM.faceCenter(f)); f.b = f.p.map(() => b); }
    }
  }

  /* "arm.L" <-> "arm.R", "leftLeg" <-> "rightLeg"... */
  function mirrorName(n) {
    const pairs = [[/\.L$/, '.R'], [/\.R$/, '.L'], [/_L$/, '_R'], [/_R$/, '_L'], [/^L_/, 'R_'], [/^R_/, 'L_'],
      [/[Ll]eft/, m => m[0] === 'L' ? 'Right' : 'right'], [/[Rr]ight/, m => m[0] === 'R' ? 'Left' : 'left']];
    for (const [re, to] of pairs) if (re.test(n)) return n.replace(re, to);
    return null;
  }

  /* the bone colour in the views: a hue per bone */
  function boneColour(i) {
    const h = (i * 0.618034) % 1, s = 0.65, v = 0.95;
    const k = n => { const x = (n + h * 6) % 6; return v - v * s * Math.max(0, Math.min(1, Math.min(x, 4 - x))); };
    return (Math.round(k(5) * 255) << 16) | (Math.round(k(3) * 255) << 8) | Math.round(k(1) * 255);
  }

  BM.rig = {
    Q, M, boneLocal, restPose, clonePose, boneMatrices, worldRot, ease, mixPose, samplePose, keyAt, setKey,
    posedFaces, posedBones, segDist2, nearestBone, autoSkin, mirrorName, boneColour,
  };
})(typeof window !== 'undefined' ? window : globalThis);
