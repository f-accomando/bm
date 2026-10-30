# Clinic: map kit for Dark Night

`art/maps/clinic.bmk` is a map kit (`.bmk`, kind 6) in the format of `island.bmk` ([map-kit](../../../docs/design/map-kit.md)).
It is one file with the models, the sheet, the map layers and the object table of the first area of Dark Night.
Dark Night is a new, independent `.bm`. The kit is only its map asset.

## What is in it

- **Map**: 80 x 48 tiles (160 x 96 cells), about 5000 triangles for the whole map. Layers `floor`, `height`, `walls`, `objects`.
- **Layout** (rotated and simplified from the reference map): Ward A (the start), a hall with a red runner and a stair down
  (five treads of 0.5 units), Wards B and C (beds, operating tables, cabinets), a grand atrium with pillars, a courtyard
  with a house, a ruin and two sheds, an alley to the south that ends in a barricade, and a stair up to the Courtyard Gate.
- **Models**: 18 static elements, each in a textured look and a flat look (`bed`, `bed_f`): beds, operating table, cabinet,
  street lamp, checkpoint lantern, doors (open and locked, two directions), the gate, crate, barrel, pillar, bench, cart, barricade.
- **Logic objects** (no model): `player_start` (1024) and `enemy` spawn points. 15 enemies, 3 lanterns, 7 doors, 1 gate.
- **Screen**: 640 x 360, shown at 1280 x 720 by 2x scaling. Textures are 16 x 16 for one unit.

## How the picture stays readable

- Floors and walls differ in hue and value: floors are pale green-grey, warm grey or red, walls are dark green dado over
  dingy plaster inside and blue-grey stone outside.
- Props use colours that the walls do not use: white linen, brass, amber glass, red blood. Lamps and lanterns are bright.
- The texture of a wall is stretched over its whole height, so each wall texture is drawn for its own height.

## Rules of the walls layer

The value in `walls` is the height (value x 0.25 units) and also the number of the sheet cell with the texture.
So a wall texture sits in row 0 of the sheet, and tile k of row 0 is a wall of height k x 0.5 units.
This kit has 15 tiles in row 0. Each is a floor and a wall style (`WALL_OF` in `build_clinic.py`).

## Build and check

```bash
python3 art/maps/clinic/build_clinic.py art/maps/clinic.bmk --png top.png
python3 art/maps/clinic/check_kit.py
```

`check_kit.py` reads the kit with `bmkit.py`, walks the layout (the player reaches the gate, the atrium, the alley and the
lanterns without a key, and every enemy with the keys), and runs `kit_load`, `map3d_build`, `map3d_objects`, `model()` and
`map3d_blocked` in bmhost (`make build/host/bmhost-bin` first). Result on 2026-10-30: 67 of 67 checks passed.

To look at the map in 3D on the PC:

```bash
python3 art/maps/clinic/viewer/build_viewer.py art/maps/clinic.bmk OUT_DIR [--look flat] [--light day]
build/host/bmhost-bin OUT_DIR/viewer.bm --shots OUT_DIR/shots --every 12 --from 4 --seconds 3 --quiet
```

## Install on the console

Copy `clinic.bmk` to `/bmk` on the SD card. In bm SDK use **Import a map kit (/bmk)...**.
For the Market, `bmk/clinic/clinic.bmk` and `market/info.txt` go in the repository `bm-market`.
The console then installs it with `net fetch gh:f-accomando/bm-market/bmk/clinic/clinic.bmk`.
The owner confirms the push to `bm-market`. It is not done.

## Not verified

- No device ran it. bm SDK's menu and 3D map page were not opened by hand (the check runs `kit_load` and the 3D map calls).
- The speed on the Pi Zero W is not measured.
- Objects have no turn in the map, so doors, beds and benches have two models (`_ns`, `_ew`).
- The layout is a fan study of a third-party map. No picture or text of the original is copied.
  It is not for the Market without the owner's decision.
