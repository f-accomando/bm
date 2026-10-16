# AGY-11 - BETA6-GATE: `consistency.py beta`, il criterio BETA-6 controllato da comando (BETA-6)
Assegnato ad AGY (16 ott), indipendente da AGY-10 e BETA-7. Decisioni D1-D4 adottate come raccomandate (raccomandazioni sotto).
Base: `bm-core` aggiornato; branch `agy/beta6-gate`. Mai push su bm-core.
Perimetro: `scripts/consistency.py` (docstring righe 5-8, una funzione nuova `beta(root)` dopo `gate()` che finisce a :302, parser :321-323, dispatch :330-331), `docs/spec/beta.toml` (nuovo), `tests/tools/test_consistency_beta.py` (nuovo), `docs/spec/CONSISTENCY.md` (tabella "The files" righe 30-37: una riga; riga 47: una frase).
NON toccare: `docs/spec/divergences.toml`, `platforms.toml`, `known.toml`, `specs.toml` (sola lettura; `divergences.toml` lo scrivono APP-4/APP-6), le funzioni esistenti di `consistency.py` (`run`, `gate`, `rule_*`, `divergences()`), `docs/ROADMAP.md`, `docs/progress/*`, `Makefile`, `carts/*`, `src/*`, `tools/*`, `manuals/*`, `.github/workflows/*`, `SYNC.md`, `sync/*`.
Obiettivo: `python scripts/consistency.py beta` dice quali divergenze del perimetro BETA-6 (D01-D13, D28, D30-D36) bloccano ancora l'apertura della beta. Una voce passa se è `resolved`, se è `hardware` con una capability di `platforms.toml`, oppure se è accettata per iscritto in `docs/spec/beta.toml`. Esce con 1 finché ne resta una.
Regola di evidenza: ogni `file:riga` qui è stato aperto dal planner; l'operatore lo riscontra con grep prima di usarlo; ciò che non verifica lo scrive `not verified`.

## Stato reale (verificato dal planner, su bm-core)
- Criterio: `docs/ROADMAP.md:36` ("no `open` or `untested` divergences touching menu, input, Games, SDK and saves (D01-D13, D28, D30-D36 and similar); the others accepted in writing"). Oggi nessun comando lo misura: `gate()` (`scripts/consistency.py:278-302`) conta solo `open`/`suspended` con cause bloccanti (:288) e stampa gli `untested` come "TRY ON THE CONSOLE" (:298-300), su tutte le voci.
- Campi di una voce: `id`, `area`, `pi`, `rgb30`, `cause`, `priority`, `status`, `note`, `evidence`, `action`, e `capability` per le `hardware` (`divergences.toml:9-19`, :350-359). Status ammessi: `open|suspended|untested|resolved` (`consistency.py:233`); cause: `KINDS` (:274). Capability: `[[capability]] id` (`platforms.toml:27-60`, letta così a `consistency.py:229`).
- Su bm-core: D01-D13 sono `history`/`unknown` (D01, D02, D06, D07, D08, D13 `untested`; D03, D05 `suspended`; D04, D09-D12 `open`). D28 è `todo`/`open` (`divergences.toml:318-324`). D30-D36 sono `hardware` con capability `square-panel`, `pad-only`, `no-keyboard`, `storage-path`, tutte presenti in `platforms.toml` (`divergences.toml:334-401`). D29 non esiste.
- `divergences()` legge da `ROOT` fisso (:220-222): la nuova funzione legge i file da un `root` passato, per poterla provare su una radice temporanea.

## AGY-11a - comando `beta` e `docs/spec/beta.toml` (bm-coder, 40 min)
- `docs/spec/beta.toml`: commento di testa di 3 righe (che cos'è, che `scope` riprende `docs/ROADMAP.md` BETA-6, che le `[[accept]]` le scrivono solo SPV o l'utente). Poi `scope = ["D01", "D02", ..., "D13", "D28", "D30", ..., "D36"]` (21 id, in ordine). Nessuna `[[accept]]`. Formato di una `[[accept]]`, descritto nel commento: `id`, `because`, `by`, `date` (`YYYY-MM-DD`).
- `consistency.py`:
  - docstring, dopo la riga 7: `  scripts/consistency.py beta [--root DIR]   what still blocks the beta (BETA-6): docs/spec/beta.toml`.
  - parser: `b = sub.add_parser("beta")`, `b.add_argument("--root", default=ROOT)`; dispatch `if a.cmd == "beta": return beta(a.root)`.
- `beta(root)` legge `root/docs/spec/divergences.toml`, `platforms.toml` e `beta.toml` con `tomllib`. File mancante o toml non valido: `ERROR  <percorso>: <motivo>` su stderr, return 2.
- Per ogni id di `scope`, nell'ordine, la prima regola che vale:
  1. assente nelle divergenze → `ERROR  <id>: not in docs/spec/divergences.toml`;
  2. `status == "resolved"` → `resolved     <id>`;
  3. `cause == "hardware"` e capability tra quelle di `platforms.toml` → `accepted     <id> (hardware: <capability>)`;
  4. `cause == "hardware"` senza capability valida → `BLOCKS BETA  <id> (hardware, <status>): <area>: no capability of platforms.toml`;
  5. id con una `[[accept]]` → `accepted     <id> (in writing, <by> <date>): <because>`;
  6. altrimenti → `BLOCKS BETA  <id> (<cause>, <status>): <area>: <action>`.
- Controlli sulle `[[accept]]`, dopo il ciclo:
  - id fuori da `scope` → `ERROR  accept <id>: not in scope`;
  - campo `id|because|by|date` mancante o vuoto, oppure `date` che non è `YYYY-MM-DD` → `ERROR  accept <id>: <campo> missing or invalid`;
  - id con status `resolved` → `STALE  accept <id>: resolved, remove it`;
  - id duplicato → `ERROR  accept <id>: duplicate`.
- Ultima riga: `beta: <B> blockers, <A> accepted, <R> resolved, <E> errors`. Return 1 se `B + E > 0` (STALE conta in E), altrimenti 0.
- `tests/tools/test_consistency_beta.py` (unittest + subprocess, sul modello di `tests/tools/test_agent_audit.py`), radice temporanea con i tre toml minimi. Casi:
  - tutte resolved o hardware valide → 0;
  - una `history` `open` → 1, con riga `BLOCKS BETA  D01`;
  - `untested` → 1;
  - `suspended` → 1;
  - la stessa con `[[accept]]` → 0, con `accepted     D01 (in writing`;
  - `hardware` con capability assente → 1;
  - id di scope assente → 1 `ERROR`;
  - accept fuori scope → 1;
  - accept senza `because` → 1;
  - accept su una resolved → 1 `STALE`;
  - accept duplicata → 1;
  - `beta.toml` mancante → 2.
- Verifica:
  - `python tests/tools/test_consistency_beta.py` → OK.
  - `python scripts/consistency.py beta` → exit 1. Atteso su bm-core (dedotto, da confermare): 14 righe `BLOCKS BETA` (D01-D13, D28), 7 `accepted ... (hardware: ...)` (D30-D36), `beta: 14 blockers, 7 accepted, 0 resolved, 0 errors`. Se i numeri differiscono: riportare l'uscita, non è BLOCKED.
  - `python scripts/consistency.py check` → stesso output di prima del WO.

## AGY-11b - documento (bm-mechanic, 10 min)
- `docs/spec/CONSISTENCY.md`, tabella "The files" (righe 30-37): dopo la riga di `known.toml` (:36), `| \`docs/spec/beta.toml\` | the divergences that must be closed to open the beta (BETA-6) and the ones accepted in writing, with who and when |`.
- Riga 47, dopo `**\`gate\` lists what still blocks 1.0.0.**`: ` \`beta\` lists what still blocks the beta: a divergence of its scope passes only if resolved, hardware with a capability, or accepted in \`beta.toml\`.`
- Verifica: `python scripts/docs_audit.py score` senza righe `!` nuove rispetto a bm-core (lanciarlo prima e dopo); `python scripts/consistency.py check` invariato.

Criterio di fine: verifiche di 11a e 11b OK.
Consegna: `DELIVERED AGY-11 @ agy/beta6-gate@<commit> · verifica: test_consistency_beta; consistency.py beta -> exit 1, <B> blockers <A> accepted; consistency.py check invariato; docs_audit score -> <esito> · <ISO>` in `## Consegne` di `sync/AGY.md`, poi FREE. Agente consigliato: bm-coder (11a), bm-mechanic (11b).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 perimetro della beta: elenco esplicito `scope` in `beta.toml`, senza ricerca per parole nell'`area`. "and similar" di ROADMAP:36 lo allarga SPV aggiungendo un id. Raccomandata: sì (deterministico).
- D2 `hardware` con capability valida = accettata senza `[[accept]]`, come vuole la regola di `CONSISTENCY.md:11`. Raccomandata: sì. Alternativa: chiedere un'`[[accept]]` anche per D30-D36.
- D3 l'accettazione scritta sta in un file nuovo `docs/spec/beta.toml`, non in un campo di `divergences.toml`: così il file resta disgiunto da APP-4/APP-6 e i dati della beta restano separati da quelli di 1.0.0. Raccomandata: sì.
- D4 `beta.toml` nasce senza `[[accept]]`: le classi del triage (AGY-9) le trasforma in accettazioni solo SPV o l'utente, in un passo a parte. Raccomandata: sì.

## Prove per l'utente
Nessuna.
