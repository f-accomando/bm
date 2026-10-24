# V3D: Pi measurements and decisions (21-23 Oct)

Split from [v3d](v3d.md); open work and rules stay there.

- Pi Zero W, 3D Bench, 21 Oct (bm3D 7.x hardware check): score 3412 (GPU 953, ARM 166). Spheres:
  ARM 70 fps, GPU+VS 809, GPU+VS+A8 840 (+3.8% over GPU+VS). Draw queue: VS 573 fps, VS+queue
  643 (+12%). Compressed textures: T16 161.6 fps and ETC1 160.6 fps, no gain, so they stay off.
  Step 16 of the GPU test (removed on 24 Oct 2026): identical pixels on the ARM and the GPU.
- Pi Zero W, 22 Oct (reports branch): GPU test (removed on 24 Oct 2026) 16/16 ok (3311 triangles: ARM 28.2 ms, GPU 9.7 ms). 3D Bench score 3382 (GPU bm3d 2.1 = 950, ARM = 169).
- Pi Zero W, 22 Oct, hero path (3D Bench `herotris`, one profile for each run; kernel built from `bm-core` 38799d49 plus the bench; 640x360; ARM 1000 MHz; V3D 250 MHz, measured 300; reports `20261022-171354` to `-171450` on the `reports` branch). A set is one hero of 1536 triangles, its shadow and a map of 5000 triangles. Benchmark numbers only, not games.

  | profile | sets at 60 fps | hero tris at 60 fps | drawn at 60 fps | ms | gpu_ms | vs mesh |
  |---|---:|---:|---:|---:|---:|---|
  | ARM | 0.41 | 630 | 2393 | 40.7 | - | NA |
  | GPU | 0.56 | 859 | 3265 | 29.8 | 2.1 | NA |
  | GPU+VS | 3.85 | 5907 | 28961 | 14.9 | 3.9 | ACCEPTED |
  | GPU+VS+Q | 6.74 | 10351 | 50746 | 13.0 | 7.1 | ACCEPTED |
  | GPU+VS+A8 | 4.03 | 6182 | 30310 | 16.6 | 5.1 | ACCEPTED |
  | GPU+VS+A8+Q | 6.75 | 10364 | 50809 | 13.2 | 7.1 | ACCEPTED |
  | GPU+VS+Q2 | 6.74 | 10353 | 50755 | 13.3 | 7.1 | ACCEPTED |

  - The vertex shader places the 1536 triangles of the hero (`probe_gltris` 1536, `probe_meshes` 1): GPU+VS gives x6.9 over GPU and x9.4 over ARM.
  - The queue gives x1.75 over GPU+VS (3.85 -> 6.74 sets). The design range was +10% to +45%, so the queue does better than the estimate on this scene. At GPU+VS the frame is ARM-bound (`gpu_ms` 3.9 of 14.9 ms). With the queue the GPU works 7.1 of 13.0 ms.
  - A8 gives +4.7% without the queue (3.85 -> 4.03) and 0% with it (6.74 -> 6.75).
  - The frame-later model (Q2, `later_drawn60` 51222.8 against 50755) shows no gain over Q: +0.9% of triangles drawn, and the same sets (6.74).
  - Job times at the last step: `bin_us`/`rnd_us` GPU+VS 947/2928, GPU+VS+A8 1254/3811; the queue profiles print 0 and about 7070 (the bin time is not split out under the queue).
  - Boot probe: the status line of every GPU report says `cel light yes`.
  - Open issue: the Pi froze once on `b3d tests=herotris` with all profiles in one line (see `docs/NEXT.md`, Open issues).
- Pi Zero W, 22 Oct, cel cost (3D Bench `cel`: n heroes, textured, 1536 triangles each, lit by the sun; reports `20261022-171533` ARM, `-171542` GPU+VS). The bench ramp stops at 50 heroes (76800 triangles) for lit and cel.

  | profile | lit ms | cel ms | cel over lit | outline ms (flag 512) | outline over lit | gpu_ms lit / cel / outline |
  |---|---:|---:|---:|---:|---:|---|
  | GPU+VS (n = 50) | 11.0 | 12.1 | +10% | 14.4 | +31% | 6.9 / 6.9 / 8.2 |
  | ARM (n60) | 14.5 (n60 3.4) | 14.6 (n60 3.3) | +1% | 12.5 (n60 2.5) | - | - |

  - Cel is on the ARM path already, so the ARM rows are equal: cel does not cost more there. Dither off (`nd`) is the same as dither on within 0.1 ms on both profiles.
  - Outline +31% is under the +60% gate, so the distance limit of the outline is not needed. At 1 to 4 heroes the outline over lit is +5% to +23% on GPU+VS.
  - The outline test draws the hull as a second mesh: 116736 triangles at n = 42.4 (the ramp reached 42.4 heroes at 60 fps, `n60`).
- Decision (user, 22 Oct): `gpu3d_attr` stays OFF by default. A8 gains only 3.8% on the
  spheres and it is a compact-attribute path with its own probe. Turn it ON only for the
  heavy-mesh games where the Pi reports above (heroes, match) show a gain.
- Decision (user, 23 Oct, replaced the one above for a few hours): any gain counts, so both were built on by default and the Pi decided. Pi (23 Oct): `gpu3d_attr` no gain, so it is off again (the 22 Oct decision stands). Draw merging: +24% for 100 small pieces, -26% for 10 big ones, so it stays on with a threshold (32 pieces, at most 150 faces a piece). Re-measure of `ht_p10m` pending; not verified on hardware.
