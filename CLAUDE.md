# bm (BareMetal) — note per chi lavora su questo repository

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176) e, con una seconda build
(`kernel7.img`, oggi spenta), per il Pi Zero 2 W: C + assembly + Lua 5.4. Anche PowKiddy
RGB30 (RK3566, AArch64, `make TARGET=rgb30`). Cartucce `.bm`; cartella `bm/` sulla SD.
Repo `f-accomando/bm`. Il branch principale è `bm-core`: "main" vuol dire quello (un
branch `main` non esiste). Il vecchio nome del progetto resta solo dove serve alla
compatibilità (cartelle SD e header vecchi, tag di rete in `tools/bm_net.py`).

Documenti, non questo file: `README.md`, `README_OLD.md`, `docs/ROADMAP.md`,
`docs/HARDWARE.md`, `docs/API.md` + `docs/GAME-GUIDE.md` e le copie italiane
`docs/API-IT.md` + `docs/GUIDA-GIOCHI.md`, `docs/RGB30.md`, `docs/B16.md`,
`docs/RISORSE.md`, `docs/DRIVERS.md`, `docs/BENCH3D.md`, `docs/PREDICT.md`,
`docs/PADTYPE.md`, `docs/RIFF.md`, `sdk/README.md`. Gli spunti R1, R2, … sono in fondo
a `docs/ROADMAP.md` e non sono ancora decisi.

## Come si lavora

- Sviluppo nuovo (funzione, gioco, passo di milestone): prima di toccare i file chiedere
  il nome del branch, proponendone uno breve (`claude/m19-aggiornamenti`). Parte da
  `bm-core` aggiornato; commit e push vanno lì, anche se la sessione ne ha assegnato un
  altro. Non chiederlo per domande, letture della roadmap o lavoro già indicato su un
  branch.
- In sviluppo non si lancia la suite intera. Solo i test di ciò che la modifica tocca
  (QEMU `tests/qemu_test.py -k <nome>`, i test sul PC del pezzo). Per la GPU solo Overbit
  (`make test-overbit`) e il 3D Bench (`make test-b3d`). I test dei giochi si accendono
  solo nella sessione che li tocca. `kernel7` si ignora: né build né test, finché non si
  chiede `ZERO2=1`.
- Il Pi si prova senza seriale, copiando `dist/kernel.img` sulla SD (WSL `/mnt/d`) con
  `./easy_install.sh`: kernel, `make install`, immagine, kernel/file dalla rete, release,
  Market, monitor, `bm/config.txt`. Ciò che va verificato deve vedersi a schermo. I numeri
  sul Pi o sulla RGB30 li fa la console: report nel branch `reports` di
  `f-accomando/bm` (`reports/<branch>/…`); chiedere all'utente di lanciarli.
- Lista delle milestone solo a milestone chiusa per intero, o se l'utente la chiede:
  puntata, niente tabelle né icone, una riga e lo stato; le chiuse non ci stanno, tranne
  quelle chiuse nell'ultima sessione, barrate. Poi una riga **In corso**. Una sola
  milestone chiesta ("com'è M36?") va in dettaglio.

## Non fare

- Sorgenti Linux (panfrost e simili) si leggono, non si copiano (GPL). Strutture GPU da
  Mesa (`v7.xml`, MIT).
- Chiavi (`MESHY_API_KEY`, `ANTHROPIC_API_KEY`, token) solo nell'ambiente o in
  `bm/config.txt`, mai nei file.

## Build, test, deploy

- `make` → `build/kernel.img` e `build/chainloader.img` (`arm-none-eabi-gcc`).
  `make test` = test sul PC + QEMU `-M raspi0`. CI: `make test-host`
  (`HOST_SKIP=test-overbit`) e `make test-qemu SHARD=K/4`; un test nuovo oltre 20 s va in
  `SLOW`. Un tag `v*` su un commit già verde fa la release senza rifare i test.
- `kernel7` è spento in `make`, `test`, `install`, `image`, `release` e CI. Il codice
  `BM_ZERO2` resta e deve compilare. Un kernel dalla rete deve essere per la scheda giusta
  (`bmK6`/`bmK7`, offset 4). RGB30: `kernel8.img`, `manifest-rgb30`.
- App di `GAMES` alle console da `f-accomando/bm-market`; le copie in questo repo servono
  a prove e sviluppo. Una modifica su `bm-core` va anche nel Market: `make market-seed
  MARKET=<clone>`, commit e push; da WSL `scripts/market.sh` o `easy_install` m. App nuova:
  riga in `market/about.txt` (licenza obbligatoria) e posto in `GAMES`. Gli strumenti Dev
  sono nel kernel, non nel Market.
- Release firmate: `scripts/release.sh vX.Y.Z`. Niente si scrive sul Pi finché i file non
  sono scaricati e controllati; i kernel vecchi in `/bm/backup`; quello della scheda per
  ultimo.

## Invarianti

- Sezioni: 6 AUDIO, 8 MESH, 9 ANIM (`src/bm/bm.h`). Un 6 senza firma `BMAU` è MESH vecchio
  (si legge, si scrive sempre 8 e 9). INFO 10, SPRITES 11, LAYERS 12, FLAGS 13, BOXES 14.
  Risorse `BMRES` (`.bmm` `.bmi` `.bms` `.bmt` `.bmc` `.bmk`): `docs/RISORSE.md`.
- Faccia visibile dal lato in cui i vertici sono orari (`r3d.c`). Verso glTF la z cambia
  segno e l'ordine si inverte. `animate()` in `runtime.c` e la posa in
  `carts/animator/main.lua` fanno gli stessi conti: cambiarli insieme.
- Pagine Lua degli strumenti: un `do … end` che esporta in `P`, meno di 200 locali.
  Scritte sulle righe di 16 px; la cornice di un dialogo non passa sulla riga del titolo
  (i test QEMU la leggono). In Lua `cond and nil or x` dà sempre `x`. bm Mesh riscrive
  solo le righe tra `-- [bm Mesh begin]` e `-- [bm Mesh end]`.
- API nuova o cambiata in tutti e quattro i doc (stesse tabelle e esempi) e in
  `src/ai/kb/`, poi `make ai-model` e commit di `assist.weights`. Voce nuova con
  `title_en:` e `text_en:`. Commenti `#` della kb solo in cima al file.
- Flag tile (bmlib): 0 solido, 1 piattaforma, 2 scala, 3 acqua, 4 fa male. Token del
  codice (`stat(11)`): informazione, mai un limite (`docs/B16.md` §2.4).
- Lockstep (Overbit e `bmnet`): ogni console simula tutto, viaggiano solo i comandi.
  Caso di gioco con `grandom()`; niente `G.frame`, camera, qualità o stato cambiato nel
  disegno. Banco suoni in tre posti (C `au_parse`, Lua, `scripts/bmaudio.py`): cambiarli
  insieme. Shader QPU in `tools/qpuasm.py` → `src/gpu/shaders.h` (`make test-qpu`).
- Un solo menu, `src/kernel/menu_ui.c`, per Pi e RGB30. Settings in `src/kernel/settings.c`
  per entrambi; una riga nuova va lì. Schede Pi: Market · Games · Dev · Lib · Settings,
  ma **Lib è nascosta** finché `lib_tab=1`. Il menu si apre su Games. Market non blocca il
  menu e non carica se la scheda non è attiva. Fibre: `background_stop()` prima di un'app,
  di `rescan()` e di una cancellazione; `net_wait_step` va controllato (ritorna -1 se
  annullata).
- RGB30: B conferma, A indietro (`confirm=a` li scambia). Usare `pad_ok` / `pad_back`, mai
  `PAD_A` / `PAD_B`. Barra: solo WiFi e batteria. `.bm` visibili per le prove
  (`show_bm=0` li nasconde); il Market scarica solo `.b16`.
- Tasti di sistema in `src/kernel/syskeys.c`: le app non li riusano. F12 tenuto aiuto,
  Esc menu, Ctrl+Esc = PS (torna al menu), F11 overlay, F6 assistente, F5 / Ctrl+R prova.
  In rete `online(true)`: PS non sospende, chiede "Leave the match?" e chiama `_leave()`.
  Solo un mouse muove il puntatore (`mouse(true)` per chiederlo; `mouse=off` lo spegne).
  Riavvio automatico: sempre 3 s contati.
- Cartucce non incorporate: solo `.bm` nuovi in `/carts`, e solo quelli fatti nella stessa
  partita; niente `picture3d`. Rete e `report()` chiedono il permesso la prima volta
  (`allow_…` in `bm/config.txt`).
- GPU: V3D in `src/gpu/gpu3d.c`; software se `gpu3d=0`, in QEMU, o se la GPU non risponde.
  Cosa non documentato lo impara la prova all'avvio e si spegne da sola. Versione in
  `src/gpu/version3d.h`: un passo che cambia cosa sa fare alza `bm3d` e una riga in
  `docs/DRIVERS.md`. Dettaglio driver, score e opzioni: quel doc e `docs/BENCH3D.md`, non
  qui. 2D disegnato mentre un lavoro GPU è in volo passa da `draw2d()` / `sync3d()` /
  `flush3d(1)`.

## Dove sta il resto

Non ricopiare qui la cronaca. Prima di modificare un'area, leggere il doc o i file:

- SDK, Studio, Animator, Mesh, Pixel: `sdk/README.md`, `carts/{editor,studio,animator,mesh,pixel}/`, `src/script/bm3d.lua`.
- API giochi, bmlib, bmnet, salvataggi a 8 slot: `docs/API-IT.md`, `src/script/bmlib.lua`, `src/script/bmnet.lua`.
- Overbit: `carts/overbit/` (`build.py`, `art/`). Qualità: solo 1080p (GPU) e 640×360 (ARM); il gioco sceglie il resto (`src/85_quality.lua`). Bench a flag: `cart_config("overbit_bench")`, `./easy_install.sh bench`.
- Yharnam: `carts/yharnam/`. Grafica da `mkassets.py`; dopo un cambio, rieseguirlo e commit di `sheet.png`. Non fare commit di uno sheet `YH_DRAFT=1`.
- Audio, riff, assistente musica: `src/audio/`, `docs/RIFF.md`. Assistente, mesh da immagine, riduttore: `src/ai/`, `src/ai/kb/README.md`. Predict e pad: `docs/PREDICT.md`, `docs/PADTYPE.md` (le tabelle `CROSS`/`FACE`/`KB` si cambiano in `padtype.lua`).
- bm Write e `/docs`: `carts/write/`, `docs/API-IT.md` (*Documenti*). Ctrl+I / Ctrl+M arrivano come `"^i"` / `"^m"`.
- Menu, splash, LED, report, aggiornamenti, Market: `src/kernel/{menu_ui,settings,loading,ledstate,reports,update,market}.c`. RGB30: `docs/RGB30.md`, `src/rgb30/`.
