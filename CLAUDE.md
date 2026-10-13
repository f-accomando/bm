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

- **Branch**: Prima di modificare file per una nuova feature, proporre e chiedere all'utente un nome branch breve (`claude/<nome>`). Parte sempre da `bm-core` aggiornato.
- **Test Selettivi**: Non lanciare mai l'intera test suite. Eseguire solo i test toccati dalla modifica (`tests/qemu_test.py -k <nome>`, `make test-b3d` per la GPU, unit test specifici). `kernel7` è disattivato.
- **Hardware Pi & RGB30**: Si prova senza seriale copiando `dist/kernel.img` sulla SD con `./easy_install.sh`. I report numerici sono generati dalla console sul branch `reports`.
- **Licenze & Sicurezza**: Nessun codice GPL (es. kernel Linux) copiato nei sorgenti bare-metal; strutture GPU da Mesa (MIT). Nessuna chiave (`*_KEY`, token) nei file.
- **Edizioni `.bm` e `.b16` Separate**: Un `.b16` non sostituisce mai il `.bm`; sono edizioni distinte con canali Market e build proprie. Non sovrascrivere mai i sorgenti del `.bm` per fare il `.b16`.
- **Progetti vs Giochi (M47)**: Gli editor modificano solo progetti `.bme`. I `.bm` sono in sola lettura; `Ctrl+B` compila il `.bm` definitivo.

---

## 3. Subagent

Definizioni in `.claude/agents/`:
- `research` (Haiku): Esplorazione read-only, ricerca simboli/definizioni su molti file, sintesi compatte per non inquinare il contesto.
- `light` (Sonnet): Domande, fix rapidi di 1–2 file, typo, verifiche.
- `standard` (Opus): Feature intere, nuovi moduli, modifiche coordinate su più file.
- `deep` (Opus, xhigh): Architettura e bug critici a causa ignota. **Chiedere sempre conferma all'utente prima di lanciarlo.**
- `tracker` (Haiku): Aggiornamento spunte e spostamenti righe in `docs/progress/`.

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
