# Mali (RGB30)

The RGB30's Mali-G52 driver, part of [graphics](graphics.md). Driver versions: [`docs/DRIVERS.md`](../DRIVERS.md); the device: [`docs/RGB30.md`](../RGB30.md).

## Mali-G52 (`src/rgb30/mali.c`, `mali.h`, `mali_desc.c`)
bm3d 6.0-6.1: supply, power domain, identify, reset, cores, MMU (LPAE, 2 MiB blocks), job
manager (WRITE_VALUE chain), a FRAGMENT job that clears a surface (6.1) and, written and not yet a version, a batch of 4 triangles
(Vertex, 4 Tiler and Fragment jobs, a colour per draw, a 96x96 square of the screen; the step is only on the console,
`mali_hw_t.triangle`). The triangle step is not yet run on the device: the Dev > GPU test
report says (`mali: the vertex job wrote ...`, `mali: a triangle: inside ... outside ...`, `mali: a batch of 4 triangles ...`). PC test with
a simulated GPU (no tiler there): `make TARGET=rgb30 test-mali`. The 3D on the RGB30 is `r3d`.
- Descriptors: `mali_desc.c` (Primitive, Draw, Renderer State, Blend, Local Storage, Uniform Buffer,
  Invocation, Viewport, Tiler Heap/Context, Attribute(+Buffer), Vertex and Tiler job layouts), places
  from Mesa's `v7.xml` (MIT). `tests/rgb30/v7pack.py v7.xml` makes `mali_desc_golden.h` from the XML
  itself; `mali_desc_test` compares the packers to it (in `test-mali`). A "padded" field stores 1 as 0.
- Shaders: `mali_shaders.h`, from `tools/mali/mkshader.c` with Mesa's Bifrost compiler (recipe and
  limits: `tools/mali/README.md`): vertex (a position in pixels, w = 1, no varyings) and fragment (one
  colour per draw, pushed: words 4-7). Without IDVS: a Vertex job, then a Tiler job whose Draw
  `position` is the buffer the vertex job wrote (varying buffer 1; an attribute buffer of binding n is
  entry 2n; a Renderer State is followed by one Blend per render target).
- To check on the device if the triangle does not appear (guesses read from Mesa, in order):
  the Blend's memory format order (RGBA; the screen is XRGB), `HIERARCHY_MASK` 0xee (Mesa's value for 6
  tiler levels, not read from the GPU), the fragment's pixel kill/ZS update (Weak Early), the 1 MiB heap
  (`MEM_HEAP` at 1 MiB of the 4 MiB of GPU memory), no message preload.
- Next: when the triangle works (then bm3d gets its version and a row in DRIVERS.md), an `r3d` backend (`tri` callback) that uses the batch,
  then the vertex shader with the transform.
- `origin/bm3d-mali-v1` (Grok) was not used: its additions do not compile (`mali_tri_batch_t`
  undeclared), the descriptors are invented (placeholder shaders, a made-up tiler job)
  and its `gpu3d.c` edit is the V3D driver, which the RGB30 does not build.

## M41 Roadmap: Mali GPU & Overbit in `.b16` (RGB30)

1. **Overbit in `.b16` su RGB30**:
   - Esecuzione a **360×360** (con raddoppio ×2 dal controller video hardware) e **720×720** nativo, selezionabili nel menu *RESOLUTION*.
   - Adattamento con `screen()` per modi quadrati (`fb_init_mode`: righe a 64 byte e tessere da 16).
   - In Overbit: adattamento di HUD, menu, cielo e campo visivo 1:1 (`SW`, `SH`, `LW`×`LH` e `UI` di `00_core.lua`).
2. **Benchmark con ARM**:
   - Misura delle prestazioni di Overbit (combattimento bot e anello di eroi) a entrambe le risoluzioni su ARM, con generazione report.
3. **Driver Mali a Fasi**:
   - *Fase 1*: Triangoli preparati dall'ARM e rasterizzati da GPU (triangolo nel *GPU test*).
   - *Fase 2*: Backend `r3d` con batching e un colore per disegno.
   - *Fase 3*: Vertex shader per le trasformazioni geometriche su GPU.
   - *Fase 4*: Coda di job asincrona con supporto multi-buffering.
- **Criterio di completamento**: Sulla RGB30 Overbit `.b16` gira stabilmente a 360×360 e a 720×720, il benchmark produce il report prestazionale (prima con ARM, poi con Mali) e la tabella 3D Bench della RGB30 riporta i dati delle colonne GPU.

