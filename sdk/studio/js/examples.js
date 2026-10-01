/*
 * bm Animator - an example to start from: a villager made of boxes, with a
 * skeleton (hips, spine, head, arms, legs) and three animations (idle,
 * walk, wave). bm Animator opens with it; Studio Village walks it around.
 */
(function (root) {
  'use strict';
  const BM = root.BM, R = BM.rig, Q = R.Q;

  /* the six sides of a box, showing outwards, all following `bone` */
  function box(faces, lo, hi, colour, bone, top = colour) {
    const [x0, y0, z0] = lo, [x1, y1, z1] = hi;
    const quads = [
      [[[x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [x1, y0, z0]], colour],      // front (-z)
      [[[x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [x0, y0, z1]], colour],      // back
      [[[x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [x0, y0, z0]], colour],      // -x
      [[[x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]], colour],      // +x
      [[[x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0]], top],         // top
      [[[x0, y0, z1], [x0, y0, z0], [x1, y0, z0], [x1, y0, z1]], colour],      // bottom
    ];
    for (const [p, c] of quads) faces.push({ p, uv: p.map(() => [0, 0]), c, b: p.map(() => bone) });
  }

  const deg = (x, y, z) => Q.fromEuler([x, y, z]);
  const P = (n, set) => {                 // a pose: rest, with some bones turned or moved
    const pose = R.restPose(n);
    for (const [i, q, t] of set) { if (q) pose[i].q = q; if (t) pose[i].t = t; }
    return pose;
  };

  function villager() {
    const SKIN = 0xF0C090, HAIR = 0x5A3A1E, SHIRT = 0x3478C4, BELT = 0x8A5A30, PANTS = 0x404450, SHOE = 0x2A1E18, EYE = 0x101018;
    // bones: 0 hips, 1 spine, 2 head, 3 arm.L, 4 arm.R, 5 leg.L, 6 leg.R (the villager looks
    // towards -z, at the camera: its left is +x)
    const bones = [
      { name: 'hips', parent: -1, head: [0, 0.9, 0], tail: [0, 1.0, 0] },
      { name: 'spine', parent: 0, head: [0, 0.95, 0], tail: [0, 1.5, 0] },
      { name: 'head', parent: 1, head: [0, 1.5, 0], tail: [0, 2.0, 0] },
      { name: 'arm.L', parent: 1, head: [0.33, 1.42, 0], tail: [0.33, 0.9, 0] },
      { name: 'arm.R', parent: 1, head: [-0.33, 1.42, 0], tail: [-0.33, 0.9, 0] },
      { name: 'leg.L', parent: 0, head: [0.13, 0.9, 0], tail: [0.13, 0.08, 0] },
      { name: 'leg.R', parent: 0, head: [-0.13, 0.9, 0], tail: [-0.13, 0.08, 0] },
    ];
    const f = [];
    box(f, [-0.25, 0.92, -0.15], [0.25, 1.5, 0.15], SHIRT, 1);                 // body
    box(f, [-0.26, 0.9, -0.16], [0.26, 0.98, 0.16], BELT, 0);                  // belt
    box(f, [-0.22, 1.5, -0.22], [0.22, 1.94, 0.22], SKIN, 2, HAIR);            // head
    box(f, [-0.24, 1.84, -0.2], [0.24, 1.98, 0.24], HAIR, 2);                  // hair
    box(f, [-0.13, 1.66, -0.225], [-0.06, 1.74, -0.22], EYE, 2);               // eyes
    box(f, [0.06, 1.66, -0.225], [0.13, 1.74, -0.22], EYE, 2);
    box(f, [0.25, 1.0, -0.08], [0.41, 1.48, 0.08], SHIRT, 3);                  // arm.L
    box(f, [0.26, 0.86, -0.07], [0.4, 1.0, 0.07], SKIN, 3);                    // hand.L
    box(f, [-0.41, 1.0, -0.08], [-0.25, 1.48, 0.08], SHIRT, 4);                // arm.R
    box(f, [-0.4, 0.86, -0.07], [-0.26, 1.0, 0.07], SKIN, 4);
    box(f, [0.03, 0.12, -0.1], [0.23, 0.9, 0.1], PANTS, 5);                    // leg.L
    box(f, [0.02, 0, -0.16], [0.24, 0.12, 0.1], SHOE, 5);                      // shoe.L
    box(f, [-0.23, 0.12, -0.1], [-0.03, 0.9, 0.1], PANTS, 6);
    box(f, [-0.24, 0, -0.16], [-0.02, 0.12, 0.1], SHOE, 6);

    const n = bones.length;
    const walk = (s) => P(n, [                                                  // s = +1 or -1: which leg forward
      [5, deg(28 * s, 0, 0)], [6, deg(-28 * s, 0, 0)],
      [3, deg(-24 * s, 0, 0)], [4, deg(24 * s, 0, 0)],
      [1, deg(4, -6 * s, 0)], [2, deg(-3, 6 * s, 0)],
    ]);
    const pass = P(n, [[0, null, [0, 0.05, 0]], [1, deg(4, 0, 0)], [5, deg(-6, 0, 0)], [6, deg(-6, 0, 0)]]);
    const clips = [
      { name: 'idle', length: 2, loop: true, mode: 'smooth', keys: [
        { t: 0, pose: P(n, [[3, deg(0, 0, -4)], [4, deg(0, 0, 4)]]) },
        { t: 1, pose: P(n, [[1, deg(-3, 0, 0)], [2, deg(4, 0, 0)], [3, deg(0, 0, -8)], [4, deg(0, 0, 8)], [0, null, [0, -0.02, 0]]]) },
      ] },
      { name: 'walk', length: 0.8, loop: true, mode: 'smooth', keys: [
        { t: 0, pose: walk(1) }, { t: 0.2, pose: pass }, { t: 0.4, pose: walk(-1) }, { t: 0.6, pose: pass },
      ] },
      { name: 'wave', length: 1.2, loop: true, mode: 'smooth', keys: [
        { t: 0, pose: P(n, [[4, deg(0, 0, -150)], [2, deg(0, -10, 0)]]) },
        { t: 0.3, pose: P(n, [[4, deg(0, -25, -165)], [2, deg(0, -10, 4)]]) },
        { t: 0.6, pose: P(n, [[4, deg(0, 0, -150)], [2, deg(0, -10, 0)]]) },
        { t: 0.9, pose: P(n, [[4, deg(0, 25, -165)], [2, deg(0, -10, -4)]]) },
      ] },
    ];
    return { name: 'villager', faces: f, rig: { bones, clips } };
  }

  BM.examples = { box, villager };
})(typeof window !== 'undefined' ? window : globalThis);
