# API-1 — Budget di prestazioni Pi / RGB30: cartuccia di misura, test QEMU, tabella in docs/PERFORMANCE.md e spiegazione di profile(true) (BETA-4)
Assegnato a NET (16 ott), dopo R-NET5. Decisioni adottate: D1-D6 come raccomandate. Le colonne Pi e RGB30 si riempiono alla fine, con l'utente.
Inizia dopo: assegnazione di SPV (nessuna dipendenza da APP-4/5/6).
Base: `bm-core` aggiornato; branch `claude/perf-budget-cart`. Mai push su `bm-core`.
Perimetro: `carts/budget/main.lua` (nuovo), `carts/budget/map.csv` (nuovo), `Makefile` (una sola riga `title_budget`), `tests/qemu_test.py` (una funzione nuova e una voce in `SLOW`), `tests/rgb30/qemu_test.py` (una funzione nuova), `scripts/perf_table.py` (nuovo), `tests/tools/test_perf_table.py` (nuovo), `docs/PERFORMANCE.md`, `docs/STRESS.md` (una riga di rimando), `docs/progress/graphics.md` (solo "## How it is today" e "## Open work"). NON toccare: `carts/sdk_b16/*`, `tools/pi_*`, `carts/stress/*`, `src/bm/stress.c`, `src/**`, la lista `GAMES` del `Makefile`, `rgb30.mk`, `.github/workflows/*`, `SYNC.md`, `sync/*`.

Fatti verificati dal planner: `docs/PERFORMANCE.md` esiste già (inglese, solo Pi, 9 sezioni numerate: il WO aggiunge una sezione, non crea il file). Esistono lo stress test del Pi (`carts/stress/main.lua`, `src/bm/stress.c`, `docs/STRESS.md`) e `report(kind, text)` (`docs/api/system.md:19`); intestazione dei report in `src/net/report.c:68`. Una cartuccia `carts/<n>/main.lua` si compila con la regola generica `$(BUILD)/carts/%.bm` (`Makefile:215`) senza toccare `GAMES`. Su `claude/ci-repair` `docs/ROADMAP.md` non ha la riga API-1 (c'è solo in `sync/NEXT.md` riga 39).

## API-1a — cartuccia di misura `carts/budget` (bm-coder, 45 min)
- Stesso metodo di `carts/stress/main.lua`, ricopiato e non importato: 20 frame per passo; il carico cresce con `n = max(n+1, n*3//2)`; ci si ferma oltre 40 ms o oltre `max`. Il tempo misurato è la media di `stat(1)` sui 20 frame. Le soglie a 16,667 e 33,333 ms vengono dall'interpolazione lineare, come in `threshold()` (righe 39-54). Fuori scala si scrive `<start` o `>max`.
- Ogni test ha: `setup(n)` a inizio passo (facoltativo), `update(n, f)` in ogni `_update` (facoltativo), `draw(n, f)` in ogni `_draw`. Ogni frame parte da `cls(0x0A0A1E)`.
- I 5 test, nell'ordine (nome, unità, start, max):
  1. `spr16`, `spr`, 16, 30000: come il test 1 di `carts/stress/main.lua` (cella 0 disegnata con `sset` in `_init`, come alle righe 65-69).
  2. `tiles8`, `tile`, 81, 400000: celle 8×8 della mappa (`G16_CELL`). In `_init` la cella 1 dello sheet riempita con `sset` (8×8 pixel, `rgb(40, 160, 80)`). Si disegnano le prime n celle, 81 per riga; riga r: `map(0, r % 46, -(f % 8), (r*8) % 368 - (f % 8), w, 1)` con `w = min(81, n - 81*r)`.
  3. `mesh96`, `obj`, 1, 3000: come il test 2 di stress (sfera `mesh_sphere(6, 8, 0x4080FF, 0xFFC040)`, `zclear`, `camera3d(0, 0, -8, 0, 0, 60)`). La colonna `tris60` interpolata da `stat(4)`.
  4. `part2d`, `part`, 16, 100000: `local lib = require "bmlib"`. In `setup(n)`: `P = lib.particles(n)`, poi n `P:add(i*97 % W, i*61 % H, (i%7-3)*0.25, (i%5-2)*0.25, 1000, 0xFFD050, {size = 1})` per i = 1..n. `P:update()` in update, `P:draw()` in draw.
  5. `point3d`, `pt`, 16, 100000: `zclear`, stessa `camera3d`. Per i = 0..n-1: `point3d(((i*97 + f) % 900)/100 - 4.5, ((i*61) % 500)/100 - 2.5, (i % 7)*0.5, 2, 0xFFD050)`.
- Fase profile, solo se la soglia a 60 fps di `spr16` è un numero: si rifà `spr16` a quel carico per 90 frame con `profile(true)`; al frame 90 si legge `profile()`, si tengono le prime 10 voci e si chiama `profile(false)`.
- Testo del report, esattamente queste righe nell'ordine: `bm budget 1`; `screen <SCREEN_W>x<SCREEN_H>`; `gpu3d <stat(9)>`; `test,unit,n60,n30,us_item,tris60`; una riga per test `<nome>,<unità>,<n60>,<n30>,<us %.2f>,<tris60 oppure vuoto>`; `profile_load,spr16,<n>`; fino a 10 righe `profile,<name>,<where>,<self %.3f>,<total %.3f>`.
- Alla fine: ogni riga in `log("budget: " .. riga)`; poi `ok = report("budget", testo)`, `log("budget: report " .. tostring(ok))`, `log("budget: done")`. La tabella resta a schermo 600 frame, poi `quit()`. Durante i test la riga in alto è come nello stress (`_draw`, righe 97-98).
- `carts/budget/map.csv`: 46 righe di 81 valori `1` separati da virgole.
- `Makefile`: una riga `title_budget := bm budget` dopo `title_overbit := Overbit` (riga 299). Niente `bm.info` né `res_budget`: risoluzione di default 640x360.
- Verifica: `make build/carts/budget.bm` exit 0 e il file esiste. Il funzionamento lo prova API-1b.

## API-1b — test QEMU Pi e RGB30 (bm-coder, 30 min)
- `tests/qemu_test.py`: nuova `test_budget_cart(b, opts)` sul modello di `test_permissions` (righe 5530-5583). SD con `bm/config.txt` = `layout=us\nwifi_boot=0\n`. `_upload(q, open(b("carts/budget.bm"),"rb").read())`. Attendere `asks to send a report (ok: allow, back: no)` (timeout 600), rispondere `"j"`. Attendere `budget: report true` e `budget: done` (timeout 60), poi `"> "`. Nel log: `budget: test,unit,n60,n30,us_item,tris60` e una riga `budget: spr16,`. Dopo la chiusura, `mtype` su `::/BM/REPORTS/RPT00001.TXT`: inizia con `bm report\nkind: budget\n` e contiene `bm budget 1`. In `SLOW`: `"test_budget_cart": 120`.
- `tests/rgb30/qemu_test.py`: nuova `test_budget_cart(b, opts)` sul modello di `test_bm_cartridge` (righe 528-561) e `test_bench3d` (564-607). `make_sd(tmp, {"bm/budget.bm": <bytes di build/carts/budget.bm>, "bm/config.txt": b"layout=us\n"})`. Inviare `"\r"` per giocare, attendere `play: /bm/budget.bm`. Al prompt del report `"\r"`. Attendere `budget: done` (timeout 900). Schermo 640x360. Poi `pmemsave` e `mtype` su `::/bm/reports/RPT00001.TXT`: contiene `kind: budget` e `board: QEMU virt`.
- Se il tasto ok del prompt su RGB30 non è `"\r"`: `BLOCKED` verso SPV, senza inventare un tasto.
- Verifica: `make && make build/carts/budget.bm && python tests/qemu_test.py -k budget` PASS; `make -f rgb30.mk PLAT=virt all && make build/carts/budget.bm && python tests/rgb30/qemu_test.py -k budget` PASS.

## API-1c — `scripts/perf_table.py`, compilatore della tabella (bm-coder, 30 min)
- CLI: `python scripts/perf_table.py --column pi|rgb30 REPORT.TXT [--doc docs/PERFORMANCE.md] [--allow-qemu]`.
- Legge l'intestazione di `src/net/report.c:68` (`kind`, `board`, `kernel`, `date`) e richiede `kind: budget`. Se `board:` contiene `QEMU` (senza maiuscole) e manca `--allow-qemu`: stderr `QEMU report: not a hardware measurement`, exit 1. Nel corpo legge le righe dei test dopo `test,unit,...` fino a `profile_load`.
- Riscrive solo la parte tra `<!-- budget:begin -->` e `<!-- budget:end -->`: tabella `| Load | Unit | Pi Zero W (60 / 30 fps) | RGB30 (60 / 30 fps) |`, una riga per test più riga finale `| source | | ... | ... |`. Cella: `n60 / n30`; per `mesh96` `n60 / n30 (tris60 tri)`. Cella `source`: `<board>, <kernel>, <date>, <screen>`. Tocca solo la colonna scelta.
- Exit 0 se riscrive; 1 se report non valido, QEMU o marcatori mancanti; 2 per errori d'uso o I/O.
- Test `tests/tools/test_perf_table.py` con fixture testuali: report valido su rgb30 (colonna pi intatta), report QEMU rifiutato, accettato con `--allow-qemu`, marcatori assenti, `kind` diverso.
- Verifica: `python tests/tools/test_perf_table.py` OK.

## API-1d — documenti (bm-light, 30 min)
- `docs/PERFORMANCE.md`: nuova sezione `## Game budget: Pi Zero W and RGB30` subito dopo il blocco **Sources**, senza numero (le sezioni 1-9 non si rinumerano): cosa misura la cartuccia (5 test, metodo, `stat(1)` comprende `_update` e `_draw`); come si lancia (`make build/carts/budget.bm`, copia in `bm/` sulla SD o `./easy_install.sh` voce 5); dove arriva il report (`bm/reports/RPT*.TXT`, branch `reports` con `github_token` e rete); come si compila la tabella (`scripts/perf_table.py`); la tabella tra i marcatori con celle `—`; il limite di QEMU (niente cache né bus, Pi in QEMU circa 2 volte più lento, rimando alla sezione 6; QEMU non entra in tabella).
- Sottosezione `### Reading profile(true)` (da `docs/api/system.md:13` e `docs/API.md:150-156`): `self` tempo proprio, `all`/`total` con le funzioni chiamate, in ms per frame, media dell'ultimo secondo; C misurate esatte, Lua a blocchi di 1000 istruzioni; spento non costa, acceso costa un po' (i numeri del budget a profiler spento); le righe `profile,...` del report sono un esempio vero dal report QEMU di API-1b, etichettato *(Q)*; rimando alla classifica delle 10 funzioni nel session report.
- Titolo del file: `# Performance and Technical Decisions — Raspberry Pi Zero W and RGB30`.
- `docs/STRESS.md`: in fondo a "## Come eseguirlo sul Pi", una riga di rimando alla sezione budget.
- `docs/progress/graphics.md`: in "## How it is today" una riga su `carts/budget` e `scripts/perf_table.py`; in "## Open work" la riga API-1 con la colonna RGB30 da riempire.
- `docs/ROADMAP.md` solo se `grep -n 'API-1' docs/ROADMAP.md` trova la riga su `bm-core`.
- Verifica: `python scripts/docs_audit.py score` senza link rotti né orfani. Con colonne vuote: `python scripts/perf_table.py --column pi <report QEMU di API-1b> --allow-qemu --doc <copia temporanea>` exit 0.

Criterio di fine: verifiche di API-1a…d OK. Consegna: una riga DELIVERED standard (anche una per i quattro passi), poi FREE. Le colonne Pi e RGB30 si riempiono con le prove sotto, fuori da questo WO.

## Readiness Gate
| Sezione | File | Determinismo | Verifica | ≤3 file | Nessun blocco |
|---|---|---|---|---|---|
| API-1a | SÌ | SÌ | SÌ (build; funzionamento in 1b) | SÌ (3) | SÌ dopo D1, D2 |
| API-1b | SÌ | SÌ, tranne il tasto ok su RGB30 (BLOCKED previsto) | SÌ | SÌ (2) | dipende da 1a |
| API-1c | SÌ | SÌ | SÌ | SÌ (2) | SÌ |
| API-1d | SÌ | parziale (testo: bm-light) | SÌ | NO (4 file, piccole modifiche di testo) | dipende da 1b e D3 |

## Prove per l'utente (passi, nessun ordine)
- RGB30: `make build/carts/budget.bm`, copiarla in `bm/` della SD (`./easy_install.sh` voce 5), lanciarla dal menu (`.bm` visibili, senza `show_bm=0`), confermare con B il prompt del report, aspettare la fine (tabella a schermo 10 s).
- Report RGB30: dal branch `reports` (`git fetch origin reports && git ls-tree -r --name-only origin/reports | grep _budget_`, poi `git show origin/reports:<percorso> > /tmp/budget_rgb30.txt`) o da `bm/reports/RPT*.TXT` sulla SD; in alternativa una foto della tabella.
- Colonna RGB30: `python scripts/perf_table.py --column rgb30 /tmp/budget_rgb30.txt`, poi commit di `docs/PERFORMANCE.md` sul branch.
- Pi via bridge (op-net, solo dopo 'ok' scritto dell'utente all'upload): l'utente avvia `python3 tools/pi_bridge.py <ip>`; op-net manda `play build/carts/budget.bm`. Alla prima esecuzione serve A sul pad del Pi per permettere il report (la risposta resta in `bm/config.txt`). Report e tabella come per l'RGB30, con `--column pi`.
- Facoltativa: ripetere su RGB30 con la 3D sulla GPU quando M41 (Mali) sarà attivo (il report riporta `gpu3d`).

## Decisioni per SPV
- D1. Cartuccia nuova `carts/budget` o estensione di `carts/stress/main.lua`? Raccomando nuova: `stress.bm` è incorporato nel kernel (`Makefile:106`), ha test propri (`test_stress_monitor`, `test_bench_stop`) e una serie storica in `docs/STRESS.md` che si romperebbe.
- D2. Risoluzione: solo 640x360 o anche 360x360/720x720 sull'RGB30? Raccomando solo 640x360 (colonne confrontabili); altre con un WO successivo (`--res`).
- D3. Colonna Pi: vuota fino alla misura, o riempita subito con i numeri di `docs/STRESS.md` (2026-10-05)? Raccomando vuota (cartucce diverse).
- D4. `budget.bm` nella lista `GAMES`? Raccomando no: è uno strumento; si compila col target esplicito.
- D5. Frame rate: 60 e 30 fps come lo stress (le uniche usate dalle guide, `docs/GAME-GUIDE.md:179`)? Raccomando sì.
- D6. Il test QEMU Pi aggiunge circa 2 minuti alla CI: accettarlo con la voce in `SLOW`, o metterlo in `tests/quarantine.txt` se pesa. Raccomando di accettarlo.
