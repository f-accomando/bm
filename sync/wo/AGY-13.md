# AGY-13 - CARTS-LAYOUT: `scripts/carts_layout.py`, il layout di `docs/PROJECT-LAYOUT.md` controllato da comando (APP-3)
Assegnato ad AGY (16 ott), indipendente da AGY-10, BETA-7, AGY-11 e AGY-12. Decisioni D1-D3 adottate come raccomandate.
Base: `bm-core` aggiornato; branch `agy/carts-layout`. Mai push su bm-core.
Perimetro: `scripts/carts_layout.py` (nuovo), `tests/tools/test_carts_layout.py` (nuovo), `docs/PROJECT-LAYOUT.md` (una sola riga nuova dopo la riga 4).
NON toccare: `carts/**` (sola lettura: nessun file si sposta, nemmeno quelli segnalati), `Makefile`, `scripts/mkbm.py`, `docs/progress/tools.md` (righe 81-82 "Project layout": le aggiorna SPV, il file è condiviso con BETA-7/APP-4/APP-5b/R-NET), `docs/open-decisions.md`, `docs/GAME-GUIDE.md`, `manuals/*`, `src/*`, `.github/workflows/*`, `SYNC.md`, `sync/*`.
Obiettivo: `python scripts/carts_layout.py` elenca per ogni progetto in `carts/` se segue il layout comune. Controlla `bm.info` con `name`, `author` e una `resolution` valida, e che non ci siano asset, script o risorse fuori da `assets/`, `build/` e `res/`. Verifica anche che la tabella "Migrated so far" corrisponda ai `bm.info` reali. Esce con 1 su un errore dei progetti già migrati.
Regola di evidenza: come AGY-11.

## Stato reale (verificato dal planner, su bm-core)
- Layout: `docs/PROJECT-LAYOUT.md:6-15` (`bm.info`, `main.lua`, `cover.png`, `assets/` con `sheet.png, map.csv, map_<layer>.csv, flags.csv, sprites.txt, models.bm|glb`, `res/` con `.bmm .bmi .bms .bmt .bmc .bmk`, `build/` con `mk*.py, build.py`). Una cartella senza `bm.info` "still builds the old way" (:3-4). Sintassi `key: value`, blocchi `[type name]`, righe `#` di commento (:19-21). Risoluzioni ammesse: `640x360`, `480x270`, `320x180`, `256x256`, `360x360`, `720x720` (:27).
- Tabella "Migrated so far": righe 47-58; prima colonna con 21 id tra backtick (pong; yharnam; hunt; snake, shooter, astrowing, typing, write; village; kitchen, titan; nano8; overbit; code, assistant, editor, sound, animator, mesh, pixel; studio).
- `bm.info` reali: 21, gli stessi id (glob `carts/*/bm.info`). Tutti hanno `name`, `author`, `resolution` (grep: 3 righe per file) con valori ammessi (`hunt` e `village` 320x180, `overbit` 480x270, `yharnam` 360x360, gli altri 640x360). Esempio: `carts/pong/bm.info:1-10`.
- `main.lua` senza `bm.info`: `demo`, `stress`, `texroom`, `titan_ex`, `sdk_b16`. `kitchen`, `nano8`, `overbit`, `titan` hanno `bm.info` ma nessun `main.lua` in cima (glob). File fuori posto in cima (glob): `demo/sheet.png`, `demo/map.csv`, `texroom/sheet.png`, `texroom/mkassets.py`, `titan_ex/build.py`, `sdk_b16/build.py`: tutti in progetti senza `bm.info`. Sottocartelle di `carts/` senza `main.lua` né `bm.info`: `not verified` (lo script le ignora).

## AGY-13a - lo script (bm-coder, 40 min)
- CLI: `python scripts/carts_layout.py [--root DIR]` (default la radice del repo). Progetto = ogni sottocartella diretta di `<root>/carts` con `main.lua` o `bm.info`, in ordine alfabetico.
- `bm.info`: si leggono le righe fino alla prima che inizia con `[` (i blocchi sono parti, non chiavi del gioco); si saltano vuote e `#`; `key: value` con split sul primo `:` e strip. Errori: `ERROR  <id>: bm.info has no <name|author|resolution>`, `ERROR  <id>: resolution <v> is not one of 640x360, 480x270, 320x180, 256x256, 360x360, 720x720`.
- File in cima alla cartella (solo file, non sottocartelle), con `fnmatch` senza distinzione di maiuscole:
  - `sheet.png`, `map.csv`, `map_*.csv`, `flags.csv`, `sprites.txt`, `models.bm`, `models.glb` → `assets/`;
  - `mk*.py`, `build.py` → `build/`;
  - `*.bmm`, `*.bmi`, `*.bms`, `*.bmt`, `*.bmc`, `*.bmk` → `res/`.
  Con `bm.info`: `MISPLACED <id>/<file>: goes in <dir>/` (errore). Senza: `note  <id>/<file>: goes in <dir>/ when migrated` (non errore).
- Senza `bm.info`: `old    <id>: no bm.info (old layout)`; con `bm.info` e nessun errore: `ok     <id>`.
- Tabella del documento: in `<root>/docs/PROJECT-LAYOUT.md` la sezione `## Migrated so far` fino al `## ` successivo; per ogni riga `| ... |` che non sia intestazione o separatore, gli id tra backtick della prima cella. Errori: `ERROR  doc: <id> is in "Migrated so far" but has no bm.info`, `ERROR  doc: <id> has bm.info but is not in "Migrated so far"`. Documento o sezione mancante: `ERROR  doc: no "Migrated so far" table`.
- Ultima riga: `layout: <P> projects, <M> with bm.info, <O> old layout, <E> errors`. Exit 1 se E > 0, altrimenti 0; 2 se `<root>/carts` manca.
- `tests/tools/test_carts_layout.py` (unittest + subprocess, radice temporanea con `carts/` e `docs/PROJECT-LAYOUT.md` minimi). Casi:
  - progetto migrato valido e tabella giusta → 0;
  - `bm.info` senza `author` → 1;
  - `resolution: 800x600` → 1;
  - una chiave dentro un blocco `[part x]` non conta come chiave del gioco;
  - `sheet.png` in cima a un migrato → 1 `MISPLACED`;
  - `mkassets.py` in cima a un non migrato → 0 con `note`;
  - `Sheet.PNG` → riconosciuto;
  - id in tabella senza `bm.info` → 1;
  - `bm.info` non in tabella → 1;
  - `carts/` mancante → 2.
- Verifica:
  - `python tests/tools/test_carts_layout.py` → OK.
  - `python scripts/carts_layout.py` sul repo → exit 0. Atteso (dedotto dai glob, da confermare): `layout: 26 projects, 21 with bm.info, 5 old layout, 0 errors`, con 6 righe `note` (demo x2, texroom x2, titan_ex, sdk_b16). Se compare un errore vero di un gioco: si elenca nella consegna, non si corregge (fuori perimetro). Se è un falso positivo di una regola: BLOCKED con le righe.

## AGY-13b - documento (bm-mechanic, 5 min)
- `docs/PROJECT-LAYOUT.md`, dopo la riga 4: `Check it with \`python scripts/carts_layout.py\`: every project with \`bm.info\` must pass, and the table "Migrated so far" must match the folders.`
- Verifica: `python scripts/carts_layout.py` → exit 0; `python scripts/docs_audit.py score` senza righe `!` nuove.

Criterio di fine: verifiche di 13a e 13b OK.
Consegna: `DELIVERED AGY-13 @ agy/carts-layout@<commit> · verifica: test_carts_layout -> OK; carts_layout.py -> exit 0, <riga layout:>; docs_audit score -> <esito> · <ISO>`, poi FREE. Agente consigliato: bm-coder (13a), bm-mechanic (13b).

## Decisioni (proposte del planner)
- D1 i file fuori posto dei progetti non migrati sono `note`, non errori, perché la migrazione è graduale (`PROJECT-LAYOUT.md:3-4`). Raccomandata: sì.
- D2 `main.lua` assente in un progetto con `bm.info` non è un errore: `kitchen`, `nano8`, `overbit`, `titan` lo generano o lo hanno altrove (`not verified` il perché; la tabella cita `build.py` in `build/`). Raccomandata: sì.
- D3 nessun aggancio a `make` o alla CI in questo WO (il `Makefile` è nei perimetri di API-1 e APP-5b): un eventuale target `test-layout` è un WO a parte. Raccomandata: sì.

## Prove per l'utente
Nessuna.
