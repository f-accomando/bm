"""The sounds of Yharnam 8, as lists of notes: (pitch, waveform, volume,
effect), None a rest. Pitch 33 is the A of 440 Hz; waveforms: 0 triangle,
1 tilted saw, 2 saw, 3 square, 4 pulse, 5 organ, 6 noise, 7 phaser;
effects: 1 slide, 2 vibrato, 3 drop, 4 fade in, 5 fade out, 6 and 7
arpeggios.
"""


def line(notes, speed, loop=(0, 0)):
    notes = list(notes) + [None] * (32 - len(notes))
    out = "%02x%02x%02x%02x" % (0, speed, loop[0], loop[1])
    for n in notes[:32]:
        out += "00000" if n is None else "%02x%x%x%x" % n
    return out


SFX = {
    0: ([(44, 6, 4, 0), (38, 6, 3, 0), (30, 6, 2, 5)], 2),                     # a light blow
    1: ([(36, 6, 5, 0), (32, 6, 5, 0), (26, 6, 4, 0), (20, 6, 2, 5)], 3),      # a heavy one
    2: ([(20, 3, 6, 3), (30, 6, 5, 0), (22, 6, 3, 5)], 2),                     # flesh struck
    3: ([(56, 6, 7, 0), (40, 6, 6, 3), (30, 6, 4, 0), (24, 6, 2, 5)], 2),      # the pistol
    4: ([(30, 3, 6, 0), (26, 3, 5, 3), (20, 3, 3, 5)], 3),                     # the hunter hurt
    5: ([(30, 6, 2, 4), (34, 6, 3, 0), (30, 6, 2, 5)], 2),                     # a dodge
    6: ([(60, 3, 6, 0), (55, 3, 5, 0), (48, 4, 5, 2), (48, 4, 3, 5)], 3),      # parried
    7: ([(30, 6, 7, 0), (24, 2, 6, 3), (40, 6, 5, 0), (20, 2, 5, 3), (14, 6, 3, 5)], 4),  # visceral
    8: ([(32, 2, 5, 3), (26, 2, 4, 3), (18, 2, 3, 5)], 5),                     # a creature dies
    9: ([(14, 2, 6, 2), (13, 2, 7, 2), (12, 2, 7, 2), (12, 7, 6, 2), (11, 2, 5, 5)], 10),  # a roar
    10: ([(36, 5, 4, 0), (40, 5, 4, 0), (43, 5, 4, 0), (48, 5, 5, 2), (48, 5, 3, 5)], 6),  # a lamp lit
    11: ([(62, 3, 5, 0), (50, 3, 4, 3), (63, 3, 5, 0), (52, 3, 3, 5)], 2),     # the trick: steel
    12: ([(36, 0, 4, 1), (40, 0, 4, 1), (45, 0, 5, 1), (48, 0, 3, 5)], 5),     # blood vial
    13: ([(48, 4, 3, 0)], 2),                                                  # a blip
    14: ([(43, 5, 4, 0), (48, 5, 4, 0), (52, 5, 5, 0), (55, 5, 4, 5)], 5),     # a path taken
    15: ([(12, 5, 7, 0), (24, 0, 6, 5), (19, 5, 5, 5), (12, 0, 4, 5)], 16),    # prey slaughtered
    16: ([(20, 6, 3, 0), (18, 6, 2, 5)], 3),                                   # a step on stone
}


def boss_theme():
    """a dark loop for the hunts of the bosses: a bass that walks in D minor,
    organ chords, a bell"""
    roots = [14, 14, 10, 13]                    # D D Bb C (an octave down)
    bass, organ, bell = [], [], []
    for r in roots:
        for k in range(8):
            bass.append((r + (12 if k in (3, 7) else 0), 2, 5 if k % 4 == 0 else 3, 0))
    chords = [(38, 41, 45), (38, 41, 45), (34, 38, 41), (36, 40, 43)]
    for a, b, c in chords:
        for k, p in enumerate((a, b, c, b, a, b, c, b)):
            organ.append((p, 5, 3 if k % 2 == 0 else 2, 0))
    for k in range(32):
        bell.append((62 if k % 16 == 0 else 57, 0, 4, 5) if k % 8 == 0 else None)
    return bass, organ, bell


def sfx_lines():
    lines = ["%02x%02x%02x%02x" % (0, 16, 0, 0) + "00000" * 32 for _ in range(64)]
    for n, (notes, speed) in SFX.items():
        lines[n] = line(notes, speed)
    bass, organ, bell = boss_theme()
    lines[32] = line(bass, 14)
    lines[33] = line(organ, 14)
    lines[34] = line(bell, 14)
    return lines


def music_lines():
    lines = ["00 41424344"] * 64
    lines[0] = "03 20212244"          # the boss theme, looping
    return lines
