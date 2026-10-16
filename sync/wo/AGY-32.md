# AGY-32 - READIN-CHECK: nuovo `scripts/config_readin.py`: controlla che i `read_in` di `config-keys.toml` puntino alla riga giusta
Assegnato ad AGY (16 ott). In `docs/spec/config-keys.toml` il campo `read_in = "file:riga"` si sposta ogni volta che `runtime.c` cambia (oggi `assist_lang` e `battery_icon` puntano a righe che non li contengono più). Serve un controllo; la correzione dei numeri è AGY-33.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/config-readin-check C:\Users\Utente\Documents\GitHub\bm33-agy32 origin/claude/integrazione-ottobre`); branch `claude/config-readin-check`. Mai push su bm-core; push solo di questo branch.
Perimetro: due file NUOVI: `scripts/config_readin.py` e `tests/tools/test_config_readin.py`.
NON toccare: ogni file esistente (compreso `docs/spec/config-keys.toml`, che AGY-33 corregge dopo; `consistency.py`, `Makefile`: nessun aggancio in questo WO).
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; file nuovi con nomi non usati da AGX-4/8/9/10/11/12 né da AGY-11/12/13/17; `config-keys.toml` non è nel perimetro di nessuno di loro.
Vincoli: solo host, il `python` di Windows basta (stdlib: `tomllib`, `re`, `os`, `sys`, `unittest`); nessun WSL, nessun hardware. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: `python tests/tools/test_config_readin.py` OK; `python scripts/config_readin.py` esce con 1 e dà STALE almeno per `assist_lang` e `battery_icon`.

## Stato reale (verificato su `origin/claude/integrazione-ottobre` @ f5cd71b7)
- `docs/spec/config-keys.toml` ha blocchi `[[key]]` con `name = "..."` e `read_in = "src/bm/runtime.c:4842"`; `read_in` può avere più parti separate da `;`. Si legge con `tomllib` (Python 3.11+: `python --version`; se manca: `BLOCKED`).
- `assist_lang`: `read_in` dice `src/bm/runtime.c:4842` ma la riga con `config_get("assist_lang")` è la 4897. `battery_icon`: dice `:8331`, la riga con `config_get("battery_icon")` è la 8395. Controlla: `git grep -n -F '"assist_lang"' -- src/bm/runtime.c`.
- Modello per il test: `tests/tools/test_docs_audit.py` (AGY-17, se è già nel tuo base; NON lo è su `origin/claude/integrazione-ottobre` @ f5cd71b7) cambia la variabile di modulo `ROOT`; qui lo stesso. Se non c'è, metti `ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))` in testa allo script e il test lo cambia con `mod.ROOT = tmp`. Stile unittest: vedi `tests/tools/test_agent_audit.py`.

## AGY-32a - lo script (bm-coder, 8 min)
`scripts/config_readin.py`, comportamento ESATTO:
1. variabile di modulo `ROOT` (radice del repo, due livelli sopra lo script) e `KEYS = "docs/spec/config-keys.toml"`.
2. `check()` ritorna `(lines, stale_count)`: per ogni `[[key]]` (`name`, `read_in`), `read_in` diviso su `;`; per ogni parte, regex `^\s*([\w./-]+):(\d+)` (si usa il PRIMO numero: `file:12-14` -> 12); file aperto sotto `ROOT` con `encoding="utf-8", errors="replace"`; se il file manca: `UNCHECKED`.
3. la riga N (1-based) contiene il testo `"<name>"` (con le virgolette) -> OK (nessuna uscita).
4. altrimenti, se il file contiene `"<name>"` da qualche parte -> `STALE <name>: <path>:<N> -> <M>` con M = prima riga che contiene `"<name>"` E la parola `config`, altrimenti la prima che contiene `"<name>"`; conta come stale.
5. se `"<name>"` non c'è nel file -> `UNCHECKED <name>: <path>:<N>` (NON è errore, non conta).
6. `main()`: stampa le righe; se stale > 0 esce 1, altrimenti stampa `config_readin: 0 stale` ed esce 0.

## AGY-32b - il test (bm-coder, 7 min)
`tests/tools/test_config_readin.py`: tre test `unittest` in una cartella temporanea (`tempfile.TemporaryDirectory`, `mod.ROOT = tmp`, ripristinato in `tearDown`), con un `docs/spec/config-keys.toml` e un `src/x.c` finti: (1) `ok`: `read_in = "src/x.c:2"` e la riga 2 contiene `"foo"` -> nessuna riga, stale 0; (2) `stale`: la riga 3 contiene `"foo"` senza `config`, la 5 contiene `config_get("foo")`, `read_in` dice `:2` -> `STALE foo: src/x.c:2 -> 5`; (3) `unchecked`: nome assente dal file -> `UNCHECKED bar: src/x.c:2`, stale 0.

## Verifica
1. `python tests/tools/test_config_readin.py`: 3 test OK.
2. `python scripts/config_readin.py; echo "EXIT=$?"`: EXIT=1 e almeno `STALE assist_lang: src/bm/runtime.c:4842 -> 4897` e `STALE battery_icon: src/bm/runtime.c:8331 -> 8395`. Scrivi nella consegna quante righe STALE e quante UNCHECKED escono.
3. `python -m py_compile scripts/config_readin.py tests/tools/test_config_readin.py`; cancella i `__pycache__` che hai creato. `git status --short`: solo i 2 file nuovi.

Criterio di fine: 2 file nuovi; test OK; lo script trova assist_lang e battery_icon STALE.
Commit: un solo commit sul branch `claude/config-readin-check`, messaggio `tools: config_readin.py checks the read_in lines of config-keys.toml`. Push solo di questo branch (`git push -u origin claude/config-readin-check`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-32 --repo C:\Users\Utente\Documents\GitHub\bm33-agy32 --branch claude/config-readin-check --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-32 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-32 @ claude/config-readin-check@<sha> · verifica: python tests/tools/test_config_readin.py -> OK; config_readin.py -> exit 1, STALE assist_lang e battery_icon · 2 file nuovi · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-32 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (comportamento esatto).
Stima: 15 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 UNCHECKED non è errore (chiavi composte a runtime). Raccomandata: sì.
- D2 Nessun aggancio a `make`/`consistency.py` per ora. Raccomandata: sì.

## Prove per l'utente
Nessuna.
