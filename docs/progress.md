# bm progress (index)

Working notes on the **current** state of bm (BareMetal), for whoever changes the code: how
each area is built today, what is open, and the rules that keep it working. One topic lives
in one file; the others link to it.

Not here: history (git log), measurements (`docs/PRESTAZIONI.md`, `docs/STRESS.md`,
`docs/M33-PRIMA-DOPO.md`, `docs/BENCH3D.md`), plans and milestones
([`docs/ROADMAP.md`](ROADMAP.md)), the game API ([`docs/API.md`](API.md)), working rules
(`CLAUDE.md`).

## Subsystems

| File | Scope | Main code |
|---|---|---|
| [system](progress/system.md) | boot, MMU, IRQ, fibers, crash reports, USB, SD/FAT, Bluetooth, WiFi, network, menu and kernel services | `src/boot/`, `src/arch/`, `src/kernel/`, `src/drivers/`, `src/usb/`, `src/fs/`, `src/bt/`, `src/wifi/`, `src/net/` |
| [graphics](progress/graphics.md) | framebuffer, 2D, software 3D, V3D driver, Mali bring-up, rendering tests | `src/drivers/fb.c`, `src/bm/gfx16.c`, `src/bm/r3d.c`, `src/gpu/`, `src/rgb30/mali.c` |
| [lua](progress/lua.md) | Lua VM, cartridge runtime, built-in libraries, dev kit, profiler, session report | `third_party/lua/`, `src/bm/runtime.c`, `src/bm/require.c`, `src/script/`, `src/bm/profile.c` |
| [audio](progress/audio.md) | synthesizer, sound bank and player, presets, outputs, riff, music assistant | `src/audio/`, `src/script/riff.lua`, `src/ai/music.c`, `src/rgb30/rk_audio.c` |
| [platforms](progress/platforms.md) | Pi Zero W / Pi 1, Pi Zero 2 W (off), RGB30, `bmhost`, QEMU | `src/rgb30/`, `rgb30.mk`, `tests/host/`, `tests/qemu_test.py` |
| [tools](progress/tools.md) | the suite (SDK, Code, Pixel, Studio, Animator, Mesh, Sound, Write), assistant, PC scripts, Market, videos, CI | `carts/`, `src/ai/`, `scripts/`, `tools/`, `market/`, `video/` |

## Open milestones (detail in `docs/ROADMAP.md`)

- M35, M36, M37, M39 — V3D: [graphics](progress/graphics.md)
- M41 — RGB30 Mali and Overbit `.b16`: [graphics](progress/graphics.md), [platforms](progress/platforms.md)
- M42 — the `.b16` profile: [lua](progress/lua.md)
- M43 — sprite stacking, M44 — QPU programs: [graphics](progress/graphics.md)
- M45 — bm Write, M47 — projects, M48 — mouse in the suite: [tools](progress/tools.md)
- M46 — Audio 2: [audio](progress/audio.md)

## Keeping these files

- State only: what is true in the code now, with file and symbol names to grep. No dates,
  no "we did", no measurements, no closed milestones.
- A change that makes a line false fixes or removes it in the same commit.
- Each file stays around 80–120 lines; detail belongs in the topic doc it links.
