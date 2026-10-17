# V3D driver (Pi Zero W, ARM1176 only)

Tracking for M35, M36, M37, M39. Rest of the graphics stack: [graphics](graphics.md).
Numbers: `docs/DRIVERS.md`, `docs/BENCH3D.md` (none here).

## Today

- `v3d_start` / `v3d_busy` / `v3d_wait` (`src/gpu/v3d.c`) are async and polled. No IRQ:
  `INTENA` is never written and `src/kernel/irq.h` has no V3D line.
- Bin then render are sequenced in hardware by a semaphore.
- `queue=2` = two job memory blocks (`JOB_BLOCK` / `JOB2_BLOCK`, `gpu3d.c`): the ARM fills
  N+1 while the GPU renders N. `run_async` / `gpu3d_submit` still call `job_wait()` before
  `v3d_start`, so one GPU job runs at a time (`v3d_start` rejects a second).
- `gpu3d_later=1` (with `queue=2`, off): the frame is held one frame (`held` in `runtime.c`).
  Host test: `make test-queue2d`.
- Cache: `dcache_clean_invalidate_all` 4 times per job. Binner: static 4 MiB tile list + 2 MiB
  overflow (`BPOA` / `BPOS`); `OUTOMEM` is not handled (200 ms timeout, GPU off, ARM takes over).
- Vertex shader is done (`gpu3d_vs=2` default).
- `stat(16)`: the GPU's job time of the last frame (0 on the ARM). Read with the queue on, it
  includes the wait before the ARM looks: trust it only without the queue.
- `r3d_impostor=N` (off; 2..64 px of a 1080-line screen, threshold N*h/1080: `imp_threshold`
  in `r3d.c`; `gpu3d()` 5th; skinned meshes by their pose box). Host test: `make test-impostor`.
- `gpu3d_attr=1` (bm3d 7.1, off; Settings *3D corners*, 3D Bench GPU+VS+A8): compact
  attributes for `GV_BAKED` (5 words a corner, was 9: `vs_baked8`) and `GV_LIT` (10, was 14:
  `vs_lit8`); textured faces too: `vs_tex_rgb8` 6 words (was 8), `vs_lit_tex8` 11 (12),
  `vs_lit_tex28` 10 (11), uv as halves on sheets up to 512; `JOB_CODE` 48 KiB.
  ARM side `put_mesh_corner` / `pack_abc`; shader side the regfile A unpack
  (`UNPACK_A` in `tools/qpuasm.py`, emulated in `tests/gpu/v3d_emu.c`); `probe_attr` keeps it
  only if the pixels match the floats (a step of RGB565 allowed). `make test-gpu3d`: same
  pixels, 28% fewer attribute words. Pi (probe `compact attributes yes`, 3D Bench GPU+VS+A8
  against GPU+VS+FS2 at 60 fps): spheres 837 vs 808 (+3.5%), heroes 72.3 vs 67.3 (+7%, GPU
  job 8.9 vs 10.3 ms), match 35.3 vs 30.8 (+15%, 9.3 vs 10.5 ms), big 11.4 vs 9.2 (triangles
  differ between the runs). Worth turning on; the default waits for a played Overbit with
  `gpu3d_attr=1` (the one played so far was `attr=0`, 1920x1080: 31.7 fps, GPU set the pace
  in 82% of the frames, GPU wait 10.4 ms).
- `gpu3d_tex16=1` (Settings *3D textures: ..., 16-bit*, 3D Bench GPU+T16): opaque sheets as
  RGB565 (type 4, bm3d 5.2); bm3d 7.2: sheets with transparent texels (cut-outs) as RGBA5551
  (type 3) in the same 64x32 T-tiles (`t16_word`), `cut16_of` / `tex_get`. Sheets have 1-bit
  alpha (`g16_sheet_t.alpha`), so nothing is lost but green's last bit (a step of RGB565 at
  most); the alpha shaders discard the same texels. `probe_cut16` draws a 64x32 texture
  with the alpha shader for each order of the fields (red/blue as RGB565 or swapped, alpha
  low or top bits) and keeps the first that gives every pixel within a step of the type's
  bits (`cut16_near`) and every hole as the cleared page (status `alpha
  textures RGBA5551 RGBA4444`, `gpu3d_tex16()` 2, options line `tex16=2`, `stats.cut16`).
  RGBA4444 (type 2) is probed the same way but unused: no sheet has alpha between 0 and 1.
  `make test-gpu3d`: same holes, red and blue the same, green a step at most
  (`EMU_T16_ALPHA=1` alpha on top: learned; `=2`: off; `EMU_T16_LOSSY=1` widens with half a
  step, as the Pi gives some pixels a step off: still learned).
  Pi (report 20261012-171826): type 3 works, order 0 (RGB565's, alpha low) is the right one
  (5551 1845/2048 exact, 4444 1561/2048, other orders under 45%); the exact check failed it.
  Next report: status `alpha textures RGBA5551 RGBA4444` expected.
- `gpu3d_etc1=1` (bm3d 7.3, off; Settings *3D textures: ..., ETC1* also sets `gpu3d_tex16`;
  3D Bench GPU+ETC1): opaque sheets with sides multiples of the ETC1 tile (64x128, or 128x64
  for 4x2 utiles) as ETC1 (type 8), encoded in `tex_get` (`etc1_sheet`, encoder
  `src/gpu/etc1.c`, unit test `make test-etc1`); else the 16-bit / 32-bit chain as before.
  Layout: `etc1_block` (tiles and utile slots from RGB565's `t16_inner`: `etc1_slots`),
  `etc1_put` (byte order). `probe_etc1` draws a 128x256 texture of varied blocks for 8
  candidates (utiles 2x4 then 4x2; bytes BE, words LE, LE, halves swapped) and keeps the first
  whose sampled texels come back (status `ETC1 textures yes (utiles 2x4, bytes BE)`,
  `gpu3d_etc1()`, options line `etc1=1`, `stats.etc1`). Each candidate is also checked
  against the 8 maps of a block's texels (`etc1_at`, `e1_map`: columns/rows reversed,
  transposed); `etc1_sheet` moves each block's texels by the map learned before encoding
  (status adds e.g. `, rows reversed`). Emulator: `EMU_ETC1=0` (2x4, BE), `=1` (4x2, LE: a
  later candidate), `=2` (off), `=3` (2x4, BE, rows reversed as the Pi). `make test-gpu3d`:
  same silhouette, 37 dB PSNR against RGB565, worst channel 9 (also with `=3`).
- Pi (v0.2.5-208): `alpha textures RGBA5551 RGBA4444` (the probe compares within a step of the
  type's bits; the exact compare failed) and `ETC1 textures yes` (type 8 = ETC1, 2x4 utiles,
  bytes BE, each block's rows reversed: `e1_map`). 3D Bench GPU / GPU+T16 / GPU+ETC1 are equal
  within 1-6% (quad_tex 2.20 / 2.35 / 2.19 ns/px, spheres_tex 157 / 158 / 159 fps, heroes_tex
  6.5 all): small sheets sit in the TMU cache, so fewer bytes buy nothing there.
- A failed `probe_cut16` / `probe_etc1` logs to the probes' line (report) `params0`, each
  candidate's right pixels, first 4 texels wanted/got and (5551/4444) an opaque-shader control;
  `etc1_diagnose` adds flat-block, block-map and one-block lines. Tests: `EMU_T16_ALPHA=2`,
  `EMU_ETC1=2`.

Decisions from the Pi reports, `scaling_kernel` and what to measure on the Pi: [v3d-notes](v3d-notes.md).

## To do, in order

1. Pixels at 1080p (R41): the cheap soft enlargement is scaler kernel 6 (config only, no GPU
   cost; below). A QPU bicubic / FSR-style pass stays open, only if 6 is not enough. Cost from
   the Pi 3D Bench: a full-screen 1080p pass with one bilinear read is ~3.5-5 ms of GPU
   (textured fill 1.65 ns/px nearest, 2.3 ns/px bilinear, plus ~0.3 ms fixed); bicubic/FSR
   more, not measured. 2D on GPU: 9.5 ns/px (19.6 ms at 1080p).
2. Impostors: Pi runs at `r3d_impostor` 16, 32 with the new threshold (8 = 2.7 render px
   at 640x360, no effect: session at 8 gave 57.7 fps, GPU 7.8 ms a frame, 8588 tris), photo of
   far heroes; then the default for crowds (R42).
3. Chain two jobs in hardware (CT0/CT1) only if a measurement shows the GPU idle between jobs.
4. M39 remaining. NEXT: is 16-bit / ETC1 worth more work? A played Overbit at 1080p with
   `gpu3d_tex16=1` and `gpu3d_etc1=1` against both off (fps, `GPU 3D` ms; the quads show no
   gain). If nothing moves, the options stay off and M39 closes here; if it does: ETC1 in the
   cartridge (encoded at build time, so no encoding at load), mipmaps; RGBA4444 only if
   sheets get soft alpha. Attributes:
   flags as halves, normals as bytes, then 16-bit positions (scaled per mesh,
   a separate x y z stream for the coordinate shader).
5. **QPU come coprocessore: programmi sulle QPU (M44)**:
   Le 12 QPU eseguono operazioni SIMD su 16 float alla volta: calcoli paralleli su grandi masse di dati, quando il 3D non le satura (giochi 2D, menu) o riservando parte delle QPU nei giochi 3D.
   - **Passo 1 (Driver)**: Lancio di programmi QPU "utente" al di fuori del ciclo di disegno (richieste V3D: puntatore codice, registri uniform, numero QPU, contatore di completamento; attualmente `v3d.c` assegna tutta la VPM ai vertici); coda di dispatch, attesa con timeout, rilevamento all'avvio che disabilita in caso di anomalie e fallback su ARM; esecuzione nell'emulatore `tests/gpu/v3d_emu.c`, passo dedicato nel test `g` e profilo nel 3D Bench.
   - **Passo 2 (Particelle su GPU)**: `particles()` (posizione, velocità, gravità, vita, colore) aggiornate e disegnate via GPU senza carico ARM (migliaia di particelle anziché centinaia), integrate con modulo `lib.particles` in bmlib.
   - **Passo 3 (Effetti a pieno schermo 2D)**: Buio a livelli e bagliori di Yharnam (oggi `g16_fade_*` via ARM), dissolvenze, sfocature, filtri CRT/scanline.
   - **Passo 4 (Effetti audio a blocchi)**: Riverbero FDN, eco e filtri elaborati a blocchi su QPU (anche per nano8).
   - **Passo 5 (Tracciamento raggi)**: `hit3d` batch e linee di vista per bot IA.
   - *Toolkit e registri hardware necessari (da research 2026-10-12)*:
     - Registri di lancio utente: `SRQPC/SRQUA/SRQUL/SRQCS` (0x430..0x43C): scrittura di QPU UL, UA, poi PC; terminazione al conteggio (bit 23:16 di `SRQCS`); richiede `VPMBASE > 0` (oggi 0 in `v3d.c`); reset da mailbox `0x30012` per QPU bloccate; riserva `SQRSV0/1`.
     - Salti in `qpuasm.py` e nell'emulatore (3 delay slot): predicati per pixel, salti solo su dati uniformi; DMA VPM con mutex, campionamento TMU via memoria (`t0s` + `ldtmu0`), semafori `sacq/srel`.
     - Controlli `qpuasm.py`: gestione lettura `ra39/rb39`, vincoli immediati piccoli con `ldtmu`/`thrsw`. Contatori hardware `PCTR*` (0x670) per profilazione fine.
   - *Criterio di completamento*: particelle e buio a livelli di Yharnam gestiti interamente dalla GPU con misure nel 3D Bench e fallback software identico in QEMU (`gpu3d=0`).
6. **Profilazione runtime per motori di gioco (R40)**:
   - Integrazione di due nuove metriche `stat()` nell'overlay e nel dev kit: tempo di elaborazione audio e tempo di attesa della GPU.
   - Permette al governatore dinamico di Overbit e Yharnam di stabilire a runtime se il collo di bottiglia è sul bus CPU/ARM o sulla GPU.
7. **Adattamento dinamico impostor per folle ed eroi (R42)**:
   - Modelli con scheletro semplificati a billboard quad con `r3d_impostor` quando lontani dalla camera; governor di qualità di Overbit/Yharnam per attivare `gpu3d(nil, nil, nil, nil, 8)` a livelli bassi.

