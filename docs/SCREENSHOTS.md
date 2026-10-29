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

A small banner at the top centre says `Screenshot saved` for one second (the file name is in the serial log). The picture is the game's
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
python tools/bm_net.py IP --frame out.ppm                   # the page on the screen now (menu, monitor or game), a PPM
python tools/bm_watch.py IP                                 # the screen in the terminal, live (up to 10 frames a second)
python tools/bm_watch.py IP --mode sixel                    # real pixels, 640x360: the text of the menu is legible
./easy_install.sh watch [profile] [sixel|blocks]      # the same from the installer: key w in its menu, a saved profile and code
```

`--frame` uses op `F` of the transfer port (29 Oct 2026): the kernel copies the page on the screen and sends it whole
(RGB565 or 32 bit, 460 KB at 640x360) without writing on the SD card. `bm_watch.py` uses op `V`, the stream: the kernel
averages the screen down to the size of your terminal (120 columns are 120x68 pixels), sends only the pixels that
changed (skips, runs of one colour, literals), and nothing when the screen does not change, so a Pi Zero W can keep up;
the tool draws only the cells that changed. The half-block picture has one pixel for each column: shapes, not text. For legible text use `--mode sixel`: it asks the console for 640x360 and draws a Sixel image (Windows Terminal 1.22 or later, WezTerm, foot, mlterm); only the bands of 6 rows that changed go to the terminal; `--scale 2` makes it twice as big. Not tried in a real terminal yet. The bridge must be on. A kernel from before `F` or `V` answers `BH`. The
bridge has `pi.py frame OUT.ppm` and `pi.py view SECONDS` (counts the frames and the bytes of a stream, no drawing).

The console saves the PNG in `bm/shots/` as with the key, then sends it. A game must be
drawing frames: with none, the request fails after 3 s (`NG`). Only files of `bm/shots/` (one
level) can be fetched. The RGB30 works (28 Oct).
The 3D Bench has no game loop. After each frame it shows, it calls `bm_shot_page` (`src/bm/api_shot.c`): a request from the PC saves the page as `bm/shots/B3Dnnnn.png`. Send the requests while a scene runs (for example with `bench3d_boot=heroes_tex`). A request between two scenes waits for the next frame.

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
