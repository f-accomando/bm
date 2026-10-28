# tools/mali: the Mali-G52's shader binaries

`mkshader.c` builds two shaders with Mesa's own Bifrost compiler (MIT) and writes them as a C header
(`src/rgb30/mali_shaders.h`, not made yet): a vertex shader (position and colour pass-through) and a
fragment shader (writes the interpolated colour). It is not part of the firmware build.

## Build (WSL, Ubuntu)

```sh
sudo apt install -y meson ninja-build flex bison python3-mako python3-yaml python3-ply libexpat1-dev \
  zlib1g-dev pkg-config libdrm-dev llvm-21-dev libclang-21-dev clang-21 libclc-21-dev \
  libllvmspirvlib-21-dev llvm-spirv-21 spirv-tools spirv-tools-dev spirv-tools-headers glslang-tools
git clone --depth 1 https://github.com/lfdevs/mesa-for-android-container.git /tmp/mesa   # a Mesa tree (adreno-main)
cd /tmp/mesa && meson setup /tmp/mb -Dgallium-drivers=panfrost -Dvulkan-drivers= -Dllvm=enabled \
  -Dmesa-clc=enabled -Dinstall-mesa-clc=false -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dplatforms= \
  -Dgles1=disabled -Dgles2=disabled -Dbuildtype=release -Dvalgrind=disabled -Dlibunwind=disabled \
  -Dlmsensors=disabled -Dzstd=disabled
ninja -C /tmp/mb src/panfrost/compiler/libpanfrost_compiler.a src/compiler/nir/libnir.a \
  src/compiler/libcompiler.a src/util/libmesa_util.a src/panfrost/compiler/bifrost/libpanfrost_bifrost.a \
  src/panfrost/shared/libpanfrost_shared.a src/util/blake3/libblake3.a src/panfrost/model/libpanfrost_model_lib.a \
  src/panfrost/compiler/bifrost/libpanfrost_bifrost_disasm.a \
  src/panfrost/compiler/bifrost/valhall/libpanfrost_valhall_disasm.a \
  src/panfrost/compiler/midgard/libpanfrost_midgard.a src/panfrost/compiler/midgard/libpanfrost_midgard_disasm.a
```

Then compile `mkshader.c` with the include flags of `bifrost_compile.c` from `/tmp/mb/compile_commands.json`
(plus `-I/tmp/mesa/src/compiler -I/tmp/mesa/src/panfrost/compiler`) and link the libraries above inside
`-Wl,--start-group ... -Wl,--end-group` with `-lz -lexpat -lm -lpthread -lstdc++`. `MKSHADER_DISASM=1`
prints the Bifrost disassembly; `MKSHADER_NIR0/1/2=1` print the NIR after lowering, before and after
Mesa's postprocess.

## State

`src/rgb30/mali_shaders.h` is made and committed. Run: `mkshader OUT.h`.

- `BM_VS3` (phase 3): attribute 0 a model-space vec4, 16 pushed words = a 4x4 matrix by rows (uniform buffer 0), position = M * v
  (the matrix carries projection and viewport; w is the clip w); not run on a Mali yet. Its viewport scale is a vec3 constant
  (`unit_scale` in `mkshader.c`).
- `BM_VS` is built with `full_scale = 1` (W15c): it now carries y and z. Before W15c `nir_inline_sysval` made a one-component
  constant for the vec3 viewport scale, the optimizer read its `.y` / `.z` as 0 and the shader stored `(x/w, 0, 0, 1/w)`.
- Vertex shader (`BM_VS`, 160 bytes, 32 work registers): attribute 0 is a vec4 position already in
  pixels (w = 1); the viewport transform is made the identity (`nir_inline_sysval` scale 1, offset 0).
  No varyings.
- Fragment shader (`BM_FS`, 160 bytes): writes one colour per draw, a vec4 of floats at offset 0 of
  uniform buffer 0, pushed: 8 push words, the colour is words 4-7 (`BM_FS_push`), words 0-3 (ubo 1) are
  unused and the draw's push buffer is 32 bytes with them zero.
- Textured shaders: `BM_VS_TEX` passes the position and a (u, v, k) varying (u and v divided by depth).
  `BM_FS_TEX` samples texture 0 with sampler 0 (`txl`, level 0), multiplies the texel by k and keeps blue at 1/255 or more (the key colour is `0xFF00FF`).
  Both inputs need `driver_location`. `BIFROST_MESA_DEBUG=nopreload` stops the message preload fields.
- Lit meshes (Q2): `BM_VS_LIT` has three attributes (position, face normal and emissive flag, base colour 0..255), the matrix by rows (words 0..15), the sun and up directions and the light A, B, D (vec4 at bytes 64..143) in uniform buffer 0, and gives the lit colour of a face as varying 0 (the colour is rounded to the RGB565 steps, as `be_mesh`). `BM_FS_VCOL` writes that varying. The compiler pushes the words in its own order (`BM_VS_LIT_push`): the driver writes word q of the push buffer from the uniform word `push[q]`.
- `BM_VS_LITX` adds rim light, four lamps, gloss and fog: it reads uniform buffer 0 from memory (416 bytes, no pushed words) and has a fourth attribute, the face centre (lamps use the face centre). `BM_VS_LIT` reads sun, up and A, B, D from the same offsets.
- `BM_VS_LITB` is `BM_VS_LITX` for skinned meshes: the uniform buffer has a mesh block (A, B, D, R, S, fog colour, misc, lamps; 240 bytes) and 192 bytes a bone (matrix, sun, up, view, half, camera matrix, fog amount); the fourth attribute's w is the bone, and the shader reads the bone's block at 240 + 192 bone.
- `BM_FS_SPR` (2D sprites): `BM_FS_TEX` without the light, taking the texture index from the third value of the varying (up to 16 textures: the Draw's texture array), killing the texels with alpha under 1/2 and clamping green to 1/255 (the key colour 0xFF00FF); it takes the varying (u, v) of `BM_VS_TEX`. A rectangle uses `BM_VS_TEX` and `BM_FS_VCOL` with its colour as the varying.
- Build here: `~/mesa-bifrost` (the tree) and `~/mb` (the build, kept across WSL restarts); `bash tools/mali/bld.sh` builds it; `bash tools/mali/mk.sh OUT.h` (from the repository root, OUT relative) builds `mkshader` and writes the header.
- Compiled without IDVS (`no_idvs`): a Vertex job (type 5, a Compute Job layout) then a Tiler job.
  With IDVS these inputs give an empty shader.
- Colour per vertex (a varying) was tried first: the vertex shader dropped the colour store and the
  cause was not found. A colour per draw avoids it; a mesh with several colours is several draws.
- Fragment preload is r60 only (no message preload), the Renderer State's `Uniform count` is
  `ceil(push count / 2)` = 4.
