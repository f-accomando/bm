# AGY-16 - PERF-QEMU-CASE: `perf_table.py` rifiuta anche un report QEMU scritto in minuscolo (`board: qemu virt`)
Assegnato ad AGY (16 ott), indipendente dagli altri WO. Nasce dalla revisione del budget di prestazioni: lo script respinge un report QEMU solo se la scheda contiene `QEMU` in maiuscolo; `qemu virt` passerebbe e finirebbe nella tabella come misura su hardware.
Base: `origin/claude/perf-budget-cart` @ `c18e881d` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/perf-qemu-case C:\Users\Utente\Documents\GitHub\bm33-agy16 origin/claude/perf-budget-cart`); branch `claude/perf-qemu-case`. Mai push su bm-core; push solo di questo branch.
Perimetro: `scripts/perf_table.py` (1 riga cambiata) e `tests/tools/test_perf_table.py` (un test nuovo di 6 righe + 1 riga nell'elenco `TESTS`).
NON toccare: qualunque altro file (`docs/*`, `carts/*`, `Makefile`, `SYNC.md`, `sync/*`), il testo dei messaggi di `perf_table.py`, le righe esistenti di `test_perf_table.py` (nessun test si toglie o si allenta).
Vincoli: solo host, nessun WSL necessario (il `python` di Windows basta; WSL, se mai servisse, solo con `scripts/wsl-run.ps1 ... -Tail 120`, e `/tmp` di WSL è condiviso e piccolo: non riempirlo); `not verified on hardware`. I numeri di riga qui sotto sono stati aperti dal planner su `c18e881d` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi sotto) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `python tests/tools/test_perf_table.py` passa con 11 test (oggi 10) e un report con `board: qemu virt` viene rifiutato (exit 1, doc invariato).

## Stato reale (verificato dal planner: ho applicato la modifica su una copia fuori dal repo ed eseguito il test: `All 11 perf_table tests PASSED`)
- `scripts/perf_table.py` ha UNA sola riga con `"QEMU"`: `git grep -n '"QEMU"' -- scripts/perf_table.py` deve dare esattamente una riga (oggi la 169): `    if "QEMU" in header.get("board", "") and not args.allow_qemu:`.
- Il messaggio d'errore (`QEMU report: not a hardware measurement`) resta com'è: il test lo confronta.
- `tests/tools/test_perf_table.py` ha `test_qemu_report_rejected` (oggi riga 131), `test_qemu_report_accepted_with_flag` (riga 138) e l'elenco `TESTS = [` (riga 189) con 10 funzioni. Il test nuovo ha la stessa forma di `test_qemu_report_rejected`.
- `REPORT_RGB30` contiene la sottostringa `board: RGB30`: `.replace("board: RGB30", "board: qemu virt")` funziona (come fa già `REPORT_QEMU`, riga 39).
- Fine riga: le copie su Windows possono avere CRLF (`git ls-files --eol scripts/perf_table.py tests/tools/test_perf_table.py` mostra `w/crlf` o `w/lf`; sul worktree del planner era CRLF). Mantieni quelli che il file ha già: le righe nuove con lo stesso fine riga. `git diff --stat` deve restare piccolo (vedi Criterio); se dice centinaia di righe hai cambiato i fine riga: `git checkout -- <file>` e rifai.

## AGY-16a - il test, prima rosso (bm-coder, 10 min)
Passo 1: `tests/tools/test_perf_table.py`. Trova con `git grep -n "def test_qemu_report_accepted_with_flag" -- tests/tools/test_perf_table.py` (una riga, oggi la 138). Subito PRIMA di quella riga inserisci ESATTAMENTE queste 6 righe, seguite da DUE righe vuote (nel file ci sono sempre due righe vuote tra una funzione e l'altra):
```
def test_qemu_lowercase_rejected():
    report = REPORT_RGB30.replace("board: RGB30", "board: qemu virt")
    rc, err, after = run_case(report, DOC, "rgb30")
    check(rc == 1, f"exit {rc}, expected 1")
    check("QEMU report: not a hardware measurement" in err, f"stderr: {err!r}")
    check(after == DOC, "doc changed on a lowercase qemu report")
```
poi le due righe vuote, poi `def test_qemu_report_accepted_with_flag():` come prima.

Passo 2: nell'elenco `TESTS = [`, subito DOPO la riga `    test_qemu_report_rejected,` aggiungi la riga
```
    test_qemu_lowercase_rejected,
```
(4 spazi di rientro, virgola finale). Senza questa riga il test non gira (`main()` esegue solo `TESTS`).

Passo 3 (prova rossa, codice ancora vecchio): `python tests/tools/test_perf_table.py` (PowerShell: poi `"EXIT=$LASTEXITCODE"`).
- Atteso: `FAIL test_qemu_lowercase_rejected` (il vecchio codice accetta `qemu virt` e dà exit 0 invece di 1) e EXIT diverso da 0; gli altri 10 `PASS`. Se il test nuovo PASSA già: scrivilo nella consegna e `BLOCKED`.

## AGY-16b - il fix e la prova verde (bm-coder, 5 min)
Passo 4: `scripts/perf_table.py`. Sostituisci la riga
```
    if "QEMU" in header.get("board", "") and not args.allow_qemu:
```
con ESATTAMENTE
```
    if "qemu" in header.get("board", "").lower() and not args.allow_qemu:
```
(stesso rientro di 4 spazi; resto della riga invariato).

Passo 5: `python tests/tools/test_perf_table.py` di nuovo.
- Atteso: `PASS test_qemu_lowercase_rejected`, ultima riga `All 11 perf_table tests PASSED`, EXIT=0.
Passo 6: `python -m py_compile scripts/perf_table.py tests/tools/test_perf_table.py` senza uscita. Se `git status --short` mostra cartelle `__pycache__`, cancellale (le hai create tu).

Criterio di fine: passo 3 rosso solo per il test nuovo, passo 5 verde con 11 test; `git diff --stat`: 2 file, `scripts/perf_table.py | 2 +-` e `tests/tools/test_perf_table.py | 9 +++++++++` (6 righe del test + 2 vuote + 1 nell'elenco; se il conteggio differisce di 1 per una riga vuota va bene, deve restare sotto le 12 righe in tutto). `git status --short`: solo i due file.
Commit: un solo commit sul branch `claude/perf-qemu-case`, messaggio `perf_table: reject a lowercase qemu board too`. Push solo di questo branch (`git push -u origin claude/perf-qemu-case`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-16 --repo C:\Users\Utente\Documents\GitHub\bm33-agy16 --branch claude/perf-qemu-case --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-16 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` aggiungi la riga standard `DELIVERED AGY-16 @ claude/perf-qemu-case@<sha> · verifica: python tests/tools/test_perf_table.py -> All 11 perf_table tests PASSED (prima del fix: FAIL test_qemu_lowercase_rejected) · 2 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-16 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto).
Stima: 15 minuti di lavoro; nessuna compilazione (il test dura meno di 2 secondi).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 `"qemu" in ... .lower()` copre `QEMU`, `qemu`, `Qemu virt`. Non copre scritture strane (`q-emu`): fuori scopo. Raccomandata: sì.
- D2 Il messaggio d'errore resta in inglese e invariato. Raccomandata: sì.

## Prove per l'utente
Nessuna.
