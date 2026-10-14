# Protocollo di sincronizzazione multi-operatore (`SYNC.md`)

> Template riusabile. Per un nuovo progetto: copia questo file, sostituisci i `{segnaposto}`, riempi §2 e §4 e svuota il Log (§5).
> Esempio completo di un progetto reale (Tropic Trail, 8 video, 3 operatori): [`docs/archive/sync-tropic-trail.md`](docs/archive/sync-tropic-trail.md).
> Cosa abbiamo imparato: [`docs/progress/sync-workflow.md`](docs/progress/sync-workflow.md). Regole d'area: [`docs/progress/sync.md`](docs/progress/sync.md).

- **Progetto**: {nome} · **Repo**: `f-accomando/bm` · **Base**: `bm-core` · **Governance**: [`AGENTS.md`](AGENTS.md)
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
4. File di sync e righe di Log si pushano direttamente su `bm-core` (`git pull --rebase origin bm-core`, mai commit di merge); il **codice** resta sui branch di lavoro e arriva su `bm-core` solo dopo cross-review (§1c).
5. SPV scrive le righe da un **worktree separato** di `bm-core`, per non cambiare branch di lavoro.
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
2. **Push specifiche**: SPV pubblica `SYNC.md` su `bm-core`.
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
1. Ogni **15 min** un subagent (o un controllo equivalente) fa un **diff** (`git fetch` + `git diff` dall'ultima copia vista), conferma stato e progressi e aggiorna il proprio file. Si aggiorna anche a ogni task conclusa.
2. Senza aggiornamenti per 60 min e senza `eta` valida: `IDLE`.
3. L'agente principale resta sempre raggiungibile (comunicare, interrompere, ridimensionare): il lavoro lungo va ai subagent, o a passi con un controllo in mezzo.

**Chiusura**:
1. Task concordate finite: `FREE`.
2. Da `FREE` senza lavori: controlli dopo 15, 30 e 45 min (leggi `SYNC.md` e `sync/`). Poi chi **non è SPV** si segna `DONE`.
3. Se non è una sessione cloud e a Roma sono le **00:00-08:00**: commit e push di tutto, poi spegne il computer. Mai con lavoro non salvato o processi lunghi attivi (scrivilo nel proprio file).
4. SPV: se tutti sono `DONE` e le task complete, ultimo aggiornamento di `SYNC.md`, termina il sync, si segna `DONE`, scrive il report di sessione e applica la regola dello spegnimento.

**Leggerezza**:
- Righe di Log **≤600 caratteri**: decisione, commit, verifica. Le procedure vanno nel work order o in `docs/design/`.
- Una **ipotesi** non si invia come istruzione: si verifica prima, oppure si etichetta `IPOTESI`.
- Stessa operazione 3 volte → script o skill, annotato nel Log.
- Si dichiarano presto i limiti d'ambiente (toolchain mancanti, container che si spengono dopo inattività, wifi) nel profilo e nel blocco di stato.
- Mentre si aspetta un'esecuzione lunga si può provare una soluzione migliore con un subagent, fare un audit o proporre soluzioni agli intoppi.
- Artefatti di ripiego (stand-in) si dichiarano tali; appena arrivano i file reali, si ricalibra e si ricontrolla ciò che li usava.

## 3c. Handshake e profilo

Alla prima scrittura ogni operatore aggiunge al proprio file `## Profilo` (max 10 righe): **dove gira** e i suoi punti di rottura, **modelli** e per cosa, **subagent** (sì/no, quali), **competenze e limiti**, **strumenti**. SPV ne ricava `place`, `models`, `subagents`, `strengths`, `weak_points` in `sync/operators.toml` e assegna di conseguenza.

---

## 4. Segnalazioni all'utente

- Una riga del Log che richiede un'azione dell'utente (sync manuale, merge, chiave, dispositivo) inizia con `USER:` e dice cosa serve.
- Ogni risposta all'utente chiude con lo stato di ogni operatore nel formato di §1b.

## 5. Log di handover e cross-review

Solo SPV aggiunge righe. Colonne: orario ISO con fuso · operatore · branch/commit · azione (`READY`, `REVIEW: APPROVED | CHANGES REQUESTED`, `ASSIGNED`, `DECISION`, `STOP`, `QUESTION`) · messaggio (≤600 caratteri).

| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |
|---|---|---|---|---|
| {ISO} | {ID} | `bm-core` | `INIT` | Protocollo inizializzato. |
