# bm (BareMetal) — Note Operative di Sessione (Google Antigravity / Gemini)

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

- **Branch**: Prima di modificare file per una nuova feature, proporre e chiedere all'utente un nome branch breve (`feat/<nome>`). Parte sempre da `bm-core` aggiornato.
- **Test Selettivi**: Non lanciare mai l'intera test suite. Eseguire solo i test toccati dalla modifica (`tests/qemu_test.py -k <nome>`, `make test-b3d` per la GPU, unit test specifici). `kernel7` è disattivato.
- **Hardware Pi & RGB30**: Si prova senza seriale copiando `dist/kernel.img` sulla SD con `./easy_install.sh`. I report numerici sono generati dalla console sul branch `reports`.
- **Licenze & Sicurezza**: Nessun codice GPL (es. kernel Linux) copiato nei sorgenti bare-metal; strutture GPU da Mesa (MIT). Nessuna chiave (`*_KEY`, token) nei file.
- **Edizioni `.bm` e `.b16` Separate**: Un `.b16` non sostituisce mai il `.bm`; sono edizioni distinte con canali Market e build proprie. Non sovrascrivere mai i sorgenti del `.bm` per fare il `.b16`.
- **Progetti vs Giochi (M47)**: Gli editor modificano solo progetti `.bme`. I `.bm` sono in sola lettura; `Ctrl+B` compila il `.bm` definitivo.

---

## 3. Architettura Subagent Antigravity & Routing Intelligente

### Il Principio: Separare Decidere da Eseguire
I modelli di punta (*Gemini Pro*) costano e ragionano in profondità: producono decisioni, analisi e Work Order concisi (pochi token di output). I modelli rapidi ed economici (*Gemini Flash / Flash-Lite*) scrivono ed editano il codice (tanti token). Il router sceglie l'agente non in base alla grandezza della task, ma in base al suo grado di specificazione.

### Ruoli & Mappatura Modelli Antigravity
Quando Antigravity invoca i subagenti tramite `invoke_subagent`, mappa i ruoli così:

| Ruolo | Modello Antigravity | Toolset | Fa | NON fa |
|---|---|---|---|---|
| **`bm-planner`** | `Model: "pro"` | **Sola Lettura** (Read, Grep, Glob) | Trasforma task vaghe in Work Order con criteri verificabili | Scrivere codice |
| **`bm-coder`** | `Model: "flash"` | Full (Read/Write/Exec) | Scrive codice da un Work Order preciso ed esaustivo | Design o scelte aperte |
| **`bm-mechanic`** | `Model: "flash_lite"` | Full (Modifiche atomiche) | Task a rischio zero: rename, format, template, progress docs | Decisioni di logica |
| **`bm-light`** | `Model: "flash"` | Full (Read/Write/Exec) | Task semplici con giudizio, docs verificate su C, fix 1–2 file | Lavori lunghi e vaghi |
| **`bm-deep`** | `Model: "pro"` | Full (Read/Write/Exec) | Un solo problema isolato e critico con riproduzione certa | Implementare feature intere |
| **`research`** | Subagente nativo `research` | Sola Lettura | Ricognizione codebase, ricerca simboli, documentazione | Toccare file |

### Gate di Prontezza (Readiness Gate)
Prima di assegnare una task all'esecuzione diretta o a un subagente basso, verifica le 5 condizioni:
1. **File/funzioni**: Sono esplicitamente nominati i file e le funzioni da toccare?
2. **Determinismo**: La modifica è descritta in modo che due programmatori farebbero lo stesso diff (nessuna scelta di design aperta)?
3. **Criterio verificabile**: Esiste un comando deterministico di verifica (`qemu_test.py`, build, output atteso)?
4. **Scope limitato**: Riguarda indicativamente 3 file o meno?
5. **Nessun blocco**: È indipendente da decisioni ancora aperte?

> **Regola**: Se anche una sola risposta è **NO**, la task **NON** va all'esecuzione e **NON** va a `bm-deep`: viene affidata a **`bm-planner`** (`pro`), che formula uno o più Work Order verificabili.

### Regole per `bm-deep`
- Accetta solo problem statement stretti: sintomo + riproduzione certa + area sospetta + tentativi già fatti.
- Rifiuta richieste generiche tipo *"implementa X"* e le rimanda a `bm-planner`.
- Output: diagnosi della causa radice e fix minimo; il resto torna a `bm-coder`/`bm-light`.

### Escalation Basata su Evidenze
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
