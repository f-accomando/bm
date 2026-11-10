# bm SNES-16 (native ARM)

Native ARMv6 SNES-16 engine with no 65C816, LoROM/HiROM bus, or SPC700 emulation. Reuses the ARMv6 SIMD PPU (`src/bm/snes_ppu.c`, `src/bm/snes_ppu_arm.S`) and S-DSP (`src/bm/snes_dsp.c`, detached via `dsp_set_mem`), driven directly from C and native `.b` ARMv6 modules (`BMN1`).

## How it is today

- **Engine**: `src/bm/snes16.h`, `src/bm/snes16.c` — direct PPU mode/scroll/OAM/CGRAM/Mode 7 control, scanline HDMA tables (`S16_HDMA_FIX`, `S16_HDMA_SCROLL`, `S16_HDMA_M7`, `S16_HDMA_MODE`), 2/4/8 bpp tile upload, and PCM16-to-BRR encoder for S-DSP 8-voice audio (`snes_audio_mix`).
- **Native ARM fast-path**: `BMN_HOST_S16_*` builtins in `src/bm/bmnative.h` and `src/bm/bmlc.c`; `api_vm_set_fast_host(s16_host_call)` in `src/bm/api_vm.c` dispatches calls directly in C without touching the Lua stack.
- **Lua API**: `src/bm/api_snes16.c`, registered in `src/bm/runtime.c`, documented in [api/snes16.md](../api/snes16.md). Shares `snes_blit_target` and `snes_blit_sheet` with `src/bm/api_snes.c`.
- **Cartridge & slim library**: `carts/snes16/` (`src/snes16.lua`, `src/engine.b`, `src/main.lua`, `build/build.py`) builds `build/carts/snes16.bm` and `.bme`.
- **Tests**: `make test-snes16` runs `tests/snes16/core_test.c` (8 checks, linked without `snes_cpu.c`/`snes_bus.c`/`snes_spc.c`) and `tests/snes16/render_test.py` in `bmhost`. Not measured on Pi Zero W or RGB30 hardware yet.

## Open work

- Measure `ARM us`, `PPU us`, and `DSP us` on Raspberry Pi Zero W (`native` and `GPU 2x`) and RGB30.
- Full 16-bit SDK UI (Coder, Compiler, Sprite/CGRAM, Runner) in a separate session.
