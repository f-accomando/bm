---
name: bm-docs-map
description: Mappa di quale file aprire in bm per ogni area (API, progressi, roadmap, kernel, runtime) e come leggere i file grossi senza consumare token. Usala prima di leggere docs/API.md, docs/progress*, docs/ROADMAP.md, src/bm/runtime.c o qualunque file oltre ~15 KB, quando devi capire dove sta una cosa nel repo, o quando devi aggiornare i progressi dopo un cambio.
---

# bm: dove leggere e come aggiornare (poco, a sezioni)

Un solo indice: `docs/progress.md` (aree, navigazione, doc di riferimento). Si parte da lì.
Per tenere i doc snelli dopo un cambio: skill `docs-loop`.

Regola: mai un file grosso per intero. `Grep -n` sul simbolo o su `^##`, poi `Read` con
`offset`/`limit` sul tratto. Un file già letto in sessione non si rilegge.

## API dei giochi (inglese)

`docs/API.md` = indice + basi (struttura, colori, budget). Il resto in `docs/api/`:

| Area | File |
|---|---|
| schermo, forme, sprite, mappa | `draw.md` |
| tasti, pad, mouse, touch | `input.md` |
| tempo, rete UDP, salvataggi, documenti | `system.md` |
| suono, note, strumenti, banco | `sound.md` |
| tastiera/file per gli strumenti, assistente | `tools.md` |
| nano8, luce, 3D software, collisioni | `light3d.md` |
| bmlib | `bmlib.md` |
| riff | `riff.md` |
| bmnet | `bmnet.md` |

Cambio d'API: si aggiorna il file della sua area, `docs/GAME-GUIDE.md` (sezione giusta),
`src/ai/kb/` (un file per area), poi `make ai-model`. I `-IT` no.

## Progressi (stato attuale, non cronaca)

Indice: `docs/progress.md` (sezione *Which section to open*: system, multitasking, graphics,
v3d, lua, audio, platforms, tools). Un'area = un file in `docs/progress/`; ognuno ha `## How it is today` (sottosezioni `###` per modulo con i simboli
da cercare), `## Open work`, `## Rules (do not break)`.

Per aggiornare dopo un cambio:
1. `Grep -n '^##' docs/progress/<area>.md`, leggi solo la sottosezione del modulo toccato.
2. Correggi o togli le righe diventate false; aggiungi solo stato (file, simboli), mai date
   o "abbiamo fatto". Una cosa chiusa si toglie da *Open work*.
3. Il file supera ~120 righe → spezzalo per tema (`<area>-<tema>.md`), linka dal file madre
   e aggiungi la riga in `docs/progress.md`.
4. Milestone aperta/chiusa: solo la sua sezione in `docs/ROADMAP.md` (`Grep '^## M47'`) e la
   riga in *Open milestones* di `docs/progress.md`. Nuovi spunti: in fondo a ROADMAP (R…).

Delega con `bm-mechanic` se le righe sono già decise.

## File grossi di codice (mai interi)

`runtime.c` ha la sua mappa per sezioni in `docs/progress/runtime.md`; i comandi per ogni file
grosso sono in `docs/progress/reading.md`.

**Indice sempre fresco**: `python3 scripts/cindex.py <file> [parola]` stampa i banner di sezione e le
funzioni con la riga (C e Lua); con la parola, solo le righe che la contengono. Poi `Read` con
`offset`/`limit`. Per una funzione Lua esposta ai giochi cerca `l_<nome>` (la tabella `api[]`
in `runtime.c` collega nome → `l_…`).

- `src/bm/runtime.c` (350 KB): `Grep -n 'static int l_<nome>\|"<nome>"'` per una funzione
  Lua esposta; `Grep -n '^[a-z].*(' ` limitato con `head_limit` per l'elenco funzioni.
- `src/kernel/menu_ui.c`, `settings.c`: cerca la riga/stringa visibile a schermo, poi leggi
  ±60 righe.
- `src/script/bm3d.lua`, `bmlib.lua`, `bmui.lua`: `Grep -n '^function\|^local function\|^P\.'`
  poi `Read` del tratto.
- `docs/GAME-GUIDE.md`, `docs/B16.md`, `sdk/README.md`, `docs/RGB30.md`, `docs/DRIVERS.md`:
  `Grep -n '^#'` e leggi la sezione.

## Altri doc per area

GPU: `docs/DRIVERS.md`, `docs/BENCH3D.md` · RGB30: `docs/RGB30.md` · `.b16`: `docs/B16.md` ·
risorse `BMRES`: `docs/RISORSE.md` · riff: `docs/RIFF.md` · Predict e pad: `docs/PREDICT.md`,
`docs/PADTYPE.md` · assistente: `src/ai/kb/README.md`, `AI.md` · Market: `market/README.md`.

## Ricerche larghe

"Dove sta X / chi usa Y" su più file → agente `Explore` (torna con la conclusione, non i file).
