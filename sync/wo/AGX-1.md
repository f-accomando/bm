# AGX-1 - SDK-B16-HOST: `make test-sdk-b16` torna verde su bm-core (host SDK senza `map3d_*`, `line3d`, `point3d`; hint `"UP DN"`)
Assegnato ad AGX (16 ott), primo dei due: AGX-1, poi AGX-2 (stesso file, AGX-2 parte da questo branch). Nasce da APP-4 e B16-D28FU, che hanno segnalato il fallimento preesistente (`sync/B16.md`, Consegne e Problemi).
Base: `bm-core` aggiornato (`git fetch origin`, poi `git switch -c claude/sdk-b16-host-stubs origin/bm-core` nel worktree di AGX); branch `claude/sdk-b16-host-stubs`. Mai push su bm-core.
Perimetro: `tests/studio/sdk_b16_host.lua` (un blocco di 9 righe nuove subito dopo la riga `E.camera3d = nop`, oggi riga 446, dentro `new_env`) e `carts/sdk_b16/main.lua` (una sola riga, la 2726: `"UP DN"` diventa `"UPDOWN"`).
NON toccare: `carts/editor/*` (SDK del Pi, altra edizione), `src/**`, `Makefile`, `tests/studio/sdk_host.lua` (host dell'SDK del Pi), `tests/qemu_test.py`, `tests/rgb30/*`, `docs/*`, `.github/workflows/*`, `SYNC.md`, `sync/*`, tutti gli altri `check` e tutte le altre righe di `sdk_b16_host.lua` (nessun check si allenta o si toglie).
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Ogni `file:riga` qui sotto è stato aperto dal planner su `bm-core` bf5f3215; l'operatore lo riscontra con `grep -n` (le righe possono slittare di poco) e ciò che non riscontra lo scrive `non verificato`.
Obiettivo: `make test-sdk-b16` esce con 0 su bm-core + queste due modifiche (`sdk_b16_host: 298 checks, 0 failed`).

## Stato reale (verificato dal planner: ho lanciato `make test-sdk-b16` su un worktree temporaneo di bm-core)
- Fallimento 1, sul `bm-core` pulito (5 min con la compilazione a freddo): `build/sdk_b16/main.lua:2620: attempt to call a nil value (global 'map3d_build')`, da `tests/studio/sdk_b16_host.lua:855` (il ciclo delle pagine e delle azioni, pagina "3D"). Il sorgente è `carts/sdk_b16/main.lua:2479` (la vista 3D della mappa, funzione `draw` della pagina mappa).
- L'host è il solo posto dove mancano le funzioni: i nomi esistono nell'elenco `API3D` di `carts/sdk_b16/main.lua:1292-1295` (l'evidenziatore di sintassi), ma `new_env` di `sdk_b16_host.lua` li definisce uno a uno (`E.camera3d = nop` riga 446, `E.draw3d` ~526, `E.bounds3d` ~533, `E.project3d` 447; `light3d`, `fog3d`, `lamp3d`, `zclear`, `sky3d` sono `nop` alla riga 336).
- Globali 3D letti dal codice della vista (`main.lua:2479-2530`) e NON definiti nell'host: `map3d_build` (2479), `map3d_height` (2485 e 2529), `map3d_draw` (2496), `line3d` (2503-2506, 2530), `map3d_objects` (2510), `point3d` (2522). `map3d_blocked` e `map3d_size` stanno solo in `API3D` (non vengono chiamate): si definiscono lo stesso, per coerenza con l'elenco. `target_y` NON è un globale (è una `local` di `main.lua:2485`): non serve alcuno stub. `lamp3d` e `model` sono già nell'host (336 e 508).
- Fallimento 2, dopo aver aggiunto gli stub: `tests/studio/sdk_b16_host.lua:360: prompt: not a button or a key: DN` (da `main.lua:232`, `chips()`, chiamata da `hint()` :254). La causa è un errore vero dell'SDK, non dell'host: l'hint della vista 3D, `main.lua:2726`, passa `"UP DN"`, e `chips()` spezza sugli spazi (`gmatch("%S+")`) e chiama `prompt("DN")`. I nomi maiuscoli ammessi sono quelli di `src/bm/runtime.c:1215-1226` e `docs/api/draw.md:19` (`"UP"`, `"DOWN"`, `"UPDOWN"`, `"LEFTRIGHT"`, `"DPAD"`...): `DN` non c'è, quindi il runtime non ha un chip da disegnare (cosa fa esattamente `prompt` con un nome sconosciuto: `non verificato`). L'host fa bene a rifiutarlo: NON si allenta `E.prompt` e NON si aggiunge `DN` a `PADN` (riga 274). Si corregge l'SDK: `"UPDOWN"` (il chip con su e giù, come nella riga 2728 che usa `"DPAD"`/`"ok"`).
- Dopo i due passi: `sdk_b16_host: 298 checks, 0 failed`, exit 0 (provato dal planner su bm-core bf5f3215 + queste modifiche, in WSL).
- Se compare un terzo errore uguale ("attempt to call a nil value (global 'X')" o "not a button or a key"), vale la regola del passo 3.
- Tempo: l'intera suite è lenta perché per ogni pagina rilancia l'SDK da capo per ogni azione (ciclo di riga ~850-880) e disegna ogni frame con tutti i controlli. Misurato dal planner: circa 13-14 minuti di orologio per la sola `make test-sdk-b16` in WSL (il worktree sta su `/mnt/c`), più 1-5 minuti la prima volta per compilare `build/host/luahost`. Il revisore di APP-4 l'ha vista superare i 20 minuti con stub suoi (probabile: macchina carica o stub che disegnano di più). Non c'è niente da accelerare in questo WO.

## AGX-1a - gli stub in `tests/studio/sdk_b16_host.lua` e la riga dell'SDK (bm-coder, 15 min di lavoro + attese)
Passo 0: ambiente.
- `scripts/wsl-run.ps1` compila in `/tmp` di WSL, che è un tmpfs da 3,9 GB e oggi era pieno (`No space left on device`, lasciato da altri processi QEMU). Prima di lanciare `make` controllare: `wsl -e bash -c "df -h /tmp | tail -1"`. Se la colonna Use% è 100%: lanciare sempre con `TMPDIR` nel repo: `scripts/wsl-run.ps1 "mkdir -p build/tmp && TMPDIR=\$PWD/build/tmp make test-sdk-b16; echo EXIT=\$?" -Tail 30`. Non cancellare nulla in `/tmp` (non è tuo).
- Le copie di lavoro su Windows hanno i fine riga CRLF (`file tests/studio/sdk_b16_host.lua` dice "CRLF"). Un editor che converte tutto in LF fa risultare l'intero file modificato: dopo ogni modifica `git diff --stat` deve dire `tests/studio/sdk_b16_host.lua | 9 +` e `carts/sdk_b16/main.lua | 2 +-`. Se le righe cambiate sono molte di più: annullare (`git checkout -- <file>`) e rifare mantenendo CRLF.

Passo 1: `tests/studio/sdk_b16_host.lua`. Subito DOPO la riga (unica, riscontrare con `grep -n "E.camera3d = nop"`)
```
  E.camera3d = nop
```
e PRIMA di `  E.project3d = function(x, y, z) return 244 + x * 10, 184 - y * 10, 5 end`, inserire esattamente queste 9 righe (rientro di 2 spazi, come le vicine):
```
  -- the 3D map view (carts/sdk_b16/main.lua :2479-2530): the map3d_* world, the cursor lines and the points
  E.map3d_build = function(t) assert(type(t) == "table", "map3d_build: a table"); return { map3d = true } end
  E.map3d_draw = function(w) assert(type(w) == "table", "map3d_draw: a world") end
  E.map3d_height = function() return 0.0 end
  E.map3d_objects = function() return {} end
  E.map3d_blocked = function() return false end
  E.map3d_size = function() return 0, 0 end
  E.line3d = nop
  E.point3d = nop
```
(`nop` è definita alla riga 314 nello stesso `new_env`, quindi è in scope. `E.line` della riga ~326 è il `line` 2D e non c'entra con `line3d`.)

Passo 2: `carts/sdk_b16/main.lua`, riga 2726 (riscontrare con `grep -n '"UP DN"' carts/sdk_b16/main.lua`: deve dare una sola riga). Sostituire, sulla stessa riga, solo `"UP DN"` con `"UPDOWN"`:
```
      return { { "v", "back", "2D map" }, { nil, "L1 R1", "orbit" }, { "up/dn", "UPDOWN", "zoom" }, { nil, "X", "more" } }
```
(la stringa `"up/dn"` minuscola è il tasto della tastiera e resta com'è: l'host la accetta, `n == n:lower()`).

Passo 3: verifica, con tempo massimo.
- Lanciare in background con l'output su file (`run_in_background`), perché supera i 10 minuti: `scripts/wsl-run.ps1 "make test-sdk-b16; echo EXIT=\$?" -Tail 30`, e leggere il file ogni 5 minuti.
- Atteso: ultima riga utile `sdk_b16_host: 298 checks, 0 failed` e `EXIT=0`.
- Tetto: 30 minuti di orologio dall'avvio (esclusa la compilazione di `luahost`). Se a 30 minuti non è finito: fermare il processo (`wsl -e pkill -f sdk_b16_host`, solo il tuo), scrivere `BLOCKED: test-sdk-b16 oltre 30 min` e riportare: i minuti trascorsi, `uptime` di WSL (`wsl -e uptime`), l'ultima riga di output e se `/tmp` era pieno. Non accorciare né togliere check per farlo finire prima.
- Se finisce con un fallimento diverso da quelli dei due passi: riportare l'errore intero (le righe di `FAIL` e lo stack); se è un altro globale nil o un altro nome di `prompt` sconosciuto fare UNA volta la stessa cosa (stub nell'host se il globale è del runtime e l'SDK lo usa come il runtime lo definisce; correzione nell'SDK se è un nome sbagliato) e rilanciare; al secondo errore nuovo: `FAILED x2`, con l'elenco dei globali trovati (`SDK_DEBUG=1 ... luahost tests/studio/sdk_b16_host.lua . build/sdk-b16-sd build/sdk_b16/main.lua` stampa a fine corsa `globals read but not stood in for:`).
- `git diff --stat` finale: 2 file, 9 + 1 righe cambiate in tutto (9 inserite nell'host, 1 sostituita nell'SDK).

Criterio di fine: `make test-sdk-b16` → exit 0 con `298 checks, 0 failed`.
Commit: un solo commit sul branch `claude/sdk-b16-host-stubs`, messaggio `tests: sdk_b16 host stubs map3d_*, line3d, point3d; sdk_b16 hint UPDOWN`. Push solo di questo branch.
Consegna: `DELIVERED AGX-1 @ claude/sdk-b16-host-stubs@<commit> · verifica: make test-sdk-b16 -> exit 0, <N> checks, 0 failed (<M> min) · 2 file, 10 righe · non verificato su hardware · <ISO>` in `## Consegne` di `sync/AGX.md`, poi FREE. Agente consigliato: bm-coder (le modifiche sono esatte); la sola decisione è nel passo 3 (errore nuovo) e va a SPV.
Stima: 15 minuti di lavoro, 15-20 di attesa per la suite (WSL), più 5 la prima volta per compilare.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 `"UP DN"` si corregge nell'SDK (`"UPDOWN"`), non si accetta nell'host: è un errore reale che il test ha trovato. Raccomandata: sì. Alternativa: lasciare l'SDK e aggiungere `DN = 1` a `PADN` (riga 274), che nasconderebbe un hint sbagliato sulla console (non verificato che cosa disegni il runtime).
- D2 Gli stub restituiscono valori vuoti (mondo `{ map3d = true }`, altezza 0, nessun oggetto): il test controlla il layout dei frame, non la geometria 3D. Raccomandata: sì.
- D3 Il tetto di 30 minuti per la suite: se il revisore dei prossimi WO la vede oltre i 20, lo dichiara nella consegna come fa questo WO. Un WO separato può dividerla (non qui).
- Nota per SPV: B16-D28FU (`claude/sdk-games-message`) e APP-4 (`claude/sdk-stages-template`) toccano lo stesso file in altri punti; i 9 nuovi stub stanno vicino a `E.camera3d` (riga 446) e non confliggono con i loro punti (~1036 e ~987-1090), verificato che `git apply` della modifica passa sul branch di APP-4.

## Prove per l'utente
Nessuna.
