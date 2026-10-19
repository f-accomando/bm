# Per-device UI Reference: Keys, Menus, and Screen

This document is the fixed reference for device-specific input controls, menu behaviors, default screen resolutions, and scaling rules across supported bare-metal targets.

## RGB30 (PowKiddy RGB30)

| Aspect | Specification | Source | Status |
|---|---|---|---|
| Panel size | 720×720 square DSI panel (Sitronix ST7703) | `docs/RGB30.md:6,160`, `docs/progress/platforms.md:33` | implemented |
| Menu resolution | 360×360 shown ×2 (fills 720×720 panel); 3 covers per row | `docs/RGB30.md:14,206`, `docs/spec/divergences.toml:333` | implemented |
| Default game resolution | 360×360 shown ×2 for `.b16` (or 720×720 native); `screen()` offers 360×360 and 720×720 | `docs/RGB30.md:14,175`, `docs/spec/divergences.toml:344` | implemented |
| Scaling rule | Video controller (VOP2) hardware scaling ×2 (`bm_scale`, `bm_smooth`), sharp or smoothed | `docs/RGB30.md:175-176`, `docs/spec/divergences.toml:344` | implemented |
| Confirm button | B = OK / confirm (bottom button); `confirm=a` in `bm/config.txt` swaps to A | `docs/RGB30.md:166-167,220`, `docs/progress/platforms.md:35`, `docs/spec/divergences.toml:355` | implemented |
| Back button | Y = cancel / back (target, user decision 2026-10-19; currently A = back, code uses `pad_back`) | `docs/RGB30.md:166-167,220`, `docs/progress/platforms.md:35`, `docs/spec/divergences.toml:355` | to implement |
| X button | Opens cartridge options in menu (play, open in bm SDK, type, size, author, save data, delete) | `docs/RGB30.md:210,212`, `docs/progress/platforms.md:36` | implemented |
| Y button | Target: cancel / back in menus and apps (user decision 2026-10-19; currently contextual secondary action in apps / tools) | `docs/RGB30.md:223`, `docs/spec/divergences.toml:366` | to implement |
| L1 / R1 | Move between menu tabs (Market, Games, Dev, Settings) without wrapping | `docs/RGB30.md:219`, `docs/progress/platforms.md:36` | implemented |
| D-pad | Navigate covers, menu items, and settings rows | `docs/RGB30.md:219-220`, `docs/progress/platforms.md:35` | implemented |
| Analog sticks | Dual analog sticks (SARADC channel 3 with switch) for in-game movement and camera | `docs/RGB30.md:163-165`, `docs/progress/platforms.md:35` | implemented |
| System shortcuts | Start+Select leaves game / app; Select+L1+R1 takes screenshot (`bm/shots/`) | `docs/RGB30.md:202`, `docs/spec/divergences.toml:377` | implemented |
| Button prompts | Colored button glyphs: A green, B blue, X red, Y yellow (`PROMPT_RGB30_*`) | `docs/RGB30.md:222-224`, `docs/progress/platforms.md:36-38` | implemented |
| Code abstraction | Kernel code uses `pad_ok` and `pad_back` for confirm / cancel actions | `docs/progress/platforms.md:36`, `docs/spec/divergences.toml:360` | implemented |

## Raspberry Pi Zero W

| Aspect | Specification | Source | Status |
|---|---|---|---|
| Panel / display | 16:9 HDMI output (BCM2835 framebuffer, up to 1080p) | `docs/spec/divergences.toml:332`, `docs/progress/platforms.md:10` | implemented |
| Menu resolution | 640×360 native layout, 6 covers per row | `docs/spec/divergences.toml:332`, `docs/progress/platforms.md:4` | implemented |
| Default game resolution | 640×360 16:9; square cartridges (e.g. 360×360) are boxed in 16:9 black | `docs/spec/divergences.toml:343`, `docs/PROJECT-LAYOUT.md:48` | implemented |
| Scaling rule | Integer / soft scaling to HDMI target via `menu_scale` (default 3: 1920×1080) and scaler kernel | `docs/progress/v3d.md:78-82`, `docs/spec/divergences.toml:332` | implemented |
| Confirm button | A confirms (DS4 Cross, USB keyboard Enter/Space, gamepad button 0) | `docs/RGB30.md:168-169`, `docs/spec/divergences.toml:354` | implemented |
| Back button | B cancels / goes back (DS4 Circle, USB keyboard Esc/Backspace, gamepad button 1) | `docs/RGB30.md:168-169`, `docs/spec/divergences.toml:354` | implemented |
| X button | Opens cartridge options / details (DS4 Square, keyboard C) | `docs/RGB30.md:212`, `docs/spec/divergences.toml:365` | implemented |
| Y button | Secondary action (DS4 Triangle, keyboard X) | `docs/spec/divergences.toml:365` | implemented |
| L1 / R1 | Move between menu tabs (Market, Games, Dev, Lib, Settings) | `docs/RGB30.md:219` | implemented |
| D-pad | Navigate grid covers, list rows, and options | `docs/RGB30.md:219` | implemented |
| Keyboard & mouse | Full USB and Bluetooth keyboard and mouse support (Italian/US layouts) | `docs/spec/divergences.toml:387`, `docs/progress/platforms.md:16` | implemented |
| System shortcuts | F11 (audio log), F12 (held: key list), Print Screen (shot), Ctrl+Esc / Start+Select (leave) | `docs/spec/divergences.toml:376` | implemented |
| Button prompts | DS4 glyphs or keyboard prompts depending on active input device | `src/kernel/settings.c:434-436`, `docs/RGB30.md:168` | implemented |
| Code abstraction | Kernel code uses `pad_ok` and `pad_back` for confirm / cancel actions | `docs/spec/divergences.toml:360` | implemented |

## Raspberry Pi 1 (A, B, A+, B+, CM1)

| Aspect | Specification | Source | Status |
|---|---|---|---|
| Panel / display | 16:9 HDMI output (identical BCM2835 framebuffer and video modes as Pi Zero W) | `docs/progress/platforms.md:10,16` | implemented |
| Menu resolution | 640×360 native layout, 6 covers per row | `docs/spec/divergences.toml:332`, `docs/progress/platforms.md:10` | implemented |
| Default game resolution | 640×360 16:9; square cartridges boxed in 16:9 black | `docs/spec/divergences.toml:343` | implemented |
| Scaling rule | Identical to Pi Zero W (`menu_scale`, scaler kernel 6) | `docs/progress/v3d.md:78-82` | implemented |
| Confirm button | A confirms (identical to Pi Zero W) | `docs/spec/divergences.toml:354` | implemented |
| Back button | B cancels / goes back (identical to Pi Zero W) | `docs/spec/divergences.toml:354` | implemented |
| Other buttons | Identical button mappings (X, Y, L1/R1, D-pad, Start, Select) to Pi Zero W | `docs/spec/divergences.toml:354-370` | implemented |
| Peripherals | USB onboard hub / Ethernet (SMSC95xx on B/B+), USB keyboards, mice, and gamepads | `docs/progress/platforms.md:10`, `src/kernel/settings.c:438-440` | implemented |
| System shortcuts | Identical system keys (F11, F12, Print Screen, Ctrl+Esc, Start+Select) | `docs/spec/divergences.toml:376` | implemented |
| Code abstraction | Kernel code uses `pad_ok` and `pad_back` | `docs/spec/divergences.toml:360` | implemented |

## Open decisions

- **Role of button A on RGB30**: With the target mapping B = OK and Y = cancel/back (user decision 2026-10-19), the role of button A after the change is still open. Currently, button A acts as back (`pad_back`).
