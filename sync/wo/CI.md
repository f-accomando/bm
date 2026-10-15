# Work order operatore CI — riparare la CI e audit permanente

Base: `bm-core`; branch di lavoro `claude/ci-repair` da `bm-core` aggiornato. Ordine: CI-1 → D-MON → D-RES → D-LOAD → D-SCREEN → D-RGB → D-RGB2 → CI-AUDIT. Ogni WO ≤45 min. Test selettivi (`-k`), mai la suite intera in locale; la CI vera si vede solo dopo un push del branch di lavoro (non su `sync/*`).
Dati (gh, 2026-10-15): in 300 run di `ci` nessun successo; `bm-core` rossa con gli stessi 8 test in 3 run: host `test_res`; rgb30 `test_el2_image_relocation`, `test_bm_cartridge`; qemu 1/4 `test_monitor_line`, `test_projects`; 3/4 `test_editor_mouse`, `test_publish`; 4/4 `test_studio_animator`; shard 2/4 passa. I push su `sync/*` (battiti) lanciavano l'intera CI (14 run in coda).
Ogni causa sotto è IPOTESI del planner, da verificare prima del fix. NON invocare bm-deep: se una diagnosi lo richiede, segnati WAITING e scrivi a SPV.
Perimetro: `.github/workflows/ci.yml`, `tests/qemu_test.py`, `tests/rgb30/qemu_test.py`, `tests/bm/test_res.c`, `Makefile` (solo righe nominate), `src/rgb30/start.S` (solo D-RGB2), `docs/progress/platforms.md` (Rules), `scripts/ci_audit.py` (nuovo), `CLAUDE.md` §5 (una riga), `docs/progress/platforms.md` e `tools.md`. Non toccare i file di NET, B16 e AGY.

## Decisioni di SPV (2026-10-15)
1. QEMU: si controlla la versione (8.2) con un passo, non si fissa il pacchetto apt.
2. Overbit in `Makefile:195` e `:645`: fuori da questi WO (si segnala in Problemi).
3. La CI resta completa (nessun filtro per percorso): la regola §2 vale in locale.
4. La correzione del fixture di `test_res` è approvata.
5. I `timeout-minutes` sono stime: si rivedono con `ci_audit.py --gh`.

## CI-1 — ci.yml economico e limitato nel tempo (bm-coder)
- File: `.github/workflows/ci.yml` (blocco `on.push`, intestazione, ogni job, passo "Install toolchain and QEMU" del job qemu).
- Modifica: (a) `branches-ignore: [reports, 'sync/**']`; (b) a livello workflow `concurrency: {group: ci-${{ github.ref }}, cancel-in-progress: ${{ !startsWith(github.ref, 'refs/tags/') }}}`; (c) `timeout-minutes`: tested 5, host 30, qemu 45, rgb30 30, release 30; (d) commento a `:72` → "8.2 = la versione di apt su ubuntu-24.04; cambiare runs-on cambia l'hash" e nuovo passo dopo l'install nel job qemu: `qemu-system-arm --version | tee /dev/stderr | grep -q 'version 8\.2\.'`; (e) rgb30: nessuna modifica (numpy/PIL non servono).
- Verifica: `python -c "import yaml;yaml.safe_load(open('.github/workflows/ci.yml'))"` → exit 0; se disponibile `actionlint`.

## D-MON — `test_monitor_line` (bm-mechanic)
- File: `tests/qemu_test.py:5155` `q.expect("back to the monitor", timeout=10)` → `timeout=40` (attesa "stress settle" di 19 s più margine; diagnosi già confermata su `bm-core` pulito).
- Verifica: `scripts/wsl-run.ps1 "make && make BOOT=stress && python3 tests/qemu_test.py -k test_monitor_line"` → PASS.

## D-RES — `test_res` host (bm-light)
- Riproduzione: `scripts/wsl-run.ps1 "make test-res"` → "33/34", fallisce `tests/bm/test_res.c:134` ("a game of before: no INFO, no zones").
- Diagnosi (ipotesi: `carts/village/bm.info` fa scrivere a mkbm la sezione INFO): `git log --diff-filter=A -- carts/village/bm.info`; ispeziona `build/carts/village.bm` con `python scripts/bmres.py`.
- Fix se confermato: il caso "gioco di prima" usa una cartuccia senza INFO creata dal test (mkbm senza `--info`) al posto di `village.bm`; tocca `Makefile:554-557` e `test_res.c:132-137`.
- Verifica: `make test-res` → "34/34".

## D-LOAD — `test_projects` (bm-light)
- Riproduzione: `scripts/wsl-run.ps1 "make && python3 tests/qemu_test.py -k test_projects"` (test a ~`:7366`), assert su "bm: loaded in" (la stringa esiste in `src/bm/loading.c:334`).
- Diagnosi: leggi il buffer seriale dell'assert, cerca l'errore di caricamento prima della riga 334; verifica se usa una cartuccia da `Makefile:195`. Fix solo se chiaro e nel perimetro; altrimenti scrivi la causa e fermati.

## D-SCREEN — `test_editor_mouse` (~:6435), `test_publish` (~:3488), `test_studio_animator` (~:4079) (bm-light)
- Obiettivo: verificare se i tre test "testo non sullo schermo" hanno una causa comune (riconoscimento del testo nello screendump, layout font/menu, `time.sleep` prima dello screendump).
- Riproduzione: `scripts/wsl-run.ps1 "make && python3 tests/qemu_test.py -k <nome> --shots <scratchpad>"` per ciascuno. Se passano in locale e falliscono in CI: sospetto timing o versione QEMU; se falliscono anche in locale: bisect di un commit dell'editor o del font.
- Se serve una decisione non deterministica (timing) scrivi la diagnosi, segnati WAITING e fermati (bm-deep solo se lo chiede SPV).

## D-RGB — rgb30: `test_el2_image_relocation`, `test_bm_cartridge` (bm-light)
- Riproduzione: `scripts/wsl-run.ps1 "make -f rgb30.mk PLAT=virt all && python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k el2"` poi `-k bm_cartridge`.
- Diagnosi: cattura quale stringa manca ("EL1", "2^10 =", "device tree at" in `src/rgb30/main.c:78,91`, "bm: loaded"); confronta `qemu-system-aarch64 --version` con 8.2; `git log` di `src/rgb30/start.S` e `main.c`. Se porta a `start.S` (rilocazione/EL2): fermati e scrivi a SPV.

## D-RGB2 — `start.S` copy_up non deve sovrascriversi (bm-coder; WO del planner, causa confermata da aritmetica degli indirizzi)
- Causa: in QEMU virt (link 0x40080000, carico 0x40200000) `copy_up` copia dal sorgente; la store arriva su 0x40200070 con x4=0x180070 e sovrascrive il ciclo (ELR 0x40200074 = src+0x74). Soglia sicura attuale 0x180070 B, l'immagine è 0x1C5E50. Hardware RK3566: NON colpito (U-Boot 0x02000000 < link 0x10000000 → `copy_down`, mai `copy_up`), il fix è non regressione.
- File: `src/rgb30/start.S` (blocco `copy_up`, righe ~42-49), docstring di `tests/rgb30/qemu_test.py:test_el2_image_relocation`, `docs/progress/platforms.md` (`## Rules`).
- Modifica: (1) `#define BOOT_HEAD 0x100` vicino a `SCTLR_EL1_RES1`. (2) `copy_up` in questo ordine: `mov x6, x3`; `mov x7, #BOOT_HEAD`; `cmp x3, x7`; `csel x3, x3, x7, lo`; `mov x4, #0`; etichetta `copy_up_loop:` col corpo di oggi (`ldr x5,[x1,x4]` / `str x5,[x2,x4]` / `add x4,x4,#8` / `cmp x4,x3` / `b.lo copy_up_loop`); etichetta `copy_up_end:`; `cmp x3, x6`; `b.eq copied`; `mov x3, x6`; `dsb sy`; `ic iallu`; `dsb sy`; `isb`; `ldr x7, =copy_up_loop`; `br x7` (x4 NON si azzera: riprende da BOOT_HEAD all'indirizzo di link; al secondo passaggio `cmp x3,x6` è uguale e si salta a `copied`). (3) Dopo `copy_up_end:` `.if (copy_up_end - _start) > BOOT_HEAD` / `.error "copy_up loop beyond BOOT_HEAD"` / `.endif`. (4) Commento di una riga sopra `copy_up`. (5) `copy_down`, `copied`, `relocated` invariati. (6) Docstring del test: "loaded below the link address" → "loaded away from the link address (QEMU: above it, at 0x40200000)". (7) In platforms.md Rules: "`src/rgb30/start.S` copy_up: the copy loop must stay within the first BOOT_HEAD (0x100) bytes (assembler `.error`); it copies the head to the link address and continues from there (an in-place forward copy overwrote itself above 1.5 MiB in QEMU virt)."
- Verifica: `scripts/wsl-run.ps1 "make -f rgb30.mk PLAT=virt all && python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k el2"` → build senza `.error`, test PASS ("EL1", "2^10 = 1024.0"); poi `make TARGET=rgb30` (rk3566) deve compilare. Facoltativo: `objdump -d build/rgb30-virt/kernel.elf | grep -A3 copy_up_end` con indirizzo < 0x40080100. Poi `python scripts/docs_audit.py score`.
- Console vera: la prova (`./easy_install.sh`, boot fino al menu come prima) la fa l'utente: dichiararlo nella consegna. Se in QEMU il test non passa dopo 2 tentativi: `WAITING` e scrivi a SPV (niente bm-deep).

## D-HOST — `make test-loading` non linka (bm-light; decisione SPV 2026-10-15: OK, perimetro esteso a `Makefile:465-470`)
- Causa (log CI run baffa299): `undefined reference to synth_kit_find`; `src/audio/presets.c:325` lo usa, definito in `src/audio/synth.c:838`, che il link di `test_loading` (`Makefile:468`) non include. Preesistente.
- Modifica: aggiungere `src/audio/synth.c` alle dipendenze (`:465-467`) e alla riga di link (`:468`) di `$(BUILD)/host/test_loading`, solo se il link lo richiede (altre dipendenze mancanti: nominarle in Problemi e fermarsi dopo 2 tentativi).
- Verifica: `scripts/wsl-run.ps1 "make test-loading"` → exit 0. Nessun altro target toccato.

## D-RUN2 — primo run con tutte le correzioni (run 38717019522, commit 792554f5) (bm-light; assegnato da SPV 2026-10-15)
- Dati (gh, SPV): rgb30, qemu 1/4, 2/4, 4/4 = SUCCESSO (prima 0 successi su 300 run). Falliscono ancora: **host** (job ) e **qemu (3/4)**.
- Compito: con host	Tests on the PC	﻿2026-10-15T10:01:35.5872960Z ##[group]Run make test-host
host	Tests on the PC	2026-10-15T10:01:35.5873273Z ^[[36;1mmake test-host^[[0m
host	Tests on the PC	2026-10-15T10:01:35.5938225Z shell: /usr/bin/bash -e {0}
host	Tests on the PC	2026-10-15T10:01:35.5938517Z env:
host	Tests on the PC	2026-10-15T10:01:35.5938702Z   ZERO2: 0
host	Tests on the PC	2026-10-15T10:01:35.5938889Z ##[endgroup]
host	Tests on the PC	2026-10-15T10:01:35.7660954Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_bm tests/bm/test_bm.c src/bm/gfx16.c src/bm/r3d.c src/bm/format.c src/lib/crc32.c -lm
host	Tests on the PC	2026-10-15T10:01:35.9049279Z tests/bm/test_bm.c: In function ‘test_format’:
host	Tests on the PC	2026-10-15T10:01:35.9051378Z tests/bm/test_bm.c:418:21: warning: unsigned conversion from ‘int’ to ‘uint8_t’ {aka ‘unsigned char’} changes value from ‘360’ to ‘104’ [-Woverflow]
host	Tests on the PC	2026-10-15T10:01:35.9052498Z   418 |             r[14] = 360;
host	Tests on the PC	2026-10-15T10:01:35.9052814Z       |                     ^~~
host	Tests on the PC	2026-10-15T10:01:39.1932824Z cc -O2 -Wall -Wextra -Isrc/bm -Isrc -Ithird_party/lua -Ibuild -o build/host/test_meshcap tests/bm/test_meshcap.c src/bm/meshcap.c src/bm/format.c src/bm/r3d.c src/bm/gfx16.c src/lib/crc32.c tests/host/libs.S host	Tests on the PC	2026-10-15T10:01:39.1936584Z     third_party/lua/lapi.c third_party/lua/lauxlib.c third_party/lua/lbaselib.c third_party/lua/lcode.c third_party/lua/lcorolib.c third_party/lua/lctype.c third_party/lua/ldblib.c third_party/lua/ldebug.c third_party/lua/ldo.c third_party/lua/ldump.c third_party/lua/lfunc.c third_party/lua/lgc.c third_party/lua/linit.c third_party/lua/liolib.c third_party/lua/llex.c third_party/lua/lmathlib.c third_party/lua/lmem.c third_party/lua/loadlib.c third_party/lua/lobject.c third_party/lua/lopcodes.c third_party/lua/loslib.c third_party/lua/lparser.c third_party/lua/lstate.c third_party/lua/lstring.c third_party/lua/lstrlib.c third_party/lua/ltable.c third_party/lua/ltablib.c third_party/lua/ltm.c third_party/lua/lundump.c third_party/lua/lutf8lib.c third_party/lua/lvm.c third_party/lua/lzio.c -lm
host	Tests on the PC	2026-10-15T10:01:48.2012690Z /usr/bin/ld: /tmp/ccpqSSfD.o: in function `os_tmpname':
host	Tests on the PC	2026-10-15T10:01:48.2013670Z loslib.c:(.text+0x424): warning: the use of `tmpnam' is dangerous, better use `mkstemp'
host	Tests on the PC	2026-10-15T10:01:48.2264426Z python3 scripts/mkbm.py -o build/meshcap-test.bm --lua tests/bm/meshcap_cart.lua --title "meshcap test" --author tests
host	Tests on the PC	2026-10-15T10:01:48.2637718Z build/meshcap-test.bm: 1192 bytes (meshcap test, 640x360)
host	Tests on the PC	2026-10-15T10:01:48.2704372Z cc -O2 -Wall -Wextra -Isrc/bm -Isrc -o build/host/test_decimate tests/bm/test_decimate.c src/bm/decimate.c src/bm/format.c src/lib/crc32.c -lm
host	Tests on the PC	2026-10-15T10:01:49.3580487Z cc -O2 -shared -fPIC -o build/host/libbmdecimate.so src/bm/decimate.c -lm
host	Tests on the PC	2026-10-15T10:01:49.7540349Z cc -O2 -Wall -Wextra -Isrc/bm -Isrc -o build/host/test_glb tests/bm/test_glb.c src/bm/glb.c src/bm/json.c src/bm/jpeg.c src/bm/decimate.c src/bm/png.c -lm
host	Tests on the PC	2026-10-15T10:01:51.0020091Z cc -O2 -Wall -Wextra -Isrc/bm -Isrc -o build/host/test_cutout tests/bm/test_cutout.c src/bm/cutout.c src/bm/glb.c src/bm/json.c src/bm/jpeg.c src/bm/decimate.c src/bm/png.c -lm
host	Tests on the PC	2026-10-15T10:01:52.7083582Z cc -O2 -shared -fPIC -o build/host/libbmcutout.so src/bm/cutout.c src/bm/glb.c src/bm/json.c src/bm/jpeg.c src/bm/decimate.c src/bm/png.c -lm
host	Tests on the PC	2026-10-15T10:01:54.3296375Z cc -O2 -Wall -Wextra -Isrc/bm -o build/host/test_tokens tests/bm/test_tokens.c src/bm/tokens.c
host	Tests on the PC	2026-10-15T10:01:54.4812678Z build/host/test_bm build/demo.bm
host	Tests on the PC	2026-10-15T10:01:54.4898824Z bm: 151/151 checks passed
host	Tests on the PC	2026-10-15T10:01:54.4901382Z build/host/test_tokens carts/editor/main.lua carts/pong/main.lua
host	Tests on the PC	2026-10-15T10:01:54.4922724Z carts/editor/main.lua: 20569 tokens, 135660 bytes
host	Tests on the PC	2026-10-15T10:01:54.4923263Z carts/pong/main.lua: 1778 tokens, 9061 bytes
host	Tests on the PC	2026-10-15T10:01:54.4923785Z test_tokens: ok
host	Tests on the PC	2026-10-15T10:01:54.4924255Z build/host/test_decimate build/carts/village.bm build/carts/kitchen.bm
host	Tests on the PC	2026-10-15T10:01:54.4992865Z   ground: 288 -> 144 triangles (asked 144)
host	Tests on the PC	2026-10-15T10:01:54.4993245Z   house: 180 -> 90 triangles (asked 90)
host	Tests on the PC	2026-10-15T10:01:54.4993662Z   tree: 16 -> 8 triangles (asked 8)
host	Tests on the PC	2026-10-15T10:01:54.4994108Z   bush: 8 -> 4 triangles (asked 4)
host	Tests on the PC	2026-10-15T10:01:54.4994575Z   well: 86 -> 42 triangles (asked 43)
host	Tests on the PC	2026-10-15T10:01:54.4995261Z   fence: 16 -> 8 triangles (asked 8)
host	Tests on the PC	2026-10-15T10:01:54.4995547Z   crates: 36 -> 18 triangles (asked 18)
host	Tests on the PC	2026-10-15T10:01:54.4995842Z   villager: 168 -> 84 triangles (asked 84)
host	Tests on the PC	2026-10-15T10:01:54.4996346Z decimate: 40 checks, 0 failed
host	Tests on the PC	2026-10-15T10:01:54.4996729Z python3 tests/bm/run_glb_test.py build/host/test_glb build/glb
host	Tests on the PC	2026-10-15T10:01:54.5914167Z glb (png texture): 13 vertices, 18 triangles, textured, the sheet, the corners, the colours and the winding ok
host	Tests on the PC	2026-10-15T10:01:54.5915005Z glb (jpeg texture): 13 vertices, 18 triangles, textured, the sheet, the corners, the colours and the winding ok
host	Tests on the PC	2026-10-15T10:01:54.5915526Z glb: a mirrored node ok
host	Tests on the PC	2026-10-15T10:01:54.5915805Z glb: --faces 8: 8 vertices, 8 triangles, textured
host	Tests on the PC	2026-10-15T10:01:54.5916125Z glb: a broken file is refused
host	Tests on the PC	2026-10-15T10:01:54.6014274Z python3 tests/bm/run_cutout_test.py build/host/test_cutout build/cutout
host	Tests on the PC	2026-10-15T10:01:54.8121720Z cutout (lolli_alpha): 24 vertices, 44 triangles, cutout, closed, 2 tall, the picture on the front
host	Tests on the PC	2026-10-15T10:01:54.8122591Z cutout (lolli_white): 24 vertices, 44 triangles, cutout, closed, 2 tall, the picture on the front
host	Tests on the PC	2026-10-15T10:01:54.8123202Z lathe: 62 vertices, 120 triangles, lathe, closed, round, the picture in front
host	Tests on the PC	2026-10-15T10:01:54.8123634Z cutout: an empty picture is refused
host	Tests on the PC	2026-10-15T10:01:54.8176080Z python3 tools/cutout2mesh.py build/cutout/lolli_alpha.png -o build/cutout/tool.bm
host	Tests on the PC	2026-10-15T10:01:54.8844273Z cutout2mesh: build/cutout/tool.bm: written the model lolli_alpha: 24 vertices, 44 triangles (cutout)
host	Tests on the PC	2026-10-15T10:01:54.8935577Z python3 tools/cutout2mesh.py build/cutout/lolli_white.png -o build/cutout/tool.bm --lathe --name vase
host	Tests on the PC	2026-10-15T10:01:54.9549971Z cutout2mesh: build/cutout/tool.bm: added to the model vase: 62 vertices, 120 triangles (lathe, flat colours)
host	Tests on the PC	2026-10-15T10:01:54.9635614Z python3 tools/bmreduce.py build/carts/village.bm --ratio 0.5 -o build/village-half.bm
host	Tests on the PC	2026-10-15T10:01:55.0032230Z ground: 288 -> 144 triangles
host	Tests on the PC	2026-10-15T10:01:55.0032965Z house: 180 -> 90 triangles
host	Tests on the PC	2026-10-15T10:01:55.0033401Z tree: 16 -> 8 triangles
host	Tests on the PC	2026-10-15T10:01:55.0033767Z bush: 8 -> 4 triangles
host	Tests on the PC	2026-10-15T10:01:55.0034135Z well: 86 -> 42 triangles
host	Tests on the PC	2026-10-15T10:01:55.0034511Z fence: 16 -> 8 triangles
host	Tests on the PC	2026-10-15T10:01:55.0034892Z crates: 36 -> 18 triangles
host	Tests on the PC	2026-10-15T10:01:55.0035282Z villager: 168 -> 84 triangles
host	Tests on the PC	2026-10-15T10:01:55.0035689Z build/village-half.bm
host	Tests on the PC	2026-10-15T10:01:55.0085819Z build/host/test_meshcap src/bm/runtime.c host	Tests on the PC	2026-10-15T10:01:55.0086745Z     build/meshcap-test.bm '!stop here,wheel:1,crate:12,cars1_body:1,gem:2' host	Tests on the PC	2026-10-15T10:01:55.0087686Z     build/carts/astrowing.bm ship:32,dart,tower,gate,ring:120,laser,bolt,debris,debris2,mark,core,core_hot,turret host	Tests on the PC	2026-10-15T10:01:55.0088747Z     build/texroom.bm floor_mesh,walls_mesh,crate_mesh,pillar_mesh host	Tests on the PC	2026-10-15T10:01:55.0089345Z     build/carts/kitchen.bm chef_classic1_body,chef1_body,plate,dplate
host	Tests on the PC	2026-10-15T10:01:55.0490871Z build/meshcap-test.bm: 4 meshes: wheel(3v 1f) crate(8v 12f) cars1_body(3v 1f) gem(4v 2f uv)
host	Tests on the PC	2026-10-15T10:01:55.0493309Z build/carts/astrowing.bm: 13 meshes: ship(30v 32f) dart(13v 14f) tower(8v 12f) gate(24v 36f) ring(80v 120f) laser(8v 12f) bolt(24v 36f) debris(8v 12f) debris2(8v 12f) mark(8v 12f) core(84v 144f) core_hot(84v 144f) turret(16v 24f)
host	Tests on the PC	2026-10-15T10:01:55.0495510Z build/texroom.bm: 4 meshes: floor_mesh(576v 288f uv) walls_mesh(384v 192f uv) crate_mesh(24v 12f uv) pillar_mesh(16v 8f uv)
host	Tests on the PC	2026-10-15T10:01:55.0500577Z build/carts/kitchen.bm: 60 meshes: chef_classic1_body(102v 96f) chef_classic1_leg(36v 18f) chef_classic1_arm(36v 18f) chef_classic2_body(81v 104f) chef_classic2_leg(36v 18f) chef_classic2_arm(22v 16f) chef_classic3_body(81v 102f) chef_classic3_leg(36v 18f) chef_classic3_arm(36v 18f) chef_classic4_body(66v 80f) chef_classic4_leg(36v 18f) chef_classic4_arm(36v 18f) chef1_body(81v 112f uv) chef1_legL(9v 14f uv) chef1_legR(8v 12f uv) chef1_armL(8v 8f uv) chef1_armR(7v 10f uv) chef2_body(91v 154f uv) chef2_legL(9v 8f uv) chef2_legR(12v 9f uv) chef2_armL(8v 8f uv) chef2_armR(8v 9f uv) chef3_body(80v 126f uv) chef3_legL(10v 15f uv) chef3_legR(11v 13f uv) chef3_armL(9v 14f uv) chef3_armR(5v 6f uv) chef4_body(68v 118f uv) chef4_legL(17v 19f uv) chef4_legR(14v 17f uv) chef4_armL(9v 9f uv) chef4_armR(7v 9f uv) plate(16v 22f) dplate(23v 32f) shadow(18v 6f) ring1(40v 20f) ring2(40v 20f) ring3(40v 20f) ring4(40v 20f) hilite1(16v 8f) hilite2(16v 8f) hilite3(16v 8f) hilite4(16v 8f) flame(24v 30f) cube(8v 12f) water(12v 6f) floor1(232v 116f) static1(792v 562f uv) shadow49(18v 6f) door_mesh(24v 12f) cart_mesh(117v 98f) box_mesh(36v 18f) rat(43v 66f) duck(57v 52f) ghost(38v 42f) tornado(60v 60f) knife(16v 24f) puddle(18v 6f) eyes(12v 16f) valve(32v 30f)
host	Tests on the PC	2026-10-15T10:01:55.0505914Z meshcap: 34/34 checks passed
host	Tests on the PC	2026-10-15T10:01:55.0506484Z python3 scripts/mkbm.py -o build/village-before.bm --lua carts/village/main.lua --title Village --author bm --res 320x180 host	Tests on the PC	2026-10-15T10:01:55.0507076Z     --models carts/village/assets/models.bm
host	Tests on the PC	2026-10-15T10:01:55.1090714Z build/village-before.bm: 49344 bytes (Village, 320x180)
host	Tests on the PC	2026-10-15T10:01:55.1163168Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_res tests/bm/test_res.c src/bm/format.c src/lib/crc32.c
host	Tests on the PC	2026-10-15T10:01:55.6728240Z rm -rf build/res && python3 tests/res/test_bmres.py build/carts/village.bm build/demo.bm host	Tests on the PC	2026-10-15T10:01:55.6729140Z     build/sound.bm build/res
host	Tests on the PC	2026-10-15T10:01:59.8397288Z bmres: no model 'nope' (there are: ground, house, tree, bush, well, fence, crates, villager)
host	Tests on the PC	2026-10-15T10:01:59.8425211Z build/res/WELL.bmm: 4072 bytes, models 'well'
host	Tests on the PC	2026-10-15T10:01:59.8425833Z build/res/WELL.bmm: model well
host	Tests on the PC	2026-10-15T10:01:59.8426469Z build/res/cli.bm is a game, read only: its editable copy is build/res/cli.bme
host	Tests on the PC	2026-10-15T10:01:59.8427146Z build/res/cli.bme: 4196 bytes
host	Tests on the PC	2026-10-15T10:01:59.8427558Z models: well, by bm
host	Tests on the PC	2026-10-15T10:01:59.8427908Z   MESH: 1 models
host	Tests on the PC	2026-10-15T10:01:59.8428318Z     well             58 vertices, 86 faces, 86 textured
host	Tests on the PC	2026-10-15T10:01:59.8428843Z   SHEET8: 128x16, 19 colours, 1058 bytes
host	Tests on the PC	2026-10-15T10:01:59.8429360Z   INFO: 3 lines, 1 parts
host	Tests on the PC	2026-10-15T10:01:59.8429672Z     license: CC0-1.0
host	Tests on the PC	2026-10-15T10:01:59.8429914Z     version: 1.0
host	Tests on the PC	2026-10-15T10:01:59.8430139Z     tags: village
host	Tests on the PC	2026-10-15T10:01:59.8430373Z     [model well] desc: a well
host	Tests on the PC	2026-10-15T10:01:59.8430641Z models: well, by bm
host	Tests on the PC	2026-10-15T10:01:59.8430870Z   MESH: 1 models
host	Tests on the PC	2026-10-15T10:01:59.8431423Z     well             58 vertices, 86 faces, 86 textured
host	Tests on the PC	2026-10-15T10:01:59.8431828Z   SHEET8: 128x16, 19 colours, 1058 bytes
host	Tests on the PC	2026-10-15T10:01:59.8432141Z   INFO: 3 lines, 1 parts
host	Tests on the PC	2026-10-15T10:01:59.8432393Z     license: CC0-1.0
host	Tests on the PC	2026-10-15T10:01:59.8432621Z     version: 1.0
host	Tests on the PC	2026-10-15T10:01:59.8432837Z     tags: village
host	Tests on the PC	2026-10-15T10:01:59.8433064Z     [model well] desc: a well
host	Tests on the PC	2026-10-15T10:01:59.8433328Z cartridge: cli
host	Tests on the PC	2026-10-15T10:01:59.8433542Z   LUA: 6 bytes
host	Tests on the PC	2026-10-15T10:01:59.8433768Z   SHEET8: 128x16, 19 colours, 1058 bytes
host	Tests on the PC	2026-10-15T10:01:59.8434063Z   MESH: 1 models
host	Tests on the PC	2026-10-15T10:01:59.8434724Z     well             58 vertices, 86 faces, 86 textured
host	Tests on the PC	2026-10-15T10:01:59.8435065Z   INFO: 0 lines, 1 parts
host	Tests on the PC	2026-10-15T10:01:59.8435708Z     [model well] author: bm; license: CC0-1.0; version: 1.0; tags: village; origin: 763fae910362166d9b959207dd3172ebf0f9cb6ecbda260f5e75c46a9776d6fa
host	Tests on the PC	2026-10-15T10:01:59.8436404Z bmres: 132/132 checks passed
host	Tests on the PC	2026-10-15T10:01:59.8499443Z build/host/test_res build/res build/village-before.bm
host	Tests on the PC	2026-10-15T10:01:59.8517480Z test_res: 34/34 checks passed
host	Tests on the PC	2026-10-15T10:01:59.8533858Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_hid tests/usb/test_hid.c src/usb/hid.c
host	Tests on the PC	2026-10-15T10:02:00.4490035Z cc -O2 -Wall -Wextra -Wno-format -Isrc -o build/host/test_eth tests/usb/test_eth.c tests/usb/lan9512_sim.c src/usb/smsc95xx.c
host	Tests on the PC	2026-10-15T10:02:00.7191483Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_board tests/usb/test_board.c src/drivers/board.c
host	Tests on the PC	2026-10-15T10:02:00.8481570Z cc -O2 -Wall -Wextra -DBM_ZERO2 -Isrc -o build/host/test_board7 tests/usb/test_board.c src/drivers/board.c
host	Tests on the PC	2026-10-15T10:02:00.9768600Z build/host/test_hid
host	Tests on the PC	2026-10-15T10:02:00.9774683Z hid: 87/87 checks passed
host	Tests on the PC	2026-10-15T10:02:00.9775842Z build/host/test_eth
host	Tests on the PC	2026-10-15T10:02:00.9780764Z test_eth: 42 checks, 0 failures
host	Tests on the PC	2026-10-15T10:02:00.9782197Z build/host/test_board
host	Tests on the PC	2026-10-15T10:02:00.9786900Z test_board: 19 checks, 0 failures
host	Tests on the PC	2026-10-15T10:02:00.9787963Z build/host/test_board7
host	Tests on the PC	2026-10-15T10:02:00.9792990Z test_board: 19 checks, 0 failures
host	Tests on the PC	2026-10-15T10:02:00.9808062Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_fat tests/fs/test_fat.c src/fs/fat.c
host	Tests on the PC	2026-10-15T10:02:01.5965235Z python3 tests/fs/run.py build/host/test_fat
host	Tests on the PC	2026-10-15T10:02:02.4576953Z fat: a 200000-byte file in 55 pieces, 29 of several sectors
host	Tests on the PC	2026-10-15T10:02:02.4578506Z ^[[91mfat: 64 sectors in one write failed or came back wrong: one sector at a time from now on^[[0m
host	Tests on the PC	2026-10-15T10:02:02.4579684Z fat: 2001 clusters written, 40 FAT sectors written (sectors)
host	Tests on the PC	2026-10-15T10:02:02.4580646Z fat: 2001 clusters written, 40 FAT sectors written (runs)
host	Tests on the PC	2026-10-15T10:02:02.4582256Z fat: 477/477 checks passed, 20172 sectors written
host	Tests on the PC	2026-10-15T10:02:03.1439256Z fat: a 200000-byte file in 48 pieces, 48 of several sectors
host	Tests on the PC	2026-10-15T10:02:03.1440334Z ^[[91mfat: 64 sectors in one write failed or came back wrong: one sector at a time from now on^[[0m
host	Tests on the PC	2026-10-15T10:02:03.1440996Z fat: 2001 clusters written, 40 FAT sectors written (sectors)
host	Tests on the PC	2026-10-15T10:02:03.1441944Z fat: 2001 clusters written, 40 FAT sectors written (runs)
host	Tests on the PC	2026-10-15T10:02:03.1442392Z fat: 477/477 checks passed, 77324 sectors written
host	Tests on the PC	2026-10-15T10:02:03.3904461Z fat (512 B clusters): fsck.vfat clean, mtools reads the files back
host	Tests on the PC	2026-10-15T10:02:03.3905080Z fat (4096 B clusters): fsck.vfat clean, mtools reads the files back
host	Tests on the PC	2026-10-15T10:02:03.3979335Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_audio tests/audio/test_audio.c src/audio/synth.c src/audio/player.c host	Tests on the PC	2026-10-15T10:02:03.3980266Z     src/audio/presets.c src/audio/iec958.c -lm
host	Tests on the PC	2026-10-15T10:02:06.1976848Z cc -O2 -Wall -Wextra -Isrc -o build/host/bmrender tests/audio/render.c src/audio/synth.c src/audio/player.c -lm
host	Tests on the PC	2026-10-15T10:02:07.8796416Z build/host/test_audio
host	Tests on the PC	2026-10-15T10:02:08.2370249Z   SNR of a 997 Hz sine at -1 dBFS: 92.3 dB (16 bits), 140.2 dB (24), 216.4 dB (32)
host	Tests on the PC	2026-10-15T10:02:08.2371054Z   0.6 LSB at 16 bits: dithered 0.603 / 3rd 0.0050 LSB, plainly rounded 0.708 / 3rd 0.419 LSB
host	Tests on the PC	2026-10-15T10:02:08.2372317Z   0.6 LSB at 24 bits: dithered 0.592 / 3rd 0.0031 LSB, plainly rounded 0.708 / 3rd 0.419 LSB
host	Tests on the PC	2026-10-15T10:02:08.2373605Z   the room's tail at 24 bits: 0.60 LSB rms from the float at worst, 1087 blocks under half a 16-bit step, quiet after 7.58 s
host	Tests on the PC	2026-10-15T10:02:08.2374609Z   DC of a 25% square after the blocker: 1.10e-06 of full scale (peak 0.74)
host	Tests on the PC	2026-10-15T10:02:08.2375245Z   a cutoff swept by the LFO: block-rate sidebands at -77.0 dBc (once a block: -36)
host	Tests on the PC	2026-10-15T10:02:08.2376053Z   the filter envelope's sweep: block-rate sidebands at -74.9 dBc, an acid one -62.1 dBc
host	Tests on the PC	2026-10-15T10:02:08.2376859Z   duty 25% -> 75%: 33 of 64 high in the block after, 49 in the next
host	Tests on the PC	2026-10-15T10:02:08.2378016Z   a 1 kHz sine at 22.05 kHz played at 48 kHz: 70.7 dB SNR (Hermite), 21.6 dB (the chip's)
host	Tests on the PC	2026-10-15T10:02:08.2378725Z   loops: a cycle of 48 frames again and again 90.3 dB SNR; ping-pong over half of it 998 Hz
host	Tests on the PC	2026-10-15T10:02:08.2379482Z   drive 200, its 3rd harmonic: soft -10, hard -10, fold -7, sine 1, asym -10 (2nd -28), cubic -10 dBc
host	Tests on the PC	2026-10-15T10:02:08.2380161Z   noise from 250-500 Hz to 4-8 kHz: white +12.0 dB, pink -0.2, brown -11.2
host	Tests on the PC	2026-10-15T10:02:08.2380718Z   the noises' rms against the noise wave's: pink 0.49, brown 0.49
host	Tests on the PC	2026-10-15T10:02:08.2381698Z   ducking: the music -122.0 dB while the source sounds, -0.09 dB half a second after
host	Tests on the PC	2026-10-15T10:02:08.2382325Z   vowels, 500-700 Hz against 200-300 Hz: none -6, a +13, e -2, i -19, o -5, u -6 dB
host	Tests on the PC	2026-10-15T10:02:08.2382799Z audio: 7964/7964 checks passed
host	Tests on the PC	2026-10-15T10:02:08.2383208Z python3 tests/audio/test_bmaudio.py build/host/bmrender build/audio
host	Tests on the PC	2026-10-15T10:02:08.7207537Z bmaudio: 54/54 checks passed
host	Tests on the PC	2026-10-15T10:02:08.7284247Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_music tests/ai/test_music.c src/ai/music.c src/ai/music_net.c src/ai/nn.c host	Tests on the PC	2026-10-15T10:02:08.7285470Z     src/audio/presets.c src/audio/player.c src/audio/synth.c -lm
host	Tests on the PC	2026-10-15T10:02:11.6667739Z python3 scripts/trainmusic.py --check
host	Tests on the PC	2026-10-15T10:02:11.8850498Z music: 46 tunes, 1438 examples written for the check
host	Tests on the PC	2026-10-15T10:02:11.9021980Z build/host/test_music 
host	Tests on the PC	2026-10-15T10:02:11.9266361Z music: the network on 1438 moments of the tunes: 1438 the same as Python
host	Tests on the PC	2026-10-15T10:02:11.9267169Z music: 87 recipes x 6 seeds, 35532 notes
host	Tests on the PC	2026-10-15T10:02:11.9267680Z music: over Am F C G it heard major C, Am F C G
host	Tests on the PC	2026-10-15T10:02:11.9268215Z music: 4705/4705 checks passed
host	Tests on the PC	2026-10-15T10:02:11.9316986Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/runtime.c.o src/bm/runtime.c
host	Tests on the PC	2026-10-15T10:02:15.4497476Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/tokens.c.o src/bm/tokens.c
host	Tests on the PC	2026-10-15T10:02:15.5527355Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/gfx16.c.o src/bm/gfx16.c
host	Tests on the PC	2026-10-15T10:02:16.0445474Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/r3d.c.o src/bm/r3d.c
host	Tests on the PC	2026-10-15T10:02:18.1113938Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/world3d.c.o src/bm/world3d.c
host	Tests on the PC	2026-10-15T10:02:18.2110431Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/map3d.c.o src/bm/map3d.c
host	Tests on the PC	2026-10-15T10:02:18.5617550Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/format.c.o src/bm/format.c
host	Tests on the PC	2026-10-15T10:02:19.0606030Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/meshcap.c.o src/bm/meshcap.c
host	Tests on the PC	2026-10-15T10:02:19.2993519Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/kernel/syskeys.c.o src/kernel/syskeys.c
host	Tests on the PC	2026-10-15T10:02:19.3509587Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/require.c.o src/bm/require.c
host	Tests on the PC	2026-10-15T10:02:19.3898937Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/kernel/prompts.c.o src/kernel/prompts.c
host	Tests on the PC	2026-10-15T10:02:19.7244712Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/gfx/font8x16.c.o src/gfx/font8x16.c
host	Tests on the PC	2026-10-15T10:02:19.7395684Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/gfx/font8x14.c.o src/gfx/font8x14.c
host	Tests on the PC	2026-10-15T10:02:19.7544755Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/gfx/font6x12.c.o src/gfx/font6x12.c
host	Tests on the PC	2026-10-15T10:02:19.7685739Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/lib/printf.c.o src/lib/printf.c
host	Tests on the PC	2026-10-15T10:02:19.8922718Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/lib/crc32.c.o src/lib/crc32.c
host	Tests on the PC	2026-10-15T10:02:19.9198043Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/audio/audio.c.o src/audio/audio.c
host	Tests on the PC	2026-10-15T10:02:20.0875041Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/audio/synth.c.o src/audio/synth.c
host	Tests on the PC	2026-10-15T10:02:21.2768950Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/audio/player.c.o src/audio/player.c
host	Tests on the PC	2026-10-15T10:02:21.8796878Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/audio/presets.c.o src/audio/presets.c
host	Tests on the PC	2026-10-15T10:02:22.0096769Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/audio/lua_tone.c.o src/audio/lua_tone.c
host	Tests on the PC	2026-10-15T10:02:22.1429928Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/ai/net.c.o src/ai/net.c
host	Tests on the PC	2026-10-15T10:02:22.2340168Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/ai/nn.c.o src/ai/nn.c
host	Tests on the PC	2026-10-15T10:02:22.2776198Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/decimate.c.o src/bm/decimate.c
host	Tests on the PC	2026-10-15T10:02:22.6990930Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/cutout.c.o src/bm/cutout.c
host	Tests on the PC	2026-10-15T10:02:23.2342656Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/glb.c.o src/bm/glb.c
host	Tests on the PC	2026-10-15T10:02:23.5848713Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/json.c.o src/bm/json.c
host	Tests on the PC	2026-10-15T10:02:23.7151874Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/png.c.o src/bm/png.c
host	Tests on the PC	2026-10-15T10:02:23.9130402Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/jpeg.c.o src/bm/jpeg.c
host	Tests on the PC	2026-10-15T10:02:24.1387503Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/loading.c.o src/bm/loading.c
host	Tests on the PC	2026-10-15T10:02:24.3158844Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/loading_logo.c.o src/bm/loading_logo.c
host	Tests on the PC	2026-10-15T10:02:24.3298009Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/profile.c.o src/bm/profile.c
host	Tests on the PC	2026-10-15T10:02:24.4408858Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/project.c.o src/bm/project.c
host	Tests on the PC	2026-10-15T10:02:24.5658637Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/src/bm/shot.c.o src/bm/shot.c
host	Tests on the PC	2026-10-15T10:02:24.7245973Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lapi.c.o third_party/lua/lapi.c
host	Tests on the PC	2026-10-15T10:02:25.1952252Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lauxlib.c.o third_party/lua/lauxlib.c
host	Tests on the PC	2026-10-15T10:02:25.4550882Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lbaselib.c.o third_party/lua/lbaselib.c
host	Tests on the PC	2026-10-15T10:02:25.5885551Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lcode.c.o third_party/lua/lcode.c
host	Tests on the PC	2026-10-15T10:02:26.0003095Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lcorolib.c.o third_party/lua/lcorolib.c
host	Tests on the PC	2026-10-15T10:02:26.0600247Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lctype.c.o third_party/lua/lctype.c
host	Tests on the PC	2026-10-15T10:02:26.0745632Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/ldblib.c.o third_party/lua/ldblib.c
host	Tests on the PC	2026-10-15T10:02:26.2083363Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/ldebug.c.o third_party/lua/ldebug.c
host	Tests on the PC	2026-10-15T10:02:26.4350385Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/ldo.c.o third_party/lua/ldo.c
host	Tests on the PC	2026-10-15T10:02:26.6915205Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/ldump.c.o third_party/lua/ldump.c
host	Tests on the PC	2026-10-15T10:02:26.7860885Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lfunc.c.o third_party/lua/lfunc.c
host	Tests on the PC	2026-10-15T10:02:26.8695346Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lgc.c.o third_party/lua/lgc.c
host	Tests on the PC	2026-10-15T10:02:27.2786578Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/linit.c.o third_party/lua/linit.c
host	Tests on the PC	2026-10-15T10:02:27.3042508Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/liolib.c.o third_party/lua/liolib.c
host	Tests on the PC	2026-10-15T10:02:27.5018623Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/llex.c.o third_party/lua/llex.c
host	Tests on the PC	2026-10-15T10:02:27.7145298Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lmathlib.c.o third_party/lua/lmathlib.c
host	Tests on the PC	2026-10-15T10:02:27.8425738Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lmem.c.o third_party/lua/lmem.c
host	Tests on the PC	2026-10-15T10:02:27.8947159Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/loadlib.c.o third_party/lua/loadlib.c
host	Tests on the PC	2026-10-15T10:02:28.0050688Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lobject.c.o third_party/lua/lobject.c
host	Tests on the PC	2026-10-15T10:02:28.1852184Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lopcodes.c.o third_party/lua/lopcodes.c
host	Tests on the PC	2026-10-15T10:02:28.2012503Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/loslib.c.o third_party/lua/loslib.c
host	Tests on the PC	2026-10-15T10:02:28.2929884Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lparser.c.o third_party/lua/lparser.c
host	Tests on the PC	2026-10-15T10:02:28.7753710Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lstate.c.o third_party/lua/lstate.c
host	Tests on the PC	2026-10-15T10:02:28.8709204Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lstring.c.o third_party/lua/lstring.c
host	Tests on the PC	2026-10-15T10:02:28.9732387Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lstrlib.c.o third_party/lua/lstrlib.c
host	Tests on the PC	2026-10-15T10:02:29.4572772Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/ltable.c.o third_party/lua/ltable.c
host	Tests on the PC	2026-10-15T10:02:29.6847986Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/ltablib.c.o third_party/lua/ltablib.c
host	Tests on the PC	2026-10-15T10:02:29.8042274Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/ltm.c.o third_party/lua/ltm.c
host	Tests on the PC	2026-10-15T10:02:29.9074362Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lundump.c.o third_party/lua/lundump.c
host	Tests on the PC	2026-10-15T10:02:30.0196669Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lutf8lib.c.o third_party/lua/lutf8lib.c
host	Tests on the PC	2026-10-15T10:02:30.1259973Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lvm.c.o third_party/lua/lvm.c
host	Tests on the PC	2026-10-15T10:02:31.4260949Z cc -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o build/host/bmhost/third_party/lua/lzio.c.o third_party/lua/lzio.c
host	Tests on the PC	2026-10-15T10:02:31.4619984Z cc -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -Ibuild host	Tests on the PC	2026-10-15T10:02:31.4630589Z     -o build/host/bmhost-bin tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S build/host/bmhost/src/bm/runtime.c.o build/host/bmhost/src/bm/tokens.c.o build/host/bmhost/src/bm/gfx16.c.o build/host/bmhost/src/bm/r3d.c.o build/host/bmhost/src/bm/world3d.c.o build/host/bmhost/src/bm/map3d.c.o build/host/bmhost/src/bm/format.c.o build/host/bmhost/src/bm/meshcap.c.o build/host/bmhost/src/kernel/syskeys.c.o build/host/bmhost/src/bm/require.c.o build/host/bmhost/src/kernel/prompts.c.o build/host/bmhost/src/gfx/font8x16.c.o build/host/bmhost/src/gfx/font8x14.c.o build/host/bmhost/src/gfx/font6x12.c.o build/host/bmhost/src/lib/printf.c.o build/host/bmhost/src/lib/crc32.c.o build/host/bmhost/src/audio/audio.c.o build/host/bmhost/src/audio/synth.c.o build/host/bmhost/src/audio/player.c.o build/host/bmhost/src/audio/presets.c.o build/host/bmhost/src/audio/lua_tone.c.o build/host/bmhost/src/ai/net.c.o build/host/bmhost/src/ai/nn.c.o build/host/bmhost/src/bm/decimate.c.o build/host/bmhost/src/bm/cutout.c.o build/host/bmhost/src/bm/glb.c.o build/host/bmhost/src/bm/json.c.o build/host/bmhost/src/bm/png.c.o build/host/bmhost/src/bm/jpeg.c.o build/host/bmhost/src/bm/loading.c.o build/host/bmhost/src/bm/loading_logo.c.o build/host/bmhost/src/bm/profile.c.o build/host/bmhost/src/bm/project.c.o build/host/bmhost/src/bm/shot.c.o build/host/bmhost/third_party/lua/lapi.c.o build/host/bmhost/third_party/lua/lauxlib.c.o build/host/bmhost/third_party/lua/lbaselib.c.o build/host/bmhost/third_party/lua/lcode.c.o build/host/bmhost/third_party/lua/lcorolib.c.o build/host/bmhost/third_party/lua/lctype.c.o build/host/bmhost/third_party/lua/ldblib.c.o build/host/bmhost/third_party/lua/ldebug.c.o build/host/bmhost/third_party/lua/ldo.c.o build/host/bmhost/third_party/lua/ldump.c.o build/host/bmhost/third_party/lua/lfunc.c.o build/host/bmhost/third_party/lua/lgc.c.o build/host/bmhost/third_party/lua/linit.c.o build/host/bmhost/third_party/lua/liolib.c.o build/host/bmhost/third_party/lua/llex.c.o build/host/bmhost/third_party/lua/lmathlib.c.o build/host/bmhost/third_party/lua/lmem.c.o build/host/bmhost/third_party/lua/loadlib.c.o build/host/bmhost/third_party/lua/lobject.c.o build/host/bmhost/third_party/lua/lopcodes.c.o build/host/bmhost/third_party/lua/loslib.c.o build/host/bmhost/third_party/lua/lparser.c.o build/host/bmhost/third_party/lua/lstate.c.o build/host/bmhost/third_party/lua/lstring.c.o build/host/bmhost/third_party/lua/lstrlib.c.o build/host/bmhost/third_party/lua/ltable.c.o build/host/bmhost/third_party/lua/ltablib.c.o build/host/bmhost/third_party/lua/ltm.c.o build/host/bmhost/third_party/lua/lundump.c.o build/host/bmhost/third_party/lua/lutf8lib.c.o build/host/bmhost/third_party/lua/lvm.c.o build/host/bmhost/third_party/lua/lzio.c.o -lm
host	Tests on the PC	2026-10-15T10:02:32.2320159Z /usr/bin/ld: build/host/bmhost/third_party/lua/loslib.c.o: in function `os_tmpname':
host	Tests on the PC	2026-10-15T10:02:32.2321728Z /home/runner/work/bm/bm/third_party/lua/loslib.c:174:(.text+0x424): warning: the use of `tmpnam' is dangerous, better use `mkstemp'
host	Tests on the PC	2026-10-15T10:02:32.2723019Z python3 scripts/mkbm.py -o build/riff-test.bm --lua tests/riff/cart.lua --title "riff test"
host	Tests on the PC	2026-10-15T10:02:32.3063504Z build/riff-test.bm: 19128 bytes (riff test, 640x360)
host	Tests on the PC	2026-10-15T10:02:32.3122292Z build/host/bmhost-bin build/riff-test.bm --seconds 7 --wav build/riff/riff.wav 2>&1 | tee build/riff-test.log | grep "^riff"
host	Tests on the PC	2026-10-15T10:02:32.3805563Z riff: drums: no instrument called "nothing" (instruments() lists them)
host	Tests on the PC	2026-10-15T10:02:32.3806171Z riff: 148/148 checks passed
host	Tests on the PC	2026-10-15T10:02:32.3809433Z grep -q "^riff: \([0-9]*\)/ checks passed" build/riff-test.log
host	Tests on the PC	2026-10-15T10:02:32.3846076Z cc -O2 -w -Ithird_party/lua -o build/host/luahost tests/kitchen/luahost.c third_party/lua/lapi.c third_party/lua/lauxlib.c third_party/lua/lbaselib.c third_party/lua/lcode.c third_party/lua/lcorolib.c third_party/lua/lctype.c third_party/lua/ldblib.c third_party/lua/ldebug.c third_party/lua/ldo.c third_party/lua/ldump.c third_party/lua/lfunc.c third_party/lua/lgc.c third_party/lua/linit.c third_party/lua/liolib.c third_party/lua/llex.c third_party/lua/lmathlib.c third_party/lua/lmem.c third_party/lua/loadlib.c third_party/lua/lobject.c third_party/lua/lopcodes.c third_party/lua/loslib.c third_party/lua/lparser.c third_party/lua/lstate.c third_party/lua/lstring.c third_party/lua/lstrlib.c third_party/lua/ltable.c third_party/lua/ltablib.c third_party/lua/ltm.c third_party/lua/lundump.c third_party/lua/lutf8lib.c third_party/lua/lvm.c third_party/lua/lzio.c -lm
host	Tests on the PC	2026-10-15T10:02:38.1045706Z /usr/bin/ld: /tmp/cch93qEb.o: in function `os_tmpname':
host	Tests on the PC	2026-10-15T10:02:38.1046424Z loslib.c:(.text+0x424): warning: the use of `tmpnam' is dangerous, better use `mkstemp'
host	Tests on the PC	2026-10-15T10:02:38.1271382Z python3 scripts/bmaudio.py pack carts/sound/demo.json -o build/demo.bmau
host	Tests on the PC	2026-10-15T10:02:38.1654973Z build/demo.bmau: 13938 bytes
host	Tests on the PC	2026-10-15T10:02:38.1704492Z build/host/luahost tests/sound/sim.lua carts/sound/main.lua build/demo.bmau
host	Tests on the PC	2026-10-15T10:02:39.6863168Z sound editor: 74/74 checks passed, 6740 frames, 631 banks sent
host	Tests on the PC	2026-10-15T10:02:39.6886892Z cc -O2 -w -Isrc -Ithird_party/lua -o build/host/n8host tests/nano8/n8host.c src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c src/bm/png.c src/bm/n8lua.c src/audio/n8snd.c src/bm/gfx16.c src/gfx/font8x16.c host	Tests on the PC	2026-10-15T10:02:39.6893516Z     third_party/lua/lapi.c third_party/lua/lauxlib.c third_party/lua/lbaselib.c third_party/lua/lcode.c third_party/lua/lcorolib.c third_party/lua/lctype.c third_party/lua/ldblib.c third_party/lua/ldebug.c third_party/lua/ldo.c third_party/lua/ldump.c third_party/lua/lfunc.c third_party/lua/lgc.c third_party/lua/linit.c third_party/lua/liolib.c third_party/lua/llex.c third_party/lua/lmathlib.c third_party/lua/lmem.c third_party/lua/loadlib.c third_party/lua/lobject.c third_party/lua/lopcodes.c third_party/lua/loslib.c third_party/lua/lparser.c third_party/lua/lstate.c third_party/lua/lstring.c third_party/lua/lstrlib.c third_party/lua/ltable.c third_party/lua/ltablib.c third_party/lua/ltm.c third_party/lua/lundump.c third_party/lua/lutf8lib.c third_party/lua/lvm.c third_party/lua/lzio.c -lm
host	Tests on the PC	2026-10-15T10:02:48.0879640Z /usr/bin/ld: /tmp/ccDkoXqI.o: in function `os_tmpname':
host	Tests on the PC	2026-10-15T10:02:48.0880634Z loslib.c:(.text+0x424): warning: the use of `tmpnam' is dangerous, better use `mkstemp'
host	Tests on the PC	2026-10-15T10:02:48.1108471Z cc -O2 -Wall -Wextra -Isrc -o build/host/n8cartinfo tests/nano8/cartinfo.c src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c src/bm/png.c -lm
host	Tests on the PC	2026-10-15T10:02:49.5853207Z python3 tests/nano8/run.py --build build carts/nano8/roms/bonplanto.p8.png carts/nano8/roms/momma_zilla.p8 carts/nano8/roms/nanodemo.p8 carts/nano8/roms/piconian.p8.png carts/nano8/roms/plat.p8 carts/nano8/roms/platformer.p8.png carts/nano8/roms/sixlets2.p8.png carts/nano8/roms/starmoovalley.p8.png
host	Tests on the PC	2026-10-15T10:02:51.2304621Z nano8: loading
host	Tests on the PC	2026-10-15T10:02:51.2305564Z /home/runner/work/bm/bm/tests/nano8/carts/api.p8 v41  code   5827  label no   "nano8 api test" / "by bm"  | -- nano8 api test
host	Tests on the PC	2026-10-15T10:02:51.2306890Z carts/nano8/roms/bonplanto.p8.png v43  code  36938  label yes  "bon planto" / "infinite token"  | --bon planto
host	Tests on the PC	2026-10-15T10:02:51.2308035Z carts/nano8/roms/momma_zilla.p8 v8   code  11185  label no   "" / ""  | stage_y=40
host	Tests on the PC	2026-10-15T10:02:51.2309430Z carts/nano8/roms/nanodemo.p8 v41  code   2742  label no   "comet catcher" / "a nano8 demo, by bm"  | -- comet catcher
host	Tests on the PC	2026-10-15T10:02:51.2310498Z carts/nano8/roms/piconian.p8.png v18  code  20057  label yes  "piconian" / "by aquova"  | -- piconian
host	Tests on the PC	2026-10-15T10:02:51.2311617Z carts/nano8/roms/plat.p8     v11  code  20687  label no   "" / ""  | t=0
host	Tests on the PC	2026-10-15T10:02:51.2312490Z carts/nano8/roms/platformer.p8.png v18  code   8972  label yes  "" / "linear interpolation function"  | 
host	Tests on the PC	2026-10-15T10:02:51.2313500Z carts/nano8/roms/sixlets2.p8.png v30  code  10540  label yes  "sixlets ii" / "@aquova"  | -- sixlets ii
host	Tests on the PC	2026-10-15T10:02:51.2314658Z carts/nano8/roms/starmoovalley.p8.png v18  code  11133  label yes  "starmoo valley" / "by aquova and apple"  | -- starmoo valley
host	Tests on the PC	2026-10-15T10:02:51.2315609Z nano8: translating
host	Tests on the PC	2026-10-15T10:02:51.2316027Z   00_api.p8.lua                          5827 bytes ->   7419
host	Tests on the PC	2026-10-15T10:02:51.2316605Z   01_bonplanto.p8.png.lua               36938 bytes ->  40351
host	Tests on the PC	2026-10-15T10:02:51.2317539Z   02_momma_zilla.p8.lua                 11185 bytes ->  11576
host	Tests on the PC	2026-10-15T10:02:51.2318133Z   03_nanodemo.p8.lua                     2742 bytes ->   3564
host	Tests on the PC	2026-10-15T10:02:51.2318695Z   04_piconian.p8.png.lua                20057 bytes ->  20008
host	Tests on the PC	2026-10-15T10:02:51.2319241Z   05_plat.p8.lua                        20687 bytes ->  20187
host	Tests on the PC	2026-10-15T10:02:51.2319804Z   06_platformer.p8.png.lua               8972 bytes ->   6879
host	Tests on the PC	2026-10-15T10:02:51.2320380Z   07_sixlets2.p8.png.lua                10540 bytes ->  11525
host	Tests on the PC	2026-10-15T10:02:51.2320977Z   08_starmoovalley.p8.png.lua           11133 bytes ->  11263
host	Tests on the PC	2026-10-15T10:02:51.2323793Z nano8: API test cart
host	Tests on the PC	2026-10-15T10:02:51.2324147Z   log: nano8: api: 151 checks, 0 failed
host	Tests on the PC	2026-10-15T10:02:51.2324585Z   log: nano8: api: all tests passed
host	Tests on the PC	2026-10-15T10:02:51.2324987Z nano8: playing every cart for 20 s
host	Tests on the PC	2026-10-15T10:02:51.2325483Z   bonplanto.p8.png             mode=game state=run frames=1199
host	Tests on the PC	2026-10-15T10:02:51.2326143Z   momma_zilla.p8               mode=game state=run frames=600
host	Tests on the PC	2026-10-15T10:02:51.2326799Z   nanodemo.p8                  mode=game state=run frames=1199
host	Tests on the PC	2026-10-15T10:02:51.2327455Z   piconian.p8.png              mode=game state=run frames=600
host	Tests on the PC	2026-10-15T10:02:51.2328079Z   plat.p8                      mode=game state=run frames=600
host	Tests on the PC	2026-10-15T10:02:51.2328697Z   platformer.p8.png            mode=game state=run frames=600
host	Tests on the PC	2026-10-15T10:02:51.2329336Z   sixlets2.p8.png              mode=game state=run frames=600
host	Tests on the PC	2026-10-15T10:02:51.2329977Z   starmoovalley.p8.png         mode=game state=run frames=600
host	Tests on the PC	2026-10-15T10:02:51.2330521Z nano8: all passed
host	Tests on the PC	2026-10-15T10:02:51.2404208Z cc -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -o build/host/test_netcon host	Tests on the PC	2026-10-15T10:02:51.2412076Z 	tests/net/test_netcon.c src/net/netcon.c src/net/netxfer.c src/net/stream.c src/lib/crc32.c third_party/lwip/src/core/altcp.c third_party/lwip/src/core/altcp_alloc.c third_party/lwip/src/core/altcp_tcp.c third_party/lwip/src/core/def.c third_party/lwip/src/core/dns.c third_party/lwip/src/core/inet_chksum.c third_party/lwip/src/core/init.c third_party/lwip/src/core/ip.c third_party/lwip/src/core/mem.c third_party/lwip/src/core/memp.c third_party/lwip/src/core/netif.c third_party/lwip/src/core/pbuf.c third_party/lwip/src/core/raw.c third_party/lwip/src/core/stats.c third_party/lwip/src/core/sys.c third_party/lwip/src/core/tcp.c third_party/lwip/src/core/tcp_in.c third_party/lwip/src/core/tcp_out.c third_party/lwip/src/core/timeouts.c third_party/lwip/src/core/udp.c third_party/lwip/src/core/ipv4/acd.c third_party/lwip/src/core/ipv4/autoip.c third_party/lwip/src/core/ipv4/dhcp.c third_party/lwip/src/core/ipv4/etharp.c third_party/lwip/src/core/ipv4/icmp.c third_party/lwip/src/core/ipv4/igmp.c third_party/lwip/src/core/ipv4/ip4.c third_party/lwip/src/core/ipv4/ip4_addr.c third_party/lwip/src/core/ipv4/ip4_frag.c third_party/lwip/src/netif/ethernet.c third_party/lwip/src/apps/sntp/sntp.c
host	Tests on the PC	2026-10-15T10:02:53.4755791Z cc -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -o build/host/test_ethnet host	Tests on the PC	2026-10-15T10:02:53.4760567Z 	tests/net/test_ethnet.c src/net/net.c src/net/cartnet.c src/usb/smsc95xx.c tests/usb/lan9512_sim.c third_party/lwip/src/core/altcp.c third_party/lwip/src/core/altcp_alloc.c third_party/lwip/src/core/altcp_tcp.c third_party/lwip/src/core/def.c third_party/lwip/src/core/dns.c third_party/lwip/src/core/inet_chksum.c third_party/lwip/src/core/init.c third_party/lwip/src/core/ip.c third_party/lwip/src/core/mem.c third_party/lwip/src/core/memp.c third_party/lwip/src/core/netif.c third_party/lwip/src/core/pbuf.c third_party/lwip/src/core/raw.c third_party/lwip/src/core/stats.c third_party/lwip/src/core/sys.c third_party/lwip/src/core/tcp.c third_party/lwip/src/core/tcp_in.c third_party/lwip/src/core/tcp_out.c third_party/lwip/src/core/timeouts.c third_party/lwip/src/core/udp.c third_party/lwip/src/core/ipv4/acd.c third_party/lwip/src/core/ipv4/autoip.c third_party/lwip/src/core/ipv4/dhcp.c third_party/lwip/src/core/ipv4/etharp.c third_party/lwip/src/core/ipv4/icmp.c third_party/lwip/src/core/ipv4/igmp.c third_party/lwip/src/core/ipv4/ip4.c third_party/lwip/src/core/ipv4/ip4_addr.c third_party/lwip/src/core/ipv4/ip4_frag.c third_party/lwip/src/netif/ethernet.c third_party/lwip/src/apps/sntp/sntp.c
host	Tests on the PC	2026-10-15T10:02:55.5968375Z cc -O1 -Wall -Wextra -Isrc -o build/host/test_config tests/kernel/test_config.c src/kernel/config.c src/lib/printf.c
host	Tests on the PC	2026-10-15T10:02:55.8191910Z build/host/test_netcon
host	Tests on the PC	2026-10-15T10:02:55.8236138Z ok   listening on port 3333
host	Tests on the PC	2026-10-15T10:02:55.8236571Z ok   password from the config
host	Tests on the PC	2026-10-15T10:02:55.8236888Z ok   client connected
host	Tests on the PC	2026-10-15T10:02:55.8237130Z ok   greeting
host	Tests on the PC	2026-10-15T10:02:55.8237347Z before login
host	Tests on the PC	2026-10-15T10:02:55.8237592Z ok   nothing printed before the password
host	Tests on the PC	2026-10-15T10:02:55.8237926Z ok   wrong password refused
host	Tests on the PC	2026-10-15T10:02:55.8238190Z ok   not logged in
host	Tests on the PC	2026-10-15T10:02:55.8238333Z 
host	Tests on the PC	2026-10-15T10:02:55.8238445Z net: console opened from 127.0.0.1
host	Tests on the PC	2026-10-15T10:02:55.8238784Z > ok   right password accepted
host	Tests on the PC	2026-10-15T10:02:55.8239051Z ok   logged in
host	Tests on the PC	2026-10-15T10:02:55.8239390Z ok   the 
 after the password is no key (it played a game in the menu)
host	Tests on the PC	2026-10-15T10:02:55.8239997Z ok   the login says what takes the keys (the menu: no prompt)
host	Tests on the PC	2026-10-15T10:02:55.8240513Z ok   a change of what takes the keys, said once
host	Tests on the PC	2026-10-15T10:02:55.8241032Z hello
host	Tests on the PC	2026-10-15T10:02:55.8241677Z world
host	Tests on the PC	2026-10-15T10:02:55.8242014Z ok   kprintf reaches the client, 
 line ends
host	Tests on the PC	2026-10-15T10:02:55.8242513Z ok   keys typed on the client, 
 as one Enter
host	Tests on the PC	2026-10-15T10:02:55.8242970Z ok   long output delivered
host	Tests on the PC	2026-10-15T10:02:55.8243211Z 
host	Tests on the PC	2026-10-15T10:02:55.8298753Z net: a new console connection takes over
host	Tests on the PC	2026-10-15T10:02:55.8299407Z ok   a second client takes over
host	Tests on the PC	2026-10-15T10:02:55.8299830Z ok   the first one is dropped
host	Tests on the PC	2026-10-15T10:02:55.8300235Z ok   client left
host	Tests on the PC	2026-10-15T10:02:55.8300442Z 
host	Tests on the PC	2026-10-15T10:02:55.8300943Z ^[[91mnet: console: wrong password from 127.0.0.1^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8301765Z ok   three wrong passwords: closed
host	Tests on the PC	2026-10-15T10:02:55.8302233Z ok   stream: connected to 127.0.0.1:3333
host	Tests on the PC	2026-10-15T10:02:55.8302697Z ok   stream: greeting read
host	Tests on the PC	2026-10-15T10:02:55.8303145Z ok   stream: password written
host	Tests on the PC	2026-10-15T10:02:55.8303494Z 
host	Tests on the PC	2026-10-15T10:02:55.8303653Z net: console opened from 127.0.0.1
host	Tests on the PC	2026-10-15T10:02:55.8304062Z > ok   stream: logged in
host	Tests on the PC	2026-10-15T10:02:55.8304472Z ok   stream: 10 KB read in pieces, in order
host	Tests on the PC	2026-10-15T10:02:55.8304812Z 
host	Tests on the PC	2026-10-15T10:02:55.8304964Z net: console closed
host	Tests on the PC	2026-10-15T10:02:55.8305337Z ok   stream: closed port, an error
host	Tests on the PC	2026-10-15T10:02:55.8305783Z ok   transfers listening on port 3334
host	Tests on the PC	2026-10-15T10:02:55.8306268Z net: receiving settings (27 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8306738Z ok   settings: the header taken
host	Tests on the PC	2026-10-15T10:02:55.8307464Z ^[[92mnet: settings from the PC: 1 changed in bm/config.txt^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8308058Z ok     the lines reach config_merge
host	Tests on the PC	2026-10-15T10:02:55.8308549Z ok     OK, then the length and the settings after
host	Tests on the PC	2026-10-15T10:02:55.8309549Z net: receiving settings (9 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8310040Z ok   settings: wrong lines, the header taken
host	Tests on the PC	2026-10-15T10:02:55.8310855Z ^[[91mnet: settings refused (not key=value lines), nothing changed^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8311701Z ok     the lines reach config_merge
host	Tests on the PC	2026-10-15T10:02:55.8312138Z ok     wrong lines: KV
host	Tests on the PC	2026-10-15T10:02:55.8312540Z net: receiving carts/game.bm (5000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8313222Z net: /carts/game.bm received (5000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8313939Z ok   file: QD once checked, nothing written yet
host	Tests on the PC	2026-10-15T10:02:55.8314456Z ok     queued: the menu tags that game only
host	Tests on the PC	2026-10-15T10:02:55.8314915Z ok     its name and size
host	Tests on the PC	2026-10-15T10:02:55.8315385Z ok     another file meanwhile: BY, refused after the header
host	Tests on the PC	2026-10-15T10:02:55.8315945Z ok     a kernel meanwhile: BY
host	Tests on the PC	2026-10-15T10:02:55.8316389Z net: receiving /carts/GAME.BM (7000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8317285Z net: /carts/GAME.BM received (7000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8317751Z ok     the same game again: QD, it replaces the copy waiting
host	Tests on the PC	2026-10-15T10:02:55.8318142Z ok     a pause before the write started: still waiting
host	Tests on the PC	2026-10-15T10:02:55.8318509Z net: writing /carts/GAME.BM paused, it starts again later
host	Tests on the PC	2026-10-15T10:02:55.8318899Z ok     a write stopped half way: nothing written, still waiting
host	Tests on the PC	2026-10-15T10:02:55.8319373Z ^[[92mnet: saved /carts/GAME.BM on the SD card (7000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8319770Z ok     written from the menu: the newer copy, counted, the tag gone
host	Tests on the PC	2026-10-15T10:02:55.8320116Z ok     a notice says so
host	Tests on the PC	2026-10-15T10:02:55.8320350Z net: receiving top.bm (5000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8320704Z net: /top.bm received (5000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8321509Z ^[[92mnet: saved /top.bm on the SD card (5000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8321863Z ok     a file for the root: written in /
host	Tests on the PC	2026-10-15T10:02:55.8322229Z net: receiving carts/pong.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8322637Z net: /carts/pong.bm received (100000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8323041Z ok   file saved on the SD card
host	Tests on the PC	2026-10-15T10:02:55.8323418Z ^[[92mnet: saved /carts/pong.bm on the SD card (100000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8323765Z ok     in /carts/pong.bm, same bytes
host	Tests on the PC	2026-10-15T10:02:55.8324135Z ^[[91mnet: transfer with a wrong password from 127.0.0.1^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8324460Z ok   wrong password refused
host	Tests on the PC	2026-10-15T10:02:55.8324682Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8324914Z net: receiving carts/bad.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8325274Z ^[[91mnet: transfer damaged (crc), nothing written^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8325573Z ok   damaged file refused
host	Tests on the PC	2026-10-15T10:02:55.8325796Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8326014Z net: receiving x.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8326276Z net: cartridge received, starting it
host	Tests on the PC	2026-10-15T10:02:55.8326550Z ok   cartridge to play received
host	Tests on the PC	2026-10-15T10:02:55.8326795Z ok     announced once
host	Tests on the PC	2026-10-15T10:02:55.8327015Z ok     the monitor gets the same bytes
host	Tests on the PC	2026-10-15T10:02:55.8327266Z ok     taken only once
host	Tests on the PC	2026-10-15T10:02:55.8327498Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8327770Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8327997Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8328226Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8328450Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8328677Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8328897Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8329132Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8329354Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8329574Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8330240Z ^[[91mnet: not a kernel for this console, nothing written (the Pi Zero 2 W takes kernel7.img, the RGB30 kernel8.img, the other boards kernel.img)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8330851Z ok   the Pi Zero 2 W's kernel refused
host	Tests on the PC	2026-10-15T10:02:55.8331673Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8331917Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8332197Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8332434Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8332660Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8332882Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8333102Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8333323Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8333542Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8333760Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8333981Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8334671Z ^[[91mnet: not a kernel for this console, nothing written (the Pi Zero 2 W takes kernel7.img, the RGB30 kernel8.img, the other boards kernel.img)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8335277Z ok   the RGB30's kernel refused
host	Tests on the PC	2026-10-15T10:02:55.8335541Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8335993Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8336216Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8336440Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8336664Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8336883Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8337102Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8337324Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8337544Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8337768Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8338107Z ^[[92mnet: saved kernel.img on the SD card (100000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8338450Z net: restarting with the new kernel in 3 s
host	Tests on the PC	2026-10-15T10:02:55.8338725Z ok     restart counted down from 3 s
host	Tests on the PC	2026-10-15T10:02:55.8338996Z net: restarting with the new kernel in 2 s
host	Tests on the PC	2026-10-15T10:02:55.8339276Z net: restarting with the new kernel in 1 s
host	Tests on the PC	2026-10-15T10:02:55.8339556Z net: restarting with the new kernel...
host	Tests on the PC	2026-10-15T10:02:55.8339859Z ok   the access point told we leave before the restart
host	Tests on the PC	2026-10-15T10:02:55.8340340Z ok   a clean exit marked before the restart (no freeze report at the next boot), the log flushed
host	Tests on the PC	2026-10-15T10:02:55.8340918Z ok   the network polled while the kernel was written, no write started again from inside
host	Tests on the PC	2026-10-15T10:02:55.8341656Z ok   kernel received, written as /kernel.img, then reboot
host	Tests on the PC	2026-10-15T10:02:55.8341889Z 
host	Tests on the PC	2026-10-15T10:02:55.8341992Z netcon (kernel at once): all passed
host	Tests on the PC	2026-10-15T10:02:55.8342249Z build/host/test_netcon menu
host	Tests on the PC	2026-10-15T10:02:55.8342479Z ok   listening on port 3333
host	Tests on the PC	2026-10-15T10:02:55.8342713Z ok   password from the config
host	Tests on the PC	2026-10-15T10:02:55.8342944Z ok   client connected
host	Tests on the PC	2026-10-15T10:02:55.8343140Z ok   greeting
host	Tests on the PC	2026-10-15T10:02:55.8343321Z before login
host	Tests on the PC	2026-10-15T10:02:55.8343525Z ok   nothing printed before the password
host	Tests on the PC	2026-10-15T10:02:55.8343792Z ok   wrong password refused
host	Tests on the PC	2026-10-15T10:02:55.8344017Z ok   not logged in
host	Tests on the PC	2026-10-15T10:02:55.8344140Z 
host	Tests on the PC	2026-10-15T10:02:55.8344238Z net: console opened from 127.0.0.1
host	Tests on the PC	2026-10-15T10:02:55.8344502Z > ok   right password accepted
host	Tests on the PC	2026-10-15T10:02:55.8344729Z ok   logged in
host	Tests on the PC	2026-10-15T10:02:55.8345009Z ok   the 
 after the password is no key (it played a game in the menu)
host	Tests on the PC	2026-10-15T10:02:55.8345422Z ok   the login says what takes the keys (the menu: no prompt)
host	Tests on the PC	2026-10-15T10:02:55.8345769Z ok   a change of what takes the keys, said once
host	Tests on the PC	2026-10-15T10:02:55.8346038Z hello
host	Tests on the PC	2026-10-15T10:02:55.8346204Z world
host	Tests on the PC	2026-10-15T10:02:55.8346404Z ok   kprintf reaches the client, 
 line ends
host	Tests on the PC	2026-10-15T10:02:55.8346709Z ok   keys typed on the client, 
 as one Enter
host	Tests on the PC	2026-10-15T10:02:55.8346999Z ok   long output delivered
host	Tests on the PC	2026-10-15T10:02:55.8347139Z 
host	Tests on the PC	2026-10-15T10:02:55.8347243Z net: a new console connection takes over
host	Tests on the PC	2026-10-15T10:02:55.8347512Z ok   a second client takes over
host	Tests on the PC	2026-10-15T10:02:55.8347752Z ok   the first one is dropped
host	Tests on the PC	2026-10-15T10:02:55.8347977Z ok   client left
host	Tests on the PC	2026-10-15T10:02:55.8348092Z 
host	Tests on the PC	2026-10-15T10:02:55.8348269Z ^[[91mnet: console: wrong password from 127.0.0.1^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8348748Z ok   three wrong passwords: closed
host	Tests on the PC	2026-10-15T10:02:55.8349006Z ok   stream: connected to 127.0.0.1:3333
host	Tests on the PC	2026-10-15T10:02:55.8349265Z ok   stream: greeting read
host	Tests on the PC	2026-10-15T10:02:55.8349498Z ok   stream: password written
host	Tests on the PC	2026-10-15T10:02:55.8349654Z 
host	Tests on the PC	2026-10-15T10:02:55.8349748Z net: console opened from 127.0.0.1
host	Tests on the PC	2026-10-15T10:02:55.8349992Z > ok   stream: logged in
host	Tests on the PC	2026-10-15T10:02:55.8350242Z ok   stream: 10 KB read in pieces, in order
host	Tests on the PC	2026-10-15T10:02:55.8350594Z 
host	Tests on the PC	2026-10-15T10:02:55.8350743Z net: console closed
host	Tests on the PC	2026-10-15T10:02:55.8351022Z ok   stream: closed port, an error
host	Tests on the PC	2026-10-15T10:02:55.8351684Z ok   transfers listening on port 3334
host	Tests on the PC	2026-10-15T10:02:55.8352145Z net: receiving settings (27 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8352577Z ok   settings: the header taken
host	Tests on the PC	2026-10-15T10:02:55.8352988Z ^[[92mnet: settings from the PC: 1 changed in bm/config.txt^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8353329Z ok     the lines reach config_merge
host	Tests on the PC	2026-10-15T10:02:55.8353616Z ok     OK, then the length and the settings after
host	Tests on the PC	2026-10-15T10:02:55.8354110Z net: receiving settings (9 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8354383Z ok   settings: wrong lines, the header taken
host	Tests on the PC	2026-10-15T10:02:55.8354805Z ^[[91mnet: settings refused (not key=value lines), nothing changed^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8355168Z ok     the lines reach config_merge
host	Tests on the PC	2026-10-15T10:02:55.8355411Z ok     wrong lines: KV
host	Tests on the PC	2026-10-15T10:02:55.8355641Z net: receiving carts/game.bm (5000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8356026Z net: /carts/game.bm received (5000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8356437Z ok   file: QD once checked, nothing written yet
host	Tests on the PC	2026-10-15T10:02:55.8356736Z ok     queued: the menu tags that game only
host	Tests on the PC	2026-10-15T10:02:55.8357000Z ok     its name and size
host	Tests on the PC	2026-10-15T10:02:55.8357274Z ok     another file meanwhile: BY, refused after the header
host	Tests on the PC	2026-10-15T10:02:55.8357589Z ok     a kernel meanwhile: BY
host	Tests on the PC	2026-10-15T10:02:55.8357844Z net: receiving /carts/GAME.BM (7000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8358242Z net: /carts/GAME.BM received (7000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8358671Z ok     the same game again: QD, it replaces the copy waiting
host	Tests on the PC	2026-10-15T10:02:55.8359024Z ok     a pause before the write started: still waiting
host	Tests on the PC	2026-10-15T10:02:55.8359375Z net: writing /carts/GAME.BM paused, it starts again later
host	Tests on the PC	2026-10-15T10:02:55.8359746Z ok     a write stopped half way: nothing written, still waiting
host	Tests on the PC	2026-10-15T10:02:55.8360192Z ^[[92mnet: saved /carts/GAME.BM on the SD card (7000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8360885Z ok     written from the menu: the newer copy, counted, the tag gone
host	Tests on the PC	2026-10-15T10:02:55.8361500Z ok     a notice says so
host	Tests on the PC	2026-10-15T10:02:55.8361733Z net: receiving top.bm (5000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8362077Z net: /top.bm received (5000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8362675Z ^[[92mnet: saved /top.bm on the SD card (5000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8363167Z ok     a file for the root: written in /
host	Tests on the PC	2026-10-15T10:02:55.8363652Z net: receiving carts/pong.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8364184Z net: /carts/pong.bm received (100000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8364581Z ok   file saved on the SD card
host	Tests on the PC	2026-10-15T10:02:55.8364962Z ^[[92mnet: saved /carts/pong.bm on the SD card (100000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8365298Z ok     in /carts/pong.bm, same bytes
host	Tests on the PC	2026-10-15T10:02:55.8365863Z ^[[91mnet: transfer with a wrong password from 127.0.0.1^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8366219Z ok   wrong password refused
host	Tests on the PC	2026-10-15T10:02:55.8366451Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8366683Z net: receiving carts/bad.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8367047Z ^[[91mnet: transfer damaged (crc), nothing written^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8367348Z ok   damaged file refused
host	Tests on the PC	2026-10-15T10:02:55.8367563Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8367778Z net: receiving x.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8368038Z net: cartridge received, starting it
host	Tests on the PC	2026-10-15T10:02:55.8368298Z ok   cartridge to play received
host	Tests on the PC	2026-10-15T10:02:55.8368746Z ok     announced once
host	Tests on the PC	2026-10-15T10:02:55.8368965Z ok     the monitor gets the same bytes
host	Tests on the PC	2026-10-15T10:02:55.8369220Z ok     taken only once
host	Tests on the PC	2026-10-15T10:02:55.8369448Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8369719Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8369945Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8370172Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8370393Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8370616Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8370836Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8371060Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8371667Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8371891Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8372583Z ^[[91mnet: not a kernel for this console, nothing written (the Pi Zero 2 W takes kernel7.img, the RGB30 kernel8.img, the other boards kernel.img)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8373489Z ok   the Pi Zero 2 W's kernel refused
host	Tests on the PC	2026-10-15T10:02:55.8374114Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8374516Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8374918Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8375149Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8375370Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8375590Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8375814Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8376033Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8376515Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8376755Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8376980Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8377666Z ^[[91mnet: not a kernel for this console, nothing written (the Pi Zero 2 W takes kernel7.img, the RGB30 kernel8.img, the other boards kernel.img)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8378273Z ok   the RGB30's kernel refused
host	Tests on the PC	2026-10-15T10:02:55.8378524Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8378859Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8379081Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8379300Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8379519Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8379738Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8379957Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8380173Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8380393Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8380612Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8380936Z net: kernel received (100000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8381771Z ^[[92mnet: saved kernel.img on the SD card (100000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8382117Z net: restarting with the new kernel in 3 s
host	Tests on the PC	2026-10-15T10:02:55.8382395Z ok     restart counted down from 3 s
host	Tests on the PC	2026-10-15T10:02:55.8382675Z net: restarting with the new kernel in 2 s
host	Tests on the PC	2026-10-15T10:02:55.8382961Z net: restarting with the new kernel in 1 s
host	Tests on the PC	2026-10-15T10:02:55.8383241Z net: restarting with the new kernel...
host	Tests on the PC	2026-10-15T10:02:55.8383560Z ok   the access point told we leave before the restart
host	Tests on the PC	2026-10-15T10:02:55.8384034Z ok   a clean exit marked before the restart (no freeze report at the next boot), the log flushed
host	Tests on the PC	2026-10-15T10:02:55.8384549Z ok   the menu's fiber wrote it (no write at once), nothing left set
host	Tests on the PC	2026-10-15T10:02:55.8384943Z ok   kernel received, written as /kernel.img, then reboot
host	Tests on the PC	2026-10-15T10:02:55.8385171Z 
host	Tests on the PC	2026-10-15T10:02:55.8385276Z netcon (kernel by the menu): all passed
host	Tests on the PC	2026-10-15T10:02:55.8385544Z build/host/test_netcon gone
host	Tests on the PC	2026-10-15T10:02:55.8385780Z ok   listening on port 3333
host	Tests on the PC	2026-10-15T10:02:55.8386017Z ok   password from the config
host	Tests on the PC	2026-10-15T10:02:55.8386413Z ok   client connected
host	Tests on the PC	2026-10-15T10:02:55.8386762Z ok   greeting
host	Tests on the PC	2026-10-15T10:02:55.8387001Z before login
host	Tests on the PC	2026-10-15T10:02:55.8387211Z ok   nothing printed before the password
host	Tests on the PC	2026-10-15T10:02:55.8387621Z ok   wrong password refused
host	Tests on the PC	2026-10-15T10:02:55.8388016Z ok   not logged in
host	Tests on the PC	2026-10-15T10:02:55.8388482Z 
host	Tests on the PC	2026-10-15T10:02:55.8388646Z net: console opened from 127.0.0.1
host	Tests on the PC	2026-10-15T10:02:55.8388931Z > ok   right password accepted
host	Tests on the PC	2026-10-15T10:02:55.8389174Z ok   logged in
host	Tests on the PC	2026-10-15T10:02:55.8389466Z ok   the 
 after the password is no key (it played a game in the menu)
host	Tests on the PC	2026-10-15T10:02:55.8389919Z ok   the login says what takes the keys (the menu: no prompt)
host	Tests on the PC	2026-10-15T10:02:55.8390461Z ok   a change of what takes the keys, said once
host	Tests on the PC	2026-10-15T10:02:55.8390778Z hello
host	Tests on the PC	2026-10-15T10:02:55.8390943Z world
host	Tests on the PC	2026-10-15T10:02:55.8391478Z ok   kprintf reaches the client, 
 line ends
host	Tests on the PC	2026-10-15T10:02:55.8391842Z ok   keys typed on the client, 
 as one Enter
host	Tests on the PC	2026-10-15T10:02:55.8392169Z ok   long output delivered
host	Tests on the PC	2026-10-15T10:02:55.8392319Z 
host	Tests on the PC	2026-10-15T10:02:55.8392428Z net: a new console connection takes over
host	Tests on the PC	2026-10-15T10:02:55.8392707Z ok   a second client takes over
host	Tests on the PC	2026-10-15T10:02:55.8392957Z ok   the first one is dropped
host	Tests on the PC	2026-10-15T10:02:55.8393188Z ok   client left
host	Tests on the PC	2026-10-15T10:02:55.8393499Z 
host	Tests on the PC	2026-10-15T10:02:55.8393718Z ^[[91mnet: console: wrong password from 127.0.0.1^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8394035Z ok   three wrong passwords: closed
host	Tests on the PC	2026-10-15T10:02:55.8394299Z ok   stream: connected to 127.0.0.1:3333
host	Tests on the PC	2026-10-15T10:02:55.8394572Z ok   stream: greeting read
host	Tests on the PC	2026-10-15T10:02:55.8394802Z ok   stream: password written
host	Tests on the PC	2026-10-15T10:02:55.8394955Z 
host	Tests on the PC	2026-10-15T10:02:55.8395049Z net: console opened from 127.0.0.1
host	Tests on the PC	2026-10-15T10:02:55.8395304Z > ok   stream: logged in
host	Tests on the PC	2026-10-15T10:02:55.8395546Z ok   stream: 10 KB read in pieces, in order
host	Tests on the PC	2026-10-15T10:02:55.8395752Z 
host	Tests on the PC	2026-10-15T10:02:55.8395830Z net: console closed
host	Tests on the PC	2026-10-15T10:02:55.8396043Z ok   stream: closed port, an error
host	Tests on the PC	2026-10-15T10:02:55.8396303Z ok   transfers listening on port 3334
host	Tests on the PC	2026-10-15T10:02:55.8396573Z net: receiving settings (27 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8396833Z ok   settings: the header taken
host	Tests on the PC	2026-10-15T10:02:55.8397205Z ^[[92mnet: settings from the PC: 1 changed in bm/config.txt^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8397562Z ok     the lines reach config_merge
host	Tests on the PC	2026-10-15T10:02:55.8397858Z ok     OK, then the length and the settings after
host	Tests on the PC	2026-10-15T10:02:55.8398165Z net: receiving settings (9 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8398442Z ok   settings: wrong lines, the header taken
host	Tests on the PC	2026-10-15T10:02:55.8398853Z ^[[91mnet: settings refused (not key=value lines), nothing changed^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8399213Z ok     the lines reach config_merge
host	Tests on the PC	2026-10-15T10:02:55.8399455Z ok     wrong lines: KV
host	Tests on the PC	2026-10-15T10:02:55.8399685Z net: receiving carts/game.bm (5000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8400077Z net: /carts/game.bm received (5000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8400491Z ok   file: QD once checked, nothing written yet
host	Tests on the PC	2026-10-15T10:02:55.8400790Z ok     queued: the menu tags that game only
host	Tests on the PC	2026-10-15T10:02:55.8401058Z ok     its name and size
host	Tests on the PC	2026-10-15T10:02:55.8401693Z ok     another file meanwhile: BY, refused after the header
host	Tests on the PC	2026-10-15T10:02:55.8402015Z ok     a kernel meanwhile: BY
host	Tests on the PC	2026-10-15T10:02:55.8402286Z net: receiving /carts/GAME.BM (7000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8402673Z net: /carts/GAME.BM received (7000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8403100Z ok     the same game again: QD, it replaces the copy waiting
host	Tests on the PC	2026-10-15T10:02:55.8403454Z ok     a pause before the write started: still waiting
host	Tests on the PC	2026-10-15T10:02:55.8403808Z net: writing /carts/GAME.BM paused, it starts again later
host	Tests on the PC	2026-10-15T10:02:55.8404180Z ok     a write stopped half way: nothing written, still waiting
host	Tests on the PC	2026-10-15T10:02:55.8404628Z ^[[92mnet: saved /carts/GAME.BM on the SD card (7000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8405023Z ok     written from the menu: the newer copy, counted, the tag gone
host	Tests on the PC	2026-10-15T10:02:55.8405354Z ok     a notice says so
host	Tests on the PC	2026-10-15T10:02:55.8405575Z net: receiving top.bm (5000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8405919Z net: /top.bm received (5000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8406365Z ^[[92mnet: saved /top.bm on the SD card (5000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8406885Z ok     a file for the root: written in /
host	Tests on the PC	2026-10-15T10:02:55.8407174Z net: receiving carts/pong.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8407570Z net: /carts/pong.bm received (100000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8407958Z ok   file saved on the SD card
host	Tests on the PC	2026-10-15T10:02:55.8408316Z ^[[92mnet: saved /carts/pong.bm on the SD card (100000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8408655Z ok     in /carts/pong.bm, same bytes
host	Tests on the PC	2026-10-15T10:02:55.8409012Z ^[[91mnet: transfer with a wrong password from 127.0.0.1^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8409335Z ok   wrong password refused
host	Tests on the PC	2026-10-15T10:02:55.8409557Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8409786Z net: receiving carts/bad.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8410143Z ^[[91mnet: transfer damaged (crc), nothing written^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8410452Z ok   damaged file refused
host	Tests on the PC	2026-10-15T10:02:55.8410666Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8410879Z net: receiving x.bm (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8411543Z net: cartridge received, starting it
host	Tests on the PC	2026-10-15T10:02:55.8411819Z ok   cartridge to play received
host	Tests on the PC	2026-10-15T10:02:55.8412056Z ok     announced once
host	Tests on the PC	2026-10-15T10:02:55.8412271Z ok     the monitor gets the same bytes
host	Tests on the PC	2026-10-15T10:02:55.8412519Z ok     taken only once
host	Tests on the PC	2026-10-15T10:02:55.8412743Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8413018Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8413268Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8413500Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8413731Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8413953Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8414183Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8414407Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8414642Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8414865Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8415545Z ^[[91mnet: not a kernel for this console, nothing written (the Pi Zero 2 W takes kernel7.img, the RGB30 kernel8.img, the other boards kernel.img)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8416163Z ok   the Pi Zero 2 W's kernel refused
host	Tests on the PC	2026-10-15T10:02:55.8416409Z ok     nothing written
host	Tests on the PC	2026-10-15T10:02:55.8416641Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8416914Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8417140Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8417360Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8417582Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8417803Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8418021Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8418243Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8418464Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8418685Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8419334Z ^[[91mnet: not a kernel for this console, nothing written (the Pi Zero 2 W takes kernel7.img, the RGB30 kernel8.img, the other boards kernel.img)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8419932Z ok   the RGB30's kernel refused
host	Tests on the PC	2026-10-15T10:02:55.8420194Z net: receiving a new kernel (100000 bytes)...
host	Tests on the PC	2026-10-15T10:02:55.8420467Z net: kernel 0% (0 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8420688Z net: kernel 10% (11 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8420915Z net: kernel 20% (22 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8421552Z net: kernel 30% (34 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8421838Z net: kernel 40% (45 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8422062Z net: kernel 50% (57 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8422280Z net: kernel 70% (68 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8422502Z net: kernel 80% (79 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8422727Z net: kernel 90% (91 of 97 KiB)
host	Tests on the PC	2026-10-15T10:02:55.8423066Z net: kernel received (100000 bytes): written on the SD card from the menu
host	Tests on the PC	2026-10-15T10:02:55.8423551Z ^[[92mnet: saved kernel.img on the SD card (100000 bytes)^[[0m
host	Tests on the PC	2026-10-15T10:02:55.8423884Z net: restarting with the new kernel in 3 s
host	Tests on the PC	2026-10-15T10:02:55.8424159Z ok     restart counted down from 3 s
host	Tests on the PC	2026-10-15T10:02:55.8424422Z net: restarting with the new kernel in 2 s
host	Tests on the PC	2026-10-15T10:02:55.8424891Z net: restarting with the new kernel in 1 s
host	Tests on the PC	2026-10-15T10:02:55.8425169Z net: restarting with the new kernel...
host	Tests on the PC	2026-10-15T10:02:55.8425466Z ok   the access point told we leave before the restart
host	Tests on the PC	2026-10-15T10:02:55.8425939Z ok   a clean exit marked before the restart (no freeze report at the next boot), the log flushed
host	Tests on the PC	2026-10-15T10:02:55.8426510Z ok   the network polled while the kernel was written, no write started again from inside
host	Tests on the PC	2026-10-15T10:02:55.8426977Z ok   kernel received, written as /kernel.img, then reboot
host	Tests on the PC	2026-10-15T10:02:55.8427203Z 
host	Tests on the PC	2026-10-15T10:02:55.8427332Z netcon (kernel after the menu left): all passed
host	Tests on the PC	2026-10-15T10:02:55.8427621Z build/host/test_ethnet
host	Tests on the PC	2026-10-15T10:02:55.8427840Z test_ethnet: 40 checks, 0 failures
host	Tests on the PC	2026-10-15T10:02:55.8428083Z build/host/test_config
host	Tests on the PC	2026-10-15T10:02:55.8428315Z config: all 88 passed
host	Tests on the PC	2026-10-15T10:02:55.8428544Z python3 tests/net/check_bm_net_config.py
host	Tests on the PC	2026-10-15T10:02:56.5736217Z ok   --config alone shows the settings
host	Tests on the PC	2026-10-15T10:02:56.5737469Z ok     the token hidden
host	Tests on the PC	2026-10-15T10:02:56.5737892Z ok     a comment sent: nothing changes
host	Tests on the PC	2026-10-15T10:02:56.5738249Z ok   three changes sent
host	Tests on the PC	2026-10-15T10:02:56.5738543Z ok     as key=value lines
host	Tests on the PC	2026-10-15T10:02:56.5738840Z ok     the console has them
host	Tests on the PC	2026-10-15T10:02:56.5739147Z ok     the new token not shown
host	Tests on the PC	2026-10-15T10:02:56.5739468Z ok   'no equals' refused on the PC
host	Tests on the PC	2026-10-15T10:02:56.5739811Z ok   'bad key=1' refused on the PC
host	Tests on the PC	2026-10-15T10:02:56.5740176Z ok   'x=vvvvvvvvvvvvvvvvvv' refused on the PC
host	Tests on the PC	2026-10-15T10:02:56.5740561Z ok   '=1' refused on the PC
host	Tests on the PC	2026-10-15T10:02:56.5740902Z ok   'x=a	b' refused on the PC
host	Tests on the PC	2026-10-15T10:02:56.5741649Z ok     nothing sent
host	Tests on the PC	2026-10-15T10:02:56.5741988Z ok   a wrong code: wrong password
host	Tests on the PC	2026-10-15T10:02:56.5742359Z ok   --send still the same (bm/CHECK.PY)
host	Tests on the PC	2026-10-15T10:02:56.5742831Z ok   --send to a newer kernel: QD, received, written from its menu
host	Tests on the PC	2026-10-15T10:02:56.5743360Z ok   --send while a file still waits: BY, nothing sent
host	Tests on the PC	2026-10-15T10:02:56.5743817Z ok   a kernel without C: send it a newer one
host	Tests on the PC	2026-10-15T10:02:56.5744206Z ok   --line sends ':' + the line + Enter
host	Tests on the PC	2026-10-15T10:02:56.5744573Z ok     its output shown
host	Tests on the PC	2026-10-15T10:02:56.5744991Z ok     the login ends at the line saying what takes the keys (no 5 s wait)
host	Tests on the PC	2026-10-15T10:02:56.5745496Z ok   --line in a game: nothing sent
host	Tests on the PC	2026-10-15T10:02:56.5745934Z ok   --kernel without an answer: the console's new version found
host	Tests on the PC	2026-10-15T10:02:56.5746446Z ok   --line on a kernel from before: said, still sent
host	Tests on the PC	2026-10-15T10:02:56.5746878Z bm_net --config, --line: all passed
host	Tests on the PC	2026-10-15T10:02:56.5820917Z cc -O1 -Wall -Wextra -Isrc -DHTTP_USER_AGENT='"test"' -o build/host/test_http tests/net/test_http.c src/net/http.c
host	Tests on the PC	2026-10-15T10:02:56.6195752Z tests/net/test_http.c: In function ‘main’:
host	Tests on the PC	2026-10-15T10:02:56.6197113Z tests/net/test_http.c:143:24: warning: missing initializer for field ‘any_status’ of ‘http_req_t’ [-Wmissing-field-initializers]
host	Tests on the PC	2026-10-15T10:02:56.6198372Z   143 |                        body, sizeof body - 1, 0 };
host	Tests on the PC	2026-10-15T10:02:56.6199116Z       |                        ^~~~
host	Tests on the PC	2026-10-15T10:02:56.6199594Z In file included from tests/net/test_http.c:6:
host	Tests on the PC	2026-10-15T10:02:56.6200295Z src/net/http.h:30:9: note: ‘any_status’ declared here
host	Tests on the PC	2026-10-15T10:02:56.6201056Z    30 |     int any_status;             /* the body of any status goes to the sink (an API's
host	Tests on the PC	2026-10-15T10:02:56.6202074Z       |         ^~~~~~~~~~
host	Tests on the PC	2026-10-15T10:02:56.7453092Z python3 tests/net/run_http_test.py build/host/test_http
host	Tests on the PC	2026-10-15T10:02:57.1178768Z ----------------------------------------
host	Tests on the PC	2026-10-15T10:02:57.1179497Z Exception occurred during processing of request from ('127.0.0.1', 57816)
host	Tests on the PC	2026-10-15T10:02:57.1203253Z ok   GET with Content-Length
host	Tests on the PC	2026-10-15T10:02:57.1203843Z ok   chunked body
host	Tests on the PC	2026-10-15T10:02:57.1204372Z ok   redirect (relative Location)
host	Tests on the PC	2026-10-15T10:02:57.1204962Z ok   redirect (absolute Location)
host	Tests on the PC	2026-10-15T10:02:57.1205580Z ok   redirect to a 1.5 KB address, whole
host	Tests on the PC	2026-10-15T10:02:57.1206227Z ok   404: status, no body kept
host	Tests on the PC	2026-10-15T10:02:57.1207282Z ok   3 MiB body, same bytes
host	Tests on the PC	2026-10-15T10:02:57.1207827Z ok   limit: stops at max bytes
host	Tests on the PC	2026-10-15T10:02:57.1208358Z ok   body until the server closes
host	Tests on the PC	2026-10-15T10:02:57.1208945Z ok   PUT with headers and body
host	Tests on the PC	2026-10-15T10:02:57.1209515Z ok   connection refused: an error
host	Tests on the PC	2026-10-15T10:02:57.1210108Z ok   not http: an error
host	Tests on the PC	2026-10-15T10:02:57.1210470Z 
host	Tests on the PC	2026-10-15T10:02:57.1210846Z http: all passed
host	Tests on the PC	2026-10-15T10:02:57.1232065Z Traceback (most recent call last):
host	Tests on the PC	2026-10-15T10:02:57.1260132Z   File "/usr/lib/python3.12/socketserver.py", line 692, in process_request_thread
host	Tests on the PC	2026-10-15T10:02:57.1260974Z     self.finish_request(request, client_address)
host	Tests on the PC	2026-10-15T10:02:57.1261875Z   File "/usr/lib/python3.12/socketserver.py", line 362, in finish_request
host	Tests on the PC	2026-10-15T10:02:57.1262648Z     self.RequestHandlerClass(request, client_address, self)
host	Tests on the PC	2026-10-15T10:02:57.1263358Z   File "/usr/lib/python3.12/socketserver.py", line 761, in __init__
host	Tests on the PC	2026-10-15T10:02:57.1263942Z     self.handle()
host	Tests on the PC	2026-10-15T10:02:57.1264667Z   File "/usr/lib/python3.12/http/server.py", line 436, in handle
host	Tests on the PC	2026-10-15T10:02:57.1265066Z     self.handle_one_request()
host	Tests on the PC	2026-10-15T10:02:57.1265471Z   File "/usr/lib/python3.12/http/server.py", line 424, in handle_one_request
host	Tests on the PC	2026-10-15T10:02:57.1265878Z     method()
host	Tests on the PC	2026-10-15T10:02:57.1266232Z   File "/home/runner/work/bm/bm/tests/net/run_http_test.py", line 48, in do_GET
host	Tests on the PC	2026-10-15T10:02:57.1266730Z     self.send(200, BIG, "application/octet-stream")
host	Tests on the PC	2026-10-15T10:02:57.1267193Z   File "/home/runner/work/bm/bm/tests/net/run_http_test.py", line 25, in send
host	Tests on the PC	2026-10-15T10:02:57.1267628Z     self.wfile.write(body)
host	Tests on the PC	2026-10-15T10:02:57.1267966Z   File "/usr/lib/python3.12/socketserver.py", line 840, in write
host	Tests on the PC	2026-10-15T10:02:57.1268337Z     self._sock.sendall(b)
host	Tests on the PC	2026-10-15T10:02:57.1268661Z ConnectionResetError: [Errno 104] Connection reset by peer
host	Tests on the PC	2026-10-15T10:02:57.1269044Z ----------------------------------------
host	Tests on the PC	2026-10-15T10:02:57.6365352Z cc -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/mbedtls/include host	Tests on the PC	2026-10-15T10:02:57.6366199Z 	-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -DHTTP_USER_AGENT='"test"' -o build/host/test_https host	Tests on the PC	2026-10-15T10:02:57.6366940Z 	tests/net/test_https.c tests/net/stream_posix.c src/net/tls.c src/net/http.c host	Tests on the PC	2026-10-15T10:02:57.6374239Z 	src/net/http_kernel.c third_party/mbedtls/library/aes.c third_party/mbedtls/library/asn1parse.c third_party/mbedtls/library/asn1write.c third_party/mbedtls/library/base64.c third_party/mbedtls/library/bignum.c third_party/mbedtls/library/bignum_core.c third_party/mbedtls/library/chacha20.c third_party/mbedtls/library/chachapoly.c third_party/mbedtls/library/cipher.c third_party/mbedtls/library/cipher_wrap.c third_party/mbedtls/library/constant_time.c third_party/mbedtls/library/ctr_drbg.c third_party/mbedtls/library/ecdh.c third_party/mbedtls/library/ecdsa.c third_party/mbedtls/library/ecp.c third_party/mbedtls/library/ecp_curves.c third_party/mbedtls/library/entropy.c third_party/mbedtls/library/error.c third_party/mbedtls/library/gcm.c third_party/mbedtls/library/md.c third_party/mbedtls/library/oid.c third_party/mbedtls/library/pem.c third_party/mbedtls/library/pk.c third_party/mbedtls/library/pk_ecc.c third_party/mbedtls/library/pk_wrap.c third_party/mbedtls/library/pkparse.c third_party/mbedtls/library/platform.c third_party/mbedtls/library/platform_util.c third_party/mbedtls/library/poly1305.c third_party/mbedtls/library/rsa.c third_party/mbedtls/library/rsa_alt_helpers.c third_party/mbedtls/library/sha1.c third_party/mbedtls/library/sha256.c third_party/mbedtls/library/sha512.c third_party/mbedtls/library/ssl_ciphersuites.c third_party/mbedtls/library/ssl_client.c third_party/mbedtls/library/ssl_msg.c third_party/mbedtls/library/ssl_tls.c third_party/mbedtls/library/ssl_tls12_client.c third_party/mbedtls/library/x509.c third_party/mbedtls/library/x509_crt.c
host	Tests on the PC	2026-10-15T10:03:01.7661705Z python3 tests/net/run_https_test.py build/host/test_https
host	Tests on the PC	2026-10-15T10:03:02.3791542Z ok   no roots loaded: refused
host	Tests on the PC	2026-10-15T10:03:02.3792873Z ok   boot/ca.pem: all 21 roots readable by mbedTLS
host	Tests on the PC	2026-10-15T10:03:02.3793313Z ok   test CA loaded
host	Tests on the PC	2026-10-15T10:03:02.3793585Z ok   https GET, ECDSA certificate
host	Tests on the PC	2026-10-15T10:03:02.3793925Z ok   https 1 MiB, RSA certificate, same bytes
host	Tests on the PC	2026-10-15T10:03:02.3794282Z ok   https chunked body
host	Tests on the PC	2026-10-15T10:03:02.3794557Z ok   wrong host name: refused
host	Tests on the PC	2026-10-15T10:03:02.3795151Z      (127.0.0.1: certificate not trusted: The certificate Common Name (CN) does not match with the expected CN)
host	Tests on the PC	2026-10-15T10:03:02.3795782Z ok   expired certificate: refused
host	Tests on the PC	2026-10-15T10:03:02.3796246Z      (localhost: certificate not trusted: The certificate validity has expired)
host	Tests on the PC	2026-10-15T10:03:02.3796856Z ok   certificate from another CA: refused, naming the missing root
host	Tests on the PC	2026-10-15T10:03:02.3797478Z      (localhost: certificate not trusted: no root in bm/ca.pem for CN=some other CA)
host	Tests on the PC	2026-10-15T10:03:02.3798126Z ok   chain with a cross-certificate from an unknown CA: stops at the known root
host	Tests on the PC	2026-10-15T10:03:02.3798788Z ok   redirect between https servers
host	Tests on the PC	2026-10-15T10:03:02.3798994Z 
host	Tests on the PC	2026-10-15T10:03:02.3799094Z https: all passed
host	Tests on the PC	2026-10-15T10:03:02.3935107Z cc -O1 -Wall -Wextra -Isrc -Isrc/bm -DHTTP_USER_AGENT='"test"' -o build/host/test_img3d tests/net/test_img3d.c src/net/img3d.c src/net/http.c src/bm/glb.c src/bm/json.c src/bm/jpeg.c src/bm/decimate.c src/bm/png.c -lm
host	Tests on the PC	2026-10-15T10:03:03.3335218Z python3 tests/net/run_img3d_test.py build/host/test_img3d build/img3d
host	Tests on the PC	2026-10-15T10:03:03.3895069Z img3d: 14 checks, 0 failed
host	Tests on the PC	2026-10-15T10:03:03.8902301Z img3d: the request's fields ok
host	Tests on the PC	2026-10-15T10:03:03.9022024Z cc -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/mbedtls/include host	Tests on the PC	2026-10-15T10:03:03.9023159Z 	-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o build/host/test_catalog tests/net/test_catalog.c src/net/catalog.c host	Tests on the PC	2026-10-15T10:03:03.9030935Z 	src/bm/png.c third_party/mbedtls/library/aes.c third_party/mbedtls/library/asn1parse.c third_party/mbedtls/library/asn1write.c third_party/mbedtls/library/base64.c third_party/mbedtls/library/bignum.c third_party/mbedtls/library/bignum_core.c third_party/mbedtls/library/chacha20.c third_party/mbedtls/library/chachapoly.c third_party/mbedtls/library/cipher.c third_party/mbedtls/library/cipher_wrap.c third_party/mbedtls/library/constant_time.c third_party/mbedtls/library/ctr_drbg.c third_party/mbedtls/library/ecdh.c third_party/mbedtls/library/ecdsa.c third_party/mbedtls/library/ecp.c third_party/mbedtls/library/ecp_curves.c third_party/mbedtls/library/entropy.c third_party/mbedtls/library/error.c third_party/mbedtls/library/gcm.c third_party/mbedtls/library/md.c third_party/mbedtls/library/oid.c third_party/mbedtls/library/pem.c third_party/mbedtls/library/pk.c third_party/mbedtls/library/pk_ecc.c third_party/mbedtls/library/pk_wrap.c third_party/mbedtls/library/pkparse.c third_party/mbedtls/library/platform.c third_party/mbedtls/library/platform_util.c third_party/mbedtls/library/poly1305.c third_party/mbedtls/library/rsa.c third_party/mbedtls/library/rsa_alt_helpers.c third_party/mbedtls/library/sha1.c third_party/mbedtls/library/sha256.c third_party/mbedtls/library/sha512.c third_party/mbedtls/library/ssl_ciphersuites.c third_party/mbedtls/library/ssl_client.c third_party/mbedtls/library/ssl_msg.c third_party/mbedtls/library/ssl_tls.c third_party/mbedtls/library/ssl_tls12_client.c third_party/mbedtls/library/x509.c third_party/mbedtls/library/x509_crt.c -lm
host	Tests on the PC	2026-10-15T10:03:08.0045770Z python3 tests/net/run_catalog_test.py build/host/test_catalog
host	Tests on the PC	2026-10-15T10:03:09.5710801Z ok   keys/market-pub.pem: a P-256 public key
host	Tests on the PC	2026-10-15T10:03:09.5711651Z ok   placeholder: no key
host	Tests on the PC	2026-10-15T10:03:09.5712075Z ok   without a key nothing is verified
host	Tests on the PC	2026-10-15T10:03:09.5712434Z ok   test public key read
host	Tests on the PC	2026-10-15T10:03:09.5712741Z ok   signature of the catalog: good
host	Tests on the PC	2026-10-15T10:03:09.5713100Z ok   one bit changed in the catalog: refused
host	Tests on the PC	2026-10-15T10:03:09.5713456Z ok   cut signature: refused
host	Tests on the PC	2026-10-15T10:03:09.5713759Z ok   signature by another key: refused
host	Tests on the PC	2026-10-15T10:03:09.5714477Z ok   placeholder built in, the key from the SD card: good
host	Tests on the PC	2026-10-15T10:03:09.5714948Z ok   another key built in, the key from the SD card: good
host	Tests on the PC	2026-10-15T10:03:09.5715337Z ok   neither key: refused
host	Tests on the PC	2026-10-15T10:03:09.5715607Z ok   catalog read
host	Tests on the PC	2026-10-15T10:03:09.5715847Z ok   serial
host	Tests on the PC	2026-10-15T10:03:09.5716068Z ok   four games
host	Tests on the PC	2026-10-15T10:03:09.5716335Z ok   pocket: a .b16, read as any other file
host	Tests on the PC	2026-10-15T10:03:09.5716841Z ok   snake: title and author from the header, version and license from info.txt
host	Tests on the PC	2026-10-15T10:03:09.5717442Z ok   snake: about in CP437 (UTF-8 accents converted)
host	Tests on the PC	2026-10-15T10:03:09.5717820Z ok   snake: file and cover
host	Tests on the PC	2026-10-15T10:03:09.5718116Z ok   pong: no author, no about
host	Tests on the PC	2026-10-15T10:03:09.5718411Z ok   plain: no cover
host	Tests on the PC	2026-10-15T10:03:09.5718707Z ok   cartridge with one byte changed: refused
host	Tests on the PC	2026-10-15T10:03:09.5719097Z ok   cartridge one byte short: refused
host	Tests on the PC	2026-10-15T10:03:09.5719465Z ok   each file: size and SHA-256 as in the catalog
host	Tests on the PC	2026-10-15T10:03:09.5720011Z ok   a missing cover never matches
host	Tests on the PC	2026-10-15T10:03:09.5720389Z ok   snake: cover.png decodes to the cartridge's cover
host	Tests on the PC	2026-10-15T10:03:09.5720836Z ok   pong-2: cover.png decodes to the cartridge's cover
host	Tests on the PC	2026-10-15T10:03:09.5721620Z ok   a small catalog
host	Tests on the PC	2026-10-15T10:03:09.5721917Z ok   
 line ends and unknown lines
host	Tests on the PC	2026-10-15T10:03:09.5722268Z ok   wrong first line: refused
host	Tests on the PC	2026-10-15T10:03:09.5722563Z ok   no serial: refused
host	Tests on the PC	2026-10-15T10:03:09.5722839Z ok   serial not a number: refused
host	Tests on the PC	2026-10-15T10:03:09.5723107Z ok   no games: an empty market
host	Tests on the PC	2026-10-15T10:03:09.5723361Z ok   a game without its file: refused
host	Tests on the PC	2026-10-15T10:03:09.5723639Z ok   a game without a license: refused
host	Tests on the PC	2026-10-15T10:03:09.5723916Z ok   the same id twice: refused
host	Tests on the PC	2026-10-15T10:03:09.5724209Z ok   the same id once as .bm and once as .b16: accepted
host	Tests on the PC	2026-10-15T10:03:09.5724519Z ok   id with capitals: refused
host	Tests on the PC	2026-10-15T10:03:09.5724750Z ok   id with ..: refused
host	Tests on the PC	2026-10-15T10:03:09.5724973Z ok   path with ..: refused
host	Tests on the PC	2026-10-15T10:03:09.5725214Z ok   absolute path: refused
host	Tests on the PC	2026-10-15T10:03:09.5725461Z ok   another server's address: refused
host	Tests on the PC	2026-10-15T10:03:09.5725740Z ok   a file over GitHub's 100 MiB: refused
host	Tests on the PC	2026-10-15T10:03:09.5726064Z ok   a cartridge of 95 MiB: a .bm has no limit of its own
host	Tests on the PC	2026-10-15T10:03:09.5726379Z ok   short SHA-256: refused
host	Tests on the PC	2026-10-15T10:03:09.5726610Z ok   extra field: refused
host	Tests on the PC	2026-10-15T10:03:09.5726835Z ok   title too long: refused
host	Tests on the PC	2026-10-15T10:03:09.5727081Z ok   control character in a text: refused
host	Tests on the PC	2026-10-15T10:03:09.5727363Z ok   fields before the first game: ignored
host	Tests on the PC	2026-10-15T10:03:09.5727646Z ok   kind app and known app ids set is_app
host	Tests on the PC	2026-10-15T10:03:09.5727912Z ok   invalid kind: refused
host	Tests on the PC	2026-10-15T10:03:09.5728141Z ok   serials compared as numbers
host	Tests on the PC	2026-10-15T10:03:09.5728384Z catalog: all passed
host	Tests on the PC	2026-10-15T10:03:09.5742480Z ok   --check: four good games, a .b16 too
host	Tests on the PC	2026-10-15T10:03:09.5743081Z ok   mkmarket: signed with the key from BM_MARKET_KEY
host	Tests on the PC	2026-10-15T10:03:09.5743663Z ok   mkmarket: no copy of the key left behind
host	Tests on the PC	2026-10-15T10:03:09.5744209Z ok   mkmarket: the files GitHub Pages serves
host	Tests on the PC	2026-10-15T10:03:09.5744750Z ok   index.txt: the .b16's file, its extension kept
host	Tests on the PC	2026-10-15T10:03:09.5745345Z ok   index.html: the games, with the accents of info.txt
host	Tests on the PC	2026-10-15T10:03:09.5745896Z ok   covers compressed ([644, 642] bytes)
host	Tests on the PC	2026-10-15T10:03:09.5746400Z ok   --check: a .bm and a .b16 of one game: both listed
host	Tests on the PC	2026-10-15T10:03:09.5746787Z ok   index.txt: the game twice, one record per kind
host	Tests on the PC	2026-10-15T10:03:09.5747097Z ok   no license: refused
host	Tests on the PC	2026-10-15T10:03:09.5747316Z ok   no version: refused
host	Tests on the PC	2026-10-15T10:03:09.5747529Z ok   no info.txt: refused
host	Tests on the PC	2026-10-15T10:03:09.5747759Z ok   id with capitals: refused
host	Tests on the PC	2026-10-15T10:03:09.5748043Z ok   same title and author as another game: refused
host	Tests on the PC	2026-10-15T10:03:09.5748350Z ok   damaged cartridge: refused
host	Tests on the PC	2026-10-15T10:03:09.5748597Z ok   not a cartridge: refused
host	Tests on the PC	2026-10-15T10:03:09.5748847Z ok   a space in the file name: refused
host	Tests on the PC	2026-10-15T10:03:09.5749339Z ok   a folder without a cartridge: refused
host	Tests on the PC	2026-10-15T10:03:09.5749609Z ok   a .b16 over 8 MiB: refused
host	Tests on the PC	2026-10-15T10:03:09.5749834Z ok   no title: refused
host	Tests on the PC	2026-10-15T10:03:09.5750048Z ok   about too long: refused
host	Tests on the PC	2026-10-15T10:03:09.5750316Z ok   mkmarket: a key that is not the kernel's: stops
host	Tests on the PC	2026-10-15T10:03:09.5750651Z ok   mkmarket: --require-key without a key: stops
host	Tests on the PC	2026-10-15T10:03:09.5750952Z ok   --add: a new game
host	Tests on the PC	2026-10-15T10:03:09.5751348Z ok   --add: the same bytes keep the version
host	Tests on the PC	2026-10-15T10:03:09.5751690Z ok   --add: changed bytes take the new version, the rest stays
host	Tests on the PC	2026-10-15T10:03:09.5752060Z ok   --add --kind app: writes kind: app in info.txt
host	Tests on the PC	2026-10-15T10:03:09.5752383Z ok   --add: the folder passes --check
host	Tests on the PC	2026-10-15T10:03:09.5817621Z cc -O1 -Wall -Wextra -Isrc -Isrc/net -DHTTP_USER_AGENT='"test"' -o build/host/test_github tests/net/test_github.c host	Tests on the PC	2026-10-15T10:03:09.5818557Z 	src/net/github.c src/net/http.c
host	Tests on the PC	2026-10-15T10:03:09.8597543Z cc -O1 -Wall -Wextra -Isrc -o build/host/test_report tests/net/test_report.c src/net/report.c
host	Tests on the PC	2026-10-15T10:03:09.9681779Z cc -O1 -Wall -Wextra -Isrc -o build/host/test_reports tests/kernel/test_reports.c src/kernel/reports.c src/net/report.c host	Tests on the PC	2026-10-15T10:03:09.9682727Z 	src/lib/printf.c
host	Tests on the PC	2026-10-15T10:03:10.2278059Z build/host/test_report
host	Tests on the PC	2026-10-15T10:03:10.2284894Z ok   slug: spaces
host	Tests on the PC	2026-10-15T10:03:10.2285456Z ok   slug: a branch with a slash
host	Tests on the PC	2026-10-15T10:03:10.2286014Z ok   slug: git describe kept
host	Tests on the PC	2026-10-15T10:03:10.2286571Z ok   slug: no slashes, no dashes at the ends
host	Tests on the PC	2026-10-15T10:03:10.2287048Z ok   slug: never empty
host	Tests on the PC	2026-10-15T10:03:10.2287351Z ok   slug: cut to the buffer
host	Tests on the PC	2026-10-15T10:03:10.2287660Z ok   date: 0
host	Tests on the PC	2026-10-15T10:03:10.2287939Z ok   date: 2026-10-04 15:30:12
host	Tests on the PC	2026-10-15T10:03:10.2288248Z ok   date: a leap day
host	Tests on the PC	2026-10-15T10:03:10.2288527Z ok   name with the date
host	Tests on the PC	2026-10-15T10:03:10.2288806Z ok   path
host	Tests on the PC	2026-10-15T10:03:10.2289069Z ok   header written and read back
host	Tests on the PC	2026-10-15T10:03:10.2289415Z ok   name without the date
host	Tests on the PC	2026-10-15T10:03:10.2289766Z ok   no date: said so
host	Tests on the PC	2026-10-15T10:03:10.2290052Z ok   no date: read back empty
host	Tests on the PC	2026-10-15T10:03:10.2290375Z ok   path: the branch's slash
host	Tests on the PC	2026-10-15T10:03:10.2290682Z ok   not a report
host	Tests on the PC	2026-10-15T10:03:10.2290955Z report: all passed
host	Tests on the PC	2026-10-15T10:03:10.2291566Z build/host/test_reports
host	Tests on the PC	2026-10-15T10:03:10.2292304Z reports: 19/19 checks passed
host	Tests on the PC	2026-10-15T10:03:10.2293856Z python3 tests/net/run_github_test.py build/host/test_github
host	Tests on the PC	2026-10-15T10:03:11.0481529Z ok   ids from file names
host	Tests on the PC	2026-10-15T10:03:11.0482246Z ok   owner: the pull request's address
host	Tests on the PC	2026-10-15T10:03:11.0482627Z ok   owner: a branch in the market, the .bm intact
host	Tests on the PC	2026-10-15T10:03:11.0482991Z ok   owner: info.txt
host	Tests on the PC	2026-10-15T10:03:11.0483248Z ok   owner: main untouched
host	Tests on the PC	2026-10-15T10:03:11.0483536Z ok   owner: head, base, title, body
host	Tests on the PC	2026-10-15T10:03:11.0483840Z ok   owner: no fork
host	Tests on the PC	2026-10-15T10:03:11.0484083Z ok   progress lines
host	Tests on the PC	2026-10-15T10:03:11.0484348Z ok   info.txt reads as mkmarket reads it
host	Tests on the PC	2026-10-15T10:03:11.0484717Z ok   ids from file names
host	Tests on the PC	2026-10-15T10:03:11.0484993Z ok   friend: the pull request
host	Tests on the PC	2026-10-15T10:03:11.0485289Z ok   friend: a branch in the fork
host	Tests on the PC	2026-10-15T10:03:11.0485639Z ok   update: the .bm keeps the name already there
host	Tests on the PC	2026-10-15T10:03:11.0486004Z ok   friend: head and title
host	Tests on the PC	2026-10-15T10:03:11.0486339Z ok   friend: waited for the fork, brought it up to date
host	Tests on the PC	2026-10-15T10:03:11.0486723Z ok   friend: the market untouched
host	Tests on the PC	2026-10-15T10:03:11.0487020Z ok   ids from file names
host	Tests on the PC	2026-10-15T10:03:11.0487394Z ok   bad token: the token is not valid (github_token in bm/config.txt)
host	Tests on the PC	2026-10-15T10:03:11.0487825Z ok   ids from file names
host	Tests on the PC	2026-10-15T10:03:11.0488183Z ok   branch taken: the branch: HTTP 422, Reference already exists
host	Tests on the PC	2026-10-15T10:03:11.0488600Z ok   ids from file names
host	Tests on the PC	2026-10-15T10:03:11.0488989Z ok   PUT refused: snake.bm: HTTP 422, Invalid request. content is too big
host	Tests on the PC	2026-10-15T10:03:11.0489829Z ok   report: its address (https://github.com/f-accomando/bm/blob/reports/reports/bm-core/20261004-153012_gpu_pi-zero-w_v1.txt)
host	Tests on the PC	2026-10-15T10:03:11.0490858Z ok   report: the reports branch made, the file in it
host	Tests on the PC	2026-10-15T10:03:11.0491509Z ok   report: main untouched, the branch said
host	Tests on the PC	2026-10-15T10:03:11.0491814Z ok   report: the commit's message
host	Tests on the PC	2026-10-15T10:03:11.0492086Z ok   report: a second one, the branch there
host	Tests on the PC	2026-10-15T10:03:11.0492721Z ok   report: the same name with other bytes refused (20261004-153012_gpu_pi-zero-w_v1.txt: HTTP 422, Invalid request. "sha" wasn't supplied.)
host	Tests on the PC	2026-10-15T10:03:11.0493696Z ok   report: sent again, already there (url https://github.com/f-accomando/bm/blob/reports/reports/bm-core/20261004-153012_gpu_pi-zero-w_v1.txt)
host	Tests on the PC	2026-10-15T10:03:11.0494624Z ok   session report: the first run (https://github.com/f-accomando/bm/blob/reports/reports/bm-core/session_yharnam_pi-zero-w.txt)
host	Tests on the PC	2026-10-15T10:03:11.0495616Z ok   session report: the next run replaces it (https://github.com/f-accomando/bm/blob/reports/reports/bm-core/session_yharnam_pi-zero-w.txt)
host	Tests on the PC	2026-10-15T10:03:11.0496507Z ok   report: bad token (the token is not valid (github_token in bm/config.txt))
host	Tests on the PC	2026-10-15T10:03:11.0496927Z ok   no server: error GET /user: connection refused
host	Tests on the PC	2026-10-15T10:03:11.0497224Z github: all passed
host	Tests on the PC	2026-10-15T10:03:11.0597833Z cc -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -Ithird_party/mbedtls/include host	Tests on the PC	2026-10-15T10:03:11.0599234Z 	-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o build/host/test_lan tests/net/test_lan.c src/net/lan.c src/net/stream.c host	Tests on the PC	2026-10-15T10:03:11.0607552Z 	third_party/mbedtls/library/sha256.c third_party/mbedtls/library/platform_util.c third_party/lwip/src/core/altcp.c third_party/lwip/src/core/altcp_alloc.c third_party/lwip/src/core/altcp_tcp.c third_party/lwip/src/core/def.c third_party/lwip/src/core/dns.c third_party/lwip/src/core/inet_chksum.c third_party/lwip/src/core/init.c third_party/lwip/src/core/ip.c third_party/lwip/src/core/mem.c third_party/lwip/src/core/memp.c third_party/lwip/src/core/netif.c third_party/lwip/src/core/pbuf.c third_party/lwip/src/core/raw.c third_party/lwip/src/core/stats.c third_party/lwip/src/core/sys.c third_party/lwip/src/core/tcp.c third_party/lwip/src/core/tcp_in.c third_party/lwip/src/core/tcp_out.c third_party/lwip/src/core/timeouts.c third_party/lwip/src/core/udp.c third_party/lwip/src/core/ipv4/acd.c third_party/lwip/src/core/ipv4/autoip.c third_party/lwip/src/core/ipv4/dhcp.c third_party/lwip/src/core/ipv4/etharp.c third_party/lwip/src/core/ipv4/icmp.c third_party/lwip/src/core/ipv4/igmp.c third_party/lwip/src/core/ipv4/ip4.c third_party/lwip/src/core/ipv4/ip4_addr.c third_party/lwip/src/core/ipv4/ip4_frag.c third_party/lwip/src/netif/ethernet.c third_party/lwip/src/apps/sntp/sntp.c
host	Tests on the PC	2026-10-15T10:03:13.2159379Z build/host/test_lan
host	Tests on the PC	2026-10-15T10:03:13.2207204Z ok   hello written
host	Tests on the PC	2026-10-15T10:03:13.2207587Z ok   hello read
host	Tests on the PC	2026-10-15T10:03:13.2208028Z ok   other datagrams, empty, control characters, too long: not a console
host	Tests on the PC	2026-10-15T10:03:13.2208470Z ok   hello and server on
host	Tests on the PC	2026-10-15T10:03:13.2208732Z ok   a console heard
host	Tests on the PC	2026-10-15T10:03:13.2209028Z ok   the same console once
host	Tests on the PC	2026-10-15T10:03:13.2209284Z ok   silent for 7 s: gone
host	Tests on the PC	2026-10-15T10:03:13.2209567Z ok   our own hello: ignored
host	Tests on the PC	2026-10-15T10:03:13.2219853Z lan: Living room offers Snake (300000 bytes)
host	Tests on the PC	2026-10-15T10:03:13.2220282Z ok   sent and accepted
host	Tests on the PC	2026-10-15T10:03:13.2220582Z ok   the offer: from, title, author, size, address
host	Tests on the PC	2026-10-15T10:03:13.2220930Z ok   the game received intact
host	Tests on the PC	2026-10-15T10:03:13.2221437Z ok   taken once
host	Tests on the PC	2026-10-15T10:03:13.2221669Z lan: Living room offers Pong (1000 bytes)
host	Tests on the PC	2026-10-15T10:03:13.2221957Z ok   refused by the player
host	Tests on the PC	2026-10-15T10:03:13.2222193Z ok   nothing received
host	Tests on the PC	2026-10-15T10:03:13.2222431Z lan: Living room offers Pong (1000 bytes)
host	Tests on the PC	2026-10-15T10:03:13.2222725Z lan: no answer to Living room, refused
host	Tests on the PC	2026-10-15T10:03:13.2222995Z ok   no answer in 60 s: refused
host	Tests on the PC	2026-10-15T10:03:13.2223244Z lan: raw offers Bad (2000 bytes)
host	Tests on the PC	2026-10-15T10:03:13.2223493Z ok   raw: accepted
host	Tests on the PC	2026-10-15T10:03:13.2223707Z lan: Bad from raw: damaged
host	Tests on the PC	2026-10-15T10:03:13.2224234Z ok   damaged on the way: BD, nothing kept
host	Tests on the PC	2026-10-15T10:03:13.2224534Z lan: raw offers First (2000 bytes)
host	Tests on the PC	2026-10-15T10:03:13.2224786Z ok   an offer waits
host	Tests on the PC	2026-10-15T10:03:13.2224997Z ok   a second console: busy
host	Tests on the PC	2026-10-15T10:03:13.2225231Z ok   the first refused
host	Tests on the PC	2026-10-15T10:03:13.2225441Z ok   too big: BS
host	Tests on the PC	2026-10-15T10:03:13.2225647Z ok   not our protocol: BH
host	Tests on the PC	2026-10-15T10:03:13.2225868Z ok   off
host	Tests on the PC	2026-10-15T10:03:13.2226055Z ok   off: nobody listens
host	Tests on the PC	2026-10-15T10:03:13.2226276Z lan: all passed
host	Tests on the PC	2026-10-15T10:03:13.2226707Z python3 scripts/mkbm.py -o build/keymap-test.bm --lua tests/keymap/cart.lua --title "keymap test"
host	Tests on the PC	2026-10-15T10:03:13.2555519Z build/keymap-test.bm: 3184 bytes (keymap test, 640x360)
host	Tests on the PC	2026-10-15T10:03:13.2605620Z build/host/bmhost-bin build/keymap-test.bm --input tests/keymap/input.txt --seconds 8 2>&1 | tee build/keymap-test.log | grep "^keymap"
host	Tests on the PC	2026-10-15T10:03:13.2694758Z keymap: 18/18 checks passed
host	Tests on the PC	2026-10-15T10:03:13.2697881Z grep -q "^keymap: \([0-9]*\)/ checks passed" build/keymap-test.log
host	Tests on the PC	2026-10-15T10:03:13.2744286Z python3 tests/gameapi/mksheet.py build/gameapi/sheet.png
host	Tests on the PC	2026-10-15T10:03:13.2935142Z python3 scripts/mkbm.py -o build/gameapi-test.bm --lua tests/gameapi/cart.lua --sheet build/gameapi/sheet.png host	Tests on the PC	2026-10-15T10:03:13.2936636Z     --map tests/gameapi/map.csv --map front=tests/gameapi/map_front.csv --flags tests/gameapi/flags.csv host	Tests on the PC	2026-10-15T10:03:13.2937832Z     --sprites tests/gameapi/sprites.txt --title "game api test" --author tests
host	Tests on the PC	2026-10-15T10:03:13.3304939Z build/gameapi-test.bm: 43800 bytes (game api test, 640x360)
host	Tests on the PC	2026-10-15T10:03:13.3368379Z build/host/bmhost-bin build/gameapi-test.bm --input tests/gameapi/input.txt --seconds 8 --shots build/gameapi 2>&1 host	Tests on the PC	2026-10-15T10:03:13.3369608Z     | tee build/gameapi-test.log | grep "^gameapi"
host	Tests on the PC	2026-10-15T10:03:13.3814623Z gameapi: 207/207 checks passed
host	Tests on the PC	2026-10-15T10:03:13.3817644Z grep -q "^gameapi: \([0-9]*\)/ checks passed" build/gameapi-test.log
host	Tests on the PC	2026-10-15T10:03:13.3850334Z rm -rf build/write-sd build/write && mkdir -p build/write-sd build/write
host	Tests on the PC	2026-10-15T10:03:13.3877870Z build/host/bmhost-bin build/carts/write.bm --sd build/write-sd --input tests/write/input.txt --seconds 14 host	Tests on the PC	2026-10-15T10:03:13.3878954Z     --shots build/write > build/write/run1.log 2>&1
host	Tests on the PC	2026-10-15T10:03:13.6703304Z build/host/bmhost-bin build/carts/write.bm --sd build/write-sd --input tests/write/reopen.txt --seconds 2 host	Tests on the PC	2026-10-15T10:03:13.6704401Z     --shots build/write > build/write/run2.log 2>&1
host	Tests on the PC	2026-10-15T10:03:13.7531396Z python3 tests/write/check.py build/write-sd/docs build/write
host	Tests on the PC	2026-10-15T10:03:13.7788251Z write: 43/43 checks passed
host	Tests on the PC	2026-10-15T10:03:13.7834942Z python3 scripts/mkbm.py -o build/shot-test.bm --lua tests/shot/cart.lua --title "shot test" --author tests
host	Tests on the PC	2026-10-15T10:03:13.8183514Z build/shot-test.bm: 836 bytes (shot test, 640x360)
host	Tests on the PC	2026-10-15T10:03:13.8235660Z rm -rf build/shot-sd && mkdir -p build/shot-sd
host	Tests on the PC	2026-10-15T10:03:13.8263877Z build/host/bmhost-bin build/shot-test.bm --sd build/shot-sd --input tests/shot/key.txt --seconds 1 2>&1 | tee build/shot-test.log | grep "shot:"
host	Tests on the PC	2026-10-15T10:03:13.8499863Z shot: bm/shots/SHOT0001.PNG
host	Tests on the PC	2026-10-15T10:03:13.8500498Z shot: bm/shots/SHOT0002.PNG
host	Tests on the PC	2026-10-15T10:03:13.8501353Z shot: bm/shots/ABC0001.PNG
host	Tests on the PC	2026-10-15T10:03:13.8501991Z shot: bm/shots/ABC0001.PNG
host	Tests on the PC	2026-10-15T10:03:13.8502619Z python3 tests/shot/check.py build/shot-sd
host	Tests on the PC	2026-10-15T10:03:13.8712221Z shot: names ['ABC0001.PNG', 'SHOT0001.PNG', 'SHOT0002.PNG', 'SHOT0003.PNG'] ok
host	Tests on the PC	2026-10-15T10:03:13.8712820Z shot: ABC0001.PNG 640x360 ok
host	Tests on the PC	2026-10-15T10:03:13.8713129Z shot: SHOT0001.PNG 640x360 ok
host	Tests on the PC	2026-10-15T10:03:13.8713422Z shot: SHOT0002.PNG 640x360 ok
host	Tests on the PC	2026-10-15T10:03:13.8713705Z shot: SHOT0003.PNG 640x360 ok
host	Tests on the PC	2026-10-15T10:03:13.8713987Z shot: 4/4 checks passed
host	Tests on the PC	2026-10-15T10:03:13.8751787Z python3 scripts/mkbm.py -o build/reserved-test.bm --lua tests/tools/reserved.lua --title "reserved test" --author tests
host	Tests on the PC	2026-10-15T10:03:13.9103553Z build/reserved-test.bm: 764 bytes (reserved test, 640x360)
host	Tests on the PC	2026-10-15T10:03:13.9151544Z rm -rf build/reserved-sd && mkdir -p build/reserved-sd/carts
host	Tests on the PC	2026-10-15T10:03:13.9181611Z build/host/bmhost-bin build/reserved-test.bm --sd build/reserved-sd --seconds 1 2>&1 | tee build/reserved-test.log | grep "^reserved"
host	Tests on the PC	2026-10-15T10:03:13.9265824Z reserved: pixel.bm false /carts/pixel.bm: that name is kept for one of bm's tools
host	Tests on the PC	2026-10-15T10:03:13.9266523Z reserved: SDK.BM false /carts/SDK.BM: that name is kept for one of bm's tools
host	Tests on the PC	2026-10-15T10:03:13.9267191Z reserved: assistan.bm false /carts/assistan.bm: that name is kept for one of bm's tools
host	Tests on the PC	2026-10-15T10:03:13.9267822Z reserved: code.bm false /carts/code.bm: that name is kept for one of bm's tools
host	Tests on the PC	2026-10-15T10:03:13.9268419Z reserved: Studio.bm false /carts/Studio.bm: that name is kept for one of bm's tools
host	Tests on the PC	2026-10-15T10:03:13.9269028Z reserved: sdk_b16.bm false /carts/sdk_b16.bm: that name is kept for one of bm's tools
host	Tests on the PC	2026-10-15T10:03:13.9269508Z reserved: ok.bm true nil
host	Tests on the PC	2026-10-15T10:03:13.9269773Z reserved: pixels.bm true nil
host	Tests on the PC	2026-10-15T10:03:13.9270359Z reserved: end
host	Tests on the PC	2026-10-15T10:03:13.9385075Z reserved: 8/8 checks passed
host	Tests on the PC	2026-10-15T10:03:14.0729592Z spec: 3/3 checks passed
host	Tests on the PC	2026-10-15T10:03:14.0730133Z python3 scripts/consistency.py check
host	Tests on the PC	2026-10-15T10:03:14.1886226Z consistency: 0 drift, 5 accepted, 0 stale
host	Tests on the PC	2026-10-15T10:03:14.1954534Z python3 scripts/mkbm.py -o build/profile-test.bm --lua tests/profile/cart.lua --title "profile test" --author tests
host	Tests on the PC	2026-10-15T10:03:14.2305239Z build/profile-test.bm: 2808 bytes (profile test, 640x360)
host	Tests on the PC	2026-10-15T10:03:14.2352845Z build/host/bmhost-bin build/profile-test.bm --seconds 10 --clock-scale 1 2>&1 | tee build/profile-test.log | grep "^profile"
host	Tests on the PC	2026-10-15T10:03:14.3336086Z profile: heavy        main.lua:11      self 0.557 total 0.557 calls 0
host	Tests on the PC	2026-10-15T10:03:14.3336837Z profile: spr          [C]              self 0.458 total 0.458 calls 400
host	Tests on the PC	2026-10-15T10:03:14.3337436Z profile: draw_lots    main.lua:25      self 0.434 total 0.892 calls 0
host	Tests on the PC	2026-10-15T10:03:14.3338108Z profile: light        main.lua:17      self 0.059 total 0.059 calls 0
host	Tests on the PC	2026-10-15T10:03:14.3338987Z profile: cls          [C]              self 0.014 total 0.014 calls 1
host	Tests on the PC	2026-10-15T10:03:14.3339709Z profile: _draw        main.lua:43      self 0.001 total 0.907 calls 0
host	Tests on the PC	2026-10-15T10:03:14.3340283Z profile: outer        main.lua:23      self 0.000 total 0.616 calls 0
host	Tests on the PC	2026-10-15T10:03:14.3340850Z profile: _update      main.lua:38      self 0.000 total 0.616 calls 0
host	Tests on the PC	2026-10-15T10:03:14.3341943Z profile: 12/12 checks passed
host	Tests on the PC	2026-10-15T10:03:14.3342585Z grep -q "^profile: \([0-9]*\)/ checks passed" build/profile-test.log
host	Tests on the PC	2026-10-15T10:03:14.3365445Z python3 scripts/mkbm.py -o build/online-test.bm --lua tests/online/cart.lua --title "online test"
host	Tests on the PC	2026-10-15T10:03:14.3705691Z build/online-test.bm: 2272 bytes (online test, 640x360)
host	Tests on the PC	2026-10-15T10:03:14.3769939Z build/host/bmhost-bin build/online-test.bm --input tests/online/input.txt --shots build/online host	Tests on the PC	2026-10-15T10:03:14.3770954Z     --seconds 8 2>&1 | tee build/online-test.log | grep "^online"
host	Tests on the PC	2026-10-15T10:03:14.3937763Z online: 9/9 checks passed
host	Tests on the PC	2026-10-15T10:03:14.3940341Z grep -q "^online: \([0-9]*\)/ checks passed" build/online-test.log
host	Tests on the PC	2026-10-15T10:03:14.3962120Z grep -q "left the online game" build/online-test.log
host	Tests on the PC	2026-10-15T10:03:14.3985872Z python3 tests/bmnet/run.py build
host	Tests on the PC	2026-10-15T10:03:24.3136642Z ok   lan host: bmnet: 9/9 checks passed
host	Tests on the PC	2026-10-15T10:03:24.3137096Z ok   lan guest: bmnet: 8/8 checks passed
host	Tests on the PC	2026-10-15T10:03:24.3137599Z ok   lan: the same match on both consoles [[('300', '4e16b823')], [('300', '4e16b823')]]
host	Tests on the PC	2026-10-15T10:03:24.3138120Z ok   relay host: bmnet: 9/9 checks passed
host	Tests on the PC	2026-10-15T10:03:24.3138477Z ok   relay guest: bmnet: 8/8 checks passed
host	Tests on the PC	2026-10-15T10:03:24.3138986Z ok   relay: the same match on both consoles [[('300', 'daf8664f')], [('300', 'daf8664f')]]
host	Tests on the PC	2026-10-15T10:03:24.3139482Z bmnet: all passed
host	Tests on the PC	2026-10-15T10:03:24.3201741Z cc -O2 -Wall -Wextra -Isrc -Isrc/bm -o build/host/test_loading tests/bm/test_loading.c src/bm/loading.c host	Tests on the PC	2026-10-15T10:03:24.3203079Z     src/bm/loading_logo.c src/bm/gfx16.c src/gfx/font6x12.c src/lib/printf.c src/audio/presets.c src/audio/synth.c -lm
host	Tests on the PC	2026-10-15T10:03:26.2743775Z build/host/test_loading build/loading
host	Tests on the PC	2026-10-15T10:03:26.3259241Z loading: 20/20 checks passed
host	Tests on the PC	2026-10-15T10:03:26.3277813Z cc -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/mbedtls/include host	Tests on the PC	2026-10-15T10:03:26.3284604Z 	-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o build/host/test_release tests/net/test_release.c src/net/release.c third_party/mbedtls/library/aes.c third_party/mbedtls/library/asn1parse.c third_party/mbedtls/library/asn1write.c third_party/mbedtls/library/base64.c third_party/mbedtls/library/bignum.c third_party/mbedtls/library/bignum_core.c third_party/mbedtls/library/chacha20.c third_party/mbedtls/library/chachapoly.c third_party/mbedtls/library/cipher.c third_party/mbedtls/library/cipher_wrap.c third_party/mbedtls/library/constant_time.c third_party/mbedtls/library/ctr_drbg.c third_party/mbedtls/library/ecdh.c third_party/mbedtls/library/ecdsa.c third_party/mbedtls/library/ecp.c third_party/mbedtls/library/ecp_curves.c third_party/mbedtls/library/entropy.c third_party/mbedtls/library/error.c third_party/mbedtls/library/gcm.c third_party/mbedtls/library/md.c third_party/mbedtls/library/oid.c third_party/mbedtls/library/pem.c third_party/mbedtls/library/pk.c third_party/mbedtls/library/pk_ecc.c third_party/mbedtls/library/pk_wrap.c third_party/mbedtls/library/pkparse.c third_party/mbedtls/library/platform.c third_party/mbedtls/library/platform_util.c third_party/mbedtls/library/poly1305.c third_party/mbedtls/library/rsa.c third_party/mbedtls/library/rsa_alt_helpers.c third_party/mbedtls/library/sha1.c third_party/mbedtls/library/sha256.c third_party/mbedtls/library/sha512.c third_party/mbedtls/library/ssl_ciphersuites.c third_party/mbedtls/library/ssl_client.c third_party/mbedtls/library/ssl_msg.c third_party/mbedtls/library/ssl_tls.c third_party/mbedtls/library/ssl_tls12_client.c third_party/mbedtls/library/x509.c third_party/mbedtls/library/x509_crt.c
host	Tests on the PC	2026-10-15T10:03:30.2658745Z python3 tests/net/run_release_test.py build/host/test_release
host	Tests on the PC	2026-10-15T10:03:30.4897369Z ok   keys/release-pub.pem: a P-256 public key
host	Tests on the PC	2026-10-15T10:03:30.4897995Z ok   placeholder: no key
host	Tests on the PC	2026-10-15T10:03:30.4898431Z ok   without a key nothing is verified
host	Tests on the PC	2026-10-15T10:03:30.4898911Z ok   test public key read
host	Tests on the PC	2026-10-15T10:03:30.4899319Z ok   signature of the manifest: good
host	Tests on the PC	2026-10-15T10:03:30.4899829Z ok   one bit changed in the manifest: refused
host	Tests on the PC	2026-10-15T10:03:30.4900371Z ok   cut signature: refused
host	Tests on the PC	2026-10-15T10:03:30.4900823Z ok   signature by another key: refused
host	Tests on the PC	2026-10-15T10:03:30.4901631Z ok   manifest read
host	Tests on the PC	2026-10-15T10:03:30.4902025Z ok   version and commit
host	Tests on the PC	2026-10-15T10:03:30.4902461Z ok   files: names and paths on the SD card
host	Tests on the PC	2026-10-15T10:03:30.4903024Z ok   kernel with one byte changed: refused
host	Tests on the PC	2026-10-15T10:03:30.4903627Z ok   kernel one byte short: refused
host	Tests on the PC	2026-10-15T10:03:30.4904171Z ok   each file: size and SHA-256 as in the manifest
host	Tests on the PC	2026-10-15T10:03:30.4904809Z ok   a small manifest
host	Tests on the PC	2026-10-15T10:03:30.4905243Z ok   
 line ends and unknown lines
host	Tests on the PC	2026-10-15T10:03:30.4905761Z ok   wrong first line: refused
host	Tests on the PC	2026-10-15T10:03:30.4906221Z ok   no version: refused
host	Tests on the PC	2026-10-15T10:03:30.4906645Z ok   no files: refused
host	Tests on the PC	2026-10-15T10:03:30.4907059Z ok   path with ..: refused
host	Tests on the PC	2026-10-15T10:03:30.4907504Z ok   asset name with /: refused
host	Tests on the PC	2026-10-15T10:03:30.4907969Z ok   relative path: refused
host	Tests on the PC	2026-10-15T10:03:30.4908414Z ok   short SHA-256: refused
host	Tests on the PC	2026-10-15T10:03:30.4908858Z ok   extra field: refused
host	Tests on the PC	2026-10-15T10:03:30.4909402Z ok   placeholder built in, the key from the SD card: good
host	Tests on the PC	2026-10-15T10:03:30.4910118Z ok   another key built in, the key from the SD card: good
host	Tests on the PC	2026-10-15T10:03:30.4910717Z ok   neither key: refused
host	Tests on the PC	2026-10-15T10:03:30.4911559Z ok   versions: newer, same, older, development builds, bad tags
host	Tests on the PC	2026-10-15T10:03:30.4912200Z release: all passed
host	Tests on the PC	2026-10-15T10:03:30.4912653Z ok   mkrelease: signed with the key from BM_RELEASE_KEY
host	Tests on the PC	2026-10-15T10:03:30.4913845Z ok   mkrelease: the files under their release names
host	Tests on the PC	2026-10-15T10:03:30.4914635Z ok   mkrelease: no copy of the key left behind
host	Tests on the PC	2026-10-15T10:03:30.4915239Z ok   mkrelease: a key that is not the kernel's: stops
host	Tests on the PC	2026-10-15T10:03:30.4915825Z ok   mkrelease: --require-key without a key: stops
host	Tests on the PC	2026-10-15T10:03:30.4916184Z ok   mkrelease: two files with one name: stops
host	Tests on the PC	2026-10-15T10:03:30.4989842Z cc -O1 -w -Isrc -Isrc/net -Ithird_party/mbedtls/include host	Tests on the PC	2026-10-15T10:03:30.5000574Z 	-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o build/host/test_smp tests/bt/smp_test.c src/bt/smp_crypto.c third_party/mbedtls/library/aes.c third_party/mbedtls/library/asn1parse.c third_party/mbedtls/library/asn1write.c third_party/mbedtls/library/base64.c third_party/mbedtls/library/bignum.c third_party/mbedtls/library/bignum_core.c third_party/mbedtls/library/chacha20.c third_party/mbedtls/library/chachapoly.c third_party/mbedtls/library/cipher.c third_party/mbedtls/library/cipher_wrap.c third_party/mbedtls/library/constant_time.c third_party/mbedtls/library/ctr_drbg.c third_party/mbedtls/library/ecdh.c third_party/mbedtls/library/ecdsa.c third_party/mbedtls/library/ecp.c third_party/mbedtls/library/ecp_curves.c third_party/mbedtls/library/entropy.c third_party/mbedtls/library/error.c third_party/mbedtls/library/gcm.c third_party/mbedtls/library/md.c third_party/mbedtls/library/oid.c third_party/mbedtls/library/pem.c third_party/mbedtls/library/pk.c third_party/mbedtls/library/pk_ecc.c third_party/mbedtls/library/pk_wrap.c third_party/mbedtls/library/pkparse.c third_party/mbedtls/library/platform.c third_party/mbedtls/library/platform_util.c third_party/mbedtls/library/poly1305.c third_party/mbedtls/library/rsa.c third_party/mbedtls/library/rsa_alt_helpers.c third_party/mbedtls/library/sha1.c third_party/mbedtls/library/sha256.c third_party/mbedtls/library/sha512.c third_party/mbedtls/library/ssl_ciphersuites.c third_party/mbedtls/library/ssl_client.c third_party/mbedtls/library/ssl_msg.c third_party/mbedtls/library/ssl_tls.c third_party/mbedtls/library/ssl_tls12_client.c third_party/mbedtls/library/x509.c third_party/mbedtls/library/x509_crt.c
host	Tests on the PC	2026-10-15T10:03:34.4072164Z build/host/test_smp
host	Tests on the PC	2026-10-15T10:03:34.4100726Z ok   c1
host	Tests on the PC	2026-10-15T10:03:34.4101250Z ok   s1
host	Tests on the PC	2026-10-15T10:03:34.4101577Z ok   ah
host	Tests on the PC	2026-10-15T10:03:34.4101898Z ok   resolves
host	Tests on the PC	2026-10-15T10:03:34.4102262Z ok   cmac empty
host	Tests on the PC	2026-10-15T10:03:34.4102621Z ok   cmac 16
host	Tests on the PC	2026-10-15T10:03:34.4102951Z ok   f4
host	Tests on the PC	2026-10-15T10:03:34.4103273Z ok   f5 mackey
host	Tests on the PC	2026-10-15T10:03:34.4103612Z ok   f5 ltk
host	Tests on the PC	2026-10-15T10:03:34.4103937Z ok   f6
host	Tests on the PC	2026-10-15T10:03:34.4104280Z ok   p256
host	Tests on the PC	2026-10-15T10:03:34.4104655Z ok   bad key refused
host	Tests on the PC	2026-10-15T10:03:34.4105033Z smp: ok
host	Tests on the PC	2026-10-15T10:03:34.4105420Z python3 tools/qpuasm.py -o build/shaders.h
host	Tests on the PC	2026-10-15T10:03:35.7357998Z qpuasm: regfile A unpack ok
host	Tests on the PC	2026-10-15T10:03:35.7358838Z qpuasm: two-thread shaders write as their originals (1800 pixels)
host	Tests on the PC	2026-10-15T10:03:35.7359584Z qpuasm: ok
host	Tests on the PC	2026-10-15T10:03:35.7415660Z cmp build/shaders.h src/gpu/shaders.h
host	Tests on the PC	2026-10-15T10:03:35.7445636Z cc -O2 -Wall -Wextra -Isrc -o build/host/test_etc1 tests/gpu/test_etc1.c src/gpu/etc1.c -lm
host	Tests on the PC	2026-10-15T10:03:35.9784018Z build/host/test_etc1
host	Tests on the PC	2026-10-15T10:03:36.0231949Z   flat blocks: PSNR 43.3 dB, worst channel 2
host	Tests on the PC	2026-10-15T10:03:36.0232557Z   gradients: PSNR 42.1 dB, worst channel 7
host	Tests on the PC	2026-10-15T10:03:36.0233153Z   checkers: PSNR 43.6 dB (red and blue swapped 43.6), worst channel 2
host	Tests on the PC	2026-10-15T10:03:36.0233744Z   noise: PSNR 12.8 dB, worst channel 229
host	Tests on the PC	2026-10-15T10:03:36.0234305Z   1024x1024 noise: 20.0 ms on this PC (dbd0)
host	Tests on the PC	2026-10-15T10:03:36.0234885Z   1024x1024 checkers: 9.2 ms on this PC (0)
host	Tests on the PC	2026-10-15T10:03:36.0235496Z etc1: 11/11 checks passed
host	Tests on the PC	2026-10-15T10:03:36.0253415Z cc -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free host	Tests on the PC	2026-10-15T10:03:36.0254322Z     -c src/gpu/gpu3d.c -o build/host/test_gpu3d-gpu3d.o
host	Tests on the PC	2026-10-15T10:03:38.2553207Z cc -O2 -Wall -Wextra -Isrc -Itests/gpu -o build/host/test_gpu3d tests/gpu/test_gpu3d.c tests/gpu/v3d_emu.c host	Tests on the PC	2026-10-15T10:03:38.2554804Z     src/gpu/v3d_cl.c src/gpu/etc1.c src/bm/r3d.c src/bm/gfx16.c build/host/test_gpu3d-gpu3d.o -lm
host	Tests on the PC	2026-10-15T10:03:42.2399783Z build/host/test_gpu3d 1 0 0 0 0 0
host	Tests on the PC	2026-10-15T10:03:43.7355291Z gpu3d on the emulator: byte a = red, texels in place, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:03:43.7356711Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:03:43.7357889Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:03:43.7358601Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7359327Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7360085Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:03:43.7360711Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7361675Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7362683Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:03:43.7363099Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:43.7363565Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7364064Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:43.7364471Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7364918Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7365388Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:03:43.7365786Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7366229Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7366704Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:43.7367097Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:43.7367518Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:03:43.7368010Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:43.7368485Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:03:43.7368865Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:43.7369248Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7369694Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7370171Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:03:43.7370559Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7371002Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7371649Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:43.7372041Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7372490Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7372984Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:03:43.7373354Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7373724Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7374112Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:43.7374440Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7374809Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7375209Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:03:43.7375560Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7375943Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7376366Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:03:43.7376715Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:43.7377322Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7377752Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:43.7378163Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:43.7378591Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7379050Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:03:43.7379420Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:43.7379828Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:43.7380244Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:03:43.7380579Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:43.7380915Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:03:43.7381409Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7381929Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:43.7382277Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7382611Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:03:43.7382956Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7383291Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:03:43.7383623Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:43.7383937Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:43.7384334Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:43.7384813Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:43.7385291Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:43.7385768Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:45.1798123Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:45.1799170Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:03:45.1800325Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:03:45.1802208Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:03:45.1803420Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:45.1804154Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:45.1804760Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:45.1805391Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:03:45.1806081Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:03:45.1806637Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:45.1807269Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:03:45.1807990Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:45.1808561Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:45.1809543Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:03:45.1810484Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:03:45.1810869Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:03:45.1811450Z build/host/test_gpu3d 0 0 1 1 1 1
host	Tests on the PC	2026-10-15T10:03:46.1470127Z gpu3d on the emulator: byte a = blue, texels in place, T-format 1, MSAA load one sample
host	Tests on the PC	2026-10-15T10:03:46.1471708Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:03:46.1473325Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 0)
host	Tests on the PC	2026-10-15T10:03:46.1474037Z gpu3d: clipping probe (Z planes off): pixels 11ef wrong
host	Tests on the PC	2026-10-15T10:03:46.1474655Z gpu3d: clipping probe (Z planes on): pixels 11ef wrong
host	Tests on the PC	2026-10-15T10:03:46.1475223Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1475881Z   spheres      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1476591Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:03:46.1477160Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1477807Z   textures     MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1478514Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:03:46.1479092Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:46.1479756Z   noz_zclear   MSAA off: 0.00% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1480669Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:46.1481430Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1482075Z   many         MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1482748Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:03:46.1483299Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1483932Z   3 sheets     MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1484607Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:46.1485166Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:46.1485740Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:03:46.1486430Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:46.1487088Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:03:46.1487627Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:46.1488154Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1488786Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1489460Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:03:46.1490012Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1490650Z   overbit      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1491511Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:46.1492091Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1492739Z   effects      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1493457Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:03:46.1494002Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1494622Z   vshader      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1495321Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:46.1495885Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1496529Z   vshader lit  MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1497234Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:03:46.1497803Z   vshader clip 12 triangles in 2 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1498453Z   vshader clip MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1499165Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:03:46.1499751Z   vshader heroes 654 triangles in 20 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:46.1500439Z   vshader heroes MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1501343Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:46.1501966Z   vshader textured 654 triangles in 45 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:46.1502840Z   vshader textured MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1503597Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:03:46.1504184Z   vshader skin 584 triangles in 43 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:46.1504838Z   vshader skin MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:46.1505533Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:03:46.1506088Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:46.1506619Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:03:46.1507163Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1507729Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:46.1508304Z   vshader groups 4 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1508871Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:03:46.1509602Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1510154Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:03:46.1510702Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:46.1511391Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:46.1512060Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:46.1512851Z   vertex shader against r3d's corners (clipped): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5671276Z   vertex shader against r3d's corners (heroes): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5672599Z   vertex shader against r3d's corners (textured): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5673401Z   vertex shader against r3d's corners (skin): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5674058Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:03:47.5674962Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:03:47.5676266Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:03:47.5677366Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5678329Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5679095Z   two-thread shaders: 40 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5679849Z   compact attributes: 4782 words of corners read instead of 8022, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:03:47.5680685Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:03:47.5681560Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:47.5682468Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:03:47.5683491Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:47.5684311Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:47.5685187Z   queue: 26 jobs started and waited for later, 7 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:03:47.5686008Z   vertex shader: 9054 corners shaded for 25752 indexed (M39)
host	Tests on the PC	2026-10-15T10:03:47.5686587Z gpu3d: 56 jobs, 299598 triangles; 367/367 checks passed
host	Tests on the PC	2026-10-15T10:03:47.5687279Z build/host/test_gpu3d 1 1 1 0 1 2
host	Tests on the PC	2026-10-15T10:03:49.0275389Z gpu3d on the emulator: byte a = red, texels swapped, T-format 1, MSAA load all samples
host	Tests on the PC	2026-10-15T10:03:49.0276431Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:03:49.0277256Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 0)
host	Tests on the PC	2026-10-15T10:03:49.0277777Z gpu3d: clipping probe (Z planes off): pixels 1000 wrong
host	Tests on the PC	2026-10-15T10:03:49.0278229Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0279246Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0279766Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:03:49.0280191Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0280659Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0281543Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:03:49.0282075Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:49.0282572Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0283069Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:49.0283467Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0283916Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0284394Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:03:49.0284969Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0285412Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0285891Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:49.0286295Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:49.0286697Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:03:49.0287184Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:49.0287655Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:03:49.0288041Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:49.0288431Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0288878Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0289356Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:03:49.0289746Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0290204Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0290682Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:49.0291214Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0291662Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0292150Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:03:49.0292544Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0292936Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0293338Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:49.0293675Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0294063Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0294467Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:03:49.0294811Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0295190Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0295592Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:03:49.0295934Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:49.0296337Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0296768Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:49.0297127Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:49.0297541Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0297982Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:03:49.0298336Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:49.0298717Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:49.0299259Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:03:49.0299592Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:49.0299910Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:03:49.0300232Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0300568Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:49.0300911Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0301362Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:03:49.0301695Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0302018Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:03:49.0302341Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:49.0302654Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:49.0303047Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:49.0303655Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:49.0304130Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:50.4984387Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:50.4985399Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:50.4986168Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:03:50.4987150Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:03:50.4988367Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:03:50.4989178Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:50.4989886Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:50.4990513Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:50.4991750Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:03:50.4992715Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:03:50.4993227Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:50.4993842Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:03:50.4994535Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:50.4995091Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:50.4995703Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:03:50.4996291Z   vertex shader: 67179 corners shaded for 118560 indexed (M39)
host	Tests on the PC	2026-10-15T10:03:50.4996751Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:03:50.4997383Z build/host/test_gpu3d 0 1 0 1 0 0
host	Tests on the PC	2026-10-15T10:03:51.5744143Z gpu3d on the emulator: byte a = blue, texels swapped, T-format 0, MSAA load one sample
host	Tests on the PC	2026-10-15T10:03:51.5745162Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:03:51.5745972Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:03:51.5746455Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5746956Z   spheres      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5747484Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:03:51.5747913Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5748391Z   textures     MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5748910Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:03:51.5749853Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:51.5750312Z   noz_zclear   MSAA off: 0.00% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5750783Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:51.5751381Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5751807Z   many         MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5752255Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:03:51.5752628Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5753052Z   3 sheets     MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5753531Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:51.5753914Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:51.5754295Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:03:51.5754758Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:51.5755364Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:03:51.5755728Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:51.5756093Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5756534Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5756986Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:03:51.5757363Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5757784Z   overbit      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5758238Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:51.5758606Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5759026Z   effects      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5759480Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:03:51.5759862Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5760288Z   vshader      MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5760706Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:51.5761039Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5761537Z   vshader lit  MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5761951Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:03:51.5762284Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5762666Z   vshader clip MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5763078Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:03:51.5763434Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:51.5763838Z   vshader heroes MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5764280Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:51.5764646Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:51.5765070Z   vshader textured MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5765517Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:03:51.5765877Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:51.5766270Z   vshader skin MSAA off: 0.00% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:51.5766679Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:03:51.5767017Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:51.5767335Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:03:51.5767658Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5767997Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:51.5768465Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5768806Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:03:51.5769146Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5769472Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:03:51.5769800Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:51.5770121Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:51.5770538Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:51.5771020Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:51.5771704Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:51.5772183Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:53.0029542Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:53.0030728Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:03:53.0031835Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:03:53.0033021Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:03:53.0034115Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:53.0035069Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:53.0035882Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:53.0036759Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:03:53.0037716Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:03:53.0038422Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:53.0039291Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:03:53.0040309Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:53.0041271Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:53.0042139Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:03:53.0042894Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:03:53.0043306Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:03:53.0043859Z build/host/test_gpu3d 1 0 2 0 0 0
host	Tests on the PC	2026-10-15T10:03:54.4783125Z gpu3d on the emulator: byte a = red, texels in place, T-format 2, MSAA load all samples
host	Tests on the PC	2026-10-15T10:03:54.4784407Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:03:54.4785466Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:03:54.4786131Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4786759Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4787429Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:03:54.4787977Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4788591Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4789268Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:03:54.4789822Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:54.4790452Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4791405Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:54.4791975Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4792584Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4793678Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:03:54.4794220Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4794832Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4795495Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:54.4796031Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:54.4796576Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:03:54.4797243Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:54.4797890Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:03:54.4798412Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:54.4798940Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4799549Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4800194Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:03:54.4800934Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4801724Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4802388Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:54.4802924Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4803523Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4804172Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:03:54.4804705Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4805312Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4805970Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:54.4806536Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4807153Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4807838Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:03:54.4808387Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4809002Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4809680Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:03:54.4810253Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:54.4810915Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4811796Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:54.4812402Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:54.4813081Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4813815Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:03:54.4814410Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:54.4815055Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:54.4815733Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:03:54.4816280Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:54.4816812Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:03:54.4817351Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4817913Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:54.4818483Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4819046Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:03:54.4819601Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4820137Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:03:54.4820684Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:54.4821365Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:54.4822275Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:54.4823052Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:54.4823845Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:54.4824633Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:55.8603025Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:55.8603812Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:03:55.8604631Z   RGBA5551 cut-outs: 0 sheets, 63716 pixels drawn, 0 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:03:55.8605736Z   ETC1 sheets: 0 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 99.0 dB against RGB565, worst channel 0
host	Tests on the PC	2026-10-15T10:03:55.8606780Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:55.8608097Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:55.8608910Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:55.8609786Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:03:55.8610733Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:03:55.8611727Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:55.8612604Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:03:55.8613597Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:55.8614393Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:55.8615259Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:03:55.8616092Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:03:55.8616725Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:03:55.8617575Z EMU_HANG_ZCLEAR=1 build/host/test_gpu3d 1 0 0 0 0 0
host	Tests on the PC	2026-10-15T10:03:57.2019724Z gpu3d on the emulator: byte a = red, texels in place, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:03:57.2021006Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:03:57.2022303Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:03:57.2022888Z gpu3d: the V3D did not finish:
host	Tests on the PC	2026-10-15T10:03:57.2023385Z emulator: fs_zclear: the job does not end (emu_hang_zclear)
host	Tests on the PC	2026-10-15T10:03:57.2024188Z gpu3d: the GPU did not finish a frame (registers in the log); the 3D is drawn by the ARM again
host	Tests on the PC	2026-10-15T10:03:57.2025069Z gpu3d: zclear() inside a job did not work: a zclear() ends the job as before
host	Tests on the PC	2026-10-15T10:03:57.2025973Z gpu3d: the probe of zclear() in a job: the GPU did not end its job; that stays off, the rest goes on
host	Tests on the PC	2026-10-15T10:03:57.2026821Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2027456Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2028133Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:03:57.2028694Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2029319Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2029990Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:03:57.2030544Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:57.2037813Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2038622Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:57.2039203Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2039825Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2040877Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:03:57.2041654Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2042279Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2042952Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:57.2043505Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:57.2044056Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:03:57.2044736Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:57.2045395Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:03:57.2045938Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:57.2046476Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2047095Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2047979Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:03:57.2048534Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2049166Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2049842Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:57.2050391Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2051012Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2051840Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:03:57.2052398Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2053031Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2053711Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:57.2054286Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2054922Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2055651Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:03:57.2056225Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2056874Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2057573Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:03:57.2058158Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:57.2058832Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2059560Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:03:57.2060188Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:57.2060902Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2061839Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:03:57.2062455Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:57.2063131Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:03:57.2063830Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:03:57.2064406Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:03:57.2064956Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:03:57.2065525Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2066117Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:03:57.2066696Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2067261Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:03:57.2067827Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:57.2068379Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:03:57.2068934Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:03:58.7576483Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:03:58.7577261Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7578049Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7578831Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7579655Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7580482Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7581440Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:03:58.7582420Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:03:58.7583673Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:03:58.7584775Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7586182Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7586993Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7587857Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:03:58.7588797Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:03:58.7589392Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:03:58.7589944Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:03:58.7590536Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:58.7590995Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:03:58.7592059Z   queue: 26 jobs started and waited for later, 0 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:03:58.7592898Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:03:58.7593297Z gpu3d: 62 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:03:58.7593645Z EMU_VPM_WORDS=1 build/host/test_gpu3d 0 1 0 0 1 0
host	Tests on the PC	2026-10-15T10:04:00.1526598Z gpu3d on the emulator: byte a = blue, texels swapped, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:00.1527867Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:00.1528813Z gpu3d: the V3D did not finish:
host	Tests on the PC	2026-10-15T10:04:00.1529354Z emulator: GL: the vertex shader read 9 VPM rows, the record loads 36
host	Tests on the PC	2026-10-15T10:04:00.1530218Z gpu3d: the GPU did not finish a frame (registers in the log); the 3D is drawn by the ARM again
host	Tests on the PC	2026-10-15T10:04:00.1531456Z gpu3d: vertex shader probe (VPM in bytes): the job did not end
host	Tests on the PC	2026-10-15T10:04:00.1532182Z gpu3d: vertex shader probe: drawn (VPM in words, clockwise bit 0)
host	Tests on the PC	2026-10-15T10:04:00.1532805Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1533495Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1534194Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:00.1534784Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1535414Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1536097Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:00.1536657Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:00.1537285Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1537960Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:00.1538508Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1539121Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1539789Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:00.1540798Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1541613Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1542264Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:00.1542807Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:00.1543348Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:00.1544012Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:00.1544658Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:00.1545176Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:00.1545701Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1546315Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1546974Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:00.1547706Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1548312Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1548969Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:00.1549509Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1550111Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1550760Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:00.1551474Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1552088Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1552744Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:00.1553299Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1553920Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1554603Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:00.1555169Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1555788Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1556469Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:00.1557047Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:00.1557722Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1558428Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:00.1559021Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:00.1559708Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1560441Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:00.1561030Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:00.1561829Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:00.1562512Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:00.1563060Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:00.1563593Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:00.1564130Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1564690Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:00.1565259Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1565823Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:00.1566386Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1566925Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:00.1567467Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:00.1567984Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:00.1568808Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6707473Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6708339Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6709129Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6709811Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6710342Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:01.6711025Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:01.6712540Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:01.6713385Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6714117Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6715169Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6715862Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:01.6716927Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:01.6717764Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:01.6718697Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:01.6719506Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:01.6720113Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:01.6720650Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:01.6721468Z   vertex shader: 67170 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:01.6722139Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:04:01.6722672Z EMU_NO_THREADS=1 build/host/test_gpu3d 1 0 0 0 0 0
host	Tests on the PC	2026-10-15T10:04:03.0257236Z gpu3d on the emulator: byte a = red, texels in place, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:03.0258643Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:03.0259836Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:03.0260509Z gpu3d: the V3D did not finish:
host	Tests on the PC	2026-10-15T10:04:03.0261071Z emulator: two-thread fragment shader: the job does not end (emu_no_threads)
host	Tests on the PC	2026-10-15T10:04:03.0262000Z gpu3d: the GPU did not finish a frame (registers in the log); the 3D is drawn by the ARM again
host	Tests on the PC	2026-10-15T10:04:03.0262807Z gpu3d: the two-thread shaders' probe: the GPU did not end its job; that stays off, the rest goes on
host	Tests on the PC	2026-10-15T10:04:03.0263433Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0263948Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0264465Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:03.0264933Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0265403Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0265919Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:03.0266338Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:03.0266809Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0267314Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:03.0267723Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0268179Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0268658Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:03.0269941Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0270418Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0270923Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:03.0271707Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:03.0272133Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:03.0272650Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:03.0273138Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:03.0273532Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:03.0273932Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0274435Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0275114Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:03.0275718Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0276488Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0276933Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:03.0277278Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0277653Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0278053Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:03.0278378Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0278746Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0279145Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:03.0279486Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0279860Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0280267Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:03.0280617Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0280986Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0281540Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:03.0281883Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:03.0282285Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0282718Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:03.0283076Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:03.0283485Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0283922Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:03.0284274Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:03.0284663Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:03.0285076Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:03.0285406Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:03.0285725Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:03.0286054Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0286393Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:03.0286733Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0287062Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:03.0287393Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0287716Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:03.0288042Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:03.0288353Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:04.5747106Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5748598Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5749539Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5750465Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5751657Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5752188Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:04.5752845Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:04.5753745Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:04.5754527Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5755212Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5755980Z   two-thread shaders: 0 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5756593Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:04.5757265Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:04.5757945Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:04.5758927Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:04.5760020Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:04.5760784Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:04.5761947Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:04.5762789Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:04.5763322Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:04:04.5763897Z EMU_T16_ALPHA=1 build/host/test_gpu3d 0 1 1 0 0 0
host	Tests on the PC	2026-10-15T10:04:06.0760063Z gpu3d on the emulator: byte a = blue, texels swapped, T-format 1, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:06.0761898Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:06.0763428Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:06.0764267Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0764921Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0765542Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:06.0766041Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0766594Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0767187Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:06.0767731Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:06.0768288Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0768882Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:06.0769359Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0769893Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0770463Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:06.0770931Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0771770Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0772316Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:06.0772760Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:06.0773207Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:06.0774122Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:06.0774650Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:06.0775071Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:06.0775498Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0776004Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0776540Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:06.0776979Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0777501Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0778045Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:06.0778481Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0778978Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0779507Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:06.0780095Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0780590Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0781330Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:06.0781812Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0782328Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0782792Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:06.0783137Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0783531Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0783956Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:06.0784314Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:06.0784722Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0785164Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:06.0785536Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:06.0785947Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0786385Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:06.0786745Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:06.0787132Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:06.0787538Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:06.0787876Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:06.0788235Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:06.0788565Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0788913Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:06.0789263Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0789598Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:06.0789932Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0790257Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:06.0790582Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:06.0790898Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:06.0791424Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:06.0791900Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:06.0792384Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:06.0792862Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:07.5049105Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:07.5050221Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:07.5050954Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:07.5052632Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:07.5053517Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:07.5054290Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:07.5054925Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:07.5055607Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:07.5056357Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:07.5056913Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:07.5057867Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:07.5058717Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:07.5059293Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:07.5059912Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:07.5060552Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:07.5061029Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:04:07.5061843Z EMU_T16_ALPHA=2 build/host/test_gpu3d 1 0 0 0 0 0
host	Tests on the PC	2026-10-15T10:04:08.7197269Z gpu3d on the emulator: byte a = red, texels in place, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:08.7198489Z gpu3d: RGBA5551 textures: no order of the fields drew the texels (the type stays unused; params0 412d4030)
host	Tests on the PC	2026-10-15T10:04:08.7199644Z gpu3d: 5551 mode 1: 410/2048 right, want 0000 0901 1142 1983 got 0000 0000 0000 0000; 1st (1,0) 0901/0000
host	Tests on the PC	2026-10-15T10:04:08.7200758Z gpu3d: 5551 mode 0: 410/2048 right, want 0000 0901 1142 1983 got 0000 0000 0000 0000; 1st (1,0) 0901/0000
host	Tests on the PC	2026-10-15T10:04:08.7202302Z gpu3d: 5551 mode 3: 410/2048 right, want 0000 0901 1142 1983 got 0000 0000 0000 0000; 1st (1,0) 0901/0000
host	Tests on the PC	2026-10-15T10:04:08.7203478Z gpu3d: 5551 mode 2: 410/2048 right, want 0000 0901 1142 1983 got 0000 0000 0000 0000; 1st (1,0) 0901/0000
host	Tests on the PC	2026-10-15T10:04:08.7204511Z gpu3d: 5551 type 3, opaque shader: 0/1638 right, 0/2048 not 0, got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:08.7205623Z gpu3d: RGBA4444 textures: no order of the fields drew the texels (the type stays unused; params0 412d4020)
host	Tests on the PC	2026-10-15T10:04:08.7206805Z gpu3d: 4444 mode 1: 417/2048 right, want 0000 0100 1102 1182 got 0000 0000 0000 0000; 1st (1,0) 0100/0000
host	Tests on the PC	2026-10-15T10:04:08.7207903Z gpu3d: 4444 mode 0: 417/2048 right, want 0000 0100 1102 1182 got 0000 0000 0000 0000; 1st (1,0) 0100/0000
host	Tests on the PC	2026-10-15T10:04:08.7208993Z gpu3d: 4444 mode 3: 417/2048 right, want 0000 0100 1102 1182 got 0000 0000 0000 0000; 1st (1,0) 0100/0000
host	Tests on the PC	2026-10-15T10:04:08.7210103Z gpu3d: 4444 mode 2: 417/2048 right, want 0000 0100 1102 1182 got 0000 0000 0000 0000; 1st (1,0) 0100/0000
host	Tests on the PC	2026-10-15T10:04:08.7211296Z gpu3d: 4444 type 2, opaque shader: 7/1638 right, 0/2048 not 0, got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:08.7212553Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:08.7213725Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:08.7214436Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7215176Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7215944Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:08.7216582Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7217328Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7218567Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:08.7219218Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:08.7219938Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7220701Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:08.7221716Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7222430Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7223140Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:08.7223703Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7224345Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7225029Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:08.7225596Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:08.7226392Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:08.7227095Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:08.7227767Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:08.7228320Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:08.7228875Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7229518Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7230202Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:08.7230763Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7231617Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7232309Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:08.7232872Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7233505Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7234219Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:08.7234786Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7235420Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7236104Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:08.7236675Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7237322Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7238026Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:08.7238603Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:08.7239257Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7239983Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:08.7240586Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:08.7241454Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:08.7242204Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:10.4496445Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:10.4497265Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:10.4498005Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:10.4498584Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:10.4499257Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:10.4499975Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:10.4500564Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:10.4501694Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:10.4502323Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:10.4503395Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:10.4504007Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:10.4504609Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:10.4505205Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:10.4505787Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:10.4506368Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:10.4506925Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:10.4507628Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4508451Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4509105Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4509630Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4510295Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4510691Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:10.4511617Z   RGBA5551 cut-outs: 0 sheets, 63716 pixels drawn, 0 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:10.4512820Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:10.4513927Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4514850Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4515559Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4516055Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:10.4516603Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:10.4517021Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:10.4517516Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:10.4518079Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:10.4518529Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:10.4519020Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:10.4519494Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:10.4519861Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:04:10.4520199Z EMU_T16_LOSSY=1 build/host/test_gpu3d 1 0 0 0 0 0
host	Tests on the PC	2026-10-15T10:04:11.9482971Z gpu3d on the emulator: byte a = red, texels in place, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:11.9484421Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:11.9485683Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:11.9486391Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9486940Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9487457Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:11.9487881Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9488351Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9488917Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:11.9489362Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:11.9489856Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9490378Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:11.9490787Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9491899Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9492405Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:11.9492772Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9493181Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9493614Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:11.9493963Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:11.9494309Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:11.9494742Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:11.9495149Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:11.9495478Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:11.9495805Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9496367Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9496778Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:11.9497111Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9497509Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9497934Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:11.9498264Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9498631Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9499022Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:11.9499342Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9499705Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9500093Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:11.9500424Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9500805Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9501339Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:11.9501676Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9502046Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9502458Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:11.9502803Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:11.9503201Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9503625Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:11.9503989Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:11.9504400Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9504847Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:11.9505209Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:11.9505590Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:11.9505996Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:11.9506334Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:11.9506672Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:11.9506991Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9507328Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:11.9507670Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9508002Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:11.9508338Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9508665Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:11.9509142Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:11.9509455Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:11.9509850Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:11.9510315Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:11.9510789Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:11.9511389Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:13.3752250Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:13.3753357Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:13.3754593Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 40906 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:13.3756297Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:13.3758321Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:13.3759582Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:13.3760321Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:13.3761349Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:13.3762334Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:13.3762981Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:13.3763776Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:13.3764999Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:13.3765903Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:13.3766541Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:13.3767159Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:13.3767654Z gpu3d: 58 jobs, 305774 triangles; 373/373 checks passed
host	Tests on the PC	2026-10-15T10:04:13.3768145Z EMU_T16_LOSSY=1 EMU_T16_ALPHA=1 build/host/test_gpu3d 0 1 1 0 0 0
host	Tests on the PC	2026-10-15T10:04:14.8714485Z gpu3d on the emulator: byte a = blue, texels swapped, T-format 1, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:14.8715717Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:14.8716790Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:14.8717415Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8718051Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8718733Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:14.8719333Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8719952Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8720629Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:14.8721445Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:14.8722087Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8722776Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:14.8723334Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8723950Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8724609Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:14.8725139Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8725745Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8726404Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:14.8727379Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:14.8727939Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:14.8728620Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:14.8729258Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:14.8729778Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:14.8730301Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8730923Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8731761Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:14.8732305Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8732916Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8733576Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:14.8734312Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8734926Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8735586Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:14.8736150Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8736762Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8737415Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:14.8737968Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8738586Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8739265Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:14.8739828Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8740457Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8741313Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:14.8741897Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:14.8742559Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8743311Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:14.8743914Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:14.8744606Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8745341Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:14.8745944Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:14.8746579Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:14.8747259Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:14.8747821Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:14.8748357Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:14.8748912Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8749494Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:14.8750075Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8750654Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:14.8751377Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8751918Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:14.8752465Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:14.8752996Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:14.8753654Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:14.8754437Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:14.8755235Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:14.8756225Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:16.2893676Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:16.2894422Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:16.2895294Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 40906 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:16.2896453Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:16.2897540Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:16.2898487Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:16.2899297Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:16.2900157Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:16.2901945Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:16.2902651Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:16.2903518Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:16.2904504Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:16.2905300Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:16.2906138Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:16.2906696Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:16.2907094Z gpu3d: 58 jobs, 305774 triangles; 373/373 checks passed
host	Tests on the PC	2026-10-15T10:04:16.2908858Z EMU_ETC1=1 build/host/test_gpu3d 0 1 1 0 0 0
host	Tests on the PC	2026-10-15T10:04:17.7833654Z gpu3d on the emulator: byte a = blue, texels swapped, T-format 1, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:17.7834996Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:17.7836089Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:17.7836737Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7837373Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7838065Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:17.7838629Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7839258Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7839940Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:17.7840506Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:17.7841464Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7842185Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:17.7842760Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7843387Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7844044Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:17.7844586Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7845210Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7845888Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:17.7846445Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:17.7847009Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:17.7847713Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:17.7848367Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:17.7848895Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:17.7849908Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7850547Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7851417Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:17.7851974Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7852593Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7853268Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:17.7853818Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7854445Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7855114Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:17.7855663Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7856275Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7857144Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:17.7857704Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7858343Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7859036Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:17.7859606Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7860241Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7860933Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:17.7861695Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:17.7862399Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7869553Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:17.7870202Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:17.7870959Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7871961Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:17.7872574Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:17.7873234Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:17.7873946Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:17.7874520Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:17.7875062Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:17.7875613Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7876191Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:17.7876775Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7877357Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:17.7877944Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7878512Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:17.7879072Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:17.7879620Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:17.7880300Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:17.7881290Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:17.7882285Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:17.7882950Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:19.2062102Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:19.2062908Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:19.2063706Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:19.2065187Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:19.2066127Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:19.2066923Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:19.2067599Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:19.2068374Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:19.2069109Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:19.2069659Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:19.2070347Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:19.2071628Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:19.2072887Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:19.2073817Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:19.2074818Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:19.2075473Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:04:19.2075922Z EMU_ETC1=2 build/host/test_gpu3d 1 0 0 0 0 0
host	Tests on the PC	2026-10-15T10:04:20.3834152Z gpu3d on the emulator: byte a = red, texels in place, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:20.3835820Z gpu3d: ETC1 textures: no layout of the blocks drew the texels (ETC1 stays unused; params0 412d4080)
host	Tests on the PC	2026-10-15T10:04:20.3837275Z gpu3d: ETC1 2x4 BE: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3838598Z gpu3d: ETC1 2x4 words LE: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3839629Z gpu3d: ETC1 2x4 LE: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3840495Z gpu3d: ETC1 2x4 halves: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3841559Z gpu3d: ETC1 4x2 BE: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3842284Z gpu3d: ETC1 4x2 words LE: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3842970Z gpu3d: ETC1 4x2 LE: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3843722Z gpu3d: ETC1 4x2 halves: 88|88/4096 right, want 1d01 d48d 01e7 dfff got 0000 0000 0000 0000
host	Tests on the PC	2026-10-15T10:04:20.3844664Z gpu3d: ETC1 flat f800: BE 0000 4096, words LE 0000 4096, LE 0000 4096, halves 0000 4096,
host	Tests on the PC	2026-10-15T10:04:20.3845329Z gpu3d: ETC1 flat 07e0: BE 0000 4096, words LE 0000 4096, LE 0000 4096, halves 0000 4096,
host	Tests on the PC	2026-10-15T10:04:20.3845991Z gpu3d: ETC1 2x4 BE, block read along y=0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
host	Tests on the PC	2026-10-15T10:04:20.3846645Z gpu3d: ETC1 2x4 BE, block read along x=0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
host	Tests on the PC	2026-10-15T10:04:20.3847340Z gpu3d: ETC1 one block p&3: got 0000 0000 0000 0000 want 77bb ffdd 1199 0088
host	Tests on the PC	2026-10-15T10:04:20.3847955Z gpu3d: ETC1 one block p>>2: got 0000 0000 0000 0000 want 7f98 7f98 7f98 7f98
host	Tests on the PC	2026-10-15T10:04:20.3848566Z gpu3d: ETC1 one block p&3 flip: got 0000 0000 0000 0000 want 7777 ffff 9999 8888
host	Tests on the PC	2026-10-15T10:04:20.3849421Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:20.3850251Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:20.3850762Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3851366Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3851786Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:20.3852133Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3852932Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3853342Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:20.3853681Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:20.3854123Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3854539Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:20.3854899Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3855288Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3855707Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:20.3856042Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3856417Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3856814Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:20.3857145Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:20.3857652Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:20.3858057Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:20.3858494Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:20.3858824Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:20.3859147Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3859524Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3859925Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:20.3860251Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3860619Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3861014Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:20.3861591Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3861984Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3862378Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:20.3862708Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3863078Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3863474Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:20.3863806Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3864173Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3864577Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:20.3864916Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:20.3865298Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:20.3865716Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:20.3866085Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:22.1126396Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:22.1127486Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:22.1128084Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:22.1128673Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:22.1129310Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:22.1129795Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:22.1130317Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:22.1130870Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:22.1131760Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:22.1132200Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:22.1133124Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:22.1133590Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:22.1134050Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:22.1134503Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:22.1134948Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:22.1135383Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:22.1135821Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:22.1136248Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:22.1136771Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1137397Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1138365Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1139646Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1140647Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1141673Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:22.1142692Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:22.1143692Z   ETC1 sheets: 0 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 99.0 dB against RGB565, worst channel 0
host	Tests on the PC	2026-10-15T10:04:22.1144591Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1145302Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1145784Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1146298Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:22.1146874Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:22.1147299Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:22.1147801Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:22.1148384Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:22.1148830Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:22.1149327Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:22.1149806Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:22.1150182Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:04:22.1150531Z EMU_ETC1=3 build/host/test_gpu3d 1 0 0 0 0 0
host	Tests on the PC	2026-10-15T10:04:23.6013199Z gpu3d on the emulator: byte a = red, texels in place, T-format 0, MSAA load all samples
host	Tests on the PC	2026-10-15T10:04:23.6014521Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:23.6015536Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:23.6016107Z   spheres      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6016690Z   spheres      MSAA on: 2.12% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6017313Z   spheres      640x360: 113072 pixels covered, 0.04% differ
host	Tests on the PC	2026-10-15T10:04:23.6017827Z   textures     12 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6018401Z   textures     MSAA on: 0.29% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6019021Z   textures     640x360: 154670 pixels covered, 0.01% differ
host	Tests on the PC	2026-10-15T10:04:23.6019527Z   noz_zclear   134 triangles in 3 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:23.6020105Z   noz_zclear   MSAA on: 1.10% of the pixels smoothed, 0 of 1980 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6020704Z   noz_zclear   480x270: 94641 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:23.6022184Z   many         46631 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6022679Z   many         MSAA on: 29.76% of the pixels smoothed, 10 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6023179Z   many         640x360: 143951 pixels covered, 0.65% differ
host	Tests on the PC	2026-10-15T10:04:23.6023592Z   3 sheets     8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6024055Z   3 sheets     MSAA on: 0.38% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6024559Z   3 sheets     640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:23.6024969Z   full job     98856 triangles in 5 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:23.6025386Z   full job     640x360: 181220 pixels covered, 0.76% differ
host	Tests on the PC	2026-10-15T10:04:23.6025894Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:23.6026382Z   3D 2D 3D     208 triangles in 6 batches, 4 jobs
host	Tests on the PC	2026-10-15T10:04:23.6026774Z   3D 2D 3D     640x360: 94686 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:23.6027183Z   cleared      235 triangles in 1 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6027847Z   cleared      MSAA on: 4.11% of the pixels smoothed, 0 of 880 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6028356Z   cleared      320x180: 57600 pixels covered, 0.08% differ
host	Tests on the PC	2026-10-15T10:04:23.6028762Z   overbit      298 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6029222Z   overbit      MSAA on: 1.21% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6029719Z   overbit      640x360: 84757 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:23.6030129Z   effects      48 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6030603Z   effects      MSAA on: 0.90% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6031220Z   effects      640x360: 67611 pixels covered, 0.43% differ
host	Tests on the PC	2026-10-15T10:04:23.6031561Z   vshader      789 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6031940Z   vshader      MSAA on: 1.73% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6032352Z   vshader      640x360: 116368 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:23.6032722Z   vshader lit  22 triangles in 7 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6033117Z   vshader lit  MSAA on: 0.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6033538Z   vshader lit  640x360: 103703 pixels covered, 1.04% differ
host	Tests on the PC	2026-10-15T10:04:23.6033874Z   vshader clip 6 triangles in 3 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6034251Z   vshader clip MSAA on: 0.19% of the pixels smoothed, 1 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6034659Z   vshader clip 640x360: 185250 pixels covered, 0.16% differ
host	Tests on the PC	2026-10-15T10:04:23.6035006Z   vshader heroes 1662 triangles in 24 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:23.6035403Z   vshader heroes MSAA on: 2.78% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6035838Z   vshader heroes 640x360: 96488 pixels covered, 0.03% differ
host	Tests on the PC	2026-10-15T10:04:23.6036197Z   vshader textured 1662 triangles in 32 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:23.6036606Z   vshader textured MSAA on: 1.84% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6037055Z   vshader textured 640x360: 93427 pixels covered, 0.10% differ
host	Tests on the PC	2026-10-15T10:04:23.6037406Z   vshader skin 1662 triangles in 64 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:23.6037791Z   vshader skin MSAA on: 2.07% of the pixels smoothed, 0 of 3600 blocks moved
host	Tests on the PC	2026-10-15T10:04:23.6038194Z   vshader skin 640x360: 96351 pixels covered, 0.72% differ
host	Tests on the PC	2026-10-15T10:04:23.6038523Z   10 sheets    20 triangles in 10 batches, 2 jobs
host	Tests on the PC	2026-10-15T10:04:23.6038847Z   10 sheets    640x360: 68786 pixels covered, 0.09% differ
host	Tests on the PC	2026-10-15T10:04:23.6039174Z   vshader sheets 8 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6039510Z   vshader sheets 640x360: 71395 pixels covered, 0.06% differ
host	Tests on the PC	2026-10-15T10:04:23.6039847Z   vshader groups 4 triangles in 4 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6040182Z   vshader groups 640x360: 47196 pixels covered, 0.00% differ
host	Tests on the PC	2026-10-15T10:04:23.6040523Z   tex screen   106 triangles in 6 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6040847Z   tex screen   640x360: 119176 pixels covered, 0.41% differ
host	Tests on the PC	2026-10-15T10:04:23.6041533Z   2D on GPU    281 triangles in 5 batches, 1 jobs
host	Tests on the PC	2026-10-15T10:04:23.6041856Z   2D on GPU    640x360: 119379 pixels covered, 0.02% differ
host	Tests on the PC	2026-10-15T10:04:23.6042258Z   vertex shader against r3d's corners (lit): 0.000% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:23.6042739Z   vertex shader against r3d's corners (clipped): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:23.6043216Z   vertex shader against r3d's corners (heroes): 0.008% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:23.6043695Z   vertex shader against r3d's corners (textured): 0.002% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:25.0117565Z   vertex shader against r3d's corners (skin): 0.023% of the pixels differ
host	Tests on the PC	2026-10-15T10:04:25.0118321Z   RGB565 textures: 0 pixels differ from 32-bit ones
host	Tests on the PC	2026-10-15T10:04:25.0119154Z   RGBA5551 cut-outs: 3 sheets, 63716 pixels drawn, 22810 with a step of green, 0 differ in transparency, 0 more
host	Tests on the PC	2026-10-15T10:04:25.0120295Z   ETC1 sheets: 1 made, 179783 pixels drawn, 0 differ in silhouette, PSNR 37.3 dB against RGB565, worst channel 9
host	Tests on the PC	2026-10-15T10:04:25.0122074Z   nearest first: 99960 pixels shaded instead of 416546, 24 draws moved, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:25.0123022Z   vertex cache order: 0.69 corners shaded a triangle instead of 2.96, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:25.0123824Z   two-thread shaders: 21 batches with them, 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:25.0124683Z   compact attributes: 31170 words of corners read instead of 43482, 0 pixels a step off, 0 more
host	Tests on the PC	2026-10-15T10:04:25.0125618Z   bilinear textures: 21.24% of the pixels differ from the nearest texel
host	Tests on the PC	2026-10-15T10:04:25.0126319Z   enlarged 160x90 3 times by the GPU: 0 pixels differ
host	Tests on the PC	2026-10-15T10:04:25.0127191Z   two jobs in flight: 2 jobs filled while the one before was drawn, 0 pixels differ from one job at a time
host	Tests on the PC	2026-10-15T10:04:25.0128183Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:25.0128985Z gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame
host	Tests on the PC	2026-10-15T10:04:25.0129841Z   queue: 26 jobs started and waited for later, 4 zclear() inside a job, the same pixels
host	Tests on the PC	2026-10-15T10:04:25.0130485Z   vertex shader: 67176 corners shaded for 118548 indexed (M39)
host	Tests on the PC	2026-10-15T10:04:25.0130879Z gpu3d: 58 jobs, 305774 triangles; 372/372 checks passed
host	Tests on the PC	2026-10-15T10:04:25.0148211Z cc -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free 	    -c src/gpu/gpu3d.c -o build/host/test_impostor-gpu3d.o
host	Tests on the PC	2026-10-15T10:04:27.1769902Z cc -O2 -Wall -Wextra -Isrc -Itests/gpu -o build/host/test_impostor tests/bm/test_impostor.c tests/gpu/v3d_emu.c 	    src/gpu/v3d_cl.c src/gpu/etc1.c src/bm/r3d.c src/bm/gfx16.c build/host/test_impostor-gpu3d.o -lm
host	Tests on the PC	2026-10-15T10:04:30.6205716Z build/host/test_impostor
host	Tests on the PC	2026-10-15T10:04:31.3944057Z gpu3d: before the vertex shader probe: ERRSTAT 00001000 DBGE 00000000 FDBGO 00000000 FDBGB 00000000 FDBGR 00000000 FDBGS 00000000
host	Tests on the PC	2026-10-15T10:04:31.3945589Z gpu3d: vertex shader probe: drawn (VPM in bytes, clockwise bit 1)
host	Tests on the PC	2026-10-15T10:04:31.3946815Z field ARM              off 9ac5c23b  6003 tri | on 195bbf64  4277 tri, 52 impostors |    70 px differ, mean 0.015
host	Tests on the PC	2026-10-15T10:04:31.3948008Z field GPU              off 459ca0fe  6001 tri | on 38ca736b  4277 tri, 52 impostors |    74 px differ, mean 0.016
host	Tests on the PC	2026-10-15T10:04:31.3949042Z field GPU              GPU against ARM: off 638 px, mean 0.452; on 638 px, mean 0.452
host	Tests on the PC	2026-10-15T10:04:31.3950095Z field GPU+VS           off 459ca0fe 14508 tri | on 38ca736b 10208 tri, 52 impostors |    74 px differ, mean 0.016
host	Tests on the PC	2026-10-15T10:04:31.3950819Z field GPU+VS           GPU against ARM: off 638 px, mean 0.452; on 638 px, mean 0.452
host	Tests on the PC	2026-10-15T10:04:31.3951701Z field ARM, 6 px: 23 impostors, 5137 tri
host	Tests on the PC	2026-10-15T10:04:31.3952214Z far hero at rest (2x5 px): without bones 1 impostor, 2 tri; with bones 1 impostor, 2 tri
host	Tests on the PC	2026-10-15T10:04:31.3952945Z far hero, an arm thrown away (11x6 px): 0 impostor, 144 tri
host	Tests on the PC	2026-10-15T10:04:31.3953533Z posed heroes alone: 15 impostors of 64 (14 at most 6x6 px)
host	Tests on the PC	2026-10-15T10:04:31.3954667Z out of view, an arm in it: off drawn, on drawn, 0 impostors, 138 tri
host	Tests on the PC	2026-10-15T10:04:31.3955287Z out of view: off 144 tri, on 0 tri
host	Tests on the PC	2026-10-15T10:04:31.3955862Z FAIL: crowd ARM, off: 9926f79d, not the picture of before (12a000fd)
host	Tests on the PC	2026-10-15T10:04:31.3956847Z crowd ARM              off 9926f79d 20614 tri | on 75c193d0 14929 tri, 37 impostors |    22 px differ, mean 0.006
host	Tests on the PC	2026-10-15T10:04:31.3957898Z FAIL: crowd GPU, off: f32adb17, not the picture of before (d0191095)
host	Tests on the PC	2026-10-15T10:04:31.3958805Z crowd GPU              off f32adb17 20615 tri | on 50ee88be 14930 tri, 37 impostors |    22 px differ, mean 0.007
host	Tests on the PC	2026-10-15T10:04:31.3959760Z crowd GPU              GPU against ARM: off 409 px, mean 0.480; on 406 px, mean 0.479
host	Tests on the PC	2026-10-15T10:04:31.3960519Z FAIL: crowd GPU+VS, off: f32adb17, not the picture of before (d0191095)
host	Tests on the PC	2026-10-15T10:04:31.3961570Z crowd GPU+VS           off f32adb17 20615 tri | on 50ee88be 14930 tri, 37 impostors |    22 px differ, mean 0.007
host	Tests on the PC	2026-10-15T10:04:31.3962725Z crowd GPU+VS           GPU against ARM: off 409 px, mean 0.480; on 406 px, mean 0.479
host	Tests on the PC	2026-10-15T10:04:31.3963395Z crowd ARM, 16 px (1080p):  16 impostors, 17934 tri
host	Tests on the PC	2026-10-15T10:04:31.3963828Z crowd ARM, 32 px (1080p):  50 impostors, 13079 tri
host	Tests on the PC	2026-10-15T10:04:31.3964149Z crowd ARM, 64 px (1080p):  76 impostors,  9372 tri
host	Tests on the PC	2026-10-15T10:04:31.3964527Z field ARM, 16 px (1080p):  23 impostors at 360 lines,  23 at 720,  23 at 1080
host	Tests on the PC	2026-10-15T10:04:31.3964972Z field ARM, 24 px (1080p):  52 impostors at 360 lines,  52 at 720,  52 at 1080
host	Tests on the PC	2026-10-15T10:04:31.3965402Z field ARM, 48 px (1080p): 108 impostors at 360 lines, 108 at 720, 108 at 1080
host	Tests on the PC	2026-10-15T10:04:31.3965832Z crowd ARM, 16 px (1080p):  16 impostors at 360 lines,  16 at 720,  16 at 1080
host	Tests on the PC	2026-10-15T10:04:31.3966511Z crowd ARM, 24 px (1080p):  37 impostors at 360 lines,  37 at 720,  37 at 1080
host	Tests on the PC	2026-10-15T10:04:31.3967232Z crowd ARM, 48 px (1080p):  66 impostors at 360 lines,  66 at 720,  66 at 1080
host	Tests on the PC	2026-10-15T10:04:31.3967667Z heroes alone at 1080 lines, 8 px: 13 impostors of 48
host	Tests on the PC	2026-10-15T10:04:31.3968076Z field ARM at 360 lines: 8 px (1080p) 0 impostors, 24 px (1080p, 8 here) 52
host	Tests on the PC	2026-10-15T10:04:31.3968470Z make: *** [Makefile:1339: test-impostor] Error 1
host	Tests on the PC	2026-10-15T10:04:31.3981396Z ##[error]Process completed with exit code 2.
qemu (3/4)	UNKNOWN STEP	﻿2026-10-15T10:00:41.0805408Z Current runner version: '2.337.0'
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0830862Z ##[group]Runner Image Provisioner
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0831968Z Hosted Compute Agent
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0832680Z Version: 20261012.601
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0833368Z Commit: f3dc54e1fb89da6852db922499a0233137369e1d
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0834205Z Build Date: 2026-10-12T17:48:21Z
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0834912Z Worker ID: {7f8acb3e-3907-400e-a292-532bdf8987bd}
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0835638Z Region: westus
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0836234Z Cloud: Azure
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0836812Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0838421Z ##[group]Operating System
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0839180Z Ubuntu
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0839762Z 24.04.5
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0840330Z LTS
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0840929Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0841493Z ##[group]Runner Image
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0842821Z Image: ubuntu-24.04
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0843422Z Version: 20261012.334.1
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0844784Z Included Software: https://github.com/actions/runner-images/blob/ubuntu24/20261012.334/images/ubuntu/Ubuntu2404-Readme.md
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0846355Z Image Release: https://github.com/actions/runner-images/releases/tag/ubuntu24%2F20261012.334
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0847416Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0848591Z ##[group]GITHUB_TOKEN Permissions
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0850835Z Contents: read
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0851528Z Metadata: read
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0852685Z Packages: read
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0853287Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0855385Z Secret source: Actions
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0856479Z Cache mode: write
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.0857186Z Prepare workflow directory
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.1263593Z Prepare all required actions
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.1314351Z Getting action download info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.5056602Z Download action repository 'actions/checkout@v5' (SHA:fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.6040010Z Download action repository 'actions/upload-artifact@v6' (SHA:b7c566a772e6b6bfb58ed0dc250532a479d7789f)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.8598089Z Complete job name: qemu (3/4)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9376538Z ##[group]Run actions/checkout@v5
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9377418Z with:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9377883Z   fetch-depth: 0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9378368Z   repository: f-accomando/bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9382386Z   token: ***
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9382854Z   ssh-strict: true
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9383319Z   ssh-user: git
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9383786Z   persist-credentials: true
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9384290Z   clean: true
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9384764Z   sparse-checkout-cone-mode: true
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9385308Z   fetch-tags: false
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9385769Z   show-progress: true
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9386249Z   lfs: false
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9386693Z   submodules: false
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9387171Z   set-safe-directory: true
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9387704Z   allow-unsafe-pr-checkout: false
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9388504Z env:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9388930Z   ZERO2: 0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:41.9389358Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0406617Z Syncing repository: f-accomando/bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0409139Z ##[group]Getting Git version info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0410182Z Working directory is '/home/runner/work/bm/bm'
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0411959Z [command]/usr/bin/git version
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0480858Z git version 2.55.0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0513099Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0541333Z Temporarily overriding HOME='/home/runner/work/_temp/ae6e8d6b-eca0-400a-ae41-486fb084b3bb' before making global git config changes
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0544645Z Adding repository directory to the temporary git global config as a safe directory
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0554692Z [command]/usr/bin/git config --global --add safe.directory /home/runner/work/bm/bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0615520Z Deleting the contents of '/home/runner/work/bm/bm'
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0620946Z ##[group]Initializing the repository
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0626258Z [command]/usr/bin/git init /home/runner/work/bm/bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0765331Z hint: Using 'master' as the name for the initial branch. This default branch name
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0767249Z hint: will change to "main" in Git 3.0. To configure the initial branch name
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0769557Z hint: to use in all of your new repositories, which will suppress this warning,
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0770965Z hint: call:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0771992Z hint:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0772949Z hint: 	git config --global init.defaultBranch <name>
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0774054Z hint:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0775204Z hint: Names commonly chosen instead of 'master' are 'main', 'trunk' and
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0776971Z hint: 'development'. The just-created branch can be renamed via this command:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0778395Z hint:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0779121Z hint: 	git branch -m <name>
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0780026Z hint:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0781064Z hint: Disable this message with "git config set advice.defaultBranchName false"
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0788533Z Initialized empty Git repository in /home/runner/work/bm/bm/.git/
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0798636Z [command]/usr/bin/git remote add origin https://github.com/f-accomando/bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0851883Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0853304Z ##[group]Disabling automatic garbage collection
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0856213Z [command]/usr/bin/git config --local gc.auto 0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0890005Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0891132Z ##[group]Setting up auth
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0897619Z [command]/usr/bin/git config --local --name-only --get-regexp core\.sshCommand
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.0944493Z [command]/usr/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'core\.sshCommand' && git config --local --unset-all 'core.sshCommand' || :"
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1324523Z [command]/usr/bin/git config --local --name-only --get-regexp http\.https\:\/\/github\.com\/\.extraheader
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1365434Z [command]/usr/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'http\.https\:\/\/github\.com\/\.extraheader' && git config --local --unset-all 'http.https://github.com/.extraheader' || :"
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1598217Z [command]/usr/bin/git config --local --name-only --get-regexp ^includeIf\.gitdir:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1636643Z [command]/usr/bin/git submodule foreach --recursive git config --local --show-origin --name-only --get-regexp remote.origin.url
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1886642Z [command]/usr/bin/git config --local http.https://github.com/.extraheader AUTHORIZATION: basic ***
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1930257Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1932102Z ##[group]Fetching the repository
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:42.1942156Z [command]/usr/bin/git -c protocol.version=2 fetch --prune --no-recurse-submodules origin +refs/heads/*:refs/remotes/origin/* +refs/tags/*:refs/tags/*
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7437084Z From https://github.com/f-accomando/bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7438980Z  * [new branch]        agy/sdlog               -> origin/agy/sdlog
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7440711Z  * [new branch]        bm-core                 -> origin/bm-core
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7442782Z  * [new branch]        claude/b16-profile      -> origin/claude/b16-profile
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7449773Z  * [new branch]        claude/ci-repair        -> origin/claude/ci-repair
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7452171Z  * [new branch]        claude/forge-organic-meshes -> origin/claude/forge-organic-meshes
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7454370Z  * [new branch]        claude/net-test-err     -> origin/claude/net-test-err
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7456433Z  * [new branch]        claude/organic-recipes  -> origin/claude/organic-recipes
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7458122Z  * [new branch]        meshy-out               -> origin/meshy-out
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7459719Z  * [new branch]        reports                 -> origin/reports
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7461383Z  * [new branch]        sync/protocollo-ottobre -> origin/sync/protocollo-ottobre
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7464628Z  * [new branch]        v3d-async-proposal      -> origin/v3d-async-proposal
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7466385Z  * [new tag]           v0.1.0                  -> v0.1.0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7468101Z  * [new tag]           v0.2.1                  -> v0.2.1
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7469672Z  * [new tag]           v0.2.2                  -> v0.2.2
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7471616Z  * [new tag]           v0.2.3                  -> v0.2.3
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7473339Z  * [new tag]           v0.2.4                  -> v0.2.4
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7474831Z  * [new tag]           v0.2.5                  -> v0.2.5
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7477924Z [command]/usr/bin/git branch --list --remote origin/claude/ci-repair
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7492219Z   origin/claude/ci-repair
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7533454Z [command]/usr/bin/git rev-parse refs/remotes/origin/claude/ci-repair
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7563554Z 792554f575be03cd9d90c7fb08f4325a3adcbf12
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7572275Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7574504Z ##[group]Determining the checkout info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7578950Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:51.7583258Z [command]/usr/bin/git sparse-checkout disable
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.0887054Z [command]/usr/bin/git config --local --unset-all extensions.worktreeConfig
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.1381317Z ##[group]Checking out the ref
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.1398710Z [command]/usr/bin/git checkout --progress --force -B claude/ci-repair refs/remotes/origin/claude/ci-repair
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.7908748Z Switched to a new branch 'claude/ci-repair'
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.7913539Z branch 'claude/ci-repair' set up to track 'origin/claude/ci-repair'.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.7968130Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8026355Z [command]/usr/bin/git log -1 --format=%H
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8062788Z 792554f575be03cd9d90c7fb08f4325a3adcbf12
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8264050Z ##[group]Run sudo apt-get update
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8264463Z ^[[36;1msudo apt-get update^[[0m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8264821Z ^[[36;1msudo apt-get install -y --no-install-recommends \^[[0m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8265331Z ^[[36;1m  gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi \^[[0m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8265945Z ^[[36;1m  qemu-system-arm qemu-user make python3 python3-numpy python3-pil dosfstools mtools^[[0m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8534211Z shell: /usr/bin/bash -e {0}
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8534626Z env:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8534968Z   ZERO2: 0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.8535236Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.9518937Z Get:1 file:/etc/apt/apt-mirrors.txt Mirrorlist [144 B]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.9748261Z Get:6 https://packages.microsoft.com/ubuntu/24.04/prod noble InRelease [3600 B]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.9847615Z Hit:2 http://azure.archive.ubuntu.com/ubuntu noble InRelease
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.9871620Z Get:3 http://azure.archive.ubuntu.com/ubuntu noble-updates InRelease [126 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.9897292Z Get:4 http://azure.archive.ubuntu.com/ubuntu noble-backports InRelease [126 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:52.9922700Z Get:5 http://azure.archive.ubuntu.com/ubuntu noble-security InRelease [126 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.0571433Z Get:7 https://packages.microsoft.com/ubuntu/24.04/prod noble/main amd64 Packages [581 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.0845508Z Get:8 https://packages.microsoft.com/ubuntu/24.04/prod noble/main armhf Packages [12.4 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.0874749Z Get:9 https://packages.microsoft.com/ubuntu/24.04/prod noble/main arm64 Packages [523 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.1795107Z Get:10 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 Packages [1397 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.1863205Z Get:11 http://azure.archive.ubuntu.com/ubuntu noble-updates/main Translation-en [308 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.1876379Z Get:12 http://azure.archive.ubuntu.com/ubuntu noble-updates/universe amd64 Packages [1702 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.1975691Z Get:13 http://azure.archive.ubuntu.com/ubuntu noble-updates/restricted amd64 Packages [1805 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.2068869Z Get:14 http://azure.archive.ubuntu.com/ubuntu noble-updates/restricted Translation-en [411 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.2934251Z Get:15 http://azure.archive.ubuntu.com/ubuntu noble-security/main amd64 Packages [1144 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.2998901Z Get:16 http://azure.archive.ubuntu.com/ubuntu noble-security/main Translation-en [231 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.3017807Z Get:17 http://azure.archive.ubuntu.com/ubuntu noble-security/universe amd64 Packages [1221 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.3091338Z Get:18 http://azure.archive.ubuntu.com/ubuntu noble-security/universe Translation-en [246 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.3111430Z Get:19 http://azure.archive.ubuntu.com/ubuntu noble-security/restricted amd64 Packages [1713 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:53.3644706Z Get:20 http://azure.archive.ubuntu.com/ubuntu noble-security/restricted Translation-en [392 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:56.3010822Z Fetched 12.1 MB in 1s (9673 kB/s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.2063405Z Reading package lists...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.2368952Z Reading package lists...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.4088901Z Building dependency tree...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.4097592Z Reading state information...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5470589Z make is already the newest version (4.3-4.1build2).
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5471397Z python3 is already the newest version (3.12.3-0ubuntu2.1).
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5472360Z dosfstools is already the newest version (4.2-1.1build1).
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5472977Z dosfstools set to manually installed.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5473575Z The following additional packages will be installed:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5474473Z   libblas3 libbrlapi0.8 libcacard0 libdaxctl1 libfdt1 libimagequant0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5475485Z   liblapack3 libndctl6 libnewlib-dev libpcsclite1 libpmem1 libraqm0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5476463Z   librdmacm1t64 liburing2 libusbredirparser1t64 qemu-system-common
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5477220Z   qemu-system-data
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5483122Z Suggested packages:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5484167Z   libnewlib-doc pcscd floppyd python3-pytest python-pil-doc samba vde2
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5484682Z Recommended packages:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5485046Z   libstdc++-arm-none-eabi-newlib python3-olefile qemu-utils ipxe-qemu
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5485625Z   ipxe-qemu-256k-compat-efi-roms qemu-efi-aarch64 qemu-efi-arm qemu-system-gui
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5569889Z   qemu-system-modules-spice qemu-system-modules-opengl qemu-block-extra
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5570577Z   qemu-user-binfmt
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5733878Z The following NEW packages will be installed:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5734927Z   binutils-arm-none-eabi gcc-arm-none-eabi libblas3 libbrlapi0.8 libcacard0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5736197Z   libdaxctl1 libfdt1 libimagequant0 liblapack3 libndctl6
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5737009Z   libnewlib-arm-none-eabi libnewlib-dev libpcsclite1 libpmem1 libraqm0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5740017Z   librdmacm1t64 liburing2 libusbredirparser1t64 mtools python3-numpy
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5741163Z   python3-pil qemu-system-arm qemu-system-common qemu-system-data qemu-user
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5917943Z 0 upgraded, 25 newly installed, 0 to remove and 43 not upgraded.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5918428Z Need to get 142 MB of archives.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5918816Z After this operation, 1153 MB of additional disk space will be used.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.5919442Z Get:1 file:/etc/apt/apt-mirrors.txt Mirrorlist [144 B]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.6769734Z Get:2 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 libblas3 amd64 3.12.0-3build1.1 [238 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.7510104Z Get:3 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libbrlapi0.8 amd64 6.6-4ubuntu5 [31.4 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.8210516Z Get:4 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libpcsclite1 amd64 2.0.3-1build1 [21.4 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.8911270Z Get:5 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libcacard0 amd64 1:2.8.0-3build4 [36.5 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:57.9614023Z Get:6 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libdaxctl1 amd64 77-2ubuntu2 [21.4 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.0314502Z Get:7 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libimagequant0 amd64 2.18.0-1build1 [36.3 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.2178829Z Get:8 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 liblapack3 amd64 3.12.0-3build1.1 [2646 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.3610084Z Get:9 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libndctl6 amd64 77-2ubuntu2 [62.8 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.4567407Z Get:10 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libpmem1 amd64 1.13.1-1.1ubuntu2 [84.8 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.5589506Z Get:11 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libraqm0 amd64 0.10.1-1build1 [15.0 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.6292960Z Get:12 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 librdmacm1t64 amd64 50.0-2ubuntu0.2 [70.7 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.7000526Z Get:13 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 liburing2 amd64 2.5-1build1 [21.1 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.7700037Z Get:14 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libusbredirparser1t64 amd64 0.13.0-2.1build1 [16.5 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.8406949Z Get:15 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 mtools amd64 4.0.43-1build1 [197 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:58.9143938Z Get:16 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 python3-numpy amd64 1:1.26.4+ds-6ubuntu1 [4437 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:59.1525945Z Get:17 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 python3-pil amd64 10.2.0-1ubuntu1.3 [452 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:59.5986394Z Get:18 http://azure.archive.ubuntu.com/ubuntu noble/main amd64 libfdt1 amd64 1.7.0-2build1 [20.1 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:59.6707232Z Get:19 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 qemu-system-common amd64 1:8.2.2+ds-0ubuntu1.18 [1255 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:00:59.8848389Z Get:20 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 qemu-system-data all 1:8.2.2+ds-0ubuntu1.18 [1796 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:00.0339021Z Get:21 http://azure.archive.ubuntu.com/ubuntu noble-updates/main amd64 qemu-system-arm amd64 1:8.2.2+ds-0ubuntu1.18 [11.0 MB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:00.3429709Z Get:22 http://azure.archive.ubuntu.com/ubuntu noble-updates/universe amd64 qemu-user amd64 1:8.2.2+ds-0ubuntu1.18 [9635 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:00.6362089Z Get:23 http://azure.archive.ubuntu.com/ubuntu noble/universe amd64 binutils-arm-none-eabi amd64 2.42-1ubuntu1+23 [3137 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:00.7814774Z Get:24 http://azure.archive.ubuntu.com/ubuntu noble/universe amd64 gcc-arm-none-eabi amd64 15:13.2.rel1-2 [57.5 MB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:02.3038887Z Get:25 http://azure.archive.ubuntu.com/ubuntu noble/universe amd64 libnewlib-dev all 4.4.0.20231231-2 [140 kB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:02.3767867Z Get:26 http://azure.archive.ubuntu.com/ubuntu noble/universe amd64 libnewlib-arm-none-eabi all 4.4.0.20231231-2 [49.2 MB]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.7524346Z Fetched 142 MB in 6s (24.0 MB/s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.7782173Z Selecting previously unselected package libblas3:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8072665Z (Reading database ... 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8073147Z (Reading database ... 5%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8073495Z (Reading database ... 10%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8073813Z (Reading database ... 15%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8074689Z (Reading database ... 20%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8075191Z (Reading database ... 25%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8075651Z (Reading database ... 30%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8076101Z (Reading database ... 35%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8076592Z (Reading database ... 40%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8077017Z (Reading database ... 45%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8077450Z (Reading database ... 50%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.8137599Z (Reading database ... 55%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:03.9007127Z (Reading database ... 60%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:04.0966292Z (Reading database ... 65%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:04.2930666Z (Reading database ... 70%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:04.4909351Z (Reading database ... 75%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:04.6524616Z (Reading database ... 80%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:04.8424032Z (Reading database ... 85%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.0096134Z (Reading database ... 90%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.1975258Z (Reading database ... 95%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.1976145Z (Reading database ... 100%
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.1977232Z (Reading database ... 203540 files and directories currently installed.)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2032730Z Preparing to unpack .../00-libblas3_3.12.0-3build1.1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2090754Z Unpacking libblas3:amd64 (3.12.0-3build1.1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2343333Z Selecting previously unselected package libbrlapi0.8:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2469246Z Preparing to unpack .../01-libbrlapi0.8_6.6-4ubuntu5_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2480540Z Unpacking libbrlapi0.8:amd64 (6.6-4ubuntu5) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2697912Z Selecting previously unselected package libpcsclite1:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2830608Z Preparing to unpack .../02-libpcsclite1_2.0.3-1build1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.2841542Z Unpacking libpcsclite1:amd64 (2.0.3-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3067187Z Selecting previously unselected package libcacard0:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3192703Z Preparing to unpack .../03-libcacard0_1%3a2.8.0-3build4_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3203921Z Unpacking libcacard0:amd64 (1:2.8.0-3build4) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3423473Z Selecting previously unselected package libdaxctl1:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3546449Z Preparing to unpack .../04-libdaxctl1_77-2ubuntu2_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3554963Z Unpacking libdaxctl1:amd64 (77-2ubuntu2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3774998Z Selecting previously unselected package libimagequant0:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3899207Z Preparing to unpack .../05-libimagequant0_2.18.0-1build1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.3909554Z Unpacking libimagequant0:amd64 (2.18.0-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.4139780Z Selecting previously unselected package liblapack3:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.4263379Z Preparing to unpack .../06-liblapack3_3.12.0-3build1.1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.4296495Z Unpacking liblapack3:amd64 (3.12.0-3build1.1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.4818723Z Selecting previously unselected package libndctl6:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.4947245Z Preparing to unpack .../07-libndctl6_77-2ubuntu2_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.4955361Z Unpacking libndctl6:amd64 (77-2ubuntu2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.5177549Z Selecting previously unselected package libpmem1:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.5304204Z Preparing to unpack .../08-libpmem1_1.13.1-1.1ubuntu2_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.5313749Z Unpacking libpmem1:amd64 (1.13.1-1.1ubuntu2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.5574321Z Selecting previously unselected package libraqm0:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.5701812Z Preparing to unpack .../09-libraqm0_0.10.1-1build1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.5710880Z Unpacking libraqm0:amd64 (0.10.1-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.5921943Z Selecting previously unselected package librdmacm1t64:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6044215Z Preparing to unpack .../10-librdmacm1t64_50.0-2ubuntu0.2_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6052500Z Unpacking librdmacm1t64:amd64 (50.0-2ubuntu0.2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6283087Z Selecting previously unselected package liburing2:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6407102Z Preparing to unpack .../11-liburing2_2.5-1build1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6416954Z Unpacking liburing2:amd64 (2.5-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6628489Z Selecting previously unselected package libusbredirparser1t64:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6753558Z Preparing to unpack .../12-libusbredirparser1t64_0.13.0-2.1build1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6763014Z Unpacking libusbredirparser1t64:amd64 (0.13.0-2.1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.6966733Z Selecting previously unselected package mtools.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.7103625Z Preparing to unpack .../13-mtools_4.0.43-1build1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.7113923Z Unpacking mtools (4.0.43-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.7509305Z Selecting previously unselected package python3-numpy.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.7631479Z Preparing to unpack .../14-python3-numpy_1%3a1.26.4+ds-6ubuntu1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.7639193Z Unpacking python3-numpy (1:1.26.4+ds-6ubuntu1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.9585884Z Selecting previously unselected package python3-pil:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.9716659Z Preparing to unpack .../15-python3-pil_10.2.0-1ubuntu1.3_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:05.9734430Z Unpacking python3-pil:amd64 (10.2.0-1ubuntu1.3) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.0177256Z Selecting previously unselected package libfdt1:amd64.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.0303437Z Preparing to unpack .../16-libfdt1_1.7.0-2build1_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.0312352Z Unpacking libfdt1:amd64 (1.7.0-2build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.0535084Z Selecting previously unselected package qemu-system-common.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.0658996Z Preparing to unpack .../17-qemu-system-common_1%3a8.2.2+ds-0ubuntu1.18_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.0671066Z Unpacking qemu-system-common (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.1357434Z Selecting previously unselected package qemu-system-data.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.1482847Z Preparing to unpack .../18-qemu-system-data_1%3a8.2.2+ds-0ubuntu1.18_all.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.1491553Z Unpacking qemu-system-data (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.2138606Z Selecting previously unselected package qemu-system-arm.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.2266012Z Preparing to unpack .../19-qemu-system-arm_1%3a8.2.2+ds-0ubuntu1.18_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.2335149Z Unpacking qemu-system-arm (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.4374918Z Selecting previously unselected package qemu-user.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.4501442Z Preparing to unpack .../20-qemu-user_1%3a8.2.2+ds-0ubuntu1.18_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.4511469Z Unpacking qemu-user (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.6618548Z Selecting previously unselected package binutils-arm-none-eabi.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.6744742Z Preparing to unpack .../21-binutils-arm-none-eabi_2.42-1ubuntu1+23_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.6752080Z Unpacking binutils-arm-none-eabi (2.42-1ubuntu1+23) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.7561352Z Selecting previously unselected package gcc-arm-none-eabi.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.7687711Z Preparing to unpack .../22-gcc-arm-none-eabi_15%3a13.2.rel1-2_amd64.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:06.7695932Z Unpacking gcc-arm-none-eabi (15:13.2.rel1-2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:08.2425298Z Selecting previously unselected package libnewlib-dev.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:08.2557661Z Preparing to unpack .../23-libnewlib-dev_4.4.0.20231231-2_all.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:08.2567316Z Unpacking libnewlib-dev (4.4.0.20231231-2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:08.2971856Z Selecting previously unselected package libnewlib-arm-none-eabi.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:08.3103008Z Preparing to unpack .../24-libnewlib-arm-none-eabi_4.4.0.20231231-2_all.deb ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:08.3111750Z Unpacking libnewlib-arm-none-eabi (4.4.0.20231231-2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6443833Z Setting up binutils-arm-none-eabi (2.42-1ubuntu1+23) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6467580Z Setting up mtools (4.0.43-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6492931Z Setting up gcc-arm-none-eabi (15:13.2.rel1-2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6511273Z Setting up libfdt1:amd64 (1.7.0-2build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6530441Z Setting up libblas3:amd64 (3.12.0-3build1.1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6588238Z update-alternatives: using /usr/lib/x86_64-linux-gnu/blas/libblas.so.3 to provide /usr/lib/x86_64-linux-gnu/libblas.so.3 (libblas.so.3-x86_64-linux-gnu) in auto mode
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6604829Z Setting up qemu-user (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6622206Z Setting up libraqm0:amd64 (0.10.1-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6640753Z Setting up libnewlib-dev (4.4.0.20231231-2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6713826Z Setting up libimagequant0:amd64 (2.18.0-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6731853Z Setting up libpcsclite1:amd64 (2.0.3-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6751187Z Setting up libdaxctl1:amd64 (77-2ubuntu2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6769077Z Setting up qemu-system-data (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6787080Z Setting up libnewlib-arm-none-eabi (4.4.0.20231231-2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6860251Z Setting up libusbredirparser1t64:amd64 (0.13.0-2.1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6878669Z Setting up libndctl6:amd64 (77-2ubuntu2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6897278Z Setting up librdmacm1t64:amd64 (50.0-2ubuntu0.2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6917838Z Setting up libbrlapi0.8:amd64 (6.6-4ubuntu5) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6937202Z Setting up liburing2:amd64 (2.5-1build1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6956162Z Setting up libpmem1:amd64 (1.13.1-1.1ubuntu2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.6974455Z Setting up liblapack3:amd64 (3.12.0-3build1.1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.7033547Z update-alternatives: using /usr/lib/x86_64-linux-gnu/lapack/liblapack.so.3 to provide /usr/lib/x86_64-linux-gnu/liblapack.so.3 (liblapack.so.3-x86_64-linux-gnu) in auto mode
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.7058192Z Setting up libcacard0:amd64 (1:2.8.0-3build4) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.7087479Z Setting up python3-pil:amd64 (10.2.0-1ubuntu1.3) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:09.9814239Z Setting up python3-numpy (1:1.26.4+ds-6ubuntu1) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:11.4035846Z Setting up qemu-system-common (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:11.5298663Z Created symlink /etc/systemd/system/multi-user.target.wants/qemu-kvm.service → /usr/lib/systemd/system/qemu-kvm.service.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:11.5299360Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:11.8465610Z Setting up qemu-system-arm (1:8.2.2+ds-0ubuntu1.18) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:11.8538886Z Processing triggers for hicolor-icon-theme (0.17-2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.1132882Z Processing triggers for libc-bin (2.39-0ubuntu8.9) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.1493902Z Processing triggers for man-db (2.12.0-4build2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.1519536Z Not building database; man-db/auto-update is not 'true'.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.1532859Z Processing triggers for install-info (7.1-3build2) ...
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8978292Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8979110Z Running kernel seems to be up-to-date.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8979573Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8979764Z No services need to be restarted.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8980075Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8980250Z No containers need to be restarted.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8980551Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8980758Z No user sessions are running outdated binaries.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8981130Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:12.8981470Z No VM guests are running outdated hypervisor (qemu) binaries on this host.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.8548565Z ##[group]Run qemu-system-arm --version | tee /dev/stderr | grep -q 'version 8\.2\.'
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.8549229Z ^[[36;1mqemu-system-arm --version | tee /dev/stderr | grep -q 'version 8\.2\.'^[[0m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.8619387Z shell: /usr/bin/bash -e {0}
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.8619699Z env:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.8619929Z   ZERO2: 0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.8620165Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9015362Z QEMU emulator version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9016753Z Copyright (c) 2003-2023 Fabrice Bellard and the QEMU Project developers
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9067165Z ##[group]Run make -j"$(nproc)"
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9067519Z ^[[36;1mmake -j"$(nproc)"^[[0m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9134857Z shell: /usr/bin/bash -e {0}
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9135165Z env:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9135395Z   ZERO2: 0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:13.9135634Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.5339669Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/lib/crc32.c -o build/k/src/lib/crc32.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.5343606Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/lib/printf.c -o build/k/src/lib/printf.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.5350531Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/lib/syscalls.c -o build/k/src/lib/syscalls.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.5363921Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/arch/cache.c -o build/k/src/arch/cache.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.5734200Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard   -g -Isrc -Isrc/kernel -Wa,-Ibuild -c src/arch/fiber.S -o build/k/src/arch/fiber.S.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.5744917Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/arch/mmu.c -o build/k/src/arch/mmu.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.5904588Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/mesh.c -o build/k/src/ai/mesh.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.6204832Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/music.c -o build/k/src/ai/music.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.6304547Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/sprite.c -o build/k/src/ai/sprite.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:14.7444808Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/assist.c -o build/k/src/ai/assist.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:15.0985107Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/lua_music.c -o build/k/src/ai/lua_music.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:15.2724893Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/mesh_chars.c -o build/k/src/ai/mesh_chars.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:15.9665062Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/mesh_script.c -o build/k/src/ai/mesh_script.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.0934768Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/nn.c -o build/k/src/ai/nn.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.1534839Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/text.c -o build/k/src/ai/text.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.1864853Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/music_net.c -o build/k/src/ai/music_net.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.2295688Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/lua_ai.c -o build/k/src/ai/lua_ai.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.3006315Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/ai/net.c -o build/k/src/ai/net.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.3358760Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/usb/hid.c -o build/k/src/usb/hid.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.4524828Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/usb/smsc95xx.c -o build/k/src/usb/smsc95xx.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.5636792Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/usb/usb.c -o build/k/src/usb/usb.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.6744774Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/usb/dwc2.c -o build/k/src/usb/dwc2.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.6874826Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/wifi/wifi.c -o build/k/src/wifi/wifi.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.9163416Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/wifi/sdio.c -o build/k/src/wifi/sdio.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:16.9744459Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gfx/font6x12.c -o build/k/src/gfx/font6x12.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.0044920Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gfx/console.c -o build/k/src/gfx/console.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.1264915Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gfx/draw.c -o build/k/src/gfx/draw.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.2014968Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gfx/font8x16.c -o build/k/src/gfx/font8x16.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.2284543Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gfx/font8x14.c -o build/k/src/gfx/font8x14.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.2524683Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/crumbs.c -o build/k/src/kernel/crumbs.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.2665343Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/input.c -o build/k/src/kernel/input.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.2879147Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/bench.c -o build/k/src/kernel/bench.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.3625697Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/fiber.c -o build/k/src/kernel/fiber.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.3763652Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/testpattern.c -o build/k/src/kernel/testpattern.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.4384376Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/icons.c -o build/k/src/kernel/icons.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.4544713Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/upload.c -o build/k/src/kernel/upload.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.5217066Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/libview.c -o build/k/src/kernel/libview.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.5225511Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/pager.c -o build/k/src/kernel/pager.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.5674855Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/pointer.c -o build/k/src/kernel/pointer.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.6154894Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/menu_ui.c -o build/k/src/kernel/menu_ui.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.6995119Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/prompts.c -o build/k/src/kernel/prompts.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.8508298Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/splash.c -o build/k/src/kernel/splash.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:17.9229187Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/update.c -o build/k/src/kernel/update.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.0465033Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/b3dpi.c -o build/k/src/kernel/b3dpi.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.1734940Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/tick.c -o build/k/src/kernel/tick.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.2214939Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/carts.c -o build/k/src/kernel/carts.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.2564827Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/pmu.c -o build/k/src/kernel/pmu.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.2885070Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/demo.c -o build/k/src/kernel/demo.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.2943228Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard   -g -Isrc -Isrc/kernel -Wa,-Ibuild -c src/kernel/vectors.S -o build/k/src/kernel/vectors.S.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.3095044Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/lib.c -o build/k/src/kernel/lib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.4434788Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/config.c -o build/k/src/kernel/config.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.6845498Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/sysinfo.c -o build/k/src/kernel/sysinfo.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.6934819Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/irq.c -o build/k/src/kernel/irq.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.7572012Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/devtool.c -o build/k/src/kernel/devtool.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.7694750Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/selftest.c -o build/k/src/kernel/selftest.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.8565074Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/market.c -o build/k/src/kernel/market.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.8704728Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/dmatest.c -o build/k/src/kernel/dmatest.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:18.9785110Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/exceptions.c -o build/k/src/kernel/exceptions.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.0760632Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/logo_data.c -o build/k/src/kernel/logo_data.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.1484936Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/publish.c -o build/k/src/kernel/publish.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.3004582Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/monitor.c -o build/k/src/kernel/monitor.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.3484949Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/gputest.c -o build/k/src/kernel/gputest.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.5494951Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/notice.c -o build/k/src/kernel/notice.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.6084689Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/settings.c -o build/k/src/kernel/settings.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.7035062Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/reports.c -o build/k/src/kernel/reports.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.8945265Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/ledstate.c -o build/k/src/kernel/ledstate.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:19.9274829Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/main.c -o build/k/src/kernel/main.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.0314714Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/home.c -o build/k/src/kernel/home.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.1854422Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/kernel/syskeys.c -o build/k/src/kernel/syskeys.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.1884258Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/iec958.c -o build/k/src/audio/iec958.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.1993277Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/player.c -o build/k/src/audio/player.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.2445220Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/lua_tone.c -o build/k/src/audio/lua_tone.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.2714794Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/n8snd.c -o build/k/src/audio/n8snd.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.4944724Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/synth.c -o build/k/src/audio/synth.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.5284402Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/presets.c -o build/k/src/audio/presets.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.6764801Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/audio.c -o build/k/src/audio/audio.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.7874860Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/audio/hdmi_audio.c -o build/k/src/audio/hdmi_audio.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.9324929Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gpu/v3d_cl.c -o build/k/src/gpu/v3d_cl.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:20.9874727Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gpu/v3d.c -o build/k/src/gpu/v3d.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.0435007Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gpu/etc1.c -o build/k/src/gpu/etc1.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.1734725Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/gpu/gpu3d.c -o build/k/src/gpu/gpu3d.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.2535122Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/watchdog.c -o build/k/src/drivers/watchdog.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.2935265Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/sd_emmc.c -o build/k/src/drivers/sd_emmc.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.3064630Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/board.c -o build/k/src/drivers/board.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.4522781Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/sdhost.c -o build/k/src/drivers/sdhost.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.5134892Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/rng.c -o build/k/src/drivers/rng.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.5604919Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/dma.c -o build/k/src/drivers/dma.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.6642728Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/led.c -o build/k/src/drivers/led.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.6795327Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/timer.c -o build/k/src/drivers/timer.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.7135005Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/gpio.c -o build/k/src/drivers/gpio.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.7344973Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/uart.c -o build/k/src/drivers/uart.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.7564893Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/prop.c -o build/k/src/drivers/prop.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.8319978Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/fb.c -o build/k/src/drivers/fb.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.8354588Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/mbox.c -o build/k/src/drivers/mbox.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.8654736Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/drivers/sd.c -o build/k/src/drivers/sd.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.9184788Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/fs/fat.c -o build/k/src/fs/fat.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:21.9264477Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/png.c -o build/k/src/bm/png.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:22.2954741Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/meshcap.c -o build/k/src/bm/meshcap.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:22.5854881Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/profile.c -o build/k/src/bm/profile.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:22.7064064Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/n8cart.c -o build/k/src/bm/n8cart.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:22.7295357Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/b3d.c -o build/k/src/bm/b3d.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:22.7794556Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/roombench.c -o build/k/src/bm/roombench.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:22.8875028Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/n8font.c -o build/k/src/bm/n8font.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:22.9424719Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/stress.c -o build/k/src/bm/stress.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:23.2564705Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/json.c -o build/k/src/bm/json.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:23.3364682Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/format.c -o build/k/src/bm/format.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:23.6501135Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/gfx16.c -o build/k/src/bm/gfx16.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:24.2405032Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/loading_logo.c -o build/k/src/bm/loading_logo.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:24.2624252Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/jpeg.c -o build/k/src/bm/jpeg.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:24.5424808Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/decimate.c -o build/k/src/bm/decimate.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:24.6214643Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/project.c -o build/k/src/bm/project.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:24.6844839Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/r3d.c -o build/k/src/bm/r3d.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:24.8125287Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/loading.c -o build/k/src/bm/loading.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:25.0904882Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/world3d.c -o build/k/src/bm/world3d.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:25.2534937Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/glb.c -o build/k/src/bm/glb.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:25.3015007Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/runtime.c -o build/k/src/bm/runtime.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:25.8464747Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/shot.c -o build/k/src/bm/shot.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:26.1144648Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/tokens.c -o build/k/src/bm/tokens.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:26.1444536Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/n8.c -o build/k/src/bm/n8.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:26.3124771Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/cutout.c -o build/k/src/bm/cutout.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:27.3004318Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/require.c -o build/k/src/bm/require.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:27.3758254Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/map3d.c -o build/k/src/bm/map3d.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:28.3515009Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bm/n8lua.c -o build/k/src/bm/n8lua.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:28.3833304Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard   -g -Isrc -Isrc/kernel -Wa,-Ibuild -c src/boot/start.S -o build/k/src/boot/start.S.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:28.3957685Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/github.c -o build/k/src/net/github.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:28.9526527Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/tls.c -o build/k/src/net/tls.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.0534704Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/cartnet.c -o build/k/src/net/cartnet.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.1361972Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/report.c -o build/k/src/net/report.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.1824811Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/release.c -o build/k/src/net/release.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.2744483Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/stream.c -o build/k/src/net/stream.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.3824675Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/img3d.c -o build/k/src/net/img3d.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.4064517Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/http.c -o build/k/src/net/http.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.5802602Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/lan.c -o build/k/src/net/lan.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.6074990Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/http_kernel.c -o build/k/src/net/http_kernel.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.6254566Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/catalog.c -o build/k/src/net/catalog.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.6554838Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/netxfer.c -o build/k/src/net/netxfer.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.8794806Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/wifi_auto.c -o build/k/src/net/wifi_auto.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.8894994Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/netcon.c -o build/k/src/net/netcon.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:29.9574779Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/net/net.c -o build/k/src/net/net.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.0085073Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/script/luavm.c -o build/k/src/script/luavm.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.0844573Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/script/repl.c -o build/k/src/script/repl.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.1074723Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/script/lib_bm.c -o build/k/src/script/lib_bm.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.1135917Z python3 scripts/mkbm.py -o build/demo.bm --lua carts/demo/main.lua --sheet carts/demo/sheet.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.1137495Z     --map carts/demo/map.csv --title "bm native demo" --author bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.1703137Z python3 scripts/mkbm.py -o build/stress.bm --lua carts/stress/main.lua --title "bm stress test" --author bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.1913070Z python3 scripts/mkbm.py -o build/editor.bm --lua carts/editor/main.lua --cover carts/editor/icon.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.1972452Z     --info carts/editor/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.2152501Z build/demo.bm: 97424 bytes (bm native demo, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.2182300Z build/stress.bm: 3276 bytes (bm stress test, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.2253471Z python3 scripts/mkbm.py -o build/sound.bm --lua carts/sound/main.lua --cover carts/sound/icon.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.2256158Z     --audio carts/sound/demo.json --info carts/sound/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.2257363Z python3 scripts/mkbm.py -o build/studio.bm --lua carts/studio/main.lua --cover carts/studio/icon.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.2258675Z     --sheet carts/studio/assets/sheet.png --sheet8 --info carts/studio/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.2962456Z build/editor.bm: 167004 bytes (bm SDK, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.3053091Z python3 scripts/mkbm.py -o build/animator.bm --lua carts/animator/main.lua --cover carts/animator/icon.png --info carts/animator/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.3305260Z build/sound.bm: 161796 bytes (bm Sound, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.3412959Z python3 scripts/mkbm.py -o build/mesh.bm --lua carts/mesh/main.lua --cover carts/mesh/icon.png --info carts/mesh/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.3592398Z build/studio.bm: 105872 bytes (bm Studio, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.3713077Z python3 scripts/mkbm.py -o build/pixel.bm --lua carts/pixel/main.lua --cover carts/pixel/icon.png --info carts/pixel/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.4064067Z build/animator.bm: 101224 bytes (bm Animator, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.4163558Z python3 scripts/mkbm.py -o build/assistant.bm --lua carts/assistant/main.lua --cover carts/code/icon.png --info carts/assistant/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.4423673Z build/mesh.bm: 122056 bytes (bm Mesh, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.4622879Z python3 scripts/mkbm.py -o build/code.bm --lua carts/code/main.lua --cover carts/code/icon.png --info carts/code/bm.info
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.4772392Z build/pixel.bm: 101616 bytes (bm Pixel, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.4892848Z python3 scripts/mkassist.py -o build/assist.bin --ref build/ai/ref.txt --snippets build/ai/snippets.txt
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.5182557Z build/assistant.bm: 38172 bytes (bm assistant, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.5463110Z python3 scripts/mkbm.py -o build/texroom.bm --lua carts/texroom/main.lua --title "Texture Room" --author bm --res 320x180 qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.5465222Z     --cover carts/texroom/cover.png --sheet carts/texroom/sheet.png
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.5640825Z build/code.bm: 98152 bytes (bm Code, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.5739405Z python3 scripts/mkwords.py -o build/words.lua
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.6752599Z build/texroom.bm: 103944 bytes (Texture Room, 320x180)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:30.6844337Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bt/ble.c -o build/k/src/bt/ble.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:31.1435036Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bt/btuart.c -o build/k/src/bt/btuart.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:31.2255017Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bt/rtlbt.c -o build/k/src/bt/rtlbt.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:31.3264800Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bt/smp_crypto.c -o build/k/src/bt/smp_crypto.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:31.5634922Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bt/bt.c -o build/k/src/bt/bt.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:31.7505282Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bt/h5.c -o build/k/src/bt/h5.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:31.9004322Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -MMD -MP -c src/bt/hci.c -o build/k/src/bt/hci.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:31.9374806Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lapi.c -o build/k/third_party/lua/lapi.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.0094832Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lauxlib.c -o build/k/third_party/lua/lauxlib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.3636147Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lbaselib.c -o build/k/third_party/lua/lbaselib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.4744792Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lcode.c -o build/k/third_party/lua/lcode.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.5804934Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lcorolib.c -o build/k/third_party/lua/lcorolib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.6704928Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lctype.c -o build/k/third_party/lua/lctype.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.6904761Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/ldblib.c -o build/k/third_party/lua/ldblib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.7295233Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/ldebug.c -o build/k/third_party/lua/ldebug.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:32.9034868Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/ldo.c -o build/k/third_party/lua/ldo.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.1185050Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/ldump.c -o build/k/third_party/lua/ldump.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.1935022Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lfunc.c -o build/k/third_party/lua/lfunc.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.2764851Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lgc.c -o build/k/third_party/lua/lgc.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.3104832Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/linit.c -o build/k/third_party/lua/linit.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.3305079Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/liolib.c -o build/k/third_party/lua/liolib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.3484853Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/llex.c -o build/k/third_party/lua/llex.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.6764882Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lmathlib.c -o build/k/third_party/lua/lmathlib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.6914512Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lmem.c -o build/k/third_party/lua/lmem.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.7644961Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/loadlib.c -o build/k/third_party/lua/loadlib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.8905048Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lobject.c -o build/k/third_party/lua/lobject.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.9394826Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lopcodes.c -o build/k/third_party/lua/lopcodes.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.9624682Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/loslib.c -o build/k/third_party/lua/loslib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:33.9804684Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lparser.c -o build/k/third_party/lua/lparser.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.1025015Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lstate.c -o build/k/third_party/lua/lstate.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.1605067Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lstring.c -o build/k/third_party/lua/lstring.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.2585011Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lstrlib.c -o build/k/third_party/lua/lstrlib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.3204607Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/ltable.c -o build/k/third_party/lua/ltable.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.4062896Z build/assist.bin: 739948 bytes, 536 entries, network 4096 -> 96 -> 536
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.4072683Z build/ai/ref.txt: 339 questions
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.4154676Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/ltablib.c -o build/k/third_party/lua/ltablib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.6374850Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/ltm.c -o build/k/third_party/lua/ltm.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.6968539Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lundump.c -o build/k/third_party/lua/lundump.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.8074901Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lutf8lib.c -o build/k/third_party/lua/lutf8lib.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.8464719Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lvm.c -o build/k/third_party/lua/lvm.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.8955061Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lua/lzio.c -o build/k/third_party/lua/lzio.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.9504863Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/altcp.c -o build/k/third_party/lwip/src/core/altcp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.9801588Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/altcp_alloc.c -o build/k/third_party/lwip/src/core/altcp_alloc.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:34.9955317Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/altcp_tcp.c -o build/k/third_party/lwip/src/core/altcp_tcp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.0100069Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/def.c -o build/k/third_party/lwip/src/core/def.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.0275179Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/dns.c -o build/k/third_party/lwip/src/core/dns.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.0745083Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/inet_chksum.c -o build/k/third_party/lwip/src/core/inet_chksum.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.1054852Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/init.c -o build/k/third_party/lwip/src/core/init.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.1524741Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ip.c -o build/k/third_party/lwip/src/core/ip.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.1859582Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/mem.c -o build/k/third_party/lwip/src/core/mem.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.1954999Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/memp.c -o build/k/third_party/lwip/src/core/memp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.2345045Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/netif.c -o build/k/third_party/lwip/src/core/netif.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.2683927Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/pbuf.c -o build/k/third_party/lwip/src/core/pbuf.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.2994630Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/raw.c -o build/k/third_party/lwip/src/core/raw.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.3295175Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/stats.c -o build/k/third_party/lwip/src/core/stats.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.3564754Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/sys.c -o build/k/third_party/lwip/src/core/sys.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.3864811Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/tcp.c -o build/k/third_party/lwip/src/core/tcp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.4164960Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/tcp_in.c -o build/k/third_party/lwip/src/core/tcp_in.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.6575085Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/tcp_out.c -o build/k/third_party/lwip/src/core/tcp_out.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.9105228Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/timeouts.c -o build/k/third_party/lwip/src/core/timeouts.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:35.9964689Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/udp.c -o build/k/third_party/lwip/src/core/udp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.0134399Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/acd.c -o build/k/third_party/lwip/src/core/ipv4/acd.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.0454985Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/autoip.c -o build/k/third_party/lwip/src/core/ipv4/autoip.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.0724858Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/dhcp.c -o build/k/third_party/lwip/src/core/ipv4/dhcp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.1355131Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/etharp.c -o build/k/third_party/lwip/src/core/ipv4/etharp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.1985068Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/icmp.c -o build/k/third_party/lwip/src/core/ipv4/icmp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.2825012Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/igmp.c -o build/k/third_party/lwip/src/core/ipv4/igmp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.3133335Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/ip4.c -o build/k/third_party/lwip/src/core/ipv4/ip4.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.3765048Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/ip4_addr.c -o build/k/third_party/lwip/src/core/ipv4/ip4_addr.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.4555208Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/core/ipv4/ip4_frag.c -o build/k/third_party/lwip/src/core/ipv4/ip4_frag.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.4930023Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/netif/ethernet.c -o build/k/third_party/lwip/src/netif/ethernet.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.5604901Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/lwip/src/apps/sntp/sntp.c -o build/k/third_party/lwip/src/apps/sntp/sntp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.6175006Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/aes.c -o build/k/third_party/mbedtls/library/aes.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.6845240Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/asn1parse.c -o build/k/third_party/mbedtls/library/asn1parse.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.7164954Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/asn1write.c -o build/k/third_party/mbedtls/library/asn1write.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.8684896Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/base64.c -o build/k/third_party/mbedtls/library/base64.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.9191368Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/bignum.c -o build/k/third_party/mbedtls/library/bignum.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:36.9355636Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/bignum_core.c -o build/k/third_party/mbedtls/library/bignum_core.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.0081030Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/chacha20.c -o build/k/third_party/mbedtls/library/chacha20.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.1804977Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/chachapoly.c -o build/k/third_party/mbedtls/library/chachapoly.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.2095183Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/cipher.c -o build/k/third_party/mbedtls/library/cipher.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.3003639Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/cipher_wrap.c -o build/k/third_party/mbedtls/library/cipher_wrap.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.3246016Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/constant_time.c -o build/k/third_party/mbedtls/library/constant_time.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.3875079Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ctr_drbg.c -o build/k/third_party/mbedtls/library/ctr_drbg.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.4235043Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ecdh.c -o build/k/third_party/mbedtls/library/ecdh.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.4264640Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ecdsa.c -o build/k/third_party/mbedtls/library/ecdsa.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.5592689Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ecp.c -o build/k/third_party/mbedtls/library/ecp.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.5604463Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ecp_curves.c -o build/k/third_party/mbedtls/library/ecp_curves.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.5906571Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/entropy.c -o build/k/third_party/mbedtls/library/entropy.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.6954857Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/error.c -o build/k/third_party/mbedtls/library/error.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.7105594Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/gcm.c -o build/k/third_party/mbedtls/library/gcm.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.8485043Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/md.c -o build/k/third_party/mbedtls/library/md.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:37.9595064Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/oid.c -o build/k/third_party/mbedtls/library/oid.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.0425084Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/pem.c -o build/k/third_party/mbedtls/library/pem.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.0764837Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/pk.c -o build/k/third_party/mbedtls/library/pk.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.1195110Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/pk_ecc.c -o build/k/third_party/mbedtls/library/pk_ecc.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.1884843Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/pk_wrap.c -o build/k/third_party/mbedtls/library/pk_wrap.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.2274938Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/pkparse.c -o build/k/third_party/mbedtls/library/pkparse.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.2424620Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/platform.c -o build/k/third_party/mbedtls/library/platform.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.2844555Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/platform_util.c -o build/k/third_party/mbedtls/library/platform_util.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.3064852Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/poly1305.c -o build/k/third_party/mbedtls/library/poly1305.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.3406912Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/rsa.c -o build/k/third_party/mbedtls/library/rsa.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.4435202Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/rsa_alt_helpers.c -o build/k/third_party/mbedtls/library/rsa_alt_helpers.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.4615162Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/sha1.c -o build/k/third_party/mbedtls/library/sha1.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.4734853Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/sha256.c -o build/k/third_party/mbedtls/library/sha256.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.5895132Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/sha512.c -o build/k/third_party/mbedtls/library/sha512.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.7605236Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ssl_ciphersuites.c -o build/k/third_party/mbedtls/library/ssl_ciphersuites.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.8574975Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ssl_client.c -o build/k/third_party/mbedtls/library/ssl_client.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:38.8834912Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ssl_msg.c -o build/k/third_party/mbedtls/library/ssl_msg.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.0345157Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ssl_tls.c -o build/k/third_party/mbedtls/library/ssl_tls.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.0995225Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/ssl_tls12_client.c -o build/k/third_party/mbedtls/library/ssl_tls12_client.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.2026178Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/x509.c -o build/k/third_party/mbedtls/library/x509.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.3045103Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -w -MMD -MP -c third_party/mbedtls/library/x509_crt.c -o build/k/third_party/mbedtls/library/x509_crt.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.3883488Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard   -g -Isrc -Isrc/kernel -Wa,-Ibuild -c chainloader/start.S -o build/l/chainloader/start.S.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.4004059Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c chainloader/main.c -o build/l/chainloader/main.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.4544340Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c chainloader/string.c -o build/l/chainloader/string.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.4844387Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/drivers/uart.c -o build/l/src/drivers/uart.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.5644342Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/drivers/gpio.c -o build/l/src/drivers/gpio.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.6014175Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/drivers/mbox.c -o build/l/src/drivers/mbox.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.6304062Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/drivers/prop.c -o build/l/src/drivers/prop.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.6824663Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/drivers/timer.c -o build/l/src/drivers/timer.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.7124419Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/drivers/led.c -o build/l/src/drivers/led.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.7583362Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/drivers/board.c -o build/l/src/drivers/board.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.7624184Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/arch/cache.c -o build/l/src/arch/cache.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.7984144Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -MMD -MP -c src/lib/crc32.c -o build/l/src/lib/crc32.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8272644Z python3 scripts/mkbm.py -o build/carts/pong.bm --lua carts/pong/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8296358Z     --info carts/pong/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8315409Z     --cover carts/pong/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8352383Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8362933Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8363467Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8364036Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8364509Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8364963Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8393203Z python3 scripts/mkbm.py -o build/carts/snake.bm --lua carts/snake/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8394103Z     --info carts/snake/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8394842Z     --cover carts/snake/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8395407Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8395846Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8396269Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8396694Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8397114Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8397525Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8942920Z python3 scripts/mkbm.py -o build/carts/shooter.bm --lua carts/shooter/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.8973983Z     --info carts/shooter/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9032276Z     --cover carts/shooter/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9062082Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9092108Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9122069Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9142197Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9162117Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9162460Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9385847Z build/carts/pong.bm: 40396 bytes (Pong, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9482586Z build/carts/snake.bm: 38580 bytes (Snake, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9512921Z python3 scripts/mkbm.py -o build/carts/astrowing.bm --lua carts/astrowing/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9513728Z     --info carts/astrowing/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9572306Z     --cover carts/astrowing/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9573061Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9602344Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9602733Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9662224Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9662614Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9662961Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9663565Z python3 scripts/mkbm.py -o build/carts/hunt.bm --lua carts/hunt/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9664332Z     --info carts/hunt/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9664857Z     --cover carts/hunt/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9665397Z     --sheet carts/hunt/assets/sheet.png  qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9665962Z     --map carts/hunt/assets/map.csv qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9666456Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9666782Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9667102Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:39.9667418Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0012738Z build/carts/shooter.bm: 43368 bytes (Star Shooter, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0083335Z python3 carts/kitchen/build/build.py build/kitchen/main.lua --map build/kitchen/main.map
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0192996Z python3 carts/titan/build/build.py build/titan/main.lua --map build/titan/main.map
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0492575Z build/carts/astrowing.bm: 54672 bytes (Astro Wing, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0583016Z python3 scripts/mkbm.py -o build/carts/village.bm --lua carts/village/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0584188Z     --info carts/village/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0586672Z     --cover carts/village/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0587343Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0587846Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0588664Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0589196Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0589649Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0590189Z     --models carts/village/assets/models.bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0591165Z python3 carts/nano8/build/build.py build/nano8/main.lua --map build/nano8/main.map
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0907007Z cc -O2 -std=c11 -Wall -Isrc -o build/host/mappvs tools/mappvs.c src/bm/r3d.c src/bm/gfx16.c src/bm/format.c qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.0909808Z     src/lib/crc32.c -lm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1099117Z python3 carts/overbit/art/sounds.py build/overbit/sounds.json
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1169504Z build/carts/hunt.bm: 255524 bytes (Hunter's Night, 320x180)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1255859Z python3 scripts/mkbm.py -o build/carts/yharnam.bm --lua carts/yharnam/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1257614Z     --info carts/yharnam/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1258459Z     --cover carts/yharnam/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1259245Z     --sheet carts/yharnam/assets/sheet.png --sheet8 qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1261536Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1263319Z     --map ground=carts/yharnam/assets/map_ground.csv --map overlay=carts/yharnam/assets/map_overlay.csv --map objects=carts/yharnam/assets/map_objects.csv qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1267517Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1268019Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1268486Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1806999Z build/carts/village.bm: 80572 bytes (Studio Village, 320x180)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1890847Z python3 scripts/mkbm.py -o build/carts/typing.bm --lua carts/typing/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1893875Z     --info carts/typing/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1894726Z     --cover carts/typing/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1896710Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1897300Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1897836Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1898354Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1899023Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.1899970Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2823671Z build/carts/typing.bm: 48556 bytes (Pad Typing, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2897589Z python3 scripts/mkbm.py -o build/carts/write.bm --lua carts/write/main.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2899505Z     --info carts/write/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2900283Z     --cover carts/write/cover.png qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2900989Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2902009Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2902654Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2903201Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2903746Z      qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.2904284Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.3747228Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard  -std=c11 -O2 -Wall -Wextra -g -Isrc -ffunction-sections -fdata-sections -DUART_BAUD=115200   -D_DEFAULT_SOURCE -Ithird_party/lua -Ithird_party/lwip/src/include -Isrc/net -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"'  -DBM_VERSION="v0.2.5-703-g792554f5" -DBM_BRANCH="claude/ci-repair" -MMD -MP -c src/kernel/version.c -o build/k/src/kernel/version.c.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.3943035Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard   -g -Isrc -Isrc/kernel -Wa,-Ibuild -c src/script/embed.S -o build/k/src/script/embed.S.o
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.4002470Z build/carts/write.bm: 87400 bytes (bm Write, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.4050744Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard -nostartfiles -Wl,--gc-sections -T chainloader/linker.ld -Wl,-Map=build/chainloader.map build/l/chainloader/start.S.o build/l/chainloader/main.c.o build/l/chainloader/string.c.o build/l/src/drivers/uart.c.o build/l/src/drivers/gpio.c.o build/l/src/drivers/mbox.c.o build/l/src/drivers/prop.c.o build/l/src/drivers/timer.c.o build/l/src/drivers/led.c.o build/l/src/drivers/board.c.o build/l/src/arch/cache.c.o build/l/src/lib/crc32.c.o -nostdlib -lgcc -o build/chainloader.elf
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.4107611Z python3 scripts/mkbm.py -o build/carts/kitchen.bm --lua build/kitchen/main.lua --info carts/kitchen/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.4109824Z     --sheet carts/kitchen/assets/sheet.png --cover carts/kitchen/cover.png
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.4160481Z python3 scripts/mkbm.py -o build/carts/titan.bm --lua build/titan/main.lua --info carts/titan/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.4162542Z     --sheet carts/titan/assets/sheet.png --sheet8 --cover carts/titan/cover.png
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.6792592Z build/carts/kitchen.bm: 876528 bytes (Chaos Kitchen, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.6879839Z python3 scripts/mkbm.py -o build/carts/nano8.bm --lua build/nano8/main.lua --info carts/nano8/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.6892520Z     --cover carts/nano8/cover.png
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.7973927Z build/carts/nano8.bm: 94568 bytes (nano8, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:40.8140077Z arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard -nostartfiles -Wl,--gc-sections -T linker.ld -Wl,-Map=build/kernel.map build/k/src/lib/crc32.c.o build/k/src/lib/printf.c.o build/k/src/lib/syscalls.c.o build/k/src/arch/cache.c.o build/k/src/arch/fiber.S.o build/k/src/arch/mmu.c.o build/k/src/ai/mesh.c.o build/k/src/ai/music.c.o build/k/src/ai/sprite.c.o build/k/src/ai/assist.c.o build/k/src/ai/lua_music.c.o build/k/src/ai/mesh_chars.c.o build/k/src/ai/mesh_script.c.o build/k/src/ai/nn.c.o build/k/src/ai/text.c.o build/k/src/ai/music_net.c.o build/k/src/ai/lua_ai.c.o build/k/src/ai/net.c.o build/k/src/usb/hid.c.o build/k/src/usb/smsc95xx.c.o build/k/src/usb/usb.c.o build/k/src/usb/dwc2.c.o build/k/src/wifi/wifi.c.o build/k/src/wifi/sdio.c.o build/k/src/gfx/font6x12.c.o build/k/src/gfx/console.c.o build/k/src/gfx/draw.c.o build/k/src/gfx/font8x16.c.o build/k/src/gfx/font8x14.c.o build/k/src/kernel/crumbs.c.o build/k/src/kernel/input.c.o build/k/src/kernel/bench.c.o build/k/src/kernel/fiber.c.o build/k/src/kernel/testpattern.c.o build/k/src/kernel/icons.c.o build/k/src/kernel/version.c.o build/k/src/kernel/upload.c.o build/k/src/kernel/libview.c.o build/k/src/kernel/pager.c.o build/k/src/kernel/pointer.c.o build/k/src/kernel/menu_ui.c.o build/k/src/kernel/prompts.c.o build/k/src/kernel/splash.c.o build/k/src/kernel/update.c.o build/k/src/kernel/b3dpi.c.o build/k/src/kernel/tick.c.o build/k/src/kernel/carts.c.o build/k/src/kernel/pmu.c.o build/k/src/kernel/demo.c.o build/k/src/kernel/vectors.S.o build/k/src/kernel/lib.c.o build/k/src/kernel/config.c.o build/k/src/kernel/sysinfo.c.o build/k/src/kernel/irq.c.o build/k/src/kernel/devtool.c.o build/k/src/kernel/selftest.c.o build/k/src/kernel/market.c.o build/k/src/kernel/dmatest.c.o build/k/src/kernel/exceptions.c.o build/k/src/kernel/logo_data.c.o build/k/src/kernel/publish.c.o build/k/src/kernel/monitor.c.o build/k/src/kernel/gputest.c.o build/k/src/kernel/notice.c.o build/k/src/kernel/settings.c.o build/k/src/kernel/reports.c.o build/k/src/kernel/ledstate.c.o build/k/src/kernel/main.c.o build/k/src/kernel/home.c.o build/k/src/kernel/syskeys.c.o build/k/src/audio/iec958.c.o build/k/src/audio/player.c.o build/k/src/audio/lua_tone.c.o build/k/src/audio/n8snd.c.o build/k/src/audio/synth.c.o build/k/src/audio/presets.c.o build/k/src/audio/audio.c.o build/k/src/audio/hdmi_audio.c.o build/k/src/gpu/v3d_cl.c.o build/k/src/gpu/v3d.c.o build/k/src/gpu/etc1.c.o build/k/src/gpu/gpu3d.c.o build/k/src/drivers/watchdog.c.o build/k/src/drivers/sd_emmc.c.o build/k/src/drivers/board.c.o build/k/src/drivers/sdhost.c.o build/k/src/drivers/rng.c.o build/k/src/drivers/dma.c.o build/k/src/drivers/led.c.o build/k/src/drivers/timer.c.o build/k/src/drivers/gpio.c.o build/k/src/drivers/uart.c.o build/k/src/drivers/prop.c.o build/k/src/drivers/fb.c.o build/k/src/drivers/mbox.c.o build/k/src/drivers/sd.c.o build/k/src/fs/fat.c.o build/k/src/bm/png.c.o build/k/src/bm/meshcap.c.o build/k/src/bm/profile.c.o build/k/src/bm/n8cart.c.o build/k/src/bm/b3d.c.o build/k/src/bm/roombench.c.o build/k/src/bm/n8font.c.o build/k/src/bm/stress.c.o build/k/src/bm/json.c.o build/k/src/bm/format.c.o build/k/src/bm/gfx16.c.o build/k/src/bm/loading_logo.c.o build/k/src/bm/jpeg.c.o build/k/src/bm/decimate.c.o build/k/src/bm/project.c.o build/k/src/bm/r3d.c.o build/k/src/bm/loading.c.o build/k/src/bm/world3d.c.o build/k/src/bm/glb.c.o build/k/src/bm/runtime.c.o build/k/src/bm/shot.c.o build/k/src/bm/tokens.c.o build/k/src/bm/n8.c.o build/k/src/bm/cutout.c.o build/k/src/bm/require.c.o build/k/src/bm/map3d.c.o build/k/src/bm/n8lua.c.o build/k/src/boot/start.S.o build/k/src/net/github.c.o build/k/src/net/tls.c.o build/k/src/net/cartnet.c.o build/k/src/net/report.c.o build/k/src/net/release.c.o build/k/src/net/stream.c.o build/k/src/net/img3d.c.o build/k/src/net/http.c.o build/k/src/net/lan.c.o build/k/src/net/http_kernel.c.o build/k/src/net/catalog.c.o build/k/src/net/netxfer.c.o build/k/src/net/wifi_auto.c.o build/k/src/net/netcon.c.o build/k/src/net/net.c.o build/k/src/script/luavm.c.o build/k/src/script/repl.c.o build/k/src/script/lib_bm.c.o build/k/src/script/embed.S.o build/k/src/bt/ble.c.o build/k/src/bt/btuart.c.o build/k/src/bt/rtlbt.c.o build/k/src/bt/smp_crypto.c.o build/k/src/bt/bt.c.o build/k/src/bt/h5.c.o build/k/src/bt/hci.c.o build/k/third_party/lua/lapi.c.o build/k/third_party/lua/lauxlib.c.o build/k/third_party/lua/lbaselib.c.o build/k/third_party/lua/lcode.c.o build/k/third_party/lua/lcorolib.c.o build/k/third_party/lua/lctype.c.o build/k/third_party/lua/ldblib.c.o build/k/third_party/lua/ldebug.c.o build/k/third_party/lua/ldo.c.o build/k/third_party/lua/ldump.c.o build/k/third_party/lua/lfunc.c.o build/k/third_party/lua/lgc.c.o build/k/third_party/lua/linit.c.o build/k/third_party/lua/liolib.c.o build/k/third_party/lua/llex.c.o build/k/third_party/lua/lmathlib.c.o build/k/third_party/lua/lmem.c.o build/k/third_party/lua/loadlib.c.o build/k/third_party/lua/lobject.c.o build/k/third_party/lua/lopcodes.c.o build/k/third_party/lua/loslib.c.o build/k/third_party/lua/lparser.c.o build/k/third_party/lua/lstate.c.o build/k/third_party/lua/lstring.c.o build/k/third_party/lua/lstrlib.c.o build/k/third_party/lua/ltable.c.o build/k/third_party/lua/ltablib.c.o build/k/third_party/lua/ltm.c.o build/k/third_party/lua/lundump.c.o build/k/third_party/lua/lutf8lib.c.o build/k/third_party/lua/lvm.c.o build/k/third_party/lua/lzio.c.o build/k/third_party/lwip/src/core/altcp.c.o build/k/third_party/lwip/src/core/altcp_alloc.c.o build/k/third_party/lwip/src/core/altcp_tcp.c.o build/k/third_party/lwip/src/core/def.c.o build/k/third_party/lwip/src/core/dns.c.o build/k/third_party/lwip/src/core/inet_chksum.c.o build/k/third_party/lwip/src/core/init.c.o build/k/third_party/lwip/src/core/ip.c.o build/k/third_party/lwip/src/core/mem.c.o build/k/third_party/lwip/src/core/memp.c.o build/k/third_party/lwip/src/core/netif.c.o build/k/third_party/lwip/src/core/pbuf.c.o build/k/third_party/lwip/src/core/raw.c.o build/k/third_party/lwip/src/core/stats.c.o build/k/third_party/lwip/src/core/sys.c.o build/k/third_party/lwip/src/core/tcp.c.o build/k/third_party/lwip/src/core/tcp_in.c.o build/k/third_party/lwip/src/core/tcp_out.c.o build/k/third_party/lwip/src/core/timeouts.c.o build/k/third_party/lwip/src/core/udp.c.o build/k/third_party/lwip/src/core/ipv4/acd.c.o build/k/third_party/lwip/src/core/ipv4/autoip.c.o build/k/third_party/lwip/src/core/ipv4/dhcp.c.o build/k/third_party/lwip/src/core/ipv4/etharp.c.o build/k/third_party/lwip/src/core/ipv4/icmp.c.o build/k/third_party/lwip/src/core/ipv4/igmp.c.o build/k/third_party/lwip/src/core/ipv4/ip4.c.o build/k/third_party/lwip/src/core/ipv4/ip4_addr.c.o build/k/third_party/lwip/src/core/ipv4/ip4_frag.c.o build/k/third_party/lwip/src/netif/ethernet.c.o build/k/third_party/lwip/src/apps/sntp/sntp.c.o build/k/third_party/mbedtls/library/aes.c.o build/k/third_party/mbedtls/library/asn1parse.c.o build/k/third_party/mbedtls/library/asn1write.c.o build/k/third_party/mbedtls/library/base64.c.o build/k/third_party/mbedtls/library/bignum.c.o build/k/third_party/mbedtls/library/bignum_core.c.o build/k/third_party/mbedtls/library/chacha20.c.o build/k/third_party/mbedtls/library/chachapoly.c.o build/k/third_party/mbedtls/library/cipher.c.o build/k/third_party/mbedtls/library/cipher_wrap.c.o build/k/third_party/mbedtls/library/constant_time.c.o build/k/third_party/mbedtls/library/ctr_drbg.c.o build/k/third_party/mbedtls/library/ecdh.c.o build/k/third_party/mbedtls/library/ecdsa.c.o build/k/third_party/mbedtls/library/ecp.c.o build/k/third_party/mbedtls/library/ecp_curves.c.o build/k/third_party/mbedtls/library/entropy.c.o build/k/third_party/mbedtls/library/error.c.o build/k/third_party/mbedtls/library/gcm.c.o build/k/third_party/mbedtls/library/md.c.o build/k/third_party/mbedtls/library/oid.c.o build/k/third_party/mbedtls/library/pem.c.o build/k/third_party/mbedtls/library/pk.c.o build/k/third_party/mbedtls/library/pk_ecc.c.o build/k/third_party/mbedtls/library/pk_wrap.c.o build/k/third_party/mbedtls/library/pkparse.c.o build/k/third_party/mbedtls/library/platform.c.o build/k/third_party/mbedtls/library/platform_util.c.o build/k/third_party/mbedtls/library/poly1305.c.o build/k/third_party/mbedtls/library/rsa.c.o build/k/third_party/mbedtls/library/rsa_alt_helpers.c.o build/k/third_party/mbedtls/library/sha1.c.o build/k/third_party/mbedtls/library/sha256.c.o build/k/third_party/mbedtls/library/sha512.c.o build/k/third_party/mbedtls/library/ssl_ciphersuites.c.o build/k/third_party/mbedtls/library/ssl_client.c.o build/k/third_party/mbedtls/library/ssl_msg.c.o build/k/third_party/mbedtls/library/ssl_tls.c.o build/k/third_party/mbedtls/library/ssl_tls12_client.c.o build/k/third_party/mbedtls/library/x509.c.o build/k/third_party/mbedtls/library/x509_crt.c.o -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o build/kernel.elf
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:41.0582812Z arm-none-eabi-objcopy -O binary build/chainloader.elf build/chainloader.img
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:41.0642738Z build/chainloader.img: 3932 bytes
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:41.0662768Z arm-none-eabi-objcopy -O binary build/kernel.elf build/kernel.img
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:41.0753193Z build/kernel.img: 4347272 bytes
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:43.1505161Z python3 carts/overbit/art/models.py build/overbit/models.bm --map build/overbit/21_map.lua --pvs build/host/mappvs qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:43.1506468Z     
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.1921028Z build/carts/titan.bm: 1713484 bytes (Titan Clash, 640x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7658657Z pt: 4122 triangles, 674 hidden
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7659273Z   3816 triangles with edges up to 12.0 m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7659794Z   3816 triangles after refining
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7660259Z   249 pictures
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7661052Z   100 models, 5328 faces
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7661603Z   visibility: 24 x 14 squares, 2320 points, 43.8 of 96 chunks seen on average
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7662505Z   nav: 75 nodes, 500 links
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7886370Z python3 carts/overbit/build/build.py build/overbit/main.lua --map build/overbit/main.map --extra build/overbit/21_map.lua qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.7887357Z     --define OVERBIT_TV_BM=true
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.8298475Z python3 scripts/mkbm.py -o build/carts/overbit.bm --lua build/overbit/main.lua --info carts/overbit/bm.info qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.8299622Z     --models build/overbit/models.bm --audio build/overbit/sounds.json qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.8300336Z     --cover carts/overbit/cover.png
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:01:54.9351912Z build/carts/overbit.bm: 2374196 bytes (Overbit, 480x270)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.0432956Z build/carts/yharnam.bm: 6411612 bytes (Yharnam, 360x360)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.0593715Z ##[group]Run make test-qemu SHARD=3/4
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.0594105Z ^[[36;1mmake test-qemu SHARD=3/4^[[0m
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.0659083Z shell: /usr/bin/bash -e {0}
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.0659423Z env:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.0659673Z   ZERO2: 0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.0659939Z ##[endgroup]
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:08.2965232Z cc -O2 -Wall -Wextra -Isrc -o build/host/meshview tests/ai/meshview.c src/ai/assist.c src/ai/nn.c src/ai/text.c src/ai/sprite.c src/ai/mesh.c src/ai/mesh_chars.c src/ai/mesh_script.c src/ai/music.c src/ai/music_net.c src/lib/crc32.c -lm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:11.6135130Z python3 tests/qemu_test.py --build build --shard 3/4
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:11.7412036Z group 3 of 4: 24 tests, about 6 min on the CI
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:13.7486005Z PASS test_exc_prefetch_abort (2.0s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:16.2537013Z PASS test_lua_repl (2.5s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:38.1918394Z PASS test_bm_boot_demo (21.9s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:02:50.5605376Z PASS test_keys_help (12.4s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:03:01.2572166Z PASS test_sd_cartridges (10.7s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:04:11.7084806Z PASS test_home_ui (70.5s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:04:13.7287524Z PASS test_old_folder_and_carts (2.0s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:04:26.4688472Z     lag after burst: 0.11 s
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:04:26.4689102Z PASS test_bt_pair_and_reconnect (12.7s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:04:26.4690303Z     skipped: pip install pycryptodome
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:04:26.4690924Z PASS test_bt_keyboard (0.0s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:04:42.5644902Z PASS test_publish (16.1s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:05:00.9185388Z PASS test_usb_mouse (18.4s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:05:32.6111146Z picture: the cutout hero made on the console (28 triangles, red), saved; the service's message
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:05:32.6112245Z PASS test_picture_model (31.7s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:05:50.9869265Z PASS test_pixel (18.4s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:14.4811028Z PASS test_pixel_big (23.5s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:17.2162584Z PASS test_menu_scale (2.7s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:18.6194116Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:18.6194636Z [bm-load] sending 87400 bytes, crc32 69285d61
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:19.7854093Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:19.7854659Z [bm-load] done in 1.1s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:26.3094294Z PASS test_bm_write (9.1s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:40.5594090Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:40.5594643Z [bm-load] sending 40564 bytes, crc32 973843e0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:41.0943932Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:41.0944445Z [bm-load] done in 0.5s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:06:43.1112945Z PASS test_sound_editor (16.8s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:04.8262379Z PASS test_room_bench (21.7s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:06.2182111Z PASS test_gpu_absent (1.4s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:07.5583503Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:07.5584060Z [bm-load] sending 1100 bytes, crc32 ebd022eb
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:07.6524530Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:07.6525135Z [bm-load] done in 0.1s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:07.7145195Z PASS test_models (1.5s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:18.0945995Z PASS test_debugger (10.4s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8563427Z FAIL test_editor_mouse
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8573550Z Traceback (most recent call last):
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8589638Z   File "/home/runner/work/bm/bm/tests/qemu_test.py", line 7725, in main
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8591300Z     fn(b, opts)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8592582Z   File "/home/runner/work/bm/bm/tests/qemu_test.py", line 6533, in test_editor_mouse
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8593631Z     cx, cy = find(text[:1], "code", 8, 16)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8594159Z              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8594854Z   File "/home/runner/work/bm/bm/tests/qemu_test.py", line 6478, in find
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8595810Z     raise AssertionError(f"{word!r} not on screen:
" + "
".join(text))
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8596584Z AssertionError: 'code' not on screen:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:41.8596946Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.2014633Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.2015186Z [bm-load] sending 184 bytes, crc32 ed370c0c
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.2853365Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.2854174Z [bm-load] done in 0.0s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.7866093Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.7866691Z [bm-load] sending 184 bytes, crc32 7ac39a55
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.8703589Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:43.8704201Z [bm-load] done in 0.0s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.1457877Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.1458812Z [bm-load] sending 164 bytes, crc32 3e3e79e6
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.2313869Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.2314341Z [bm-load] done in 0.0s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.7336668Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.7337093Z [bm-load] sending 164 bytes, crc32 39acd4c8
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.8194132Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:45.8194565Z [bm-load] done in 0.0s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.3218196Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.3218655Z [bm-load] sending 252 bytes, crc32 794ef0f6
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.4093969Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.4094436Z [bm-load] done in 0.0s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.4503375Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.4504025Z [bm-load] sending 200 bytes, crc32 6f88e75c
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.5363912Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:46.5364466Z [bm-load] done in 0.0s, kernel starting
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:07:48.2259332Z PASS test_bm_upload_errors (6.4s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.7992935Z PASS test_stress_monitor (49.6s)
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.8000981Z 
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.8001173Z 23/24 passed
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.8089325Z make: *** [Makefile:843: test-qemu] Error 1
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.8108286Z ##[error]Process completed with exit code 2.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.8269171Z Post job cleanup.
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9167528Z [command]/usr/bin/git version
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9208118Z git version 2.55.0
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9255543Z Temporarily overriding HOME='/home/runner/work/_temp/f60a2491-3d96-4ba8-b50a-8c15d0572408' before making global git config changes
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9257783Z Adding repository directory to the temporary git global config as a safe directory
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9262763Z [command]/usr/bin/git config --global --add safe.directory /home/runner/work/bm/bm
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9302834Z [command]/usr/bin/git config --local --name-only --get-regexp core\.sshCommand
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9338455Z [command]/usr/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'core\.sshCommand' && git config --local --unset-all 'core.sshCommand' || :"
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9592977Z [command]/usr/bin/git config --local --name-only --get-regexp http\.https\:\/\/github\.com\/\.extraheader
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9621517Z http.https://github.com/.extraheader
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9633972Z [command]/usr/bin/git config --local --unset-all http.https://github.com/.extraheader
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9668752Z [command]/usr/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'http\.https\:\/\/github\.com\/\.extraheader' && git config --local --unset-all 'http.https://github.com/.extraheader' || :"
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9934880Z [command]/usr/bin/git config --local --name-only --get-regexp ^includeIf\.gitdir:
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:37.9974796Z [command]/usr/bin/git submodule foreach --recursive git config --local --show-origin --name-only --get-regexp remote.origin.url
qemu (3/4)	UNKNOWN STEP	2026-10-15T10:08:38.0394704Z Cleaning up orphan processes trova il test che fallisce in ciascuno dei due job (nel log host compaiono anche messaggi rumorosi non fatali, es. ,  di test di rete: distingui i veri fallimenti). Per ognuno: riproduzione locale (), causa, fix solo se chiaro e nel perimetro CI (test o Makefile con righe nominate); altrimenti scrivi la causa in Problemi e fermati.  è in 3/4: se è lui, probabile QEMU 8.2 vs 10.2.1 (confronta con il log CI, controlla il passo di versione).
- Verifica: test selettivi locali; poi push di  e run CI. Consegna: DELIVERED standard con SHA e stato dei job. Non invocare bm-deep (WAITING + scrivi a SPV).

## CI-AUDIT — `scripts/ci_audit.py` (bm-coder, dopo CI-1 e D-MON)
- Nuovo `scripts/ci_audit.py`, solo stdlib e PyYAML, exit 1 se trova problemi. Controlli: 1) ogni job di `ci.yml` ha `timeout-minutes` e il workflow ha `concurrency`; 2) `on.push` ignora `sync/**`; 3) in `tests/qemu_test.py` e `tests/rgb30/qemu_test.py` segnala `expect(..., timeout=T)` con T minore di un'attesa nota in una tabella interna `KNOWN_WAITS = {"stress settle": 19}`; 4) elenca i test che leggono `build/carts/<gioco>.bm` con i giochi da `Makefile` `GAMES`; 5) con `--gh`: `gh run list -w ci.yml -L 20 --json conclusion,createdAt,updatedAt` per durata media e quota di successi (senza gh avvisa e salta).
- `CLAUDE.md` §5: aggiungere il punto 5: "se tocchi `.github/workflows/` o i timeout dei test, esegui `python scripts/ci_audit.py`".
- Verifica: `python scripts/ci_audit.py` → exit 0 dopo CI-1 e D-MON; su una copia di `ci.yml` con un timeout tolto → exit 1; `python scripts/docs_audit.py score` senza regressioni.

## Non verificabile senza la CI reale (serve un push del branch di lavoro)
Che un push su `sync/*` non lanci run; che la cancellazione funzioni e non tocchi i tag `v*`; la versione di QEMU su ubuntu-24.04; i timeout effettivi; i fallimenti di D-SCREEN, D-RGB, D-LOAD se in locale passano; il primo run verde.
