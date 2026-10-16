# APP-8 - SDK-KB-STABLE: i test QEMU RGB30 della SDK da tastiera sono stabili e si chiamano per quello che fanno (`test_sdk_kb_*`, un solo Su nel test Stages)
Assegnato a B16 (16 ott), SOLO DOPO `DELIVERED AGY-15` e `REVIEW: APPROVED` (stesso file, `tests/rgb30/qemu_test.py`: perimetri non disgiunti, quindi AGY-15 va prima). Nasce dalla consegna di APP-6h: `test_sdk_stages_build` era instabile sotto carico (8 Giù con `sleep(0.4)` ciascuno, 3,2 s di attese cieche, timeout portato a 30 s) e i 7 test `test_sdk_pad_*` pilotano la SDK dalla tastiera della seriale (`sdk_kb_run`), non dal pad: il nome mente.
Base: `claude/sdk-b16-dev-without-pc` con AGY-15 integrato (`claude/rgb30-test-tmp-cleanup` @ `a118467a` è il suo commit). SPV scrive nell'avvio se è già un merge nel branch di base; altrimenti: `git worktree add -b claude/sdk-kb-tests-stable C:\Users\Utente\Documents\GitHub\bm33-b16app8 claude/rgb30-test-tmp-cleanup`. Worktree: `C:\Users\Utente\Documents\GitHub\bm33-b16app8`. Branch: `claude/sdk-kb-tests-stable`. Mai push su bm-core; push solo di questo branch.
Perimetro: `tests/rgb30/qemu_test.py` (le righe del ciclo dei Giù in `test_sdk_stages_build`; i nomi delle 7 funzioni `test_sdk_pad_*` e le loro occorrenze nel file), `docs/progress/tools.md` (solo le righe 23 e 86).
NON toccare: nessun altro file; in `tests/rgb30/qemu_test.py` il resto di AGY-15 (gli helper di pulizia, `Qemu.close`, `main()`), `sdk_kb_run` (nome e corpo: è già giusto), i docstring dei test salvo il nome, i timeout; `tests/qemu_test.py` (Pi), `carts/*`, `src/**`, `sync/*`, `SYNC.md`.
Vincoli: solo QEMU virt (`not verified on hardware`: la console reale ha il pad, la tastiera della seriale non prova nulla del pad). Il tuo worktree NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120`. Il `/tmp` di WSL è condiviso e piccolo (3,9 GB): controlla `df -h /tmp | tail -1; ls -d /tmp/bm64* 2>/dev/null | wc -l` prima e dopo ogni giro; con AGY-15 il secondo numero deve restare 0. Ogni `file:riga` qui sotto è stato aperto dal planner su `claude/sdk-b16-dev-without-pc` @ `3db6a92c` e sul branch di AGY-15 (le righe sono spostate di ~16 dopo AGY-15): PRIMA di modificare ri-trovalo con `git grep -n` (comandi indicati) e non fidarti del numero; ciò che non ritrovi come descritto: `non verificato` e `BLOCKED`.
Obiettivo: `python tests/rgb30/qemu_test.py --build build/rgb30-virt -k sdk_` esce 0 per tre volte di fila, e nessun nome di test dice "pad" per un test che usa la tastiera.

## Stato reale (letto dal planner)
- Il ciclo da correggere in `test_sdk_stages_build` (`tests/rgb30/qemu_test.py:620-622` sulla base di `3db6a92c`; `:636-638` sul branch di AGY-15):
```
        for _ in range(8):                      # down (arrow) to the ninth row
            q.send("\x1b[B")
            time.sleep(0.4)
```
  Il menu "new project from a template" è `D.choose` di `carts/sdk_b16/main.lua`: il giro è alla 1441 (`if k == "up" then pick.sel = (pick.sel - 2) % n + 1`) e alla 1442 per Giù. Con n = 9 voci e la selezione sulla prima riga, UN Su porta alla nona ("Stages 2D"): (1 - 2) % 9 + 1 = 9. Il test Pi `test_sdk_stages_pi` (APP-7b) fa già così con il `D.choose` di `carts/editor/main.lua:951`. Conclusione del planner: la correzione è giusta; `BLOCKED` solo se il punto 1 del passo 1 dice altro.
- Prima del ciclo il test aspetta `screen(["new project from a template", "Stages 2D"])` (la voce è nella lista, quindi n = 9 e la selezione parte dalla prima). Dopo: `q.send("\r")`, `screen(["new project (Stages 2D)"])`.
- I 7 test `test_sdk_pad_*` (`:988-1140` su `3db6a92c`, `:1004-1170` circa dopo AGY-15): `sprite`, `map`, `code`, `sound`, `lib`, `sfx`, `build`. Tutti passano per `sdk_kb_run(b, drive, check)` (`:988` dopo AGY-15), che apre la SDK e manda tasti della seriale (F3, spazio, `w s a d`...). Nessun altro file del repo li chiama per nome: `git grep -n "sdk_pad"` dà solo `tests/rgb30/qemu_test.py` (7 `def`, più eventuali citazioni nei docstring) e `docs/progress/tools.md:23` e `:86` (`QEMU \`test_sdk_pad_*\``). `.github/workflows/ci.yml`, `Makefile`, `rgb30.mk` filtrano per `-k sdk_` o niente: non citano `sdk_pad`.
- Come si costruisce la cartella di build: `make -f rgb30.mk PLAT=virt all` produce `build/rgb30-virt` (`rgb30.mk:19` `BUILD := build/rgb30-virt`; usata così dai WO CI e API-1). Se un test chiede una cartuccia di `build/carts/` che manca: `non verificato`, riporta il nome e `BLOCKED`.

## APP-8a - le tre modifiche (bm-coder, 15 min; più 3 giri di test)
Passo 0: ambiente. Crea il worktree (riga "Base"). Build: `scripts/wsl-run.ps1 'make -f rgb30.mk PLAT=virt all > build/rgb30.log 2>&1; echo EXIT=$?' -Tail 120` (la prima volta 10-20 minuti: `run_in_background` e leggi `build/rgb30.log`). Atteso `EXIT=0` e `build/rgb30-virt/kernel.elf` presente.
Passo 1 (verifica prima di cambiare): controlla il punto 1 con `git grep -n 'pick.sel - 2' -- carts/sdk_b16/main.lua` (deve dare la riga del `Su` di `D.choose`, `(pick.sel - 2) % n + 1`). Se è diverso (niente giro): `BLOCKED: D.choose non fa il giro` e fermati.
Passo 2: il ciclo. Trova `git grep -n 'for _ in range(8):' -- tests/rgb30/qemu_test.py` (UNA riga, in `test_sdk_stages_build`). Sostituisci le tre righe del ciclo con (stesso rientro di 8 spazi):
```
        q.send("\x1b[A")                        # Up: D.choose wraps from the first row to the ninth
        time.sleep(0.4)
```
  Lascia intatte le righe prima (`screen([...])`) e dopo (`q.send("\r")`). Dopo la modifica `git grep -n 'for _ in range(8)' -- tests/rgb30/qemu_test.py` non dà più nulla.
Passo 3: i nomi. Per ognuno dei 7: `sdk_pad_sprite`, `sdk_pad_map`, `sdk_pad_code`, `sdk_pad_sound`, `sdk_pad_lib`, `sdk_pad_sfx`, `sdk_pad_build` → stessa cosa con `sdk_kb_` al posto di `sdk_pad_`. Comando per trovare tutte le occorrenze: `git grep -n 'sdk_pad' -- tests/rgb30/qemu_test.py docs/progress/tools.md` e sostituisci OGNI occorrenza (le `def` e qualunque citazione nei docstring o nei commenti). `sdk_kb_run` NON si tocca. Poi `git grep -n 'sdk_pad' -- .` deve dare 0 righe fuori da `sync/` e `docs/progress/` storici (se compare in un file che non è nel perimetro: `BLOCKED: <file>`).
Passo 4: `docs/progress/tools.md`: righe 23 e 86 (ri-trova con `git grep -n 'sdk_pad' -- docs/progress/tools.md`): `test_sdk_pad_*` → `test_sdk_kb_*` (nella 23 la frase dice "QEMU `test_sdk_pad_*` drive it by keyboard": dopo la modifica dice la verità senza altre parole). Nient'altro nel file.
Passo 5: verifica, tre volte di fila, con gli occhi sul `/tmp`:
`scripts/wsl-run.ps1 'for i in 1 2 3; do df -h /tmp | tail -1; python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k sdk_ > build/sdk_run$i.log 2>&1; echo RUN$i=$?; tail -3 build/sdk_run$i.log; ls -d /tmp/bm64* 2>/dev/null | wc -l; done' -Tail 120`
Atteso: `RUN1=0`, `RUN2=0`, `RUN3=0`; ogni log finisce con `N passed, 0 failed` e contiene `PASS test_sdk_stages_build`, `PASS test_sdk_tool` e i sette `PASS test_sdk_kb_*`; l'ultimo numero di ogni giro (`/tmp/bm64*`) è 0. Il tempo di un giro (stima del planner: 5-8 minuti; `non verificato`) va nel rapporto.
  Se un giro dà FAIL: riporta il test, le ultime 20 righe e il numero del giro. Un FAIL isolato in `test_sdk_stages_build` con "want [...] on the screen" è un'attesa troppo corta: NON aumentare il timeout (è il punto dell'ordine: 30 s ci sono già); scrivi `FAILED x2` dopo il secondo tentativo con lo stesso esito e fermati.
Criterio di fine: tre `RUN=0` consecutivi; `git diff --stat` = `tests/rgb30/qemu_test.py` (circa 10 righe: il ciclo 3→2 e 7 `def` più eventuali citazioni) e `docs/progress/tools.md | 2 +-`; `python scripts/docs_audit.py score` senza `! broken` / `! orphan` nuovi.
Commit: `tests(rgb30): SDK keyboard tests named sdk_kb_*, one Up to the Stages template (APP-8)`.

## Coordinamento
- AGY-15 prima (stesso file): questo WO parte dal suo risultato. Altri WO che toccano `tests/rgb30/qemu_test.py`: API-1 (`test_budget_cart`, una funzione nuova), CI (`CI.md`: `test_el2_image_relocation` e i timeout), AGY-5 (lo cita solo fra i file da non toccare), APP-6 (consegnato). Le righe di questo WO (il ciclo di Stages, i 7 nomi) non coincidono con le loro; un conflitto di merge è di SPV.
- `tests/qemu_test.py` (Pi): non lo tocca. `docs/progress/tools.md`: AGY-23 (righe 97-99), BETA-7 (riga 80), APP-7c (righe 10-11): righe diverse dalle 23 e 86.

## Decisioni adottate (SPV, raccomandazioni del planner)
- D1 Si cambia il test, non si allunga il timeout: l'instabilità veniva da 8 attese cieche in fila, non da un timeout corto.
- D2 Il prefisso nuovo è `sdk_kb_` (tastiera della seriale), allineato a `sdk_kb_run`. I test veri da pad (il pad della RGB30 non esiste in QEMU virt) restano un lavoro a parte, `non verificato su hardware`.
- D3 Nessun alias dei vecchi nomi: `-k sdk_` continua a prendere tutti (contiene `sdk_`); i documenti si aggiornano nello stesso commit.

## Prova per l'utente
Nessuna richiesta dal WO. Facoltativa, sulla RGB30 reale (il pad non è provato da QEMU): Dev > bm SDK, Start > New project, scorrere con Su dalla prima voce: arriva a "Stages 2D".

## Consegna
Dal worktree di sync di B16 (`bm33-sync-b16`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py B16 --close APP-8 --repo C:\Users\Utente\Documents\GitHub\bm33-b16app8 --branch claude/sdk-kb-tests-stable --commit <sha> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; riporta in chat la riga `done WO APP-8 ...` stampata).
2. In `## Consegne` di `sync/B16.md`: `DELIVERED APP-8 @ claude/sdk-kb-tests-stable@<sha> · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k sdk_ x3 -> exit 0 0 0 (<N> passed ciascuno), /tmp/bm64* 0 dopo ogni giro · 2 file · non verificato su hardware (QEMU) · <ISO con fuso>`.
3. `python tools/sync_hdr.py B16 --state FREE --task "APP-8 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto). Stima: 15 minuti di modifica + 15-25 di test (3 giri) + la prima `make -f rgb30.mk` se manca.
