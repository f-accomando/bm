# Screenshots

A game or an app saves the frame it is drawing as a PNG on the SD card, and the player can do
the same with a key. Done in R4 ([progress/system.md](progress/system.md)); the pictures
also feed the PDF manuals.

## For the player

| How | Where |
|---|---|
| **Print Screen** on the keyboard | `bm/shots/` on the SD card |
| **Select** held for 3 seconds on a pad (Share on a DS4) | |

Hold Select alone. Another button, or letting go, starts the count again. One hold takes one
picture: let go of Select before the next one.

A box says `Screenshot saved` and the file name for two seconds. The picture is the game's
own screen as it was drawn: the dev kit's overlays, the battery icon, the pointer and that box
are **not** in it. Games and apps run by bm's menu take it; the menu's own screens do not yet
([open decisions](open-decisions.md)).

## For an app

```lua
local path = shot()          -- "bm/shots/PONG0007.png"
local path = shot("menu")    -- "bm/shots/MENU0001.png"
```

`shot([prefix])` asks for the frame being drawn: it is saved when `_draw` ends (so call it
at any point of the frame; calling it twice in a frame gives one file). It returns the
name it will have, or `nil` and a message if the numbers are over. No question to the player
(the file only goes to `bm/shots/`).

## Over the network

From the PC, with the console's network link ([design/pi-test.md](design/pi-test.md)):

```
python tools/bm_net.py IP --shot out.png                    # the running game, saved as out.png
python tools/bm_net.py IP --get bm/shots/pong0007.png out.png   # a file already in bm/shots
python3 tools/pi_test.py --cart build/carts/pong.bm --shot out.png
```

The console saves the PNG in `bm/shots/` as with the key, then sends it. A game must be
drawing frames: with none, the request fails after 3 s (`NG`). Only files of `bm/shots/` (one
level) can be fetched. Not yet proved on the RGB30.

## Names and format

- **Name**: `<PREFIX><NNNN>.PNG`, 8.3 as the card's file system wants. The prefix is the
  first (up to) 4 letters or digits of the game's title (`Pong` → `PONG`, `Star Shooter`
  → `STAR`; `SHOT` if there are none) or of the `prefix` the app gave. The number is one more
  than the highest of that prefix already in `bm/shots/`, from 0001 to 9999: the Pi Zero has
  no clock, so no date.
- **Format**: PNG, 8 bits RGB, the size of the game's screen (640×360, 360×360...). Nothing is
  lost: the console's colours are 16 bits (RGB565) and are widened to 8 bits as `g16_to_rgb24`
  does. No JPG: it spoils pixel art.
- **Encoder** (`src/bm/shot.c`, no library): one deflate block with the fixed codes, whose
  matches are "the pixel to the left" and "the pixel above". A flat screen takes a few KB; noise
  can take as much as the raw picture (about 0.7 MB at 640×360).

## For the manuals

Pictures for a manual are taken on the console, copied from `bm/shots/` to the repository
and given a name that says what they show; the manual cites that name, never the file in
`bm/`. The folder and the naming rule are set with the manuals (open decision). On the PC,
`bmhost --shots` and `scripts/qemu-screenshot.sh` make pictures without a console.

## Test and code

- `make test-shot`: a cart in `bmhost` asks for three pictures (two numbered, one with a
  prefix, two requests in one frame); `tests/shot/check.py` reads the PNGs (CRC, zlib, size,
  pixels).
- `src/bm/shot.c` (encoder and names), `shot_grab` / `l_shot` in `src/bm/runtime.c`.
- Not tried on a Pi or an RGB30 (no hardware in the session): the 3D "later" mode, where
  the frame is shown a frame after, takes the held frame in `later_show`; check it with a 3D
  game on the GPU.
