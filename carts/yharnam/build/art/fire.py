"""The flames of the braziers and the pyres, in 2D: a loop of frames drawn by
the game after the light (they make their own). Each frame is a field of
intensity: a bed of embers along the base and tongues rising from it, wide
and hot at the foot, thin and swaying at the tip, each of its own height
and pace; the field is cut into the bands of the flame's colours, dithered
where one band meets the next. Every phase is a whole turn of the loop, so
the last frame leads back to the first.

  frames(kind) -> [ (rgba image, (anchor x, anchor y)) ] * FRAMES
  (the anchor: the middle of the base of the flames)
"""
import numpy as np

FRAMES = 8

# from the edge of the flame to its white heart (multiples of 8: SHEET8)
RAMP = [(104, 24, 16), (168, 48, 24), (224, 96, 32), (248, 144, 48), (248, 192, 80), (248, 232, 144),
        (248, 248, 216)]
BANDS = [0.10, 0.24, 0.46, 0.76, 1.08, 1.45, 1.85]     # the least intensity of each colour

BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5

KINDS = {
    # w, h: the frame; base: the row of the embers; bw: half the width of the
    # bed; n: tongues; tall: the tallest; fat: half the width of a tongue's
    # foot; sway: how far the tips swing
    'big': dict(w=56, h=64, base=58, bw=19, n=9, tall=50, fat=7.5, sway=4.0, seed=3),
    'small': dict(w=24, h=30, base=26, bw=7, n=5, tall=21, fat=3.6, sway=1.8, seed=7),
}


def frame(kind, f):
    k = KINDS[kind]
    w, h, base = k['w'], k['h'], k['base']
    rng = np.random.default_rng(k['seed'])
    n = k['n']
    th = 2 * np.pi * f / FRAMES
    ys, xs = np.mgrid[0:h, 0:w].astype(float)
    up = base - ys                                    # height above the base
    cx = w / 2.0
    field = np.zeros((h, w))
    us = np.linspace(-1, 1, n) * 0.9
    phases = rng.uniform(0, 2 * np.pi, (n, 3))
    paces = rng.integers(1, 3, (n, 2))                # whole turns in the loop
    for i in range(n):
        u = us[i]
        x0 = cx + u * k['bw'] * 0.85 + rng.uniform(-0.6, 0.6)
        tall = k['tall'] * (1 - u * u * 0.55) * (0.62 + 0.38 * (0.5 + 0.5 * np.sin(paces[i, 0] * th + phases[i, 0])))
        sway = k['sway'] * np.sin(paces[i, 1] * th + phases[i, 1]) * (0.6 + 0.4 * abs(u))
        lean = -u * k['bw'] * 0.3
        a = np.clip(up / tall, 0, None)
        centre = x0 + (sway + lean) * a * a + 0.6 * np.sin(a * 6 + th + phases[i, 2]) * a
        half = k['fat'] * (1 - u * u * 0.3) * np.clip(1 - a, 0, 1) ** 0.75 + 0.35
        v = np.clip(1 - np.abs(xs - centre) / half, 0, 1) * np.clip(1 - a, 0, 1) ** 0.45
        v[up < -1.5] = 0
        field += v
    # the bed of embers, brightest in the middle
    bed = np.clip(1 - np.abs(xs - cx) / (k['bw'] + 1), 0, 1) * np.clip(1 - np.abs(up + 0.5) / 3.0, 0, 1)
    field += bed * 1.1
    # flicker: a ripple rising through the flame, whole turns in the loop
    rip = 0.86 + 0.14 * np.sin(xs * 0.9 + up * 0.55 - 2 * th) * np.sin(xs * 0.37 - up * 0.21 + th)
    field *= rip
    field = field + BAYER[ys.astype(int) % 4, xs.astype(int) % 4] * 0.16
    img = np.zeros((h, w, 4), np.uint8)
    for c, lo in zip(RAMP, BANDS):
        m = field >= lo
        img[m, :3] = c
        img[m, 3] = 255
    return img, (int(cx), base)


def frames(kind):
    return [frame(kind, f) for f in range(FRAMES)]


if __name__ == '__main__':                              # a look: python3 fire.py OUTDIR
    import os
    import sys
    from PIL import Image
    for kind in KINDS:
        fs = frames(kind)
        w, h = fs[0][0].shape[1], fs[0][0].shape[0]
        strip = Image.new('RGBA', (w * FRAMES, h), (24, 20, 28, 255))
        for i, (img, _) in enumerate(fs):
            strip.alpha_composite(Image.fromarray(img, 'RGBA'), (i * w, 0))
        strip.resize((w * FRAMES * 4, h * 4), Image.NEAREST).save(os.path.join(sys.argv[1], 'fire_%s.png' % kind))
