"""The background layer (BG2) of the first level as a map layer of 8x8 cells.

bg_layer() folds the samples of scripts/smw_bg2.py on a period of 512 pixels, fills what the foreground always
hid (the same row a quarter or half period away, else the pixel above, else the common colour), cuts the
picture in 8x8 cells and gives the cells the numbers after `first`. It returns the new cells (images, to put in the
sheet), the map (a list of rows of cell numbers, 64 cells a row, repeated to the width of the level) and the height."""
import numpy as np


def bg_layer(ex, first, width_cells, restyle=None):
    from PIL import Image
    img = np.load(ex + "/bg2.npy")
    H, W, _ = img.shape
    fold = np.zeros((H, 512, 4), dtype=np.uint8)
    for u in range(W):                                  # the first sample of each column of the period
        col, dst = img[:, u], fold[:, u % 512]
        m = (dst[:, 3] == 0) & (col[:, 3] > 0)
        dst[m] = col[m]
    base = (32, 104, 0)
    for shift in (256, 128, 384):                       # holes: the same row, a part of the period away
        seen = fold[..., 3] > 0
        sh = np.roll(fold, shift, axis=1)
        m = ~seen & (sh[..., 3] > 0)
        fold[m] = sh[m]
    rows = np.nonzero((fold[..., 3] > 0).sum(axis=1) > 100)[0]
    top = rows.min()
    for y in range(top - 1, -1, -1):                    # above the top of what was seen: the rows below, mirrored
        fold[y] = fold[min(H - 1, 2 * top - y)]
    for y in range(H):                                  # what is left: the pixel above, else the common colour
        for x in np.nonzero(fold[y, :, 3] == 0)[0]:
            fold[y, x] = fold[y - 1, x] if y and fold[y - 1, x, 3] else (*base, 255)
    pic = Image.fromarray(fold, "RGBA")
    if restyle:
        pic = restyle(pic)
    pic = pic.convert("RGBA")
    cells, images, rows_out = {}, [], []
    for cy in range(H // 8):
        row = []
        for cx in range(64):
            t = pic.crop((cx * 8, cy * 8, cx * 8 + 8, cy * 8 + 8))
            key = t.tobytes()
            if key not in cells:
                cells[key] = first + len(cells)
                images.append(t)
            row.append(cells[key])
        rows_out.append(row * (width_cells // 64 + 1))
    return images, [r[:width_cells] for r in rows_out], H
