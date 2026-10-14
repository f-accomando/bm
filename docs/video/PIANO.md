# Tropic Trail — piano della serie video 3D

Serie di 8 video YouTube (inglese) che insegna gli editor di bm costruendo un platform 3D lineare
ispirato a un classico PSX. Coordinamento con Antigravity: [`SYNC.md`](../../SYNC.md). Stato di ogni
passo: [`STATUS.md`](STATUS.md). Cose da ricordare per la serie 2D già fatta: [`video/SERIE.md`](../../video/SERIE.md)
e skill [`bm-video-tutorial`](../../claude/skills/bm-video-tutorial/SKILL.md).

Convenzioni dei documenti: prosa di servizio in italiano; **tutto ciò che compare nel video**
(narrazione, didascalie, titoli, YouTube) in inglese.

## 1. Visione

- Ogni video insegna **un editor** e fa un passo concreto verso lo stesso gioco, giocabile alla fine.
- Solo strumenti dentro la console (tab Dev): bm Studio, bm Pixel, bm Animator, bm SDK (mappa 3D, luci), bm Sound, bm Code
  con assistente AI. Niente Blender, niente file fatti fuori dalla console nei video (gli script `bm-forge`/`bmres.py`
  servono solo a verificare e a preparare riferimenti, mai a "barare" davanti allo spettatore).
- Spettatore tipo: chi ha una console bm e non ha mai fatto un gioco 3D. Ritmo: ogni azione resta sullo schermo il tempo di
  leggerla; sempre in sovrimpressione la tab/l'opzione e il tasto premuto (vedi [`claude/skills/bm-video-series/overlay.md`](../../claude/skills/bm-video-series/overlay.md)).
- Originalità: stile e meccaniche di un platform 3D lineare "a corridoio" sì; personaggio, nome, modelli, musiche del gioco
  ispiratore no. Le immagini di riferimento ricevute servono solo per il *tipo* di inquadratura (camera dietro al personaggio,
  mele sospese, casse di legno), non si copia nulla.

## 2. Il gioco: Tropic Trail

| Voce | Valore (fisso per tutta la serie) |
|---|---|
| Titolo | **Tropic Trail** |
| Eroe | **Pico**, geco verde menta, pancia crema, scarpe arancioni, coda con punta gialla. Modello `PICO` |
| Oggetti | mela `APPLE` (fluttuante, ruota), cassa di legno `CRATE` (si rompe con spin o saltandoci sopra e rilascia 1 mela) |
| Livello | un solo sentiero a corridoio nella giungla, 9 × 96 tessere (z crescente), 2 buche, 1 gradino, 1 muro basso; ~60 mele, 14 casse; `GOAL` in fondo |
| Mosse | corsa (stick/frecce), **salto** (azione `jump` = pulsante A), **spin** (azione `spin` = pulsante B), subire un colpo (`hit`) |
| Punteggio | mele raccolte (HUD in alto a sinistra) e casse rotte; 100 mele = 1 vita bonus (stesso schema del 2D) |
| Risoluzione di gioco | **480×270** (M33, texture 3D in software; 4× su 1080p) |
| Frequenza | **60 fps**, orologio virtuale (`bmhost`) per i video |
| Pagina video | **1920×1080, 60 fps**, H.264 (come `video/lib/bmvideo.py`: console + pannello laterale con tab e tasti) |
| Luci | `light3d` (sole), `sky3d` (colori cielo/terra), `lamp3d` (1–4 lanterne), `fog3d` (nebbia verso l'orizzonte), ombra sotto Pico e casse con `draw3d` flag 8 + `shadow3d(1)` |
| Cartuccia | `tropic.bm` (di lavoro), `models.bm` solo nel video 1; catena in `video/3d/chain/` |

Budget (misurati con `bm_forge.py check`): Pico ≤ 300 triangoli, `APPLE` ≤ 80, `CRATE` ≤ 40, terreno ≤ 6000 triangoli
visibili, tutto a 60 fps. Se la misura dice no, si riduce (la risoluzione resta 480×270; mai sotto).

### Nomi fissi (usati in TUTTI i documenti)

- Modelli: `PICO`, `APPLE`, `CRATE`.
- Animazioni: `idle` (loop), `run` (loop), `jump`, `spin`, `hit` (non in loop).
- Suoni (banco, 8 caselle): `APPLE`, `CRATE`, `JUMP`, `STEP`, `SPIN`, `HIT`; canzone `JUNGLE`.
- Layer mappa: `floor`, `height`, `walls`, `objects`; segnaposto sugli id pari (il pennello 16×16 scrive 2×2 celle): 1024 `PLAYER_START`, 1026 `APPLE`, 1028 `CRATE`,
  1030 `GOAL`, 1032 `LAMP`, 1034 `CRATE_BOUNCE` (riservato). Contratto in `SYNC.md` §2.
- API di mondo: `map3d_build/draw/height/blocked/objects/size`.
- Scheletro di Pico (proposta, da confermare con i limiti di bm Animator — vedi §6): `root, spine, head, tail1, tail2,
  arm.L, arm.R, leg.L, leg.R` (9 ossa; i suffissi `.L`/`.R` li crea il tasto `m` di bm Animator; nomi ≤ 16 lettere; limiti verificati: 64 ossa, 255 animazioni, un solo osso per faccia/angolo, timeline a 12 fps).

## 3. Scaletta definitiva

Ordine di pubblicazione = quello richiesto. Ordine di registrazione consigliato: 1, 2, 3, 5, **4** (quando l'editor c'è), 6, 7, 8.

| # | Cartella | Editor | Cosa si costruisce | Durata |
|---|---|---|---|---|
| 1 | `01-studio` | bm Studio | modelli `PICO`, `APPLE`, `CRATE` con blocchi e vertici; riduzione triangoli | ~3:30 |
| 2 | `02-pixel` | bm Pixel (+ pittura in Studio) | texture (erba, sentiero, roccia, legno, pelle di Pico, mela), icona mela per l'HUD; pittura sul modello | ~3:30 |
| 3 | `03-animator` | bm Animator | scheletro di Pico, skinning, clip `idle`, `run`, `jump`, `spin`, `hit` | ~3:30 |
| 4 | `04-map` | bm SDK, pagina Map 3D | il sentiero: pavimento, quote, muri, mele, casse, lanterne, partenza e arrivo | ~3:30 — **BLOCCATO** |
| 5 | `05-sound` | bm Sound | 6 effetti morbidi + canzone `JUNGLE` | ~3:30 |
| 6 | `06-sdk` | bm SDK | progetto "3D with models", luci, ombre, nebbia, anteprima (F5), Build | ~3:00 |
| 7 | `07-code` | bm Code + assistente (F6) | movimento, salto, spin, mele, casse, HUD, camera | ~4:00 |
| 8 | `08-gameplay` | — | partita completa giocata da un bot, con musica, dal titolo al traguardo | ~2:30 |

Modifiche proposte alla scaletta (motivo): nessuna fusione o divisione. Il video 7 è il più lungo e va a ~4 minuti: è accettabile
perché ha tre blocchi di codice distinti (capitoli YouTube). Il video 5 (suono) si registra *prima* del 4 perché non dipende dalla mappa:
così la serie avanza mentre l'editor non c'è. La numerazione di pubblicazione resta quella richiesta.

## 4. Dipendenze tra i video

| Video | Riceve | Produce (file) | Chi lo legge dopo |
|---|---|---|---|
| 1 Studio | progetto vuoto `tropic` | `chain/01-studio.bm` (MESH: `PICO`, `APPLE`, `CRATE`, solo colori) | 2, 3 |
| 2 Pixel | 01 | `chain/02-pixel.bm` (sheet: tessere del terreno, texture dei modelli, icona mela; modelli dipinti) | 3, 4, 6 |
| 3 Animator | 02 | `chain/03-animator.bm` (scheletro `PICO` + 5 clip) | 4, 6, 7 |
| 4 Map | 03 | `chain/04-map.bm` (layer `floor/height/walls/objects`) | 6, 7 |
| 5 Sound | 03 (qualunque) | `chain/05-sound.bm` (sezione BMAU: 6 effetti + `JUNGLE`) | 6, 7 |
| 6 SDK | 04 + 05 | `chain/06-sdk.bm` (progetto assemblato, luci, ombre, nebbia, codice minimo di anteprima) | 7 |
| 7 Code | 06 | `chain/07-code.bm` = `tropic.bm` finale (gioco completo) | 8 |
| 8 Gameplay | 07 | `video/3d/08-gameplay/replay.txt` (copione tasti del bot) + video | pubblicazione |

Nota: 4 e 5 sono indipendenti; 6 unisce i loro output (`start.bm` del 6 = merge deterministico di `04-map.bm` e `05-sound.bm`,
fatto da script prima della registrazione, che tocca solo le sezioni LAYERS/FLAGS e BMAU — da verificare con `scripts/bmres.py`).
Prima che l'editor mappa esista, il video 4 si può provare con una mappa fatta a script **solo per sbloccare 6–8 nelle prove**,
mai nel video finale.

## 5. Cosa esiste già nella repo

- bm Studio (blocchi, tessere, vertici, pittura, riduzione mesh), bm Animator (ossa, skinning, clip), bm Pixel, bm Sound (8 voci,
  FM/PLUCK/ORGAN/SUPERSAW, filtro, room, echo, canzoni), bm SDK (progetti `.bme`, template "3D with models", mappa 2D a layer,
  Build `Ctrl+B`), bm Code con assistente F6. Manuali in inglese: `manuals/content/en/editor-*.md`.
- API 3D: `draw3d`, `model`, `animate`, `light3d`, `sky3d`, `lamp3d`, `fog3d`, `shadow3d`, `bounds3d`, `hit3d`: `docs/api/light3d.md`.
- Infrastruttura video 2D (7 episodi): `video/lib/bmvideo.py`, `video/NN-*/record.py`, skill `bm-video-tutorial`.
  Si riusa la libreria e si aggiunge la parte 3D in `video/3d/` (cartella separata: la numerazione 2D non cambia).
- `bm-forge` (già in `bm-core`): costruisce modelli/scheletri/animazioni da JSON, valida e fa anteprime PNG/GIF. Serve per i
  riferimenti, la verifica dei budget e come "risposta corretta" nei `verify.py` (come per gli sprite nel 2D).
- Manuali PDF (già in `bm-core`, branch `pdf-manuals` fuso): sono la fonte dei tasti.

## 6. Cosa manca, rischi e domande aperte

1. **Editor mappa 3D** non esiste → video 4 `BLOCCATO` (AGY-01…03 in `SYNC.md`). Requisiti: `docs/video/04-map/editor-requirements.md`.
2. **QEMU** (aggiornato su indicazione dell'utente: nei video precedenti è sempre stato fluido): è il registratore principale
   (`-M raspi0`, stesso kernel del Pi, tab Dev comprese); `bmhost --tool` resta per le prove locali e per i `verify.py`
   (orologio virtuale, stesso copione = stesso video). Resta da misurare in prova il frame rate del 3D a 480×270 in QEMU
   (`tools/showreel/console.py` fa ~12 screendump/s: per 60 fps veri serve una cattura di schermo, come `video/titan-clash/record_qemu.py`:
   Xvfb + QEMU gtk + `ffmpeg x11grab`). D1 è quindi risolta a favore di QEMU.

   **Misura (Claude cloud, 2026-10-13, Xeon 2,1 GHz 4 core, `qemu-system-arm` 8.2.2, kernel 759599a, cartuccia 3D Studio Village a 480×270, rasterizzatore ARM)**:
   - QEMU in tempo reale: il gioco fa **~39 fps** (580–630 frame, ~200 persi), con o senza cattura; lo schermo catturato a 60 fps mostra solo ~21 immagini distinte al secondo. **Non è 60 fps veri** su questo host: la registrazione in tempo reale non va bene qui.
   - QEMU con **`-icount shift=1,sleep=off`** (tempo virtuale deterministico, 2 ns per istruzione): il gioco fa **59,4 fps, 0 frame persi** (tempo del guest). L'host gira a ~**3,4× più lento del reale** (17,6 immagini distinte al secondo di muro): 3,5 minuti di video ≈ 12 minuti di registrazione. Si cattura con `x11grab` a 60 fps (≥ del ritmo del guest), si tolgono i doppioni e si rimette un passo costante di 1/60 s: ogni frame del guest = un frame del video. Stesso copione = stesso video.
   - `shift=2` è troppo lento (28,7 fps, tutti persi). Il registratore usa `shift=1`; il budget 3D va letto come "Pi un po' più lento del vero".
   - Da verificare in B: nessun frame del guest perso (display gtk ~30 Hz, guest in muro ~18 Hz: margine ok).
   - **Registratore fatto (B)**: `video/lib/qemurec.py`. Legge l'orologio del guest (timer di sistema BCM2835, `xp` via QMP), invia i tasti alla tastiera USB emulata (QMP `input-send-event`) al frame giusto del copione (formato `FRAME type CODICI` di bmhost), cattura con `x11grab`, mette ogni immagine sul tempo del guest e scrive un frame per ogni 1/60 s: `console.mkv` (ffv1, lossless). Poi `assemble()` fa la pagina 1920×1080 di `bmvideo.py`. Prova: `python3 video/3d/smoke_qemu.py 2` (12 s di guest, 0 tasti in ritardo, mp4 1920×1080 a 60 fps).
   - **Costo reale (host cloud)**: l'ozio del kernel (menu, editor fermi) è un'attesa sul timer che in icount costa molto: `shift=1` ≈ **19× il tempo reale** nei menu/editor (3,4× solo in una scena 3D piena), `shift=2` ≈ 7,4×, `shift=3` ≈ 2×. Quindi si sceglie lo `shift` per video: 3D a 480×270 (video 6–8) `shift=1`; editor (1–5) `shift=2` o `3` se il kernel non perde frame (si controlla con la riga `N frames, X fps, D dropped` che il kernel stampa sulla seriale quando si esce da uno strumento). Un video di 3,5 min: ~25 min (shift 2) o ~1 h (shift 1).
   - Tastiera: layout `us` (`bm/config.txt` scritto dal registratore). Menu: `Tab` = scheda successiva (Games → Dev → Settings), `Enter` apre; la scheda Dev è a una pressione di `Tab` dalla prima.
   - **Audio**: la `raspi0` di QEMU non ha un'uscita audio che il kernel usi, quindi il wav per i video con suono (5, 8) si prende da `bmhost --wav` (stesso synth); da confermare in C prima del video 5.
3. **Dove c'è QEMU**: nel cloud (sessione "Claude cloud", branch `feat/claude-video3d-rec`); in locale WSL ha `arm-none-eabi-gcc`,
   `gcc`, `python3` ma non `qemu-system-arm` né `ffmpeg` (ffmpeg è su Windows via winget). Divisione: **Claude local** = documenti,
   asset, script, `verify.py` con `bmhost`; **Claude cloud** = registrazione in QEMU e montaggio.
4. **`bmhost` non ha il menu del kernel** (schede Market/Games/Dev) né la GPU: gli editor funzionano con `--tool`, le luci/ombre
   software sì (stesso output in QEMU `gpu3d=0`), la GPU no. Se il video 6 vuole mostrare la GPU serve la console vera (D3).
5. **bmhost 3D a 60 fps con 480×270**: da misurare con la mappa vera; piano B: nebbia più vicina, meno oggetti visibili (`visible3d`).
6. **Limiti di bm Animator**: verificati sul codice (vedi §2); niente pesi misti, quindi le articolazioni si vedono "a blocchi" (stile PSX, voluto).
7. **Suono**: vedi §7 (l'utente conferma che l'audio funziona; non lo verifichiamo all'ascolto). Domanda D4: vuoi anche campioni WAV (solo via script, non dall'editor)?
8. **Il pannello laterale a 1920×1080** con console 640×360 lascia l'area di gioco piccola: il layout va tarato in prova (D5).
9. **Pittura sul modello** (Studio) ha limiti di texel: da provare sul modello vero prima del copione definitivo (video 2).
10. **Licenze**: musica e suoni originali; nessun asset ispiratore.

Domande aperte per l'utente: ~~D1~~ (risolta: QEMU), ~~D2~~ (risolta: QEMU nel cloud), D3 (GPU/console vera nel video 6),
D4 (campioni WAV), D5 (layout pagina), D6 (quanta qualità richiesta alla musica: 1 canzone loopabile di ~60 s va bene?).

## 7. Suono: cosa permette il sintetizzatore (onestà)

Fonti: `docs/progress/audio.md`, `docs/progress/audio-samples.md`, `manuals/content/en/editor-sound.md`.

- **Si può fare morbido e piacevole**: 48 kHz stereo a 16 bit, 8 voci con onde SINE/TRIANGLE/FM/PLUCK/ORGAN/SUPERSAW,
  filtro (passa-basso/banda/alto/notch) con inviluppo e LFO, ADSR esponenziale senza click, **room** (riverbero FDN 8 linee) e
  **echo** ping-pong, pan, compressore e limitatore. Con questi si fanno raccolta della mela (due note FM brillanti con echo
  corto), rottura cassa (rumore filtrato + legno `PLUCK`/`CRACKLE`), salto (seno con bend), passi (colpo breve filtrato),
  una canzone da giungla con marimba (`PLUCK`), basso `SINE`, pad `ORGAN`/`SUPERSAW` filtrato.
- **Limiti veri**: (a) solo **8 voci in tutto**: musica ed effetti si contendono le stesse; un effetto prende una voce libera e **silenzia
  la traccia di musica su quella voce** finché suona → la canzone usa solo 6 tracce e lascia le voci 6 e 7 agli effetti;
  (b) un solo timbro per traccia (niente accordi veri su una traccia: servono tracce diverse); (c) 64 passi per pattern, 64 pattern,
  8 canzoni; (d) l'editor sulla console **non registra né importa WAV**: i campioni si importano solo con `scripts/bmaudio.py`
  (fuori dalla console, quindi non nei video) — dentro l'editor si possono usare solo i campioni già nel banco e il kit della console
  (`bd sd hh oh cp rim tom cb`); (e) il modo "8-bit" (`sound=8bit`) toglie room, echo e onde band-limited: il video usa il modo normale;
  (f) QEMU e `bmhost` rendono lo stesso audio della console, ma i valori di volume reali dipendono dall'uscita (HDMI) — il mix va
  controllato anche con `verify.py` (RMS a finestre, come nel 2D).
- **Volume (regola dell'utente)**: non esagerare. Picco ≤ −6 dBFS, RMS di effetti e musica sotto −18 dBFS, musica più bassa degli effetti (~6 dB); `verify.py` lo controlla sul wav (RMS a finestre e picco). Il volume nei video si regola nel montaggio, non alzando i valori nell'editor.
- **Realismo**: non sarà una registrazione di strumenti veri; sarà un "synth morbido anni 2000", pulito e senza ruvidità da chip.

## 8. Scelte fatte (da riportare all'utente)

1. Personaggio **Pico** (geco), titolo **Tropic Trail**, mele e casse: originali, nessun nome del gioco ispiratore.
2. Livello **lineare 9 × 96 tessere**, camera dietro l'eroe che segue (z crescente), 480×270 a 60 fps.
3. Controlli: movimento con stick/frecce, `jump` = A, `spin` = B, via `keymap` (mapping tasto/pad in `docs/api/input.md`).
4. Codice e asset del video in `video/3d/` (non toccano `video/NN-*` del 2D); documenti in `docs/video/`.
5. Catena di cartucce in `video/3d/chain/NN-nome.bm` (non versionata se > 1 MB: `.gitignore` + `make video-3d-NN` la rigenera).
6. Copertine da template (`video/lib/cover.py`, Antigravity AGY-05), target `make video-3d-covers`.
7. Registrazione con `bmhost --tool` + QEMU opzionale (vedi §6).
8. Ordine di registrazione 1, 2, 3, 5, 4, 6, 7, 8 (pubblicazione 1–8).

## 9. Scoperte della verifica sul codice (cambiano il piano)

1. **Studio, Pixel, Animator e SDK salvano `.bme` (progetto), non `.bm`.** I file `chain/NN-nome.bm` si ottengono con `Ctrl+B` dell'SDK
   o con `bm_forge.py export`; i video 2 e 3 partono dal `.bme` salvato dal precedente. `video/3d/series.json` e §4 parlano di
   "cartuccia" come di questa coppia progetto/`.bm`. Da confermare nelle prove (domanda D7).
2. **Segnaposto della mappa sugli id pari** e rinomina layer assente: contratto corretto in `SYNC.md` (voce `CONTRACT FIX` del Log).
3. **Una tessera condivisa cambia su tutte le facce**: la sheet ha 8 tessere (`grass, path, rock, wood, skin, face, apple, apple_icon`).
4. **Studio non avvisa dei budget** 300/80/40 (solo sopra 1200 triangoli): si controllano con `bm_forge.py check` e a vista nella pagina Models (F2).
5. **`bmhost` non ha la tab Dev**: le righe "Dev ▸ editor" sono cartelli in sovrimpressione; la ripresa vera della tab Dev è in QEMU (D1).
6. **Conflitti di tasti** nella mappa: `[ ] , .` sono già "tessera prima/dopo", `Ctrl+Z` esiste già.
7. **Formato intermedio**: `.bme` (progetto) contro `.bm`: la prima `Ctrl+S` su un `.bm` crea una copia `.BME`. Come diventa `chain/NN-nome.bm`
   (Ctrl+B dell'SDK, `bm_forge.py export`, `mkbm.py`) è da provare nei video 1, 3 e 5 (D7).
8. **Video 6**: `Ctrl+N` con un template azzera il progetto (`new_project()` in `carts/editor/main.lua`) e la scheda Lib è nascosta: il video non
   può "importare" modelli, mappa e suono in un template. Flusso provvisorio: `start.bm` contiene già il codice adattato; il video mostra la lista dei template.
   Se serve un import vero, è una feature mancante dell'SDK (candidata per Antigravity dopo AGY-03).
9. **`hit`** nel gioco = urto contro il muro basso o caduta in una buca (decisione del video 7); il bot del video 8 sbatte una volta di proposito.
10. **Video 8** a schermo intero 1920×1080 con il gioco 480×270 scalato 4× (niente pannello laterale, quindi niente overlay dei tasti); serve una variante a 480×270 dell'encoder di `bmvideo.py`.
11. **Video 5**: i passi di ogni effetto restano a 8 (STEPS non cambia); CHORD è un arpeggio veloce, non un vero accordo; `bmhost --wav` è mono (il pan non si verifica nel wav).
12. **Passi `+`/`_` di bm Sound**: il codice usa passi per parametro (es. BEND FROM 12) diversi dal "±10" del manuale; le shot list seguono il codice (da correggere il manuale: `docs-loop`/`bm-manuals`).
13. **Asset di riferimento** (`video/3d/assets/`, bm-forge): PICO guarda verso **+z** (corre verso +z con `ry = 0`), 192 triangoli; APPLE 60; CRATE 36 (totale 288). Chiude i `[DA VERIFICARE]` sulla direzione di Pico dei video 6 e 7 *per il riferimento*; il modello fatto a mano in Studio nel video 1 deve guardare anch'esso verso +z.

## Decisione 2026-10-13: il Pico dei video resta a blocchi

Il Pico costruito a mano in Studio nei video 1-3 (116 triangoli, blocchi squadrati) **si tiene com'è**; rifarlo piu dettagliato costava 4-6 ore reali piu la riregistrazione di 1, 2 e 3. Il gecko di riferimento (`video/3d/assets/`, 192 triangoli) resta come asset di verifica e per le copertine. Conseguenza dichiarata: copertina e video non coincidono del tutto (la copertina mostra il riferimento). Riaprire la scelta solo se avanza tempo a fine serie.
