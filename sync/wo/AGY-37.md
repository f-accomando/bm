# AGY-37 - TEST-MKICONS: nuovo test `tests/tools/test_mkicons.py` per `inside()` e per le sette icone in `carts/`
Assegnato ad AGY (16 ott). `scripts/mkicons.py` (angoli arrotondati delle icone) non ha test; `inside(x, y)` è pura matematica.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/test-mkicons C:\Users\Utente\Documents\GitHub\bm33-agy37 origin/claude/integrazione-ottobre`); branch `claude/test-mkicons`. Mai push su bm-core; push solo di questo branch.
Perimetro: un file NUOVO: `tests/tools/test_mkicons.py` (unittest).
NON toccare: `scripts/mkicons.py` (importato così com'è, `main()` non si chiama: ridisegnerebbe le icone) e i file `carts/*/icon.png` (si LEGGONO soltanto); ogni altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; nome file non usato da altri WO.
Vincoli: solo host, stdlib più Pillow se c'è; nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: `python -m unittest tests/tools/test_mkicons.py` OK oppure saltato (skip) se manca Pillow.

## Stato reale (verificato su `origin/claude/integrazione-ottobre`)
`scripts/mkicons.py` importa `from PIL import Image` in testa (riga 16): senza Pillow l'import fallisce. `SIZE = 128`, `RADIUS = 26`, `ICONS` ha 7 nomi (`studio code animator editor sound mesh pixel`), `inside(x, y)` alla riga 31 ritorna il punto più vicino del quadrato arrotondato. Le icone sono `carts/<nome>/icon.png`. Verifica che esistano sulla base: `git ls-tree -r --name-only origin/claude/integrazione-ottobre | grep icon.png`.

## AGY-37a - il test (bm-coder, 10 min)
Importa `mkicons` con `sys.path.insert(0, <ROOT>/scripts)`; se l'import fallisce per Pillow, ogni test fa `self.skipTest("Pillow missing")`. Controlli:
- `inside(64, 64) == (64, 64)`; `inside(0, 0) == (9, 9)`; `inside(127, 127) == (118, 118)`; `inside(127, 0) == (118, 9)`
- per ogni `name` in `mkicons.ICONS`: esiste `<ROOT>/carts/<name>/icon.png` (7 icone)
- nessun punto di bordo resta sul bordo: `inside(64, 0) != (64, 0)`, `inside(0, 64) != (0, 64)`, `inside(127, 64) != (127, 64)`, `inside(64, 127) != (64, 127)`.
Calcolati a mano: ESEGUILI; se un'aspettativa è sbagliata correggi l'ASPETTATIVA e scrivilo nella consegna.

## Verifica
1. `python -m unittest tests/tools/test_mkicons.py -v`: OK oppure `skipped` (scrivi quale e se Pillow c'era: `python -c "import PIL"`).
2. `git status --short`: solo il file nuovo; niente `__pycache__`.

Criterio di fine: 1 file nuovo, test OK o saltati per Pillow mancante.
Commit: un solo commit sul branch `claude/test-mkicons`, messaggio `tests: unit tests for mkicons.py`. Push solo di questo branch (`git push -u origin claude/test-mkicons`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-37 --repo C:\Users\Utente\Documents\GitHub\bm33-agy37 --branch claude/test-mkicons --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-37 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-37 @ claude/test-mkicons@<sha> · verifica: python -m unittest tests/tools/test_mkicons.py -> OK (o skipped: Pillow assente) · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-37 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (casi forniti).
Stima: 10 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Senza Pillow il test salta: ammesso. Raccomandata: sì.

## Prove per l'utente
Nessuna.
