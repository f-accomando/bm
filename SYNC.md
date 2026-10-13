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

| WO | Stima (lavoro effettivo) | Nota |
|---|---|---|
| AGY-05 | 1–2 h | indipendente, da fare per prima |
| AGY-01 | 4–8 h | il più rischioso (C, GPU/software) |
| AGY-02 | 3–5 h | dipende da AGY-01 |
| AGY-03 | 3–5 h | dipende da AGY-02 |
| AGY-04 | 3–5 h | dopo il video 4 |
| CLD-01 | 1–2 h | in corso |
| CLD-03 | 1–2 h per video | local: record.py e verify; cloud: ripresa QEMU |

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
