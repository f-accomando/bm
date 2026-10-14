# Protocollo di sincronizzazione multi-operatore (`SYNC.md`)

> Template riusabile. Per un nuovo progetto: **crea il branch di sync** `sync/{nome}` da `bm-core`, copia questo file, sostituisci i `{segnaposto}`, riempi §2 e §3 e svuota il Log (§5). Il sync di un progetto vive solo su quel branch: così più sessioni di sync (altri agenti, altri progetti) possono correre in parallelo senza toccarsi.
> Esempio completo di un progetto reale (Tropic Trail, 8 video, 3 operatori): [`docs/archive/sync-tropic-trail.md`](docs/archive/sync-tropic-trail.md).
> Cosa abbiamo imparato: [`docs/progress/sync-workflow.md`](docs/progress/sync-workflow.md). Regole d'area: [`docs/progress/sync.md`](docs/progress/sync.md).

- **Progetto**: {nome} · **Repo**: `f-accomando/bm` · **Base del codice**: `bm-core` · **Branch di sync**: `sync/{nome}` · **Governance**: [`AGENTS.md`](AGENTS.md)
- **Avviato**: {data} · **Supervisore (SPV)**: {operatore} · **Utente**: decide, fa i merge finali

---

## 1. Ruoli e file (un solo scrittore per file)

| Operatore | ID | Dove gira | Scrive | Branch |
|---|---|---|---|---|
| {es. Claude local} | `SPV` | {locale/cloud} | `SYNC.md` + `sync/SPV.md` | `feat/{nome}-*` |
| {es. Claude cloud} | `CLD` | {cloud} | `sync/CLD.md` | `feat/{nome}-rec` |
| {es. Antigravity} | `AGY` | {Windows} | `sync/AGY.md` | `agy/*` |
| utente | `UTE` | - | niente (parla nelle sessioni) | - |

1. `SYNC.md` lo scrive **solo SPV**: contratti, work order, decisioni, tabella di stato (§1b), avvisi a tutti.
2. Ogni altro operatore scrive **solo il proprio** `sync/<ID>.md`: stato, task, progressi, problemi, richieste. Tutti leggono tutto.
3. Gli operatori sono elencati in [`sync/operators.toml`](sync/operators.toml) (lo scrive SPV; nuovo operatore = nuovo blocco).
4. **File di sync e righe di Log si pushano solo sul branch di sync** `sync/{nome}` (`git pull --rebase origin sync/{nome}`, mai commit di merge): `SYNC.md`, `sync/*` e `tools/sync_hdr.py` non si toccano su `bm-core` durante il progetto. Il **codice** resta sui branch di lavoro (da `bm-core`) e arriva su `bm-core` solo dopo cross-review (§1c). A fine progetto SPV archivia `SYNC.md` in `docs/archive/` su `bm-core` e il branch di sync si chiude.
5. SPV scrive le righe da un **worktree separato** del branch di sync, per non cambiare branch di lavoro.
6. Le richieste fra operatori passano da SPV; le urgenze si scrivono nel proprio file e SPV le smista. Se un operatore è bloccato e muto, l'unico canale è un messaggio dell'utente nella sua sessione.

### Stati (stessi nomi ovunque)

| Stato | Significato |
|---|---|
| `WORKING {task}` | lavora a quella task |
| `WAITING {chi}` | bloccato finché non agisce `utente`, un altro operatore o un servizio |
| `FREE` | task concordate finite e pushate, disponibile ad altro lavoro |
| `DONE` | terminale: sessione conclusa, nessun lavoro da reclamare |
| `IDLE` | nessun segnale per >60 min senza `eta` dichiarata: da segnalare all'utente |

### Blocco di stato (in cima a ogni `sync/<ID>.md`)

Scritto da [`tools/sync_hdr.py`](tools/sync_hdr.py), mai a mano. Campi: `schema`, `id`, `state`, `task`, `waiting_on`, `progress`, `eta`, `updated`, `ack`.

- Orari **ISO 8601 con fuso** (`2026-10-14T12:00+02:00`); chi legge confronta in UTC.
- `eta`: obbligatoria prima di ogni operazione >15 min. Il silenzio prima dell'`eta` non è `IDLE`.
- `ack`: orario dell'ultima riga di `SYNC.md` letta (ricevuta di lettura).
- La tabella §1b si **genera** dai blocchi: SPV non scrive stati previsti, solo letti dal file con l'orario del file. Le ipotesi si etichettano `IPOTESI`.

### 1b. Stato e attese

Ultimo aggiornamento: {data/ora con fuso}.

| Operatore | Stato | Dettaglio | ETA |
|---|---|---|---|
| {nome} | `{stato}` | {una riga} | {ISO} |

---

## 1c. Ciclo di lavoro

1. **Architettura**: SPV riceve i requisiti dall'utente e compila §2 e §3.
2. **Push specifiche**: SPV pubblica `SYNC.md` sul branch di sync.
3. **Sviluppo parallelo**: ogni operatore lavora sul proprio branch, partendo da `bm-core` aggiornato.
4. **Cross-review** a ogni milestone: push del branch, riga nel Log, l'altro fa `git fetch`, verifica con il comando del work order e rivede il diff.
5. **Merge** su `bm-core` solo dopo via libera reciproco (o commenti risolti) e consenso dell'utente.

Regole di integrazione:
- Un file ha un solo proprietario di area; chi ha bisogno di una modifica in un file altrui la chiede a SPV.
- File condivisi inevitabili (es. `Makefile`): una riga a testa, nominata nel Work Order.
- I contratti di §2 non si cambiano senza una riga nel Log e `BLOCKED` sui work order che ne dipendono.

---

## 2. Architettura e contratti condivisi

### Obiettivo globale
{una frase: cosa si consegna, per chi, in quanto tempo}

### Requisiti di sviluppo (opzionale, vedi §3c)
- {requisito: toolchain / hardware / emulatore / accesso / modello}

### Decisioni prese (non cambiano senza una riga nel Log)
1. {decisione, motivo, data}

### Ripartizione dei file
- {ID}: `{cartelle/file}` su `{branch}`

---

## 3. Task board

Un blocco per work order, formato di [`CLAUDE.md`](CLAUDE.md) §3 e Gate di Prontezza (`bm-work-order`). Task **≤45 min** (obiettivo 15): più grandi si spezzano in passi, un work order per passo. Passi indipendenti da decisioni si autorizzano **in sequenza** con una sola review finale.

### WORK ORDER: {ID-NN}
- **Obiettivo**: {una frase}
- **File / Funzioni da toccare**: {nomi esatti}
- **Modifica richiesta**: {descritta senza scelte di design aperte}
- **Prova breve**: {run di 20-50 s che esercita la parte rischiosa, con log; obbligatoria prima di ogni ripresa/esecuzione lunga}
- **Comando di verifica**: {comando e risultato atteso}
- **Operatore / stato**: `{ID}` · `PENDING | IN_PROGRESS | DONE | BLOCKED: {motivo} | FAILED x2`

---

## 3b. Tempi, timer e chiusura

**Stime**: un agente LLM impiega ~0.3-0.5× la stima "umana", tranne dove aspetta un'esecuzione (build, emulazione). Ricalibra la tabella con i tempi osservati a ogni milestone.

| WO | Stima | Osservato |
|---|---|---|
| {ID-NN} | {h} | {h} |

**Battito**:
0. **All'avvio** (dall'handshake): **battiti brevi di 3-5 min** (leggi `SYNC.md` e `sync/`, aggiorna il tuo file) **finché non hai preso in carico il primo work order**: cioe hai letto la riga `ASSIGNED`, scritto `ack` e ti sei segnato `WORKING`; da li si passa al ritmo normale. **SPV** tiene i battiti brevi finché ogni nuovo operatore ha confermato la presa in carico (`WORKING` e `ack` sul work order). Così l'utente non fa da tramite. Tetto di sicurezza: dopo 6 battiti brevi senza assegnazione si passa a 15 min e SPV lo segnala all'utente.
1. Poi ogni **15 min** un subagent (o un controllo equivalente) fa un **diff** (`git fetch` + `git diff` dall'ultima copia vista), conferma stato e progressi e aggiorna il proprio file. Si aggiorna anche a ogni task conclusa.
2. Senza aggiornamenti per 60 min e senza `eta` valida: `IDLE`.
3. L'agente principale resta sempre raggiungibile (comunicare, interrompere, ridimensionare): il lavoro lungo va ai subagent, o a passi con un controllo in mezzo.

**Chiusura**:
1. Task concordate finite: `FREE`.
2. Da `FREE` senza lavori: controlli dopo 15, 30 e 45 min (leggi `SYNC.md` e `sync/`). Poi chi **non è SPV** si segna `DONE`.
3. Prima di segnarsi `DONE`: working tree pulito e tutto pushato; se c'è lavoro non salvato o un processo lungo attivo, non chiudere e scrivilo nel proprio file.
4. SPV: se tutti sono `DONE` e le task complete, ultimo aggiornamento di `SYNC.md`, termina il sync, si segna `DONE` e scrive il report di sessione.

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
1. **Allinearsi** al **branch di sync** (il nome che l'utente dice dopo "fai handshake", es. `sync/{nome}`): `git fetch origin`, checkout (meglio in un worktree o clone separato) e `git pull --rebase`. Il branch di sviluppo del codice resta `bm-core` o quello indicato dal work order.
2. **Leggere `SYNC.md`** (ruoli, contratti §2, work order §3, regole) e la propria cartella `sync/`.
3. **Produrre il proprio file** `sync/<ID>.md`, con blocco di stato (`tools/sync_hdr.py`) e `## Profilo`.
4. **Rendersi disponibile**: compilato il file, si segna `FREE` (o `WORKING` se ha già una task). Fino ad allora SPV non gli assegna lavoro.

**Profilo** (max 10 righe): **dove gira** e i suoi punti di rottura, **modelli** e per cosa, **subagent** (sì/no, quali), **competenze e limiti**, **strumenti**. SPV ne ricava `place`, `models`, `subagents`, `strengths`, `weak_points` in `sync/operators.toml`.

**Requisiti di sviluppo**: se il progetto lo richiede, SPV scrive in §2 un elenco di **requisiti specifici** (toolchain, hardware, emulatori, accessi, modelli, subagent). Ogni operatore, nel proprio file, sezione `## Requisiti`, conferma per ciascuno `SÌ`, `NO` o `PARZIALE {nota}`. SPV assegna i work order solo a chi li soddisfa e, se nessuno li soddisfa, lo segnala all'utente.

**Consegne (un solo formato)**: quando un operatore finisce un work order scrive nel proprio file, sezione `## Consegne`, UNA riga: `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`, poi si segna `FREE` (o passa al work order successivo). `DONE` NON è una consegna: è lo stato terminale della sessione (§3b, sign-off). SPV, letta la riga, risponde nel Log con `REVIEW: APPROVED` o `CHANGES REQUESTED`; solo dopo `APPROVED` il lavoro è integrabile e SPV fa i merge che l'utente ha chiesto. Un segnale diverso da `DELIVERED` (testo libero, `DONE {task}`) non fa partire nulla: SPV chiede di riscriverlo nel formato.

**Sign-off (congedo)**: SPV può **liberare** un operatore dal sync in qualsiasi momento, con una riga di Log `SIGNOFF {ID}: {motivo}`. Motivi: non c'è più lavoro adatto, i **requisiti** (§2) non sono soddisfatti, l'operatore è sostituito o troppo lento/instabile, o il progetto è finito. L'operatore, letta la riga: 1) non inizia nuovo lavoro; 2) chiude o consegna ciò che ha in corso (commit e push; se c'è un processo lungo lo dice nel proprio file e SPV decide se aspettare); 3) scrive una riga di chiusura nel proprio file e si segna `DONE` con `sync_hdr.py`; 4) smette i battiti. SPV mette l'operatore `DONE` in §1b e in `sync/operators.toml` (`active = false`, `signoff = "{motivo}"`); l'operatore rientra solo con un nuovo handshake chiesto da SPV. Un sign-off non è una colpa: non cancella i commit e il lavoro già consegnato resta in cross-review. Se l'operatore non risponde alla riga entro 60 min, SPV lo segna `IDLE` e lo segnala all'utente.

**Caselle, controllo e risveglio** (le regole che evitano i passaggi mancati):
- **Caselle**: ogni compito nuovo e ogni urgenza SPV la scrive in `sync/inbox/<ID>.md` (max 5 righe, un compito; vedi `sync/inbox/README.md`). Il battito controlla il `git diff` della propria casella, non tutto il Log. Conferma di lettura: `python tools/sync_hdr.py <ID> --ack auto`.
- **Controllo oggettivo**: `python tools/sync_check.py` (età, `eta`, `ack`, righe non lette, consegne fuori formato, caselle >5 righe, righe di Log >600 caratteri). SPV lo lancia a ogni controllo: lo stato riferito all'utente è quello che stampa, col suo orario.
- **Righe di Log ≤600 caratteri**; il resto va nel work order o nella casella.
- **Costo dei battiti**: `python tools/sync_watch.py --id <ID> --branch sync/{nome}` fa il battito senza modello e sveglia l'agente solo se cambia la sua casella; vedi `docs/progress/sync.md` ("Cost of the heartbeats").
- **Modifiche alle regole**: riga `RULE` con ack richiesto; vale per un operatore solo dopo il suo `ack`.
- **Profilo**: campo `wakeup yes|no` (`--wakeup`): l'agente si risveglia da solo (timer, scheduler)? Se `no`, SPV non pretende battiti da 15 min: assegna lavori con `eta` esplicita e dice quando richiamarlo; l'utente lo sveglia con la frase di avvio (`docs/progress/sync.md`).

---

## 4. Segnalazioni all'utente

- Una riga del Log che richiede un'azione dell'utente (sync manuale, merge, chiave, dispositivo) inizia con `USER:` e dice cosa serve.
- Lo stato di ogni operatore (formato di §1b) si riporta all'utente **solo se lo chiede** (o se qualcuno è `IDLE` o serve una decisione): la verità è la tabella §1b.

## 5. Log di handover e cross-review

Solo SPV aggiunge righe. Colonne: orario ISO con fuso · operatore · branch/commit · azione (`READY`, `REVIEW: APPROVED | CHANGES REQUESTED`, `ASSIGNED`, `RULE`, `SIGNOFF`, `DECISION`, `STOP`, `QUESTION`) · messaggio (≤600 caratteri).

| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |
|---|---|---|---|---|
| {ISO} | {ID} | `sync/{nome}` | `INIT` | Protocollo inizializzato. |
