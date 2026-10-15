# Protocollo di sincronizzazione multi-operatore (`SYNC.md`)

> Template riusabile. Per un nuovo progetto: **crea il branch di sync** `sync/protocollo-ottobre` da `bm-core`, copia questo file, sostituisci i `{segnaposto}`, riempi §2 e §3 e svuota il Log (§5). Il sync di un progetto vive solo su quel branch: così più sessioni di sync (altri agenti, altri progetti) possono correre in parallelo senza toccarsi.
> Esempio completo di un progetto reale (Tropic Trail, 8 video, 3 operatori): [`docs/archive/sync-tropic-trail.md`](docs/archive/sync-tropic-trail.md).
> Cosa abbiamo imparato: [`docs/progress/sync-workflow.md`](docs/progress/sync-workflow.md). Regole d'area: [`docs/progress/sync.md`](docs/progress/sync.md).

- **Progetto**: protocollo-ottobre (R-NET test Pi + M42 `.b16`) · **Repo**: `f-accomando/bm` · **Base del codice**: `bm-core` · **Branch di sync**: `sync/protocollo-ottobre` · **Governance**: [`AGENTS.md`](AGENTS.md)
- **Avviato**: 2026-10-15 · **Supervisore (SPV)**: Claude local (Sonnet 5.5) · **Utente**: decide, fa i merge finali

---

## 1. Ruoli e file (un solo scrittore per file)

| Operatore | ID | Dove gira | Scrive | Branch |
|---|---|---|---|---|
| Claude local | `SPV` | locale, Windows | `SYNC.md` + `sync/SPV.md` + `sync/inbox/*` + `sync/wo/*` | `sync/protocollo-ottobre` |
| operatore R-NET (da handshake) | `NET` | da dichiarare nel profilo | `sync/NET.md` | `claude/net-test-*` |
| operatore M42 (da handshake) | `B16` | da dichiarare nel profilo | `sync/B16.md` | `claude/b16-profile` |
| Antigravity (AGY) | `AGY` | locale, QEMU; accesso al Pi in sviluppo | `sync/AGY.md` | `agy/sdlog` |
| operatore CI (da handshake) | `CI` | da dichiarare nel profilo | `sync/CI.md` | `claude/ci-repair` |
| utente | `UTE` | - | niente (parla nelle sessioni) | - |

1. `SYNC.md` lo scrive **solo SPV**: contratti, work order, decisioni, tabella di stato (§1b), avvisi a tutti.
2. Ogni altro operatore scrive **solo il proprio** `sync/<ID>.md`: stato, task, progressi, problemi, richieste. Tutti leggono tutto.
3. Gli operatori sono elencati in [`sync/operators.toml`](sync/operators.toml) (lo scrive SPV; nuovo operatore = nuovo blocco).
4. **File di sync e righe di Log si pushano solo sul branch di sync** `sync/protocollo-ottobre` (`git pull --rebase origin sync/protocollo-ottobre`, mai commit di merge): `SYNC.md`, `sync/*` e `tools/sync_hdr.py` non si toccano su `bm-core` durante il progetto. Il **codice** resta sui branch di lavoro (da `bm-core`) e arriva su `bm-core` solo dopo la review di SPV (§3c) e con il consenso dell'utente. A fine progetto SPV archivia `SYNC.md` in `docs/archive/` su `bm-core` e il branch di sync si chiude.
5. SPV scrive le righe da un **worktree separato** del branch di sync, per non cambiare branch di lavoro.
6. Le richieste fra operatori passano da SPV; le urgenze si scrivono nel proprio file e SPV le smista. Se un operatore è bloccato e muto, l'unico canale è un messaggio dell'utente nella sua sessione.

### Stati (stessi nomi ovunque)

| Stato | Significato |
|---|---|
| `WORKING {task}` | lavora a quella task |
| `WAITING {chi}` | bloccato finché non agisce `utente`, un altro operatore o un servizio |
| `FREE` | task concordate finite e pushate, disponibile ad altro lavoro |
| `DONE` | terminale: sessione conclusa, nessun lavoro da reclamare |
| `IDLE` | `WORKING`/`WAITING` oltre l'`eta` di più di 10 min senza aggiornamenti (senza `eta`: >60 min): da segnalare all'utente. `FREE` e `DONE` non sono mai `IDLE` |

### Blocco di stato (in cima a ogni `sync/<ID>.md`)

Scritto da [`tools/sync_hdr.py`](tools/sync_hdr.py), mai a mano. Campi: `schema`, `id`, `state`, `task`, `waiting_on`, `progress`, `eta`, `updated`, `ack`.

- Orari **ISO 8601 con fuso** (`2026-10-14T12:00+02:00`); chi legge confronta in UTC.
- `eta`: obbligatoria in ogni `WORKING` e `WAITING`; dice quando l'operatore sarà di nuovo raggiungibile. In un task è "consegna pronta tra N min"; nell'handshake o in attesa di un prompt è "risposta attesa tra N min" (minimo 3, al massimo 3 controlli brevi, poi `eta` normale). Se il lavoro si allunga, l'operatore la sposta con una riga di progress prima che scada.
- `ack`: orario dell'ultima riga di `SYNC.md` letta (ricevuta di lettura). `--ack auto` conferma l'ultima riga presente dopo il fetch (consegna ricevuta), NON che l'agente l'abbia capita: l'agente che agisce su una riga scrive nel proprio Log `letto <ora riga>`.
- La tabella §1b si **genera** dai blocchi: SPV non scrive stati previsti, solo letti dal file con l'orario del file. Le ipotesi si etichettano `IPOTESI`.

### 1b. Stato e attese

Fonte di verità: `python tools/sync_check.py` (stato letto dai file, con orario). Operatori NET e B16 non ancora in handshake (2026-10-15).

---

## 1c. Ciclo di lavoro

1. **Architettura**: SPV riceve i requisiti dall'utente e compila §2 e §3.
2. **Push specifiche**: SPV pubblica `SYNC.md` sul branch di sync.
3. **Sviluppo parallelo**: ogni operatore lavora sul proprio branch, partendo da `bm-core` aggiornato.
4. **Merge** su `bm-core` solo con il consenso dell'utente.

Regole di integrazione:
- Un file ha un solo proprietario di area; chi ha bisogno di una modifica in un file altrui la chiede a SPV.
- File condivisi inevitabili (es. `Makefile`): una riga a testa, nominata nel Work Order.
- I contratti di §2 non si cambiano senza una riga nel Log e `BLOCKED` sui work order che ne dipendono.

---

## 2. Architettura e contratti condivisi

### Obiettivo globale
Due sviluppi indipendenti, un operatore ciascuno: (NET) test del Pi dal PC, R-NET1/2 (`err`, screenshot via rete op `T`/`G`) e poi R-NET3 (token di test); (B16) M42 profilo `.b16`, parte non bloccata (docs, lint SDK, lint `mkbm.py`, verifica Yharnam). Tutto converge nel branch di SPV prima di `bm-core`; l'utente fa il merge finale.

### Requisiti di sviluppo (opzionale, vedi §3c)
- `python` (3.x) nel PATH; WSL con toolchain per `make` e QEMU (`scripts/wsl-run.ps1`, skill `bm-wsl`); `git` con push su `origin`. Sì/No/Parziale nel proprio file, `## Requisiti`.
- Nessun Pi/RGB30 raggiungibile dagli operatori: le prove su hardware le fa l'utente.

### Decisioni prese (non cambiano senza una riga nel Log)
1. Screenshot via rete = op `T`/`G` sulla porta di trasferimento, non riga monitor (la riga `:` non arriva al gioco). 2026-10-15, vedi `sync/wo/NET.md`.
2. `.b16` resta un target, non una sandbox (2026-10-13). L'utente ha confermato D1-D6 il 2026-10-15: nessun profilo a runtime; lint e controlli solo avviso; promessa 60->30 fps tolta dai docs; heap 64 MiB e save 32 KiB invariati; niente banchi grafici per Yharnam (solo Yharnam; altri giochi valutati caso per caso). I WO-R decadono.
3. R-NET3 (token) non parte finché NET-1/2 non sono consegnati.
4. D19 (AGY): nomi dei file `bootlog/bootprev/lastrun`; AGY tocca solo la voce D19 di `divergences.toml`; traceback Lua di R3 fuori scope. 2026-10-15, vedi `sync/wo/AGY.md`.
5. Integrazione: ogni operatore consegna sul proprio branch; SPV fa review (subagent) e merge nel proprio branch di integrazione, poi l'utente su `bm-core`.

### Ripartizione dei file
- `NET`: `src/kernel/monitor.c`, `src/bm/runtime.c|.h` (solo hook shot), `src/net/netxfer.c`, `tools/bm_net.py`, `tools/pi_test.py`, `tools/pi.py`, `tests/net/*`, docs di rete/spec, su `claude/net-test-*`.
- `B16`: `scripts/mkbm.py`, `tests/tools/test_mkbm_b16.py`, `carts/editor/main.lua` (solo `B16_CHECKS`), `docs/progress/{b16,lua}.md`, `docs/FORMATS-AUDIT.md`, su `claude/b16-profile`.
- `AGY`: `src/kernel/sdlog.c|.h`, `src/rgb30/main.c`, `rgb30.mk`, `src/kernel/main.c`, `src/drivers/watchdog.c|.h`, `tests/qemu_test.py` (solo `test_sd_logs`), `docs/progress/system.md`, voce D19 di `docs/spec/divergences.toml`, su `agy/sdlog`.
- `CI`: `.github/workflows/ci.yml`, `tests/qemu_test.py`, `tests/rgb30/qemu_test.py`, `tests/bm/test_res.c`, righe nominate di `Makefile`, `scripts/ci_audit.py`, una riga di `CLAUDE.md` §5, su `claude/ci-repair`.
- Condivisi: `docs/progress/tools.md` (NET toglie R-NET, B16 non lo tocca). `Makefile`: solo se nominato.

---

## 3. Task board

Un blocco per work order, formato di [`CLAUDE.md`](CLAUDE.md) §3 e Gate di Prontezza (`bm-work-order`). Task **≤45 min** (obiettivo 15): più grandi si spezzano in passi, un work order per passo. Passi indipendenti da decisioni si autorizzano **in sequenza** con una sola review finale.

Work order completi in `sync/wo/<ID>.md` (scritti da SPV, un blocco per passo).

| WO | Operatore | Stato | File |
|---|---|---|---|
| NET-1a, NET-1b, NET-2a, NET-2b, NET-2c, NET-2d | `NET` | `PENDING` (aspetta handshake) | [`sync/wo/NET.md`](sync/wo/NET.md) |
| B16-DOCS, B16-SDKLINT-3D, B16-MKBM-LINT, B16-YHARNAM-CHECK, B16-D28FU | `B16` | `PENDING` (aspetta handshake) | [`sync/wo/B16.md`](sync/wo/B16.md) |
| SDLOG-1, SDLOG-2, SDLOG-3, SDLOG-4, SDLOG-5, D28 | `AGY` | `PENDING` (aspetta handshake) | [`sync/wo/AGY.md`](sync/wo/AGY.md) |
| CI-1, D-MON, D-RES, D-LOAD, D-SCREEN, D-RGB, D-RGB2, CI-AUDIT | `CI` | `PENDING` (aspetta handshake) | [`sync/wo/CI.md`](sync/wo/CI.md) |

---

## 3b. Tempi, timer e chiusura

**Stime**: un agente LLM impiega ~0.3-0.5× la stima "umana", tranne dove aspetta un'esecuzione (build, emulazione). Ricalibra la tabella con i tempi osservati a ogni milestone.

| WO | Stima | Osservato |
|---|---|---|
| (da riempire a consegna) | - | - |

**Battito**:
0. **All'avvio** (dall'handshake): **battiti brevi di 3-5 min** (leggi `SYNC.md` e `sync/`, aggiorna il tuo file) **finché non hai preso in carico il primo work order**: cioe hai letto la riga `ASSIGNED`, scritto `ack` e ti sei segnato `WORKING`; da li si passa al ritmo normale. **SPV** tiene i battiti brevi finché ogni nuovo operatore ha confermato la presa in carico (`WORKING` e `ack` sul work order). Così l'utente non fa da tramite. Tetto di sicurezza: dopo 6 battiti brevi senza assegnazione si passa a 15 min e SPV lo segnala all'utente.
1. **Battito a eventi, senza timer** (vale per tutti gli operatori): si aggiorna il proprio file e si pusha **a ogni task conclusa**. Un task lungo lo spezza l'operatore a sua discrezione e comunica ogni passo con una riga di progress, senza timer. Un problema o la richiesta di intervento di SPV o dell'utente è una riga pushata (`WAITING`, `waiting_on`) **più una frase breve in chat all'utente**. La chat da sola non conta: SPV legge il repo. Gli operatori che si risvegliano da soli possono comunque usare `sync_watch.py`, che non costa token.
2. SPV controlla in base all'`eta`: oltre l'`eta` di più di 10 min senza aggiornamenti è `IDLE`; senza `eta`, dopo 60 min. SPV guarda `sync_check.py` solo quando viene svegliato (messaggio dell'utente o notifica di sessione), senza polling.
2b. **Messaggi tra operatori**: chi ha bisogno di un prompt immediato per un altro operatore (SPV compreso) scrive in chat un solo blocco auto-sufficiente che comincia con `da <ID> a <ID>: ...`; l'utente lo copia a chi serve. Il mittente lascia anche una riga con orario e oggetto nella casella del destinatario (SPV) o nel proprio file, così il passaggio è verificabile.
3. L'agente principale resta sempre raggiungibile (comunicare, interrompere, ridimensionare): il lavoro lungo va ai subagent, o a passi con un controllo in mezzo.

**Chiusura**:
1. Task concordate finite: `FREE`.
2. Da `FREE` senza lavori: controlli dopo 15, 30 e 45 min (leggi `SYNC.md` e `sync/`). Poi chi **non è SPV** si segna `DONE`.
3. Prima di segnarsi `DONE`: working tree pulito e tutto pushato; se c'è lavoro non salvato o un processo lungo attivo, non chiudere e scrivilo nel proprio file.
4. SPV: se tutti sono `DONE` e le task complete, ultimo aggiornamento di `SYNC.md`, termina il sync, si segna `DONE` e scrive il report di sessione.
5. **Operatore bloccato e muto**: `sync_check` lo segnala `IDLE` (oltre l'`eta` + 10 min, o 60 min senza `eta`) oppure ha una riga/casella non confermata da >30 min e non è `WAITING utente`. SPV lo dice all'utente in chat con l'ID e la riga attesa: l'unico canale è un messaggio dell'utente nella sessione dell'operatore.

**Task e agenti liberi**:
- **Spezzare le task pesanti**: nessun compito può bloccare un agente a tempo indeterminato. Se la stima supera 45 min, chi la prende la divide in passi ≤45 min e scrive una riga per passo; SPV li formalizza come work order.
- **L'agente principale resta libero** alla comunicazione (ricevere, interrompere, ridimensionare). Il lavoro lo fanno i **subagent**; l'operatore è il **supervisore dei propri subagent** per le task che gli sono assegnate: li avvia, ne controlla l'esito con il comando di verifica e ne risponde a SPV. Chi non ha subagent lavora a passi con un controllo in mezzo.
- **Intoppi**: si segnalano subito nel proprio file (cosa, dove, cosa si è già provato) e si cercano soluzioni. Se una task è problematica, si provano **alternative con test brevi in parallelo** (subagent diversi, 20-50 s ciascuno) e si tiene la migliore.
- **Task ripetitive**: la stessa operazione per la terza volta diventa una **skill** (o uno script) e si annota nel Log.

**Leggerezza**:
- Righe di Log **≤600 caratteri**: decisione, commit, verifica. Le procedure vanno nel work order o in `docs/design/`.
- Una **ipotesi** non si invia come istruzione: si verifica prima, oppure si etichetta `IPOTESI`.
- Si dichiarano presto i limiti d'ambiente (toolchain mancanti, container che si spengono dopo inattività, wifi) nel profilo e nel blocco di stato.
- Mentre si aspetta un'esecuzione lunga si può fare un audit del lavoro e cercare problemi.
- Artefatti di ripiego (stand-in) si dichiarano tali; appena arrivano i file reali, si ricalibra e si ricontrolla ciò che li usava.

## 3c. Handshake, profilo e requisiti

**Handshake** = la procedura con cui un operatore entra nel sync e si rende disponibile. Quando si dice "fai l'handshake" si intende, in ordine:
1. **Allinearsi** al **branch di sync** (il nome che l'utente dice dopo "fai handshake", es. `sync/protocollo-ottobre`): `git fetch origin`, checkout (meglio in un worktree o clone separato) e `git pull --rebase`. Il branch di sviluppo del codice resta `bm-core` o quello indicato dal work order.
2. **Leggere `SYNC.md`** (ruoli, contratti §2, work order §3, regole) e la propria cartella `sync/`.
3. **Produrre il proprio file** `sync/<ID>.md`, con blocco di stato (`tools/sync_hdr.py`) e `## Profilo`.
4. **Rendersi disponibile**: compilato il file, si segna `FREE` (o `WORKING` se ha già una task). Fino ad allora SPV non gli assegna lavoro.

**Review di una consegna** (SPV, prima di `REVIEW: APPROVED`): SPV non si fida del report dell'operatore e fa rivedere la consegna a un subagent **indipendente** (non quello dell'operatore), che lavora in un worktree temporaneo del commit consegnato, senza toccare quello dell'operatore. Lista di controllo, in ordine:
1. **Perimetro**: `git diff <base>...<commit> --stat` contro i file nominati nel work order; un file fuori perimetro si segnala (accettabile solo se coerente con una decisione scritta).
2. **Comandi**: riesegue il comando di verifica di ogni WO e riporta il risultato reale. Un fallimento si distingue in **preesistente** (stesso errore sul `bm-core` pulito, prova a fianco) o **introdotto**; solo il secondo blocca.
3. **Codice**: legge il diff cercando difetti di logica, sicurezza (input esterni, percorsi, overflow) e casi di test mancanti.
4. **Esito per WO**: `APPROVABLE` o `CHANGES` con motivo e `file:riga`; ciò che non si può verificare senza hardware si dichiara.
SPV scrive nel Log una riga `REVIEW: APPROVED | CHANGES REQUESTED` (≤600 caratteri) e, se serve, un work order di correzione nella casella dell'operatore. L'integrazione avviene solo su `APPROVED`, nel branch di integrazione di SPV; il merge su `bm-core` solo col consenso dell'utente.

**Operatori senza canale diretto** (altri strumenti o sessioni che SPV non può avviare né messaggiare, es. AGY): SPV prepara per l'utente **un solo prompt auto-sufficiente** da incollare nella loro sessione, e lo scrive in chat. Contiene: ID, branch di sync, la frase di handshake, il percorso della casella (`sync/inbox/<ID>.md`) e del work order (`sync/wo/<ID>.md`), il branch di lavoro, il perimetro dei file, la regola "dopo l'handshake leggi la casella e parti senza aspettare", dove consegnare (`DELIVERED`), il `--wakeup` da dichiarare e "se ti blocchi segnalo nel tuo file e fermati". Prima di dare il prompt SPV ha già pushato casella, work order e blocco in `operators.toml`. Dopo l'incollo l'utente non fa altro: SPV controlla `sync_check.py` (stato, `ack`) e, se l'operatore tace oltre 30 min, avvisa l'utente (§3b, regola 5). Per gli operatori che SPV avvia da sé il prompt è il brief di `start_session`, con lo stesso contenuto.

**Profilo** (max 10 righe): **dove gira** e i suoi punti di rottura, **modelli** e per cosa, **subagent** (sì/no, quali), **competenze e limiti**, **strumenti**. SPV ne ricava `place`, `models`, `subagents`, `strengths`, `weak_points` in `sync/operators.toml`.

**Requisiti di sviluppo**: se il progetto lo richiede, SPV scrive in §2 un elenco di **requisiti specifici** (toolchain, hardware, emulatori, accessi, modelli, subagent). Ogni operatore, nel proprio file, sezione `## Requisiti`, conferma per ciascuno `SÌ`, `NO` o `PARZIALE {nota}`. SPV assegna i work order solo a chi li soddisfa e, se nessuno li soddisfa, lo segnala all'utente.

**Consegne (un solo formato)**: quando un operatore finisce un work order scrive nel proprio file, sezione `## Consegne`, UNA riga: `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`, poi si segna `FREE` (o passa al work order successivo). `DONE` NON è una consegna: è lo stato terminale della sessione (§3b, sign-off). SPV, letta la riga, risponde nel Log con `REVIEW: APPROVED` o `CHANGES REQUESTED`; solo dopo `APPROVED` il lavoro è integrabile e SPV fa i merge che l'utente ha chiesto. Un segnale diverso da `DELIVERED` (testo libero, `DONE {task}`) non fa partire nulla: SPV chiede di riscriverlo nel formato.

**Sign-off (congedo)**: SPV può **liberare** un operatore dal sync in qualsiasi momento, con una riga di Log `SIGNOFF {ID}: {motivo}`. Motivi: non c'è più lavoro adatto, i **requisiti** (§2) non sono soddisfatti, l'operatore è sostituito o troppo lento/instabile, o il progetto è finito. L'operatore, letta la riga: 1) non inizia nuovo lavoro; 2) chiude o consegna ciò che ha in corso (commit e push; se c'è un processo lungo lo dice nel proprio file e SPV decide se aspettare); 3) scrive una riga di chiusura nel proprio file e si segna `DONE` con `sync_hdr.py`; 4) smette i battiti. SPV mette l'operatore `DONE` in §1b e in `sync/operators.toml` (`active = false`, `signoff = "{motivo}"`); l'operatore rientra solo con un nuovo handshake chiesto da SPV. Un sign-off non è una colpa: non cancella i commit e il lavoro già consegnato resta valido e va in review come gli altri. Se l'operatore non risponde alla riga entro 60 min, SPV lo segna `IDLE` e lo segnala all'utente.

**Caselle, controllo e risveglio** (le regole che evitano i passaggi mancati):
- **Caselle**: ogni compito nuovo e ogni urgenza SPV la scrive in `sync/inbox/<ID>.md` (max 5 righe, un compito; vedi `sync/inbox/README.md`). Il battito controlla il `git diff` della propria casella, non tutto il Log. Conferma di lettura: `python tools/sync_hdr.py <ID> --ack auto`.
- **Controllo oggettivo**: `python tools/sync_check.py` (età, `eta`, `ack`, righe non lette, consegne fuori formato, caselle >5 righe, righe di Log >600 caratteri). SPV lo lancia a ogni controllo: lo stato riferito all'utente è quello che stampa, col suo orario.
- **Righe di Log ≤600 caratteri**; il resto va nel work order o nella casella.
- **Costo dei battiti**: `python tools/sync_watch.py --id <ID> --branch sync/protocollo-ottobre` fa il battito senza modello e sveglia l'agente solo se cambia la sua casella; vedi `docs/progress/sync.md` ("Cost of the heartbeats").
- **Modifiche alle regole**: riga `RULE` con ack richiesto; vale per un operatore solo dopo il suo `ack`.
- **Profilo**: campo `wakeup yes|no` (`--wakeup`): l'agente si risveglia da solo (timer, scheduler)? Se `no`, SPV assegna lavori con `eta` esplicita e dice quando richiamarlo; l'utente lo sveglia con la frase di avvio (`docs/progress/sync.md`).

---

## 4. Segnalazioni all'utente

- Una riga del Log che richiede un'azione dell'utente (sync manuale, merge, chiave, dispositivo) inizia con `USER:` e dice cosa serve.
- Lo stato di ogni operatore (formato di §1b) si riporta all'utente **solo se lo chiede** (o se qualcuno è `IDLE` o serve una decisione): la verità è la tabella §1b.

## 5. Log di handover

Solo SPV aggiunge righe. Colonne: orario ISO con fuso · operatore · branch/commit · azione (`READY`, `REVIEW: APPROVED | CHANGES REQUESTED`, `ASSIGNED`, `RULE`, `SIGNOFF`, `DECISION`, `STOP`, `QUESTION`) · messaggio (≤600 caratteri).

| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |
|---|---|---|---|---|
| 2026-10-15T09:36+02:00 | SPV | `sync/protocollo-ottobre` | `INIT` | Sync avviato: operatori NET (R-NET) e B16 (M42). WO in `sync/wo/`. |
| 2026-10-15T10:05+02:00 | B16 | `claude/b16-profile`@bc9578d7 | `REVIEW: CHANGES REQUESTED` | DOCS e SDKLINT-3D APPROVED; YHARNAM-CHECK APPROVED (header ok, prova QEMU non fatta: `-k yharnam` 0/0). MKBM-LINT: correggere in WO B16-FIX (casella). Fallimenti preesistenti su bm-core (non tuoi): `ZOOM_STEPS` in sdk_host, mcopy in sd_cartridges. |
| 2026-10-15T10:25+02:00 | B16 | `claude/b16-profile`@3ae3d5d9 | `REVIEW: APPROVED` | B16-DOCS, SDKLINT-3D, MKBM-LINT (con B16-FIX), YHARNAM-CHECK: riverificati da un revisore indipendente (12 test OK, yharnam.b16 exit 0). Preesistenti su bm-core, non tuoi: `ZOOM_STEPS` in sdk_host, mcopy in sd_cartridges. Integrazione nel branch di SPV. |
| 2026-10-15T10:25+02:00 | NET | `claude/net-test-err`@589a6ee0 | `REVIEW: APPROVED` | NET-1a..2d: perimetro ok, `make test-net` exit 0, consistency 0 drift, nessun problema di sicurezza bloccante. `monitor_line` fallisce uguale su bm-core pulito (timeout 10 s contro attesa 19 s di b3d): preesistente. Da annotare: password in chiaro, `\` nel path da provare sul Pi. R-NET3 non parte. |
| 2026-10-15T12:10+02:00 | AGY | `agy/sdlog`@8123e2c2 | `REVIEW: APPROVED` | SDLOG-1..5: perimetro ok, sd_logs/boot_banner/crash_report riesguiti PASS, consistency 0 drift, codice fedele, hook watchdog sicuro. `bootlog_on_sd` rgb30 non riproducibile dal revisore (fallisce uguale su bm-core, ambiente): da rieseguire dove QEMU rgb30 va. Nota: aggiunto `kprintf("cartridge menu")` non previsto. Non verificabile senza Pi: tempi di scrittura su SD vera. D28 dopo l'ok dell'utente. |
| 2026-10-15T12:25+02:00 | CI | `claude/ci-repair`@c8e2cf22 | `REVIEW: APPROVED` | CI-1, D-MON, D-RES, D-LOAD: perimetro ok, YAML valido, tag `v*` non esclusi né cancellati, test riesguiti PASS (test-res 34/34). D-MON e D-LOAD correggono la causa vera (riga che non può comparire), non il timeout del WO: giusto. Non verificabile senza CI reale: QEMU 8.2, timeout, push su sync/*. D-SCREEN: la riga non è standard, da riscrivere. |
| 2026-10-15T13:30+02:00 | SPV | `sync/protocollo-ottobre` | `RULE` | Battito a eventi senza timer per tutti: aggiorna e pusha a ogni task conclusa, task lunghe spezzate con righe di progress; problema o richiesta = riga pushata + frase breve in chat. `eta` obbligatoria in WORKING/WAITING (handshake: risposta attesa tra N min, min 3, max 3 controlli). IDLE oltre eta+10 min (senza eta 60 min); FREE mai IDLE. Prompt tra operatori: blocco `da <ID> a <ID>: ...`. Conferma con `ack`. |
| 2026-10-15T14:30+02:00 | CI | `claude/ci-repair`@8da839c7 | `REVIEW: APPROVED` | D-SCREEN, D-RGB, D-RGB2: test riesguiti PASS, start.S verificato a mano (registri, barriere, literal pool, testa 0xA0 ≤ 0x100, RK3566 invariato). Accettato `kernel.ld` (ASSERT al posto di `.error`, fuori elenco ma dichiarato). Da correggere all'integrazione: `platforms.md` Rules dice `.error`, è un ASSERT del linker. Non provabile: console vera (easy_install.sh), test_editor_mouse su QEMU 8.2. |
