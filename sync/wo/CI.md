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

## CI-AUDIT — `scripts/ci_audit.py` (bm-coder, dopo CI-1 e D-MON)
- Nuovo `scripts/ci_audit.py`, solo stdlib e PyYAML, exit 1 se trova problemi. Controlli: 1) ogni job di `ci.yml` ha `timeout-minutes` e il workflow ha `concurrency`; 2) `on.push` ignora `sync/**`; 3) in `tests/qemu_test.py` e `tests/rgb30/qemu_test.py` segnala `expect(..., timeout=T)` con T minore di un'attesa nota in una tabella interna `KNOWN_WAITS = {"stress settle": 19}`; 4) elenca i test che leggono `build/carts/<gioco>.bm` con i giochi da `Makefile` `GAMES`; 5) con `--gh`: `gh run list -w ci.yml -L 20 --json conclusion,createdAt,updatedAt` per durata media e quota di successi (senza gh avvisa e salta).
- `CLAUDE.md` §5: aggiungere il punto 5: "se tocchi `.github/workflows/` o i timeout dei test, esegui `python scripts/ci_audit.py`".
- Verifica: `python scripts/ci_audit.py` → exit 0 dopo CI-1 e D-MON; su una copia di `ci.yml` con un timeout tolto → exit 1; `python scripts/docs_audit.py score` senza regressioni.

## Non verificabile senza la CI reale (serve un push del branch di lavoro)
Che un push su `sync/*` non lanci run; che la cancellazione funzioni e non tocchi i tag `v*`; la versione di QEMU su ubuntu-24.04; i timeout effettivi; i fallimenti di D-SCREEN, D-RGB, D-LOAD se in locale passano; il primo run verde.
