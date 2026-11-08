# Pi Zero W and PlayStation 1: 3D cost compared

Why a Dark Night scene of 10k triangles felt heavy on the Pi, and what a PS1 does differently. The PS1 numbers are from the public hardware documents and from memory: they are approximate. The Pi numbers are measured (8 Nov 2026, Pi Zero W, clean kernel `v0.2.5-2137-g4ff425ca`). Reports: branch `reports`, folder `claude-darknight-pi-perf`.

## 1. Correction first

The first Dark Night runs on the Pi gave 12.4 fps. They were not valid. The file `bm/config.txt` of that Pi held `gpu3d=0`, a key left by an earlier benchmark. With it the 3D runs on the ARM (`3d=arm` in the `options` line of the report), and the ARM places, lights and rasterizes each face. That path cost about 8 us a face (about 8000 ARM cycles).

With the V3D on (`3d=gpu`) the same walk gives **59.0 fps**, 7.0 ms a frame, 9549 triangles a frame. The ARM spends about 1.1 ms a frame on 8300 faces (0.13 us a face) because the V3D vertex shader places the corners.

The lesson: check `3d=gpu` in the `options` line before you read any 3D number. A key in `bm/config.txt` wins over the default of the code.

## 2. The two machines

| Item | PS1 (1994) | Pi Zero W (bm) |
|---|---|---|
| CPU | MIPS R3000A, 33.9 MHz, no FPU | ARM1176, 1 GHz, VFP |
| RAM | 2 MB (+1 MB video) | 512 MB |
| Vertex work | GTE: a fixed-point coprocessor. One vertex 15 cycles, three vertices 23 cycles, a normal clip 8 cycles, light a few tens | On the V3D: the vertex shader (`gpu3d_vs=2`). On the ARM (3D off): floats, about 8000 cycles a face with the raster |
| Triangles to the GPU | A list in RAM (the ordering table), read by DMA. The CPU writes a small packet | A command list in GPU memory, written by the ARM; the V3D reads it |
| Visibility | No depth buffer. Polygons sorted into the ordering table (painter's order) | Depth buffer, in the GPU |
| Textures | Affine (no perspective correction): they swim. 4, 8 or 15 bit. A 2 KB texture cache | Perspective correct. 16 bit or ETC1 |
| Colour | 15 bit, dithered | RGB565 in bm |
| Typical screen | 320x240 (up to 640x480 interlaced) | 640x360 in Dark Night (360x360 in Yharnam) |
| Rate quoted by Sony | about 360k flat or 180k textured polygons a second | not quoted; measured below |
| Real games | 1.5k to 6k polygons a frame, 30 fps (peaks near 10k) | Dark Night: 9.5k triangles a frame at 59 fps (measured) |

## 3. Measured on the Pi

| Case | fps | Frame | Note |
|---|---|---|---|
| Dark Night, 3D on the ARM (`gpu3d=0`) | 12.4 | 79 ms | 8 us a face; not valid for the default mode |
| Dark Night, 3D on the ARM, 480x270 | 17.1 | 58 ms | the cost follows the pixels and the faces |
| Dark Night, V3D on | **59.0** | 7.0 ms | update 1.9, draw 2.8, audio 2.7, GPU wait 0.8 |
| Yharnam, V3D on | 49.7 | 17.2 ms | the Lua is about 7 ms of it |

## 4. What this says

- The PS1 does not have a stronger machine. The Pi has about 30 times the clock, a floating-point unit and a GPU with a depth buffer and perspective correct textures.
- The PS1 spends little because its work is fixed and small: the GTE places a triangle in about a microsecond, the CPU writes a short packet, and nothing sorts or tests depth per pixel. Its images accept affine textures and a coarse sort.
- The Pi is fast only when the V3D does the vertex work. When the ARM does it in floats, one face costs as much as the whole PS1 triangle path, and a scene of 10k triangles is too heavy.
- So the first rule for a bm 3D game on the Pi: the V3D must be on (`3d=gpu`), the meshes must be taken by the GPU path (`backend mesh` in the phase counters, not `arm path`), and the ARM must not light faces one by one.

## 5. Where to give up quality for speed (PS1 level)

The gain is measured only where a number is given. The others are options that the code allows.

| Lever | PS1 level | What it costs in bm | Measured |
|---|---|---|---|
| Resolution | 320x240 | `screen(w, h)`; the pixels fall with the area | 3D on the ARM: 640x360 to 480x270 gave +38%. V3D not measured |
| Triangle count | 2k to 6k a frame | fewer faces in the map, `FAR` shorter | 3D on the ARM: `FAR` 22 to 14 gave +4%. V3D not measured |
| Light | vertex colours baked in the model | a "lit" mesh (baked light) instead of lamps per face | not measured |
| Shadows | a blob or none | a second draw of each model; leave it out for far models | not measured |
| Sort instead of depth | ordering table | not offered; the V3D has the depth buffer for free | not applicable |
| Affine textures | yes | not offered; no gain on the V3D | not applicable |
| Small textures | 64x64, 4 or 8 bit | `gpu3d_tex16`, `gpu3d_etc1` (keys, off) | not measured |

The levers that cost nothing on the Pi are the first three. Measure each one on the Pi with the V3D on before you keep it.

## 6. Open

- Measure the three levers (resolution, faces, baked light) with `3d=gpu` on a heavy scene. Today they have a number only for the ARM path.
- Find a scene that the V3D path does not take (`arm path` rows in the phase counters). Dark Night has none.
- Keep the test keys out of the shipped `config.txt`: `gpu3d=0`, `r3d_fast=0`, `hero_test`, `hero_prof`.
