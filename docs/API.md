# Native `.bm` cartridges: API and first cartridge

Italian version: [API-IT.md](API-IT.md).

> A step-by-step practical guide (sprites, maps, 3D models, sound, lights, saves):
> [GAME-GUIDE.md](GAME-GUIDE.md). In Italian: [API-IT.md](API-IT.md) and
> [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md).

A `.bm` cartridge is a game for bm written in **Lua 5.4**. The kernel draws in C
(640×360, 16-bit RGB565 colour, 60 frames a second); Lua only does the logic. Complete
examples in `carts/`: `pong/`, `snake/`, `shooter/` (code only) and `demo/` (with a PNG
sprite sheet and a CSV map).

## The first cartridge in 5 minutes

1. Create `carts/hello/main.lua`:

   ```lua
   local x, y = 300, 160

   function _init()                 -- once, at start
   end

   function _update()               -- 60 times a second: logic
     if btn(0) then x = x - 3 end   -- left
     if btn(1) then x = x + 3 end   -- right
     if btn(2) then y = y - 3 end   -- up
     if btn(3) then y = y + 3 end   -- down
   end

   function _draw()                 -- 60 times a second: drawing
     cls(0x102040)                  -- dark blue background
     circfill(x, y, 20, 0xFFD050)   -- a yellow ball
     print("hello from bm!", 8, 8, 0xFFFFFF)
   end
   ```

2. Pack it:

   ```sh
   python3 scripts/mkbm.py -o hello.bm --lua carts/hello/main.lua --title "Hello" --author "me"
   ```

   To add it to the build, put its name in `GAMES` in the `Makefile` and its title in a
   line `title_hello := Hello`: `make` creates it in `build/carts/hello.bm`.

   **Cover** (optional): `--cover cover.png`, a PNG of any size that the menu prints on
   the card: an 88×88 square (2026-10-04; 128×80 before). A square picture is scaled
   down; others stay whole, as wide as the square, over a blurred and darker copy of
   themselves that fills the rest. In the build a file `carts/hello/cover.png` is enough;
   those of the demo games are drawn by `scripts/mkcovers.py`. Without a cover the menu
   prints the title.

3. Try it:
   - **on the Pi**: copy `hello.bm` into the `carts/` folder of the SD card, power on (or
     press `R` in the menu): it appears in the menu with its title and author;
   - **in QEMU** (without a Pi):

     ```sh
     qemu-system-arm -M raspi0 -bios build/kernel.img -serial tcp:127.0.0.1:4444,server=on,wait=off -serial null &
     python3 tools/bm_load.py tcp:127.0.0.1:4444 --cart hello.bm
     ```

     (from the menu or the monitor: the loader sends `U`, then the cartridge);
   - **from the serial line** (a Pi with a USB-serial adapter):
     `python3 tools/bm_load.py /dev/ttyUSB0 --cart hello.bm`.

Ctrl+Esc (keyboard), PS or Start+Select (gamepad) go back to bm's menu leaving the game
**suspended** (still in memory: A on its cover resumes it where it was; if the cartridge has
`_exit()`, they ask it first); Esc alone, in a game, is Start (its menu). In a **network
match** (`online(true)`) the game is not suspended: the system asks the player who leaves,
only on their console, "Leave the match?" (they will leave the game and disconnect from the
server); yes calls `_leave()` and closes the game, back stays. From the serial line Ctrl+\
is Ctrl+Esc and `q` closes without asking. The system's keys (one table,
`src/kernel/syskeys.c`) and the cartridge's are shown while F12 is held. If there is a Lua
error, the cartridge stops and the error, with its line, appears on the console.

## Structure

| Function | When |
|---|---|
| `_init()` | once, after loading |
| `_update()` | every frame (60 Hz), before `_draw` |
| `_draw()` | every frame, after `_update` |
| `_exit()` | (optional) Ctrl+Esc, PS or Start+Select: `true` closes now; `false` stays (the cartridge asks, for example "unsaved changes: Ctrl+Esc again leaves", and closes later with `quit()` or at the second Ctrl+Esc) |
| `_leave()` | (optional) in a network match (`online(true)`), when the player confirms leaving: the game tells the server (or the host), then the cartridge closes without being suspended |

Globals: `SCREEN_W` and `SCREEN_H` (640 and 360; 480 and 270 with `--res 480x270`; 320 and
180 with `--res 320x180`; 256 and 256 with `--res 256x256`; 360 and 360 with `--res 360x360`, the
`.b16`'s screen). The cartridge can change its
resolution while it runs with `screen(w, h)` (below): from the next frame the new sizes hold.
The screen is **not** cleared by itself: `_draw` usually starts with `cls()`.

Limits: an error or an endless loop (over **20 million Lua instructions** in one frame)
stops the cartridge without blocking the console. Sandbox: no `io`, `os`, `load`,
`dofile`; `require` loads only the libraries built into the kernel (`"bmlib"`, the games'
shared library: [below](api/bmlib.md#bmlib-the-games-shared-library); `"assist"`, the assistant's
panel; `"bm3d"`, what bm Studio and bm Animator share; `"predict"` and `"words"`, the word
completion); there are `string`, `table`, `math`, `utf8`, `coroutine`.

## Colours

Colours are integers `0xRRGGBB` (e.g. `0xFF8000` orange) or `rgb(r, g, b)` with values
0–255. The screen turns them into RGB565 (5 bits red, 6 green, 5 blue).

## Reference

Coordinates are in pixels, (0,0) at the top left; `w` and `h` are width and height.

The reference is split by area. **Read only the file you need** (and grep inside it).

| File | Contents |
|---|---|
| [api/draw.md](api/draw.md) | Screen and shapes · Sprites and map |
| [api/input.md](api/input.md) | Keys, gamepad, mouse, touch |
| [api/system.md](api/system.md) | Time and system · Network (UDP) · Saves · Documents |
| [api/sound.md](api/sound.md) | Sound effects and music · Notes · Instruments, tone, room · The bank |
| [api/tools.md](api/tools.md) | Keyboard and files · Assistant (M30) |
| [api/light3d.md](api/light3d.md) | nano8 · Light · Light by levels · 3D (software) · Collision worlds |
| [api/bmlib.md](api/bmlib.md) | The games' shared library |
| [api/riff.md](api/riff.md) | Music as patterns |
| [api/bmnet.md](api/bmnet.md) | Games over the network |

## Budget and tips

- 60 fps = **16.7 ms** per frame for `_update` + `_draw` + the copy to the screen. At the
  top left of the demo, `stat(1)` shows how much the cartridge uses.
- The **dev kit**: the performance overlay over any game, at the top right. It is turned on
  from Settings > Screen and sound > "Performance overlay" (Off, Simple, Detailed,
  Functions: it stays saved), with F11 on the keyboard (a system key, also in the tools; it
  was F3), with `p` from the serial line or with `devkit(mode)` from the game: once the
  simple page, again the detailed one, again the functions page, again off. F11, `p` and `devkit()` last one run: every game starts (and
  resumes) as Settings says. The simple page:

      60fps 6.1ms ^7.5      frames a second; ms of _update + _draw: average and,
                            after ^, the top of the last second
      lua 9k ^10k           Lua instructions of a frame (thousands): average, top
      ram 612k ^700k        memory: Lua now plus the data (sheet, map, models,
                            sounds, z-buffer) and, after ^, the top
      1234 tokens           the code's tokens (code_tokens)

  under it, the time of the last 64 frames: the top is 16.7 ms; green under half, yellow up
  to 16.7, red beyond (the frame is skipped). The detailed page adds the last frame in its
  phases and the 3D:

      update 5.8ms x2       all its _update (x2: two ran, frameskip)
      draw   17.4ms         its _draw
      3D     13.3ms         the 3D calls (stat(6))
      gpu 9.1ms 2 jobs      the GPU's work (with the ARM: px, the pixels it drew)
      tri 3620 vtx 5699     triangles drawn, vertices placed
      bm3d 2.1 GPU          the 3D driver (ARM, GPU, GPU+AA)
      quality HIGH          the game's lines (devinfo)

  The **functions** page (R14) shows, under the simple one, the ten functions that cost
  the most, in ms a frame, the average of the last second (60 frames):

      function        self   all
      draw_scene      2.28  6.43    a Lua function: its own time and with what it calls
      map             0.94  0.94    in light blue the console's functions (spr, map, the 3D...)
      _draw           0.03  7.70    the callbacks have their names

  `self` is the function's own time, `all` the time with the functions it calls: a
  function with a big `all` and a small `self` spends its time in the others. The console's
  functions are measured exactly, the Lua ones in pieces of 1000 instructions (enough to see
  where the time goes). The profiler runs only with this page or `profile(true)`; off it
  costs nothing. From the code: `profile()` (below).

- The **last session's report** (2026-10-10): when a game ends the dev kit writes one file
  per game into the reports, `reports/<branch>/session_<game>_<board>.txt`, which the next
  run of the same game **replaces**. In it: screen, frames and fps, the mean and most time
  of `_update` + `_draw`, memory, and the time **from one picture to the next** in six
  classes (up to 18 ms, 25, 34, 50, 100, more) with the 12 longest frames and their parts
  (`cpu`, `upd`, `draw`, 3D, copy and `outside`: the time that is none of them, the SD
  card, the GPU, the machine), and the game's own `devinfo()` lines as the run ended ("the game says:"; Yharnam: the chunks the view waited for). A half-second freeze is no longer lost after the overlay's 64
  frames. It goes from the menu when there is a network. `bm/config.txt`:
  `session_report=0` never, `=1` always; without the line, only with a `github_token`.

  On the big screens it is bigger (x2 from 1280 wide, x3 at 1920). From the code:
  `stat(1)`, `stat(2)`, `stat(6)`, `stat(10)`, `stat(11)`–`stat(15)`, `devkit()`,
  `profile()`. The
  limit is 20 million instructions per call. The
  **SDK's dev kit** (F1 twice) has the same numbers for the project: tokens, the biggest
  functions, the data's memory, the file against the 8 MiB of a `.b16` and the numbers of
  the last try (F5). At the end of a run the serial line also writes the line `dev kit:
  Lua peak ... KiB, data ... KiB, busiest frame ...k instructions, ... frames over 16.7 ms,
  ... tokens`.
- Drawing is in C: a `spr` or `rectfill` call costs a few microseconds, but every call from
  Lua has a fixed cost. Orders of magnitude on the Pi (docs/STRESS.md): ~1800 16×16 sprites
  called from Lua at 60 fps, ~4500 from C; ~1200 3D triangles.
- Avoid creating new tables in every frame if not needed (less work for the GC).
- Text is in 8×16 cells: for well aligned writing use x multiples of 8 and y of 16.
