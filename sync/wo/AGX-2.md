# AGX-2 - STAGES-FLOW: il record (D4) e una corsa con la geometria vera nel test host di Stages 2D (da APP-4)
Assegnato ad AGX (16 ott), secondo dei due. DIPENDE da AGX-1 (stesso file `tests/studio/sdk_b16_host.lua`, e senza gli stub di AGX-1 `make test-sdk-b16` non arriva ai check di Stages 2D): inizia solo dopo `DELIVERED AGX-1` e la sua approvazione da parte di SPV.
Base: il branch di AGX-1 `claude/sdk-b16-host-stubs`, con dentro la consegna di APP-4 (`DELIVERED APP-4 @ claude/sdk-stages-template@68c0c10d`, `sync/B16.md`, Consegne). Branch: `claude/stages-flow-checks`. Mai push su bm-core.
Perimetro: `tests/studio/sdk_b16_host.lua`, solo la funzione `stages_flow` (APP-4, oggi righe ~996-1066): due righe cambiate nella funzione interna `play` e un blocco di righe nuove in fondo alla funzione, dopo il check `cont`. Nessun altro punto del file.
NON toccare: `carts/sdk_b16/main.lua` (il template Stages 2D e il resto dell'SDK), `tests/qemu_test.py`, `tests/rgb30/qemu_test.py`, `sync/wo/APP-4.md` (file di SPV, vedi "Per SPV" in fondo), `sync/*`, `SYNC.md`, `docs/*`, `Makefile`, gli stub di AGX-1 (righe vicine a `E.camera3d`).
Vincoli: solo test host, nessun hardware (`not verified on hardware`). Nessun check esistente si allenta o si toglie. Ogni `file:riga` qui è stato aperto dal planner sul branch di APP-4; l'operatore lo riscontra con `grep -n` (le righe slittano dopo il merge) e ciò che non riscontra lo scrive `non verificato`.
Obiettivo: `stages_flow` controlla anche (1) che il record `best` salvato resti quello della corsa 1 quando si continua dallo stage 3 (regola D4 di APP-4: il record cambia solo se la partita parte dallo stage 1) e (2) una corsa con la mappa vera del template (terreno, buco, asse), in cui l'eroe cade tre volte nel primo buco e arriva al game over.

## Stato reale (verificato dal planner: ho applicato questo WO sopra APP-4 + gli stub di AGX-1 ed eseguito `make test-sdk-b16`)
- `stages_flow(code)` (APP-4, `sdk_b16_host.lua` ~996): la funzione interna `play(n, pulse)` crea un `G2` copiato da `E` con `G2.mget = function() return 7 end` (ogni stage finisce subito), `G2.btn` sempre falso, `G2.btnp` guidato da `ok_now`/`down_now`, `G2.log` che riempie `logs`, `G2.save`/`G2.saved` su `store.v`. Corsa 1 (da stage 1): salva `best`; corsa 2 (stesso `store`, CONTINUE STAGE 3): oggi controlla solo il titolo e la riga "stages: stage 3".
- Regola D4 nel template (`carts/sdk_b16/main.lua`, in `update_play` del codice `stages`): `if from_start and (not best or timer < best) then best = timer end`, poi `save({ best = best, reached = reached })`. Dalla corsa 2 `from_start` è falso, quindi `store.v.best` deve restare quello della corsa 1.
- Il test può usare la mappa vera perché `stages_flow` viene chiamata subito dopo il Save as del template, nello stesso ambiente host: `paint()` del template ha già riempito la mappa (`E.mget`/`E.mset`, layer 1) e i flag (`E.fset`). Tile: 2 = terreno (flag 0, valore 1), 6 = asse (flag 1, valore 2), 7 = uscita. Stage 1: terreno sulla riga 40 (colonne 0..127 tranne il buco 30..33), assi sulla riga 34 (x 12..17) e 35 (x 40..46), uscita in (120, 38).
- Ostacolo: `bmlib` (`src/script/bmlib.lua`, `lib.step` -> `lib.move` -> `blocked`, :264-288) legge i flag con `mflags(x, y, w, h, layer)`. L'host definisce `E.mflags = function() return 0 end` (`sdk_b16_host.lua` ~ riga 497 dopo AGX-1: `grep -n "E.mflags"`), e `bmlib` viene caricato nell'ambiente `E` e messo in cache (`E.require`, ~661): quindi sovrascrivere `G2.mflags` NON basta, bisogna sostituire `E.mflags` per la durata della corsa e rimetterlo dopo.
- Risultato del planner: `sdk_b16_host: 319 checks, 0 failed`, `EXIT=0`, circa 13 minuti di orologio in WSL (la suite intera, vedi AGX-1). La corsa 3 aggiunge 5 check e qualche centinaio di frame: tempo trascurabile.

## AGX-2a - le modifiche (bm-coder, 20 min di lavoro + attesa della suite)
Passo 0: partire dal branch giusto (nel worktree di AGX, con AGX-1 già nel repo locale):
```
git switch claude/sdk-b16-host-stubs
git switch -c claude/stages-flow-checks
git merge --no-edit origin/claude/sdk-stages-template
```
Il merge deve passare senza conflitti (i punti toccati da APP-4 e da AGX-1 sono lontani: stubs ~446 / riga 2726 di main.lua contro `stages_flow` e `TEMPLATES`). Conflitto o merge che non parte: `BLOCKED: merge <file>`, senza risolverlo a mano. Poi `grep -n "local function stages_flow" tests/studio/sdk_b16_host.lua` deve dare una riga.
Fine riga: il file ha CRLF. Dopo le modifiche `git diff --stat` deve dire poche decine di righe in `tests/studio/sdk_b16_host.lua` (circa 31 inserite, 2 sostituite); se risulta tutto il file riscrivere mantenendo CRLF (`git checkout -- tests/studio/sdk_b16_host.lua` e rifare).
Ambiente: se `/tmp` di WSL è pieno (`wsl -e bash -c "df -h /tmp | tail -1"` dice 100%), lanciare `make` con `TMPDIR=$PWD/build/tmp` come in AGX-1 (passo 0).

Passo 1: due righe nella funzione interna `play`.
- La riga `  local function play(n, pulse)` diventa `  local function play(n, pulse, geo)`.
- Subito dopo la riga (unica nella funzione) `    G2.mget = function() return 7 end` aggiungere questa riga:
```
    if geo then G2.mget, G2.btn = E.mget, function(i) return i == 1 end end   -- the real map; right held
```

Passo 2: il blocco nuovo. Cercare il check (unico) che finisce con `"Stages 2D run 2: CONTINUE STAGE 3: stage 3 in the logs")` seguito dalla riga `end` che chiude `stages_flow`. Inserire questo blocco subito dopo quel check e PRIMA di quell'`end` (rientro di 2 spazi, come le righe vicine; il blocco non contiene l'`end` finale della funzione):
```
  -- D4: a run that continues at stage 3 does not touch the best (it counts only from stage 1)
  check((best or 0) > 1 and store.v ~= nil and store.v.best == best,
        "Stages 2D run 2: the best stays " .. tostring(best) .. ": " .. tostring(store.v and store.v.best))

  -- run 3, real geometry: the map the template painted (ground tile 2 flag 0, plank tile 6 flag 1, exit 7)
  -- and mflags read from it. Right held in stage 1: the hero walks into the first gap, falls, starts again;
  -- three falls and it is game over.
  local function mfl(x, y, w, h)
    local o = 0
    for ty = y // 8, (y + math.max((h or 0) - 1, 0)) // 8 do
      for tx = x // 8, (x + math.max((w or 0) - 1, 0)) // 8 do o = o | E.fget(E.mget(tx, ty)) end
    end
    return o
  end
  check(E.mget(120, 38) == 7 and E.mget(5, 40) == 2 and E.mget(31, 40) == 0 and E.mget(12, 34) == 6,
        "Stages 2D map: exit at 120,38, ground at 5,40, a gap at 31,40, a plank at 12,34")
  check(mfl(40, 320, 8, 8) & 1 == 1 and mfl(248, 320, 8, 8) == 0 and mfl(96, 272, 8, 0) & 2 == 2,
        "Stages 2D map: the ground is solid, the gap is empty, the plank is a platform")
  local real_mflags = E.mflags
  E.mflags = mfl
  store.v, logs = nil, {}
  play(900, function(f) return f % 10 == 0, false end, true)
  E.mflags = real_mflags
  local want, j = { "stages: stage 1", "stages: lost 2", "stages: lost 1", "stages: lost 0", "stages: gameover" }, 1
  for _, l in ipairs(logs) do if l == want[j] then j = j + 1 end end
  check(j == #want + 1, "Stages 2D run 3: three falls into the gap, game over; missing: " .. tostring(want[j]))
  check(store.v == nil, "Stages 2D run 3: nothing saved before an ending")
```
Conteggio: circa 31 righe nuove (sotto il tetto di 40 del brief). Spiegazione dei numeri, per chi rilegge: `mfl` è `mflags` in pixel sulla mappa vera (un punto se `w`/`h` sono 0; `E.fget(tile)` senza flag dà il byte intero, flag 0 = valore 1, flag 1 = valore 2); la corsa 3 tiene premuto "destra" (`btn(1)`), preme "ok" ogni 10 frame (il primo, al frame 10, sceglie NEW GAME dal titolo); l'eroe parte in x=16 e cammina verso il buco di x 240..271 (colonne 30..33), cade sotto la riga 45 (360 px) e perde una vita: "lost 2", "lost 1", "lost 0" e "gameover"; 900 frame bastano con ampio margine (servono circa 450).

Passo 3: verifica, con tempo massimo (come AGX-1 passo 3).
- In background con l'output su file: `scripts/wsl-run.ps1 "make test-sdk-b16; echo EXIT=\$?" -Tail 30`; leggere il file ogni 5 minuti.
- Atteso: `sdk_b16_host: 319 checks, 0 failed` (il numero può differire se APP-4 o AGX-1 cambiano; deve essere 0 failed) e `EXIT=0`; nell'output NON deve comparire nessuna riga `FAIL Stages 2D`.
- Tetto: 30 minuti di orologio. Oltre: fermare il tuo processo, `BLOCKED: test-sdk-b16 oltre 30 min`, riportare minuti, `wsl -e uptime`, ultima riga di output.
- Se un check nuovo fallisce: riportare la riga `FAIL` intera. Non cambiare i numeri dei test per farli passare; il caso più probabile è `Stages 2D map: ...` (la mappa non è dove il template la mette: significa che `stages_flow` non è più chiamata subito dopo il Save as, riportare e fermarsi: `BLOCKED`) oppure `run 3 ... missing: stages: lost N` (riportare `logs` con un `io.write` temporaneo, da togliere prima del commit).
- Controllo di sensibilità (facoltativo, costa un'altra esecuzione della suite): cambiare temporaneamente nel test `store.v.best == best` in `store.v.best == best + 1`, rilanciare e vedere `FAIL Stages 2D run 2: the best stays`; poi ripristinare prima del commit. Se non si fa: scrivere nella consegna `non verificato che il check fallisca sul caso sbagliato` (il planner non l'ha fatto).
- `git diff --stat` finale: solo `tests/studio/sdk_b16_host.lua`.

Criterio di fine: `make test-sdk-b16` → exit 0, 0 failed, con i 5 check nuovi (`the best stays`, `Stages 2D map` x2, `run 3`, `nothing saved`).
Commit: un solo commit sul branch `claude/stages-flow-checks`, messaggio `tests: Stages 2D flow checks the best (D4) and a run on the real map`. Push solo di questo branch.
Consegna: `DELIVERED AGX-2 @ claude/stages-flow-checks@<commit> · verifica: make test-sdk-b16 -> exit 0, <N> checks, 0 failed (<M> min) · 5 check nuovi, 1 file · non verificato su hardware · <ISO>` in `## Consegne` di `sync/AGX.md`, poi FREE. Agente consigliato: bm-coder.
Stima: 20 minuti di lavoro, 15-20 di attesa per la suite.

## Per SPV (fuori dal perimetro di AGX, non lo fa l'operatore)
- `sync/wo/APP-4.md`, riga 63 (APP-4c, test RGB30): nel secondo `"\r"` (`"\r"` (confirm = ok)` dopo `expect("stages: title best -", timeout=30)`) il tasto per l'ok del gioco sul RGB30 virt non è Invio: B16 ha provato `""`, `' '`, `''` senza esito e ha usato il tasto seriale `x` (consegna APP-4, `sync/B16.md` Problemi). Il testo diventa `"x"` (confirm = ok, tasto seriale; Invio e Spazio non funzionano nel gioco). Il primo `"\r"` (menu) resta.
- Integrazione: B16-D28FU (`claude/sdk-games-message`) e APP-6 (`claude/sdk-b16-dev-without-pc`) toccano `sdk_b16_host.lua` in altri punti; l'ordine proposto è AGX-1, poi APP-4, poi AGX-2 (che contiene già il merge di APP-4).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Il caso "geometria vera" è il game over per tre cadute nel buco (nessun salto: l'eroe cammina a destra), non l'attraversamento con salto: è deterministico e sta nelle 40 righe; la prova dell'asse è un controllo dei flag (`mfl(...) & 2`), non una corsa. Raccomandata: sì. Alternativa: una corsa con salti calcolati a frame (più fragile, nessuna ragione per ora).
- D2 `E.mflags` viene sostituito solo durante la corsa 3 e rimesso subito dopo (`play` usa `pcall`, quindi il ripristino avviene anche se la corsa fallisce). Raccomandata: sì. Alternativa: sostituirlo per tutto il file (cambierebbe il comportamento delle altre corse: no).
- D3 Il controllo di D4 usa `best > 1`: se il record valesse 1 la regola violata (`timer < best` con timer 1 al frame di uscita) non si vedrebbe. Il record della corsa 1 è di centinaia di frame, quindi il controllo è sensibile.

## Prove per l'utente
Nessuna.
