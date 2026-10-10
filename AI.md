# bm — AI on the Pi Zero W: what can and cannot be done

Italian version: [AI-IT.md](AI-IT.md).

Notes from 2026-09-30. They replace the old milestones AI-01…AI-12, which are **not** a
roadmap: here is only what makes sense for bm, in order of usefulness.
Reference numbers: [docs/HARDWARE.md](docs/HARDWARE.md), [docs/PRESTAZIONI.md](docs/PRESTAZIONI.md).

## Decision 2026-10-01: the first step is the development assistant (M30)

bm's first AI helps **make** games: an assistant that the development tools open with a key
(how to write a snippet, what an error means, the base of a sprite). It is only called
directly, does not work in the background and takes no RAM until asked. It is built from the
pieces below: an INT8 engine in C with the ARMv6 SIMD, training on the PC and inference only
on the Pi, bit-exact tests inside `make test`, and the hybrid approach (the network chooses
among the entries of a knowledge base and among the sprite recipes, classic code does the
rest). Details, numbers and what to try on the Pi: [docs/ROADMAP.md](docs/ROADMAP.md), M30;
API in [docs/API-IT.md](docs/API-IT.md) ([docs/API.md](docs/API.md) in English). The same
engine will later serve the games (the Titan Clash CPU opponent, below).

## Most sensible and useful for bm

**In games: this is where AI really helps.** Titan Clash CPU opponent, recognition of combos
and gestures from the gamepad, recognition of drawings in the editor, small sprite or level
generators. They cost very little per frame and are the most concrete use case.

**Inference engine in C, with Lua functions: yes.** A few hundred lines (dense, conv2d,
depthwise, ReLU, pooling, softmax) plus `ai.load` / `ai.run` for cartridges. In pure Lua no:
~100 ns per operation, ~140 000 operations per frame.

**Tiny ML (MLP, INT8, fixed point): yes, it is the strong point.** No NEON, but ARMv6 has SIMD
(SMLAD: 2 16-bit multiply-accumulates per instruction): an MLP of ~10 000 parameters runs in
tens of µs. The real limit is RAM bandwidth (~100–200 MB/s): INT8 weights and small networks.

**Train on the PC, run on the Pi: yes.** Training and quantization on the PC (PyTorch), a
script in `tools/` exports, the Pi only does inference, with no PC or cloud while running.

**Model format: yes, simple.** Header + layers + INT8 weights + scales, CRC like the `.bm`
files; better as a section inside the `.bm` cartridge (the game carries its model). No
elaborate versioning or metadata while there is only one consumer.

**No separate AI task: not needed.** bm has no scheduler (one core, 60 fps loop, interrupts).
Inference is split into steps with a per-frame budget (e.g. 2 ms), in the time the game
leaves free; it is measured and shown on screen.

**Tools: few, on the PC.** Scripts (train → quantize → export) and a test that compares the
engine's outputs bit for bit with the reference model inside `make test`. An emulator is
useless: the C already runs on the PC and in QEMU; the profiler is a monitor item.

**Hybrid approach: yes, preferred.** A classic algorithm does the bulk + a tiny network
chooses the parameters (how much to sharpen, which palette) instead of computing every pixel:
the only way to use it in graphics without going over the frame budget.

## Possible, but later

**Learning during play: on a small scale.** Q tables or a tiny MLP that adapts to the player
during the match; real training stays on the PC.

**"1080p → ~244p" downscaling: only as asset import.** On the Pi there is no 1080p source and
bm uses 640×360 and 320×180. Useful for photos/renders → sprites (the "3D→sprite" of M22), on
the PC or on the Pi in a few seconds; first it must be checked against classic averaging +
sharpening + dithering.

**Super-resolution: only on still images.** Not in real time, and not needed: the GPU already
scales 640×360 → 1080p for free. On covers and sprites yes, also as SR-LUT (the trained
network becomes a table), but in seconds, not per frame.

**Tiny CNN: the network yes, the source is missing.** A MobileNet reduced to 96×96 greyscale
runs in a few tens of ms (a few fps). Images only from SD or the network (see camera below).

**VideoCore IV GPU (QPU): a project of its own.** The only acceleration available (~24 GFLOPS
theoretical), startable without Linux through the firmware mailbox, like GPU_FFT; it needs
QPU assembly. It is what would make neural graphics feasible, not a first step.

**Tiny language model: demo only.** ~15M parameters (llama2.c "stories" style) would produce,
by estimate, a few tokens per second: little fables in English, not an assistant.

**Model library and sharing: when needed.** Only when two or three games really use a model;
then it goes through the GitHub store (M25), not a separate channel.

## To discard or out of reach

**Real-time neural graphics (denoising, edges, textures): no.** A full-screen pass reads and
rewrites 0.46 MB, ~5 ms before computing anything; bm's rasterizer produces no noise to
remove. Offline only, on assets.

**Neural image compression: no.** RAM (448 MiB) and SD are not a limit, and decoding with a
network costs much more than RLE or PNG.

**Sensors, signals, anomalies: not today.** bm has no I2C/SPI, microphone or analog inputs;
the available data are controllers, keyboard, network and CPU temperature.

**Camera: no.** CSI without Linux depends on the GPU's closed stack; a USB webcam would take
the Zero's only port (one device at a time, no hub).

**Voice: no.** No microphone (it would need an I2S microphone on the GPIOs and its driver);
speech synthesis only with formants on the existing synth, not neural.

**Other Pis and accelerators (Zero 2 W, Pi 3/4/5, NPU): out of reach.** bm runs only on the
BCM2835 (Zero W, Pi 1); a multicore ARMv8 is a new kernel, the Pi 5's AI is an external PCIe
board. It is enough to keep the format portable (INT8 + scales).

## A sensible first step

INT8 engine in C + Lua functions + export script, tried on a real case (the Titan Clash CPU
opponent) with the measured time shown on screen. Vision, scaling and neural graphics come
later, and only if the GPU is tackled.
