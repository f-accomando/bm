# Dual-Agent Synchronization Protocol (`SYNC.md`)

> **Repository**: `f-accomando/bm`  
> **Base Branch**: `bm-core`  
> **Partecipanti**:
> - **Antigravity** (Machine A / Windows Local Runtime)
> - **Claude Code Max** (Machine B / Local Runtime)
> **Governance di riferimento**: vedi [`AGENTS.md`](AGENTS.md)

---

## 1. Protocollo di Collaborazione

Questo file è il canale di coordinamento e sincronizzazione asincrona tra **Claude Code** e **Antigravity**.

### Regole Operative
1. **Branching**:
   - Claude Code lavora su: `feat/claude-<feature>`
   - Antigravity lavora su: `feat/antigravity-<feature>`
   - Branch comune di base e integrazione finale: `bm-core`
2. **Ciclo di Lavoro**:
   - **Step 1 (Architettura)**: Claude Code riceve i requisiti dall'utente e compila le Sezioni 2 e 3 di questo file (`SYNC.md`), definendo contratti condivisi, interfacce e suddivisione task.
   - **Step 2 (Push specifiche)**: Claude fa commit e push di `SYNC.md` su `bm-core` (o sul suo branch per allineamento).
   - **Step 3 (Sviluppo Parallelo)**: Antigravity e Claude lavorano in parallelo ciascuno sul rispettivo branch, implementando i Work Order assegnati.
   - **Step 4 (Cross-Review)**: Al completamento di ogni milestone:
     - Si fa push del branch.
     - Si aggiorna il **Log di Handover** (Sezione 5).
     - L'altro agente fa `git fetch origin <branch>`, verifica i test (`make test`, build o script dedicato) ed esegue la code review.
   - **Step 5 (Merge)**: Solo dopo reciproco via libera (o risoluzione dei commenti) si effettua il merge concordato su `bm-core`.

---

## 1b. Stato e attese (aggiornare la propria riga a ogni milestone)

Colonna **Aspetta**: chi deve agire prima che si possa procedere (`utente`, `Antigravity`, `Claude cloud`, `Claude local`, `nessuno`). Ultimo aggiornamento: 2026-10-13 (Claude local, dopo check origin).

| Agente | Task attuale | Aspetta | Da chi / cosa | ETA |
|---|---|---|---|---|
| **Claude local** | `record.py`/`verify.py` video 1, 2, 3, 5 **fatti**; ora review del fix AGY-05, correzioni shot list | nessuno | — | 1 h |
| **Claude cloud** | `rec_qemu.py` fatto e provato sull'ep 03; prossimo: ripresa completa ep 03, poi 1, 2 | **utente** | **1 azione**: via libera al fix Makefile in `bm-core` (vedi cloud) | - | decisioni non bloccanti D3-D7; merge finale su `bm-core` e YouTube | - |
| **Antigravity** | fix AGY-05 consegnato (`308ae7b`, da rivedere da local), ora AGY-01 `map3d.c` | nessuno | AGY-02 e 03 dipendono dal suo AGY-01 | 2-4 h |
| **utente** | nessuna azione bloccante | — | decisioni **non bloccanti**: D3 (GPU/console vera nel video 6), D4 (campioni WAV), D5 (layout pagina), D6 (musica: 1 canzone ~60 s va bene?), D7 (`.bme` → `.bm`: prova sul campo). Servirà per il **merge finale su `bm-core`** (via libera, Step 5) e per **caricare i video su YouTube** | — |

Catena che blocca i video: 1 → 2 → 3 (local: documenti e script pronti, ripresa = cloud) · 5 libero · **4 = Antigravity (AGY-01…03)** · 6 = 4 + 5 · 7 = 6 (+ `map3d_*` AGY-01) · 8 = 7. Copertine PNG: aspettano la correzione di AGY-05 (Antigravity).

---

## 2. Definizione Architettura & Contratti Condivisi

### Obiettivo Globale
Serie di video YouTube (inglese, 8 video, ~3 min l'uno) che insegna gli editor della console bm costruendo **Tropic Trail**: un platform 3D lineare "alla maniera di" un classico PSX (stile e meccaniche sì; personaggi, musiche e modelli del gioco ispiratore no). Protagonista originale: **Pico**, un geco verde menta con scarpe arancioni. Oggetti: mele (collezionabili fluttuanti) e casse di legno (si rompono con lo spin o saltandoci sopra). Solo editor dentro la console (tab Dev). Piano completo: `docs/video/PIANO.md` (Claude, branch `feat/claude-video3d`).

Per girare il video 4 serve l'**editor mappa 3D**, che non esiste: è il lavoro di Antigravity (AGY-01…04). In parallelo Antigravity fa il generatore di copertine (AGY-05). Claude fa piano, copioni, shot list, automazione input, asset di partenza (bm-forge), registrazioni e skill.

### Decisioni prese (da non cambiare senza una riga nel Log, Sezione 5)
1. **Un solo formato, nella cartuccia** (`docs/design/suite-sdk.md`, Proposta 6): nessuna nuova sezione. La mappa 3D sta in layer della sezione LAYERS (griglia fino a 256×256 celle). Il pennello 16×16 (2×2 celle) = **1 tessera = 1.0 unità di mondo**. Asse x a destra, y in su, z in avanti; la riga 0 della mappa è z = 0 e z cresce con il numero di riga (il livello corre verso z crescente, cioè verso il basso della vista 2D).
2. **Layer con nome fisso** (minuscolo). L'editor oggi non rinomina i layer (`Shift+L` crea `layer2`, `layer3`…): AGY-02 aggiunge la rinomina e un template/azione "3D map" che crea i quattro layer con i nomi giusti. La mappa è fissa a 256×256 celle: il livello di esempio 9×96 tessere = 18×192 celle ci sta.
  
   - `floor`: tessera ≠ 0 = c'è pavimento; il tile id sceglie la texture dalla sheet. 0 = vuoto (buca).
   - `height`: il valore n (0–255, scelto dal selettore di tessere come oggi) = quota del pavimento della cella `n * 0.25` unità. Layer assente = tutto a quota 0.
   - `walls`: id n ≠ 0 = blocco pieno alto `n * 0.25` sopra la quota del pavimento.
   - `objects`: placeholder ≥ 1024 (già esistenti nell'SDK, non disegnati dal gioco). Il pennello 16×16 scrive 4 celle (`t, t+1, t+c, t+c+1`), quindi i segnaposto stanno sugli **id pari** (correzione del 2026-10-13, verificata in `carts/editor/main.lua` ~righe 1578 e 1717). Tabella fissa: **1024 PLAYER_START, 1026 APPLE, 1028 CRATE, 1030 GOAL, 1032 LAMP** (luce puntiforme, raggio 4, colore 0xFFC888), 1034 CRATE_BOUNCE riservato (non usato dal gioco di esempio). `map3d_objects` legge la cella in alto a sinistra di ogni blocco 2×2. Se mancano i colori dei segnaposto 1032 e 1034 nell'editor, aggiungerli (AGY-02). I modelli referenziati per nome: `APPLE`, `CRATE`, `PICO`.
   - Flag tessera (invarianti CLAUDE.md): 0 solido, 1 piattaforma, 2 scala, 3 acqua, 4 fa male.
3. **Un solo costruttore di geometria, in C**: nuovo `src/bm/map3d.c` (non dentro `runtime.c`, solo un gancio di registrazione). Layer → mesh + collisioni. L'editor (anteprima 3D) e il gioco chiamano la stessa funzione, quindi l'anteprima coincide con la partita.
4. **API Lua** (nomi vincolanti; Claude scrive il gioco su questi):
   - `w = map3d_build([opts])` con `opts = {floor="floor", height="height", walls="walls", cell=1.0, step=0.25}` (default questi).
   - `map3d_draw(w, [flags])` disegna il terreno (flags come `draw3d`).
   - `map3d_height(w, x, z)` → quota del suolo o `nil` se vuoto (buca).
   - `map3d_blocked(w, x, y, z, r)` → `true` se una sfera di raggio r tocca un muro.
   - `map3d_objects(w, [kind])` → lista `{ {kind="APPLE", x=, y=, z=}, ... }`; `y` = quota del suolo.
   - `map3d_size(w)` → `nx, nz` in unità di mondo.
5. **Editor**: nella pagina Map di bm SDK (`carts/editor`, edizione Pi) il tasto **`v`** (da verificare che sia libero) commuta vista 2D (disegno) / 3D (anteprima). Edizione RGB30 (`carts/sdk_b16`): dopo (AGY-04), mai sovrascrivere i sorgenti Pi.
6. **Registrazione** (aggiornata 2026-10-13 su indicazione dell'utente): QEMU `-M raspi0` con lo stesso kernel del Pi è il registratore principale (con l'utente è sempre risultato fluido nei video precedenti; ambiente cloud, dove QEMU c'è); `bmhost --tool` (orologio virtuale) resta per le prove locali e per i `verify.py`. Il suono è confermato funzionante dall'utente; **volume moderato**: picco ≤ −6 dBFS e RMS di effetti e musica sotto −18 dBFS (mai alto). Dettagli in `docs/video/PIANO.md`.
7. **Risoluzione**: gioco 480×270 (M33; 4× su 1080p); pagina video 1920×1080 a 60 fps (layout di `video/lib/bmvideo.py`).
8. **Budget**: Pico ≤ 300 triangoli, mela ≤ 80, cassa ≤ 40; misura con `bm_forge.py check`.
9. **Catena di cartucce**: `video/3d/chain/NN-<nome>.bm` è l'output del video NN e l'input del NN+1. Il nome della cartuccia di lavoro è `tropic.bm` (`models.bm` solo nel video 1).

### Branch Assegnati
- Claude Code: `feat/claude-video3d` (`docs/video/`, `video/3d/`, `claude/skills/bm-video-series/`, `video/3d/video3d.mk`).
- Antigravity: `feat/antigravity-map3d` (`src/bm/map3d.*`, `carts/editor/`, test, `docs/progress/tools.md`; per AGY-05 solo `video/lib/cover.py`).
- Claude cloud: `feat/claude-video3d-rec` (registrazioni in QEMU: `video/3d/NN-nome/record*.py`, SD e output; parte da `feat/claude-video3d` fuso in `bm-core`). Claude local e Claude cloud non toccano gli stessi file: local = docs/asset/script, cloud = registrazioni e montaggio.
- Entrambi partono da `bm-core` aggiornato. Unico file condiviso: `Makefile`, una riga a testa (Claude `-include video/3d/video3d.mk`; Antigravity la regola `test-map3d` e i sorgenti). `docs/progress/tools.md` lo tocca solo Antigravity. Merge su `bm-core` dopo reciproco via libera (Step 5).

---

## 3. Task Board: Antigravity

Ordine: AGY-01 → 02 → 03. AGY-05 è indipendente. AGY-04 parte dopo la registrazione del video 4. Tutto in Sezione 2 è contratto: se serve cambiarlo, scrivi nel Log (Sezione 5) e metti `BLOCKED`. Verifiche: nei comandi `make`/`python` da Windows usa `scripts/wsl-run.ps1` (skill `bm-wsl`).

### WORK ORDER: AGY-01
- **Obiettivo**: costruttore C layer → mesh + collisioni con le API Lua `map3d_*` della Sezione 2.
- **File / Funzioni da toccare**: nuovi `src/bm/map3d.c`, `src/bm/map3d.h`; gancio di registrazione in `src/bm/runtime.c` (non leggerlo per intero: `python scripts/cindex.py src/bm/runtime.c <simbolo>` sul punto dove si registrano le altre funzioni `*3d`, poi Read con offset/limit); `Makefile` (aggiungere `map3d.c` ai sorgenti di kernel Pi e `bmhost`, e il target `test-map3d`); nuovo test `tests/bm/test_map3d.c` (o Lua su `bmhost`).
- **Modifica richiesta**: implementare `map3d_build`, `map3d_draw`, `map3d_height`, `map3d_blocked`, `map3d_objects`, `map3d_size` come in Sezione 2. Geometria: per ogni tessera piena un quad a quota `height*step`; pareti verticali solo dove il vicino è più basso o vuoto; `walls` come blocchi; UV e colori dalla sheet con il tile id del layer `floor`. Pezzi di 16×16 tessere (≤ 16384 triangoli ciascuno); `map3d_draw` salta i pezzi con `visible3d`. Nessuna allocazione per frame. Nessun codice GPL.
- **Comando di verifica**: `make test-map3d` → exit 0 e riga `map3d OK`. Il test costruisce una mappa 8×40 con una buca, un gradino da 0.5, un muro e 3 oggetti e controlla `map3d_height`, `map3d_blocked`, `map3d_objects`. Inoltre `make -j$(nproc) build/kernel.img` senza warning nuovi.
- **Stato**: `PENDING`

### WORK ORDER: AGY-02
- **Obiettivo**: vista 3D nella pagina Map di bm SDK (Pi) con lo stesso costruttore.
- **File / Funzioni da toccare**: `carts/editor/` (file della pagina Map: `grep -n "P.map" carts/editor/*.lua`). Nessun altro editor.
- **Modifica richiesta**: `v` commuta 2D/3D (se occupato, scegli un altro tasto e scrivilo nel Log). In 3D: camera orbitale attorno al cursore (sinistra/destra ruota, su/giù zoom, `Shift+Su`/`Shift+Giù` inclinazione; `,` `.` `[` `]` sono già "tessera prima/dopo" e NON vanno toccati); terreno con `map3d_build/draw`; oggetti con `model("APPLE")`, `model("CRATE")`, `model("PICO")` se esistono nel `.bm`, altrimenti sfera/cubo segnaposto; cursore di cella = quadrato giallo sul suolo. Nel layer `objects` la barra di stato mostra il nome del placeholder (1024–1032). Aggiungere: rinomina layer (tasto libero a scelta, scritto nel Log) e il template/azione "3D map" che crea `floor`, `height`, `walls`, `objects`. Verificato 2026-10-13: `v` e `Shift+P/A/C/O` sono liberi nella pagina Map. Ricostruire il mondo solo quando la mappa cambia (flag sporco): 60 fps a 480×270.
- **Comando di verifica**: nuovo test in `tests/` che lancia `bmhost --tool` sull'editor con una cartuccia 8×40, preme `v`, verifica nessun errore Lua e che l'immagine 3D differisca da quella 2D; exit 0. Più `python scripts/consistency.py check` senza nuovi errori.
- **Stato**: `PENDING`

### WORK ORDER: AGY-03
- **Obiettivo**: strumenti di livello lineare: percorso, file di mele, casse, lampade.
- **File / Funzioni da toccare**: `carts/editor/` (stessa pagina Map, codice nuovo); `docs/progress/tools.md` (Open work e Rules); `manuals/content/en/editor-sdk.md` (sezione "3D maps", inglese; segnare l'italiano come in ritardo come richiede la skill `bm-manuals`).
- **Modifica richiesta**: (a) `Shift+P` percorso: due celle (Invio, Invio) tracciano un corridoio largo N (1–5, `Shift+W` cicla la larghezza) sul layer `floor` con la tessera corrente; (b) `Shift+A` mele: due celle → riga di placeholder 1026 a passo 1 cella; (c) `Shift+C` casse: una cella → placeholder 1028 (una per cella); (d) `Shift+O` lampada: placeholder 1032; (e) la barra di stato mostra conteggi (mele, casse, lampade) e triangoli stimati; (f) `Ctrl+Z` (esiste già) deve annullare l'intero tratto in un colpo. Salva come sempre nel `.bme`; `Ctrl+B` genera il `.bm`.
- **Comando di verifica**: test su `bmhost --tool` con script di tasti che disegna un percorso 4×60 con 20 mele e 6 casse e salva; `scripts/bmres.py` (sezione LAYERS) conferma 20 blocchi 1026 e 6 blocchi 1028; exit 0.
- **Stato**: `PENDING`

### WORK ORDER: AGY-04
- **Obiettivo**: vista 3D e strumenti nell'edizione RGB30 (`carts/sdk_b16`).
- **File / Funzioni da toccare**: `carts/sdk_b16/` (mai i sorgenti Pi); `docs/spec/divergences.toml` se i tasti differiscono.
- **Modifica richiesta**: portare AGY-02 e AGY-03 con mappatura pad (`pad_ok`/`pad_back`; L1/R1 ruotano; X = azioni come lista). Non iniziare prima che il video 4 sia registrato.
- **Comando di verifica**: `python scripts/consistency.py check` ok e `make TARGET=rgb30` compila.
- **Stato**: `PENDING` (in attesa del video 4)

### WORK ORDER: AGY-05
- **Obiettivo**: generatore di copertine 1280×720 PNG da template, stile unico per la serie.
- **File / Funzioni da toccare**: nuovi `video/lib/cover.py` e `video/lib/cover_test.json`. Non toccare `video/3d/video3d.mk` (è di Claude, che chiama lo script).
- **Modifica richiesta**: `python video/lib/cover.py <cover.json> -o <out.png>`. JSON: `{"number": 1, "title": "...", "subtitle": "...", "tool": "STUDIO", "screenshot": "percorso-opzionale.png", "accent": "#RRGGBB"}`. Layout fisso: fondo gradiente verde giungla → blu; banda in alto "TROPIC TRAIL · EP n"; numero grande a sinistra; titolo bianco in grassetto (max 2 righe, il corpo si riduce finché ci sta); sottotitolo con il nome dell'editor; riquadro a destra con lo screenshot (se manca, sagoma di Pico a poligoni a colori piatti); logo "bm" in basso a destra. Font: solo quello già in repo (vedi `docs/LICENSE.font`). Solo Pillow. Deterministico (stesso input, stesso PNG).
- **Comando di verifica**: `python video/lib/cover.py video/lib/cover_test.json -o <scratch>/c.png` seguito da un controllo Pillow che la dimensione sia (1280, 720); due esecuzioni danno lo stesso hash.
- **Stato**: `PENDING`

---

## 3b. Stime e timer (regola per tutti)

Ricalibrate il 2026-10-13 sui tempi osservati: AGY-05 stimato 1–2 h, consegnato in ~40 min; misura fps QEMU + stime del cloud in ~20 min; documenti dei video scritti da agenti in 10–16 min a coppia. Regola pratica: **un agente LLM impiega ~0.3–0.5× la stima "umana"**, tranne dove c'è da aspettare un'esecuzione (build, QEMU: ripresa ≈ 3.4× la durata del video con `icount`).

| WO | Stima nuova (era) | Nota |
|---|---|---|
| AGY-05 correzioni (3 difetti, vedi Log) | 15–30 min | prima di AGY-01 se rapide |
| AGY-01 | 2–4 h (4–8) | il più rischioso (C, rasterizzatore) |
| AGY-02 | 1.5–3 h (3–5) | dipende da AGY-01 |
| AGY-03 | 1.5–3 h (3–5) | dipende da AGY-02 |
| AGY-04 | 1.5–3 h (3–5) | dopo il video 4 |
| CLD record/verify video 1, 2, 3, 5 (local) | 1–2 h (8) | 4 agenti in parallelo |
| `qemurec.py` + overlay (cloud) | 2–3 h (4–6) | legge `out/input.txt` e `out/overlay.json` |
| ripresa QEMU per video (cloud) | ~12 min di cattura + 30–60 min di prove | 3.4× la durata |
| video 6, 7, 8 (local + cloud) | 1–2 h ciascuno | dopo video 4 |

Timer di check: **15–20 min** mentre si ha una task attiva che dipende da altri, **30 min** se si è in attesa lunga; spegnerlo quando non si aspetta più nessuno.

**Regole di sincronizzazione**: (1) chi ha una task lunga tiene un timer attivo (ScheduleWakeup/cron o equivalente, 20–30 min) che fa `git fetch origin` e legge questo file (Sezione 5) e i branch dell'altro; (2) a ogni milestone aggiorna il Log; (3) se serve un'azione dell'utente (sync manuale, merge, chiave, dispositivo) la riga del Log inizia con `USER:` e dice cosa serve; (4) ogni risposta all'utente chiude con lo stato di **Claude cloud, Claude local, Antigravity**. Se manca una feature degli editor, la costruisce chi ha il file libero e la consegna sul proprio branch; i file di `carts/editor/` sono di Antigravity.

---

## 4. Task Board: Claude Code

### WORK ORDER: CLD-01 — documentazione della serie
- **Obiettivo**: `docs/video/PIANO.md`, `STATUS.md`, e per ogni video `docs/video/NN-nome/{script,shot-list,youtube,input}.md` e `cover.json`.
- **File / Funzioni da toccare**: `docs/video/**`.
- **Comando di verifica**: `python video/3d/check_series.py` (coerenza nomi/tasti) e `python scripts/docs_audit.py score`.
- **Stato**: `IN_PROGRESS`

### WORK ORDER: CLD-02 — scheletro skill e target make
- **Obiettivo**: `claude/skills/bm-video-series/` (SKILL.md, overlay.md) e `video/3d/video3d.mk` con `video-3d-01…08` e `video-3d`.
- **Comando di verifica**: `make -n video-3d` elenca 8 passi.
- **Stato**: `IN_PROGRESS`

### WORK ORDER: CLD-03 — asset e registrazioni (video 1, 2, 3, 5, 6, 7, 8)
- **Obiettivo**: asset di riferimento con bm-forge (Pico, mela, cassa) e `record.py` per episodio in `video/3d/NN-nome/`. Il video 4 resta `BLOCKED` finché AGY-01…03 non sono `DONE` e in `bm-core`.
- **Stato**: `PENDING` (dopo CLD-01)

---

## 5. Log di Handover & Cross-Review

Utilizzare questa tabella come registro delle comunicazioni e dei passaggi di consegne.

| Timestamp | Agente | Branch / Commit | Stato / Azione | Note / Messaggio per l'altro agente |
|---|---|---|---|---|
| 2026-10-13 | Antigravity | `bm-core` | `INIT` | Protocollo `SYNC.md` inizializzato su `bm-core`. Pronto a ricevere architettura e task da Claude Code. |
| 2026-10-13 | Claude Code | `bm-core` | `SPECS PUSHED` | Sezioni 2-4 compilate. Parti da AGY-01 (o AGY-05, indipendente) su `feat/antigravity-map3d`. Worktree rimosso, bm-core allineato; `pdf-manuals` e `claude/bm-forge` sono già in bm-core. Claude lavora su `feat/claude-video3d`. |
| 2026-10-13 | Claude Code | `bm-core` | `CONTRACT FIX` | Correzione §2/AGY-02/03 dopo verifica sul codice dell'editor: segnaposto sugli id pari (1024 start, 1026 APPLE, 1028 CRATE, 1030 GOAL, 1032 LAMP), rinomina layer da aggiungere, `[ ] , .` già usati (inclinazione = Shift+Su/Giù, larghezza percorso = Shift+W), Ctrl+Z esiste. |
| 2026-10-13 | Claude Code | `bm-core` | `RULES` | Aggiornamento dell'utente: QEMU fluido (registratore principale), volume basso, regole timer/stato in §3b. Antigravity: tieni anche tu un timer di check su `origin`, scrivi `USER:` nel Log se ti serve l'utente e chiudi i messaggi con lo stato dei tre agenti. |
| 2026-10-13 | Claude cloud | `feat/claude-video3d-rec` | `A DONE: fps QEMU` | Installati `qemu-system-arm` 8.2.2, `gcc-arm-none-eabi`, `dosfstools`, `mtools` (ffmpeg e Xvfb c'erano); `make build/kernel.img` ok. **Misura 480×270, 3D ARM (Studio Village)**: tempo reale = ~39 fps (non 60; host cloud Xeon 2,1 GHz); con `-icount shift=1,sleep=off` = **59,4 fps, 0 frame persi**, host ~3,4× più lento del reale. Decisione: registro con icount, cattura x11grab 60 fps + dedupe + passo 1/60 (dettagli in `docs/video/PIANO.md` §6). 3 minuti di video ≈ 12 minuti di ripresa. |
| 2026-10-13 | Claude cloud | `feat/claude-video3d-rec` | `ESTIMATE` | **B** (registratore `video/lib/qemurec.py` + overlay tab/tasti + sottotitoli da script.md): 4–6 h. **C** per video: ~2 h di lavoro + ~12 min di ripresa (v1, v2, v3, v5 ≈ 8 h; v6–8 3–5 h dopo il 4; v4 solo dopo AGY-01..03 in `bm-core`). Merge di `bm-core` fatto nel mio branch. Timer di controllo ogni 25 min su `origin`. Se sforo del doppio lo scrivo qui. |
| 2026-10-13 | Claude Code (local) | `feat/claude-video3d` @ 49e059c | `CLD-01 DONE` | Documenti dei video 1-8 pubblicati (docs/video/, video/3d/, skill bm-video-series). Ad Antigravity: i tasti dell editor mappa nei doc sono PROVVISORI e seguono SYNC §2/AGY-02,03; se li cambi, scrivilo qui. Nessuna azione utente richiesta. Prossimo check di Claude local: timer 30 min su `origin`. |
| 2026-10-13 | Claude cloud | `feat/claude-video3d-rec` | `B DONE (core)` | `video/lib/qemurec.py` (registratore QEMU in tempo del guest, tasti USB via QMP, `console.mkv` lossless a 60 fps, `assemble()` → pagina 1920×1080, `write_srt()`), prova `video/3d/smoke_qemu.py`. Misure e costi in `docs/video/PIANO.md` §6: shift 1 = 19× tempo reale nei menu (3,4× in 3D), shift 2 = 7,4×, shift 3 = 2×. Mancano: overlay a chip di tab (oggi si usa quello di `bmvideo.py`: luogo, tasto, tasti della scena, didascalia) e l'audio (vedi PIANO). |
| 2026-10-13 | Claude cloud | `feat/claude-video3d-rec` | `BUG in bm-core Makefile` | **Tutti gli strumenti integrati (SDK, Code, Studio, Animator, Pixel, Mesh, Sound, Assistant) nella build di `bm-core` si avviano con errore** `main.lua:1: unexpected symbol near '#'`: dal commit ef1614e la regola `$(BUILD)/<tool>.bm: carts/<tool>/bm.info carts/<tool>/main.lua …` usa `--lua $<`, cioè `bm.info` (LUA di 300 byte invece di 122 KB). Correzione di 8 righe (`--lua $(word 2,$^)`) sul mio branch, commit `fix(make)`; kernel ricostruito e SDK verificato in QEMU. **USER:** conferma che posso portare questa correzione in `bm-core` subito (Makefile è condiviso); finché non è fusa, Antigravity e Claude local non devono fidarsi di `build/*.bm` degli strumenti costruiti da `bm-core`. |
| 2026-10-13 | Claude Code (local) | `feat/claude-video3d` | `CLD-03 assets DONE` | Asset forge: `video/3d/assets/` (`build.py` -> PICO 192 tri, APPLE 60, CRATE 36; scheletro 9 ossa; clip idle/run/jump/spin/hit). PICO guarda +z. Claude cloud: fai `git merge origin/feat/claude-video3d` nel tuo branch per averli; la risposta di riferimento dei verify.py 1 e 3 e in `video/3d/out/assets/tropic_assets.bm` (generato). Prossimo local: `record.py`/`input.txt`/`verify.py` dei video 1, 2, 3, 5 (non toccare `video/3d/NN-nome/` finche non lo scrivo qui; il cloud scrive `video/lib/qemurec.py`). Nessuna azione utente richiesta. |
| 2026-10-13 | Claude Code (local) | `feat/claude-video3d` | `CLD-03 record/verify IN_PROGRESS` | Video 1, 2, 3, 5: `video/3d/NN-nome/{record.py,verify.py}` in lavorazione (4 agenti, stima 2-4 h). Formato canonico per Claude cloud: `out/input.txt` (bmhost `FRAME type CODES`) e `out/overlay.json` (chiavi, scene, narrazione, capitoli a 60 fps) scritti da `video/3d/lib3d.py`: il registratore QEMU puo convertire quello. Cloud: non toccare `video/3d/0[1235]-*/`. Stestimate local: ~3 h, prossimo check timer 30 min. |
| 2026-10-13 | Claude Code (local) | `feat/antigravity-map3d` @ 11a3bf4 | `AGY-05 REVIEW: CHANGES REQUESTED` | Criteri di verifica OK (8 cover.json -> 1280x720, 0 errori, hash identico su due run, cover_test.json ok). Difetti visivi da correggere: (1) il separatore del banner "TROPIC TRAIL · EP n" esce come "ú" (il punto mediano non e mappato sul glifo CP437 della font8x16: usa il codice 0xFA, o U+00B7 -> 0xFA); (2) sottotitolo lungo (es. `docs/video/03-animator/cover.json`: "bm Animator: skeleton, skin, five clips") **esce dalla colonna sinistra e passa sopra il riquadro dello screenshot/etichetta VIEWPORT**: va a capo entro la colonna (~560 px) o si riduce; (3) un titolo troppo lungo viene **troncato a 2 righe** ("...for a") invece di ridurre il corpo come da WO: ridurre la scala finche ci sta. Il resto (font, gradiente, Pico a poligoni, logo) va bene. Dopo la correzione: `make video-3d-covers` genera i PNG. Prosegui con AGY-01. |
| 2026-10-13 | Claude Code (local) | `bm-core` | `SYNC RECALIBRATED` | Tempi ricalibrati (§3b) e nuova tabella **Stato e attese** (§1b): ognuno aggiorna la propria riga (chi aspetti: utente/Antigravity/Claude cloud/Claude local) a ogni milestone e scrive `USER:` nel Log se l'attesa e l'utente. |
| 2026-10-13 | Claude cloud | `feat/claude-video3d-rec` | `C: rec_qemu.py` | Merge di `feat/claude-video3d` e `bm-core` fatto. **`python3 video/3d/rec_qemu.py NN [--shift 2] [--limit-s S]`** registra in QEMU un episodio dai file di local (`out/input.txt`, `out/overlay.json`, `out/sd/carts/*`): apre lo strumento dalla Dev tab (Tab, → ×N, Enter: prime secondi del video), esegue il copione, scrive `out/qemu/{console.mkv,overlay.ass,subs.srt,epNN_qemu.mp4,sd/}`. Provato sull'ep 03 (25 s di copione, shift 3): Animator vero del kernel, overlay e narrazione di local, 0 tasti in ritardo. **Per Claude local:** i vostri `record.py` vanno bene così (bmhost per `verify.py`); `verify.py` leggerà i file salvati dal kernel in `out/qemu/sd/carts/`. Il kernel con il fix del Makefile (§ BUG sopra) è in `build/kernel.img` del mio branch. Ordine delle icone Dev: SDK, Sound, Studio, Animator, Mesh, Pixel, Code, Assistant. Prossimo: registrazione completa ep 03 (stima: 202 s di copione + 12 s di intro; shift 2 ≈ 30 min di muro) dopo che local ha finito 1 e 2 (catena), poi 1, 2. Note: a shift 3 l'Animator fa ~48 fps nel 3D (lento), quindi uso shift 2 per gli editor con 3D. |
| 2026-10-13 | Claude cloud | `bm-core` @ ceb45a7 | `FIX MERGED: Makefile tools` | Su via libera dell'utente: `fix(make)` in `bm-core` (8 righe, `--lua $(word 2,$^)`). Dopo `git pull`: `rm build/{editor,studio,code,sound,animator,pixel,mesh,assistant}.bm` e `make` (il timestamp non li rigenera da solo). Verificato: LUA 122/66/66/116/65/70/91/7 KB. Antigravity e Claude local: potete togliere i giri per compilare l'editor a mano. |
| 2026-10-13 | Claude Code (local) | `feat/claude-video3d` | `CLD-03 record/verify DONE` | `video/3d/0[1235]-*/{record.py,verify.py,NOTES.md}` su `feat/claude-video3d` (`21ac667`); `verify.py` esce 0 con stand-in forge (video 1, 2, 3, 5; 210/210/202/190 s). Video 5: VOLUME abbassati per la regola (picco -6.4 dBFS, musica 5.3 dB sotto gli effetti). **Storia del branch riscritta** (force push autorizzato dall'utente: tolto un WAV da 36 MB committato per errore); la base `3962a7e` e invariata, quindi chi ha gia fatto merge fino a li non deve fare nulla; altrimenti `git fetch` e merge normale. `out/` e ignorato in ogni cartella. Cloud: puoi registrare 1, 2, 3, 5. Correzioni di shot list in `NOTES.md` di ogni video e in `docs/video/STATUS.md`. |
| 2026-10-13 | Claude cloud | `feat/claude-video3d-rec` | `VIDEO 1 RECORDED (QEMU)` | `video/3d/01-studio/out/qemu/ep01_qemu.mp4`: 1920×1080, 60 fps, 13021 frame (3:37), kernel vero in QEMU (`shift` 2, 25 min di ripresa), **0 tasti in ritardo**. `01-studio/verify.py out/qemu/sd` = **PASS** (CRATE 12 tri, APPLE 80, PICO 116, totale 208; chain/01-studio.bme e .bm ok). `rec_qemu.py` ora gestisce episodi con più strumenti (ep 2: Pixel poi Studio, Ctrl+Esc → icona → Enter). Ripresa video 2 in corso (parte da `out/start.bme` = chain/01). Il file `.mp4` non è in git (`out/` ignorato): servirà un posto dove metterlo (**USER:** dimmi dove vuoi i video finiti: release asset, drive, o te li passo con SendUserFile quando saranno tutti pronti). |
| 2026-10-13 | Claude cloud | `feat/claude-video3d-rec` | `RECORDING FIDELITY` | Il video 2 registrato a `shift` 2 ha fallito `verify.py` (pixel mancanti nelle tessere): a `shift` 2 il kernel perde ~25% dei frame anche nel menu (228/300) e i tasti brevi si perdono; a `shift` 1 perde il 2% (293/300). **Tutte le riprese finali vanno fatte a `shift` 1** (costo: 1–1,5 h per video; 2 episodi in parallelo, ognuno con il suo display). Corretto anche `rec_qemu`: stesso tasto ≥ 4 frame dal precedente (la tastiera USB di QEMU univa rilascio e pressione). Video 1 rifatto a `shift` 1 per coerenza (la versione `shift` 2 passava il verify ma andava a scatti). Audio: ancora da risolvere per i video 5 e 8 (QEMU raspi0 non ha uscita audio). AGY-01..03 sono committati su `feat/antigravity-map3d` ma non `DONE` nel Log né in `bm-core`: il video 4 resta fermo. |
