# bm (BareMetal) — Note Operative di Sessione

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176) e PowKiddy RGB30 (RK3566, AArch64, `make TARGET=rgb30`). Cartucce `.bm`; cartella `bm/` sulla SD. Repo `f-accomando/bm`, branch principale `bm-core`.

---

## 1. Recupero Mirato delle Informazioni (Zero Spreco Token)

Non leggere mai file per intero. Tutto lo stato attuale è tracciato in [`docs/progress.md`](docs/progress.md) e nelle schede di area in `docs/progress/<area>.md`.

### Comandi Rapidi di Consultazione
| Esigenza | Comando da eseguire |
|---|---|
| **Cosa c'è da fare in un'area** | `grep -n -A 15 '^## \(Open work\|To do\)' docs/progress/<area>.md` |
| **Regole vincolanti di un'area** | `grep -n -A 20 '^## Rules' docs/progress/<area>.md` |
| **Trovare codice in `runtime.c` (350 KB)** | `python scripts/cindex.py src/bm/runtime.c <simbolo>` (poi `Read` con offset/limit) |
| **Consultare API di gioco** | `grep -n -C 2 '<funzione>' docs/api/<area>.md` (indice: `docs/API.md`) |
| **Architettura & pattern giochi** | `docs/design/games-architecture.md` |

---

## 2. Regole di Ingaggio e Sviluppo

- **Branch** (evitare branch orfane):
  - **Quando**: si apre un branch solo se l'utente chiede di sviluppare (feature, fix, refactor). Domande, analisi, ricerche e letture non creano branch.
  - **Nome parlante**: formato `claude/<nome-descrittivo>` (es. `claude/rgb30-audio-mixer`), che dica cosa si sviluppa. Vietati nomi non parlanti o generati a caso (es. `claude/cool-volta-epblsn`, `claude/fix`, `claude/test`).
  - **Se parto di mia iniziativa**: il nome deve comunque essere parlante.
  - **Se lo sviluppo è ambiguo** (scopo, area o nome poco chiari): chiedere all'utente il nome prima di creare il branch o modificare file.
  - **Base**: parte sempre da `bm-core` aggiornato.
  - **Eccezione**: se la sessione impone esplicitamente un branch, si usa quello e non se ne crea un altro.
- **Test Selettivi**: Non lanciare mai l'intera test suite. Eseguire solo i test toccati dalla modifica (`tests/qemu_test.py -k <nome>`, `make test-b3d` per la GPU, unit test specifici). `kernel7` è disattivato.
- **Hardware Pi & RGB30**: Si prova senza seriale copiando `dist/kernel.img` sulla SD con `./easy_install.sh`. I report numerici sono generati dalla console sul branch `reports`.
- **Licenze & Sicurezza**: Nessun codice GPL (es. kernel Linux) copiato nei sorgenti bare-metal; strutture GPU da Mesa (MIT). Nessuna chiave (`*_KEY`, token) nei file.
- **Edizioni `.bm` e `.b16` Separate**: Un `.b16` non sostituisce mai il `.bm`; sono edizioni distinte con canali Market e build proprie. Non sovrascrivere mai i sorgenti del `.bm` per fare il `.b16`.
- **Progetti vs Giochi (M47)**: Gli editor modificano solo progetti `.bme`. I `.bm` sono in sola lettura; `Ctrl+B` compila il `.bm` definitivo.

---

## 3. Architettura Subagent & Routing Intelligente

### Il Principio: Separare Decidere da Eseguire
I modelli di punta (*Opus*) costano molto sui token di output: devono produrre decisioni e piani sintetici (pochi token). I modelli economici (*Haiku/Sonnet*) devono scrivere il codice (tanti token). Il router sceglie l'agente non in base alla dimensione apparente della task, ma in base a quanto è già ben specificata.

### Ruoli (definizioni in `.claude/agents/`)
| Agente | Modello / effort | Fa | NON fa |
|---|---|---|---|
| **`bm-planner`** | Opus medium (sola lettura) | Trasforma task vaghe/grosse in ordini di lavoro (Work Order) con criteri verificabili | Scrivere codice |
| **`bm-coder`** | Haiku high | Scrive codice da un Work Order preciso ed esaustivo | Design, risolvere ambiguità |
| **`bm-mechanic`** | Haiku low | Task già specificate a rischio zero (rename, format, template, spunte in `docs/progress/`) | Decisioni di logica |
| **`bm-light`** | Sonnet medium | Task semplici con giudizio tecnico, docs verificate sul codice reale, fix 1–2 file | Lavori lunghi e vaghi |
| **`bm-deep`** | Opus xhigh | Un solo problema isolato e critico (bug con riproduzione, decisione architetturale) | Implementare feature intere |
| **`bm-research`** | Haiku low (sola lettura) | Esplorazione read-only, ricerca simboli/definizioni su molti file, sintesi compatte | Modificare codice |

### Gate di Prontezza (Readiness Gate)
Prima di assegnare una task a un modello esecutivo (`bm-mechanic`, `bm-coder`, `bm-light`), applica questa checklist a 5 domande. Una task è pronta per un agente basso SOLO se TUTTE le risposte sono **SÌ**:
1. **File/funzioni**: Sono esplicitamente nominati i file e le funzioni da toccare?
2. **Determinismo**: La modifica è descritta in modo che due persone farebbero lo stesso diff (nessuna scelta di design aperta)?
3. **Criterio verificabile**: Esiste un comando deterministico di verifica (test, build, lint, output atteso)?
4. **Scope limitato**: Riguarda indicativamente 3 file o meno?
5. **Nessun blocco**: È indipendente da altre decisioni ancora in sospeso?

> **Regola**: Se manca anche una sola risposta, la task **NON** va a un agente basso e **NON** va a `bm-deep`: va a **`bm-planner`**, che la rende pronta o la spezza in Work Order atomici.

### Regole per `bm-deep`
- Accetta **solo** un problem statement stretto: sintomo + riproduzione esatta + area sospetta + tentativi già fatti.
- Se manca la riproduzione o la richiesta è generica (es. *"implementa X"*), **rifiuta e rimanda a `bm-planner`**.
- Output: diagnosi della causa radice e fix minimo; l'implementazione ritorna a `bm-coder` o `bm-light`.

### Escalation Basata su Evidenze
Gli agenti chiudono sempre con uno stato esplicito:
- **`DONE`**: Il comando di verifica passa con successo.
- **`BLOCKED: <motivo>`**: Specifica ambigua o decisione mancante $\to$ torna a `bm-planner`.
- **`FAILED x2`**: Il test continua a fallire dopo 2 tentativi $\to$ sale di un livello (es. da coder a light, da light a deep).

### Formato Work Order
Ogni ordine generato da `bm-planner` contiene:
- Obiettivo in una frase
- File/funzioni da toccare
- Modifica descritta senza scelte di design aperte
- Comando di verifica e risultato atteso
- Agente consigliato (`bm-mechanic` / `bm-coder` / `bm-light` / `bm-deep`)

---

## 4. Invarianti Globali

- **Sezioni Cartuccia** (`src/bm/bm.h`): 6 AUDIO (`BMAU`), 8 MESH, 9 ANIM, 10 INFO, 11 SPRITES, 12 LAYERS, 13 FLAGS, 14 BOXES.
- **Flag Tile (bmlib)**: 0 solido, 1 piattaforma, 2 scala, 3 acqua, 4 fa male.
- **RGB30 Controlli**: B conferma, A annulla (`confirm=a` scambia). Usare sempre `pad_ok` / `pad_back`.
- **Regole Specifiche di Modulo**: Ogni area (`system`, `v3d`, `graphics`, `lua`, `audio`, `platforms`, `tools`) elenca le proprie regole non derogabili nella sezione finale `## Rules (do not break)` del rispettivo file in `docs/progress/<area>.md`.

---

## 5. Chiusura Task & Manutenzione Continua dello Scaffolding

Prima di concludere qualsiasi attività o dichiarare completato un task:
1. **Aggiornare lo Stato**: Rimuovere o aggiornare i compiti svolti in `## Open work` del rispettivo `docs/progress/<area>.md`.
2. **Aggiornare le Regole**: Se è stato introdotto o modificato un vincolo architetturale, registrarlo in `## Rules (do not break)` del file d'area corrispondente.
3. **Verifica Integrità**: Eseguire `python scripts/docs_audit.py score` per verificare l'assenza di link rotti o file orfani prima di terminare.
4. **Coerenza**: se tocchi formati, API, chiavi di `config.txt`, menu/Settings o il comportamento di un solo kernel (Pi o RGB30), esegui `python scripts/consistency.py check` e aggiorna `docs/spec/` (`specs.toml`, `config-keys.toml`, `divergences.toml`); regole in `docs/spec/CONSISTENCY.md`. Tutto deve convergere prima della 1.0.0 (`consistency.py gate`).

---

## 6. Sync multi-operatore & Handshake

Più agenti (Claude local/cloud, Antigravity, altri) lavorano in parallelo su un progetto con il protocollo di [`SYNC.md`](SYNC.md) (template; regole e lezioni in `docs/progress/sync.md`). Ogni progetto ha un **branch di sync** dedicato `sync/<nome>`: `SYNC.md`, `sync/*` e `tools/sync_hdr.py` vivono e si pushano solo lì, mai su `bm-core`, così sessioni di sync diverse non si toccano; il codice sta su branch di lavoro da `bm-core`.

**"Fai handshake `<branch>`"** (dall'utente) = entrare nel sync di quel branch senza altre spiegazioni:
1. `git fetch origin`, poi checkout di `<branch>` in un worktree o clone separato (non sul branch di lavoro) e `git pull --rebase`.
2. Leggere `SYNC.md` (ruoli §1, contratti §2, work order §3, regole §3b/§3c) e `sync/operators.toml`; capire chi è il supervisore (SPV) e quale ID ti spetta (se non c'è, proponilo a SPV).
3. Creare **solo** `sync/<ID>.md` da `sync/_template.md`: blocco di stato scritto con `python tools/sync_hdr.py <ID> --state FREE --task handshake`, `## Profilo` (dove giri e punti di rottura, modelli, subagent, strumenti con versione) e `## Requisiti` se §2 li elenca.
4. Commit e `git pull --rebase origin <branch> && git push origin HEAD:<branch>`; poi aspettare l'assegnazione scritta da SPV in `SYNC.md`. **Non iniziare lavoro** prima.

**Consegna**: una sola riga standard nella sezione `## Consegne` del proprio file, `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`, poi `FREE`; SPV risponde `REVIEW: APPROVED|CHANGES REQUESTED` e solo dopo integra. `DONE` non è una consegna (è lo stato terminale). Un segnale non standard non fa partire niente.

**Casella e controllo**: ogni compito nuovo o urgenza arriva in `sync/inbox/<ID>.md` (max 5 righe, scritta da SPV): a ogni battito leggi il `git diff` della tua casella, poi `python tools/sync_hdr.py <ID> --ack auto` (conferma di lettura). SPV usa `python tools/sync_check.py` per sapere chi è vivo e cosa non è stato letto. Nel profilo dichiara `--wakeup yes|no` (ti risvegli da solo?). Righe di Log ≤600 caratteri; una modifica alle regole è una riga `RULE` da confermare con `ack`.

**Costo dei battiti**: il battito è `fetch` + casella + `--ack auto`; farlo fare all'agente principale costa l'intera conversazione ogni volta. Meglio un subagent piccolo, o senza modello `python tools/sync_watch.py --id <ID> [--branch <sync>]` (sveglia l'agente solo se la sua casella o una riga a lui rivolta cambia; dettagli in `docs/progress/sync.md`).

**Sign-off**: se SPV scrive `SIGNOFF <ID>: motivo` (niente lavoro adatto, requisiti non soddisfatti, sostituzione, fine progetto) l'operatore non inizia altro, consegna e pusha quello che ha, scrive la chiusura nel proprio file, si segna `DONE` con `sync_hdr.py` e smette i battiti; rientra solo con un nuovo handshake chiesto da SPV.

Regole fisse: un solo scrittore per file (`SYNC.md` solo SPV, `sync/<ID>.md` solo `<ID>`); dopo l'handshake battiti brevi di 3-5 min finché non hai preso in carico il primo work order (SPV: finché ogni nuovo operatore ha confermato; tetto 6 battiti), poi aggiornare il proprio file ogni 15 min (un subagent per il battito, controllando solo il `git diff` dall'ultima copia vista) e a ogni task conclusa; task ≤45 min; l'agente principale resta libero; 60 min di silenzio senza `eta` = `IDLE`; righe di Log ≤600 caratteri; lo stato degli operatori in chat solo se l'utente lo chiede.
