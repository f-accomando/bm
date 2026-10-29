# System: standalone-console ideas and the file layout (SYS-16)

Moved from [system](system.md) on 29 Oct 2026 to keep that sheet under 120 lines. The rules of the system stay in `system.md`.

### Standalone console and hardware ideas
- **WiFi from the menu with the pad**: Settings > WiFi and network with network selection by gamepad. The user enters credentials with the on-screen keyboard.
- **Kernel on-screen keyboard**: Turn `padtype` into a native C kernel service. Use it for WiFi setup, file renaming and the Market. Expose it to games through the Lua API (`textinput(title, default)`).
- **Lua traceback on the SD card**: Save the traceback of the last Lua error of a cartridge on the SD card (boot log, bootprev and lastrun are done).
- **Screenshot**: done in the games' runtime (`shot()`, Print Screen, Select held 3 s, `bm/shots/`: [`SCREENSHOTS.md`](../SCREENSHOTS.md)). To do: the menu's own screens, a try on the Pi and the RGB30 (3D `later` mode), the key's picture in the F12 list.
- **Local web interface (`bm.local`)**: Built-in HTTP server and mDNS responder (`bm.local`). It lets you upload `.bm` files, download screenshots and saves, and edit `bm/config.txt` from the browser of a phone or PC on the local network.
- **Rumble and LED on the DualShock 4**: Expose the APIs `rumble(p, strong, weak, ms)` and `padlight(p, color)`. Use the output report 0x11 that `bt.c` already implements.
- **Extended Bluetooth controller support**: Add profiles for DualSense, Switch Pro Controller, 8BitDo and Xbox gamepads (Bluetooth LE through `ble.c`).
- **HDMI-CEC remote control**: Use the direction arrows and the Select/OK key of the TV remote to move in the console menus without a controller.
- **Network and bmnet multiplayer**: Test two Pi consoles on the *Online 2D* model (also through a relay). Port the Overbit network protocol to the unified `bmnet` library.

- **File layout by type (SYS-16, idea, decided 28 Oct, not started).** Two parts, one rule: system code and user files each have their own place.
  - Source: move the system files of `src/bm/` to `src/core/` (`runtime.c`, `rt_internal.h`, `api_*.c`, `b3d`, `r3d`, `world3d`, `map3d`, `meshcap`, `decimate`, `cutout`, `glb`, `gfx16`, `png`, `jpeg`, `webp`, `webimg`, `web*`, `vid`, `rec`, `shot`, `loading`, `profile`, `require`, `tokens`, `json`). `src/bm/` keeps the files of the `.bm` and `.bme` formats (`format.c`, `project.c`, `n8cart`, loaders) and the files with no section yet.
  - Installed system (Pi and RGB30), what exists: projects `.bme` live in `/bme` (`BM_PROJECTS_DIR`), games `.bm` in `/bm` (the folder both menus read; `/carts` is not read). An old project next to its game is still read and saved where it is. `cart_build` writes `/bm/x.bm` from `/bme/x.bme`. `sdmap` counts `/bm` and `/bme`. The repository path of the `.bme` files does not matter: the 17 in the repository are test fixtures, video chains and two game sources, and the Makefile builds the game projects into `build/carts/` and installs them in `/bme`.
  - Media, games and apps: one folder per file type, like `/bm` and `/bme`. The work is for the other types (`.bmm` models today in `/bmm`, `.glb`, images, sounds). System `.c` files do not go there. The repository folder `carts/` stays in use but is retired step by step: new content does not go there.
  - Write: a download, a `send` (transfer port) and a file made by an app go to the folder of its type. Read: the open dialogs start in the default folder of the type.
  - Space: the SD scan (`sdmap`, `src/kernel/sdmap*.c`) already counts bytes for `bm`, `bme`, `glb`, `bmm` and other. Extend it to report the space of each resource by type folder.
  - Cost of the source move: 201 files cite `src/bm` (Makefile, `rgb30.mk`, scripts, tests, `.claude/skills`, docs, manuals, `CLAUDE.md`); 54 C files include `"bm/..."`. Use `git mv`, then a scripted replace, then build Pi and RGB30, `docs_audit.py` and `consistency.py`.
  - To decide before work: the SD folder name of each other type (`.glb`, images, sounds) and the default folder of each open dialog; the migration of the repository `carts/` content.
