# APP-4 — Template di progetto "Stages 2D" nel bm SDK (RGB30): titolo, 3 stage, finale, record salvato (APP-4, BETA-2)
Assegnato a B16 (16 ott). Decisioni adottate: D1-D8 come raccomandate. Sequenza di B16: APP-4, APP-6.
Inizia dopo: assegnazione scritta di SPV in `SYNC.md` e conferma (o modifica) di D1-D8.
Base: `bm-core` aggiornato; branch `claude/sdk-stages-template`. Mai push su bm-core.
Perimetro: `carts/sdk_b16/main.lua` (solo la tabella `ART` ~238-246 e la tabella `TEMPLATES` ~264-707: una voce nuova in fondo), `tests/studio/sdk_b16_host.lua` (solo `play_template` ~957-980, la lista `names` ~983-984, il check ~1013 e una funzione nuova), `tests/qemu_test.py` (un helper e un test nuovi), `tests/rgb30/qemu_test.py` (import e due test nuovi), `docs/GAME-GUIDE.md` (righe ~19 e ~895), `docs/progress/tools.md` (voce "bm SDK (RGB30)" ~20-27), `docs/spec/divergences.toml` (solo una voce NUOVA in fondo). NON toccare: `carts/editor/*` (SDK del Pi, altra edizione), `src/kernel/*`, `src/rgb30/*`, `src/bm/*`, `src/script/bmlib.lua`, `tools/pi_*`, `.github/workflows/*`, `docs/ROADMAP.md`, `Makefile`, `rgb30.mk`, `SYNC.md`, `sync/*`.
Regole: edizioni `.bm` e `.b16` separate (CLAUDE.md §2). Il template crea un progetto `.bme`, Ctrl+B ne compila il `.bm` (`build_project(false)` → `cart_build(path)`, main.lua ~864-879). Nessun sorgente `.bm` esistente si modifica.
Conflitto noto: B16-D28FU tocca gli stessi file in punti diversi (main.lua ~873, sdk_b16_host.lua ~1036-1039). L'integrazione la fa SPV. I numeri di riga sono verificati su `claude/ci-repair`, non su bm-core: segnati "~", l'operatore li riscontra con grep.

## Stato reale (verificato dal planner)
- I template sono la tabella `TEMPLATES` di `carts/sdk_b16/main.lua:264`, voci `{ id, name, desc, paint?, code }`. La schermata "nuovo progetto" è `choose_template()` (~1203-1219): elenca `TEMPLATES` in ordine; basta aggiungere la voce. `new_project(tp)` (~797-810) imposta 360x360, foglio e mappa 256x256 e chiama `tp.paint()`.
- Test host: `make test-sdk-b16` (Makefile:1275-1278, `tests/studio/sdk_b16_host.lua`): ogni template gira 400 frame (`play_template`) e viene salvato come `/carts/<i>.BME` (lista `names`).
- Nessun test QEMU (Pi o RGB30) fa girare il codice di un template di sdk_b16. Modelli: `test_sdk_tool` (rgb30/qemu_test.py:752) per aprire l'SDK; `test_bm_cartridge` (:528) per far girare un `.bm`.
- Macchina a stati: `docs/design/games-architecture.md` §2.A. Salvataggio: `save(t)` e `saved()` nello slot 1 (`docs/api/system.md:50-62`).
- bmlib: `GRAVITY=0.25`, salto `vy=-5`: un salto copre circa 10 celle in orizzontale e sale circa 6, quindi buchi da 4-5 celle e piattaforme alte fino a 6 sono superabili.

## Decisioni per SPV (il WO è scritto sulle raccomandazioni)
- D1 `id = "stages"`, `name = "Stages 2D"`, voce ACCODATA dopo "3D with models" (le posizioni dei template esistenti e i test non cambiano). (Sì)
- D2 I 3 stage stanno nella mappa del progetto, in fasce di righe: la fascia dello stage n inizia alla riga `(n-1)*48`, alta 45 righe, prime 128 colonne; si modificano col map editor dell'SDK (la Beta chiede "solo strumenti della suite"). Alternativa: livelli come tabelle nel codice. (Mappa)
- D3 Stati: `title`, `play`, `clear`, `gameover`, `ending`; niente `pause` (c'è la pausa di sistema). (Sì)
- D4 Salvataggio: `save({ best = <frame>, reached = <1..3> })` nello slot 1; `best` aggiornato solo se la partita è partita dallo stage 1. Il titolo ha "NEW GAME" e, se `reached >= 2`, "CONTINUE STAGE n". (Sì)
- D5 Il template chiama `log()` a ogni cambio di stato, con stringhe fissate in APP-4a: servono ai test host e QEMU e insegnano il debug. (Sì)
- D6 Solo SDK RGB30; il port in `carts/editor` è un WO separato; intanto una divergenza `open` in `docs/spec/divergences.toml`. (Sì)
- D7 I test QEMU impacchettano il codice a 360x360 (`res=(360, 360)`) come Ctrl+B da un progetto sdk_b16. Se il kernel Pi rifiuta un `.bm` 360x360: `BLOCKED`, senza cambiare risoluzione. (Sì)
- D8 Lo stato di APP-4 nella ROADMAP lo aggiorna SPV dopo il merge; non fa parte di questo WO. (Sì)

## Readiness Gate
1 File e funzioni: SÌ. 2 Determinismo: SÌ con D1-D8 (libero solo il disegno: colori, posizione dei testi; i test non lo leggono). 3 Verifica: SÌ. 4 Scope: SÌ (max 3 file per sezione). 5 Nessun blocco: SÌ dopo la conferma di D1-D8. La dipendenza da APP-1 non blocca: la prova su console è dell'utente.

## APP-4a — la voce "Stages 2D" in TEMPLATES (bm-coder, 45 min)
- File: `carts/sdk_b16/main.lua`: la tabella `ART` (~238) riceve una voce, `TEMPLATES` (~707) una voce in fondo. Nessun altro punto.
- `ART.flag = { "kwwwww..", "kwrrrw..", "kwwwww..", "k.......", "k.......", "k.......", "k.......", "k......." }` (inchiostri già in `INK`).
- `paint`: `paint(1, ART.hero); paint(2, ART.ground); paint(6, ART.plank); paint(7, ART.flag)`, poi `fset(2, 0, true)` e `fset(6, 1, true)`. Per n = 1..3, `top = (n-1)*48`:
  - terreno (tile 2) sulle righe `top+40..top+44`, colonne 0..127, tranne i buchi `GAPS[n]`;
  - muro (tile 2) sulla colonna 127, righe `top..top+44`;
  - assi (tile 6) sulla riga `top+40-h` per `x..x+len-1`, da `PLANKS[n]`;
  - uscita (tile 7) in (120, `top+38`) e (120, `top+39`).
  - `GAPS = { {{30,33}}, {{30,33},{70,74}}, {{24,27},{50,54},{90,94}} }` (colonne incluse).
  - `PLANKS = { {{12,6,6},{40,5,7}}, {{24,4,6},{52,5,7},{80,6,10}}, {{12,5,6},{40,4,7},{64,5,6},{100,6,8}} }` (x, h, len).
- `code` (stringa `[==[...]==]`, commento iniziale nello stile di Platform 2D che spiega fasce, tile 7 = uscita e flag):
  - Costanti `local EXIT, ROWS, BAND = 7, 45, 48`; `require "bmlib"`; variabile `state`; `_update` e `_draw` con catena `if/elseif` verso `update_<stato>`/`draw_<stato>` (games-architecture §2.A).
  - `_init`: `local s = saved()`; `best = s and s.best`; `reached = s and s.reached or 1`; `to_title()`.
  - `to_title()`: `state = "title"`, `sel = 1`, `log("stages: title best " .. tostring(best or "-"))`.
  - title: righe "NEW GAME" e, se `reached >= 2`, "CONTINUE STAGE " .. reached. `btnp(2)`/`btnp(3)` spostano `sel`; `btnp("ok")` sceglie. NEW GAME: `lives=3, timer=0, from_start=true`, `start_stage(1)`. CONTINUE: `lives=3, timer=0, from_start=false`, `start_stage(reached)`. A video "STAGES" e, se c'è, "best m:ss" (frame/60).
  - `start_stage(n)`: `stage=n`, `top=(n-1)*BAND`, `hero={x=16, y=(top+30)*8, w=8, h=8, vy=0}`, `cam=lib.camera({ bounds = { 0, top*8, 128*8, (top+ROWS)*8 } })`, `state="play"`, `log("stages: stage " .. n)`.
  - `update_play`, in quest'ordine: (1) `timer = timer + 1`; (2) input come Platform 2D: `vx` ±2 con `btn(0)`/`btn(1)`, `drop = btn(3)`, salto se `hero.ground and btnp(4)` con `vy = -5`; (3) uscita: se `mget((hero.x+4)//8, (hero.y+4)//8) == EXIT` allora `reached = math.max(reached, math.min(stage+1, 3))` e `log("stages: clear " .. stage)`; se `stage < 3`: `save({ best = best, reached = reached })`, `state = "clear"`, return; se `stage == 3`: se `from_start and (not best or timer < best)` allora `best = timer`; `local ok = save({ best = best, reached = reached })`, `log("stages: ending " .. timer)`, se `ok` anche `log("stages: saved")`, `state = "ending"`, return; (4) `lib.step(hero)`; (5) caduta: se `hero.y > (top+ROWS)*8`: `lives = lives - 1`, `log("stages: lost " .. lives)`; se `lives == 0`: `state = "gameover"`, `log("stages: gameover")`, altrimenti l'eroe torna in partenza; (6) `cam:follow(hero.x+4, hero.y+4)`.
  - `clear`: `btnp("ok")` → `start_stage(stage+1)`. `gameover` ed `ending`: `btnp("ok")` → `to_title()`.
  - Draw play: `cls`, `cam:apply()`, `cam:map()`, `spr(1, …)`, `camera()`, HUD con stage, vite e tempo. Gli altri stati usano `lib.printc`. Solo API già usate dagli altri template; niente `math.random`.
- Verifica: `make build/host/luahost build/sdk_b16/main.lua`, poi `make test-sdk-b16` → exit 0, nessun `check` fallito (il test esistente non cambia: voce accodata).

## APP-4b — test host del template (bm-coder, 30 min)
- File: `tests/studio/sdk_b16_host.lua`.
- (1) In `play_template` (~957), dopo `G2._init, G2._update, G2._draw = nil, nil, nil`: `G2.save = function() return true end` e `G2.saved = function() return nil end` (così il `save()` di un template non sovrascrive `saved_t` dell'SDK, che serve al check "the last project opens by itself").
- (2) `names` (~983): aggiungere `"Stages 2D"` in fondo; il check ~1013 diventa `sees("9.BME") or sees("Stages 2D")`.
- (3) Nuova `local function stages_flow(code)`, chiamata nel ciclo dopo `play_template` quando `nm == "Stages 2D" and last_save`. Il codice gira in un `G2` copiato da `E`, con: `G2.mget = function() return 7 end` (ogni stage si chiude subito); `G2.btn = function() return false end`; `G2.btnp = function(i) return (i == "ok" and ok_now) or (i == 3 and down_now) or false end`; `G2.log` che accumula in `logs` (argomenti uniti da `" "`); `G2.save`/`G2.saved` su una tabella locale `store`.
  - Run 1, 200 frame con `ok_now = f % 10 == 0`. Check: `logs` contiene in ordine "stages: title best -", "stages: stage 1", "stages: clear 1", "stages: stage 2", "stages: clear 2", "stages: stage 3", "stages: clear 3", una riga che inizia con "stages: ending ", "stages: saved", e alla fine "stages: title best <n>" con `n == store.best`. Inoltre `math.type(store.best) == "integer"` e `store.reached == 3`.
  - Run 2, nuovo `G2` con lo stesso `store`: `_init` registra "stages: title best " .. store.best; poi `down_now` al frame 2 e `ok_now` al frame 10 → compare "stages: stage 3".
- Verifica: `make test-sdk-b16` → exit 0, nessun `check` fallito; nell'output i check di "Stages 2D".

## APP-4c — il `.bm` del template gira in QEMU Pi e RGB30 virt (bm-coder, 45 min)
- File: `tests/qemu_test.py`, `tests/rgb30/qemu_test.py`.
- Helper in `tests/qemu_test.py`: `def sdk_template(tid)` legge `carts/sdk_b16/main.lua` e restituisce il testo tra `code = [==[\n` e `]==]` della voce `{ id = "<tid>"` (regex DOTALL, non greedy); se non trova la voce: `AssertionError`.
- `def test_stages_template(b, opts)` in `tests/qemu_test.py`, sul modello di `test_b16_on_pi` (~3173): scheda SD con `bm/config.txt` (`layout=us\nwifi_boot=0\n`) e `carts/stages.bm` = `mkbm.pack(sdk_template("stages").encode(), title="Stages", author="tests", res=(360, 360))`; `q.expect(MENU)`, `"\r"`, `expect("stages: title best -", timeout=30)`, `send(" ")` (ok = Spazio), `expect("stages: stage 1", timeout=10)`.
- In `tests/rgb30/qemu_test.py`: aggiungere `sdk_template` all'import `from qemu_test import ...` (:21). Nuovo `def test_stages_template(b, opts)` sul modello di `test_bm_cartridge` (:528): `bm/config.txt` = `game_intro=0\n`, `bm/stages.bm` impacchettato come sopra; `"\r"`, `expect("play: /bm/stages.bm")`, `expect("stages: title best -", timeout=30)`, `"\r"` (confirm = ok), `expect("stages: stage 1", timeout=10)`, `"q"`, `expect("fps")`.
- Se un test supera 20 s: aggiungerlo a `SLOW` (qemu_test.py:66) e dichiararlo.
- Verifica: `make` poi `python tests/qemu_test.py -k stages` → exit 0. Poi `make -f rgb30.mk PLAT=virt all` e `python tests/rgb30/qemu_test.py --build build/rgb30-virt -k stages_template` → exit 0.

## APP-4d — dall'SDK RGB30 al `.bm` (bm-light, 45 min)
- File: `tests/rgb30/qemu_test.py` (un test nuovo). `def test_sdk_stages_build(b, opts)`:
  - Apertura dell'SDK come in `test_sdk_tool` (:792-799): Dev → bm SDK, `expect("bm SDK\r\n")`.
  - Ctrl+N (`"\x0e"`), schermo "new project from a template". Scendere alla nona riga col tasto giù dei dialoghi dell'SDK (riscontrarlo in `tests/qemu_test.py` `test_editor` ~7119 e in `D.choose`), Invio. Schermo "new project (Stages 2D)".
  - Ctrl+B (`"\x02"`): Save as, schermo "file name". Invio (nome proposto MYGAME.BME). Schermo con "built /carts/MYGAME.BM": cercare solo questa parte (resta valida anche dopo B16-D28FU). Poi Ctrl+\ (`"\x1c"`).
  - Dopo `q.close()`, con mtools come in `test_bm_cartridge`/Market (:741): `/carts` contiene MYGAME.BME e MYGAME.BM. MYGAME.BM inizia con `b"BMCART\x00\x00"`. La sezione 1, letta come in `tests/qemu_test.py` ~7257-7260, contiene "stages: title best".
- Verifica: `python tests/rgb30/qemu_test.py --build build/rgb30-virt -k stages` → exit 0 (gira anche APP-4c).
- Se il dialogo con 9 voci esce dal riquadro (regola tool pages in `docs/progress/tools.md` Rules) o non si raggiunge la nona voce: `BLOCKED` con lo screenshot (`--shots`), senza toccare `choose_template()`.

## APP-4e — documenti e coerenza (bm-mechanic, 15 min)
- `docs/GAME-GUIDE.md` ~19 e ~895: aggiungere "Stages 2D (title, 3 stages, ending, saved record; RGB30 SDK)" alla lista dei template.
- `docs/progress/tools.md`, voce "bm SDK (RGB30)" (~20-27): una frase sul template Stages 2D (fasce di mappa, record nello slot 1, test `test_stages_template`/`test_sdk_stages_build`). La riga ~10-11 (`carts/editor`) non si tocca.
- `docs/spec/divergences.toml`: una voce nuova in fondo, ID successivo all'ultimo. Campi: `area = "SDK templates"`, `pi = "carts/editor has no Stages 2D template"`, `rgb30 = "carts/sdk_b16 has the Stages 2D template"`, `cause = "todo"`, `capability = "sdk-templates"`, `status = "open"`, `evidence = "carts/editor/main.lua; carts/sdk_b16/main.lua"`, `action = "port Stages 2D to carts/editor (separate WO)"`.
- Verifica: `python scripts/docs_audit.py score` senza link rotti né orfani nuovi; `python scripts/consistency.py check` senza errori nuovi (la voce è `open`; se il gate la considera bloccante, dichiararlo).

Criterio di fine: le verifiche di a-e passano. Non verificabile senza console: il template sulla RGB30 vera e il `.bm` su un Pi vero li prova l'utente con `./easy_install.sh` dopo l'integrazione (report sul branch `reports`).
Consegna: riga `DELIVERED APP-4 @ claude/sdk-stages-template@<commit> · verifica: make test-sdk-b16; python tests/qemu_test.py -k stages; python tests/rgb30/qemu_test.py --build build/rgb30-virt -k stages; docs_audit score; consistency check → <esito> · <ISO>` in `## Consegne`, poi `FREE`. Se un passo è `BLOCKED` o `FAILED x2`, la riga lo elenca. Agente consigliato: bm-coder (a, b, c), bm-light (d), bm-mechanic (e).
