"""
fp.py - pieces of the first-person models (weapons and hands seen from
the eye: the camera at the origin looking along +z, x right, y up).
"""
from geo import add, along, box, ellipsoid, norm, sub, tube


def fist(m, bone, wrist, towards, glove, r=0.045, thumb=1, lo=0, hi=3):
    """a closed hand at `wrist`, its knuckles towards `towards` (a point):
    the palm, the row of fingers wrapped round a grip, the thumb on the
    side `thumb` (1: +x, -1: -x)"""
    k = r / 0.045
    palm = box(0.07 * k, 0.085 * k, 0.06 * k, bevel=0.014 * k).move(0, 0.045 * k, 0)
    fingers = box(0.074 * k, 0.034 * k, 0.07 * k, bevel=0.01 * k).move(0, 0.09 * k, 0.008 * k)
    th = ellipsoid(0.017 * k, 0.034 * k, 0.019 * k, segs=6, rings=3).move(thumb * 0.04 * k, 0.06 * k, 0.022 * k)
    for p in (palm, fingers, th):
        m.add(along(p, wrist, towards), glove, bone, lo, hi)


def forearm(m, bone, wrist, elbow, sleeve, r=0.05, cuff=None, lo=0, hi=3):
    """the forearm from off screen (the elbow) to the wrist, with a cuff"""
    m.add(tube(elbow, wrist, r * 1.15, r * 0.92, segs=8), sleeve, bone, lo, hi)
    if cuff is not None:
        d = norm(sub(wrist, elbow))
        c0 = add(wrist, (-d[0] * 0.05, -d[1] * 0.05, -d[2] * 0.05))
        m.add(tube(c0, wrist, r * 1.08, r * 1.02, segs=8, caps=True), cuff, bone, lo, hi)
