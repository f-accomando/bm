"""Episode 6: the last level of Skyvale World, 160 cells (1280 px) long: the ground
of episode 2 carried on, three pits, more planks, clouds, a layer of trees far
behind, and the end flag (drawn in bm Pixel). The map of episode 2 is the start:
this adds to it.

Cells of the sheet: 18 19 / 50 51 the tree (the assistant, sprite 9: 16x16 = four
8x8 tiles), sprite 10 the flag with its cloth, sprite 11 the pole.
"""
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ep2 = _load("art2", os.path.join(HERE, "..", "02-sdk", "art.py"))

WIDTH = 160
GROUND_Y = ep2.GROUND_Y                       # 28
PITS = [range(44, 50), range(104, 112), range(134, 138)]
PLANKS = ep2.PLANKS + [(range(60, 66), 22), (range(70, 74), 19), (range(84, 90), 22),
                       (range(96, 102), 20), (range(118, 124), 22), (range(126, 130), 18)]
CLOUDS = ep2.CLOUDS + [(range(70, 73), 5), (range(90, 94), 8), (range(110, 114), 4),
                       (range(130, 134), 7), (range(150, 154), 5)]
TREES = [6, 22, 40, 58, 76, 94, 112, 130, 146]        # x of the top-left cell, rows 26 and 27
TREE_CELLS = ((0, 0, 18), (1, 0, 19), (0, 1, 50), (1, 1, 51))
TREE_Y = 26
FLAG_X = 150 * 8                              # pixels: the pole of the flag


def in_pit(x):
    return any(x in p for p in PITS)


def level():
    """{layer name: {(x, y): tile}} for the layers main, layer2 (clouds), layer3 (trees)"""
    main, clouds, trees = {}, {}, {}
    for x in range(WIDTH):
        if not in_pit(x):
            main[(x, GROUND_Y)] = 64
            main[(x, GROUND_Y + 1)] = 65
        else:
            main[(x, GROUND_Y + 1)] = 68
        main[(x, GROUND_Y + 2)] = 65
    for xs, y in PLANKS:
        for x in xs:
            main[(x, y)] = 66
    for xs, y in CLOUDS:
        for x in xs:
            clouds[(x, y)] = 67
    for x0 in TREES:
        for dx, dy, tile in TREE_CELLS:
            trees[(x0 + dx, TREE_Y + dy)] = tile
    return {"main": main, "layer2": clouds, "layer3": trees}


if __name__ == "__main__":
    lv = level()
    print({k: len(v) for k, v in lv.items()})
