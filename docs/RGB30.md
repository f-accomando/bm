# bm su PowKiddy RGB30

Versione bare metal di bm per la **PowKiddy RGB30**: SoC Rockchip **RK3566** (4 × Cortex-A55 a
64 bit, 1 GiB LPDDR4), schermo IPS **720×720** su MIPI-DSI (pannello Sitronix ST7703), due slot
microSD, WiFi + Bluetooth Realtek **RTL8821CS**, PMIC Rockchip RK817. Il codice vive nel branch
`rgb30-powkiddy`; la build del Pi non cambia.

Decisioni (2026-10-01, utente):

- **bare metal vero**, come sul Pi: niente Linux; U-Boot avvia il kernel dalla SD;
- **formato 1:1**: il menu era **512×512**, al centro del pannello senza ingrandimento; dal
  2026-10-03 (utente) è **360×360 ingrandito ×2** e riempie i 720×720 del pannello;
- giochi e app dell'RGB30 useranno un formato nuovo, **`.b16`** (prima `.s16`: la cartuccia a risorse limitate per le
  console portatili, `docs/B16.md`), ancora da definire: il menu li
  elenca ma non li avvia;
- le cartucce **`.bm` del Pi erano nascoste** (`show_bm=1` le elencava soltanto); dal 2026-10-03
  (utente), per le prove, il menu le mostra e le avvia (`show_bm=0` in `bm/config.txt` le
  nasconde);
- prima milestone: base + **Bluetooth + WiFi** (audio e salvataggi dopo).

## Compilare

In WSL/Ubuntu:

```sh
sudo apt install gcc-aarch64-linux-gnu picolibc-aarch64-linux-gnu mtools dosfstools \
                 qemu-system-arm python3
make TARGET=rgb30              # build/rgb30/kernel8.img, copiato in dist/rgb30/
make TARGET=rgb30 test         # lo stesso kernel per la macchina virt di QEMU, con i test
make TARGET=rgb30 firmware     # una volta: bootloader e firmware Realtek (firmware/rgb30/)
make TARGET=rgb30 image        # dist/rgb30/bm-rgb30.img (+ .img.gz): l'immagine della SD
```

- Compilatore: `aarch64-linux-gnu-gcc` (quello di Ubuntu) con **picolibc** al posto di newlib
  (Ubuntu non ha newlib per aarch64). Le opzioni "da Linux" di Ubuntu (PIE, stack protector,
  fortify, branch protection, outline atomics) sono spente in `rgb30.mk`.
- `PLAT=rk3566` (predefinito) è la console; `PLAT=virt` è QEMU (`-M virt,gic-version=3 -cpu
  cortex-a55`, seriale PL011, schermo ramfb, SD come disco in RAM). Le due build differiscono solo
  per i file `plat_*.c` / `rk_*.c` e per l'indirizzo di link.

## La scheda SD

1. `make TARGET=rgb30 firmware` scarica:
   - il **bootloader** dall'immagine ufficiale ROCKNIX per RK3566 (release `20260901`): solo il
     primo MiB del `.img.gz` (richiesta HTTP con range), da cui vengono ritagliati `idbloader.img`
     (settore 64) e `u-boot.itb` (settore 16384). Contengono l'inizializzazione della DDR di
     Rockchip, **U-Boot 2026.01** (mainline, `anbernic-rgxx3-rk3566_defconfig` + 3 patch ROCKNIX)
     e il BL31 di Rockchip. È lo stesso bootloader che la console usa già con ROCKNIX;
   - il firmware Bluetooth `rtl8821cs_fw.bin` / `rtl8821cs_config.bin` da linux-firmware.

   Ogni file è controllato con il suo SHA-256 (`scripts/fetch-rgb30.sh`).
2. `make TARGET=rgb30 image` crea `dist/rgb30/bm-rgb30.img` (256 MiB; 1,1 MB compressa): MBR con
   una partizione FAT32 "BM" da 16 MiB, attiva, che contiene `extlinux/extlinux.conf`,
   `kernel8.img`, `LEGGIMI.txt` e `bm/` con il firmware Bluetooth.
3. Scrivere l'immagine su una microSD **libera** con balenaEtcher, Raspberry Pi Imager ("Usa
   personalizzato") o Rufus. WSL non scrive direttamente sulle schede.
4. La scheda va nello slot **TF1** (quello da cui si avvia ROCKNIX); per tornare a ROCKNIX basta
   rimettere la sua scheda. Accendere con il **tasto di accensione** (collegando il caricatore
   U-Boot si spegne di nuovo).
5. Aggiornare bm: Windows vede la partizione "BM" come un'unità con quel nome; basta copiarci
   sopra `dist/rgb30/sd/` (`make TARGET=rgb30 sdcard`). Da WSL `make TARGET=rgb30 sdcard
   SD=/mnt/<lettera>` copia da solo (`scripts/copy-sd-rgb30.sh`) e, se non può, dice perché:
   unità non montata in WSL (con il comando `mount -t drvfs` per montarla), montata solo per
   root, adattatore SD bloccato, oppure non è la scheda dell'RGB30. `bm/config.txt` sulla scheda
   resta com'è (si sostituisce solo con `RGB30_CONFIG=file`).

Non formattare né ripartizionare la scheda: il bootloader sta prima della partizione.

### Come parte

ROM di avvio → `idbloader.img` (DDR, SPL) → `u-boot.itb` (BL31, U-Boot) → `bootflow scan` →
`/extlinux/extlinux.conf` della prima partizione della SD in TF1 → `booti kernel8.img`. Il
kernel ha l'intestazione delle Image arm64 (`src/rgb30/start.S`): U-Boot lo carica a
0x02000000 e lo avvia a **EL2**, MMU e cache spente, `x0` = device tree. `start.S` si sposta al
suo indirizzo di link (0x10000000), scende a EL1 e chiama `kernel_main`.

## Durante l'avvio, senza cavo seriale

| LED | Significato |
|---|---|
| rosso acceso | avvio in corso (fermo così: il kernel si è bloccato presto) |
| verde fisso | bm funziona e non c'è niente che non va (dal 2026-10-04: `src/kernel/ledstate.c`) |
| verde che lampeggia lento (1 s acceso, 1 s spento) | altro: l'avvio non è finito, la SD manca o non si legge, la batteria è scarica (sotto 3,45 V, senza caricatore), arriva un kernel o si installa un aggiornamento |
| rosso fisso + verde che lampeggia | lo schermo ha avuto problemi: leggere `bm/bootlog.txt` |
| rosso che lampeggia N volte, pausa | eccezione fatale N (1 sincrona, 4 SError, 9 panic) |

All'avvio lo schermo mostra il logo di bm (`src/kernel/splash.c`); quello che il kernel scrive
va nella console (dietro), sulla seriale, in `bm/bootlog.txt` e in *Settings > System > Log since
boot*.

Se si ferma mentre accende lo schermo (`bm/bootlog.txt` finisce con `display: starting`), i LED
fermi dicono dove:

| LED fermi | Bloccato in |
|---|---|
| rosso e verde | clock del video (VPLL, `clocks_on`) |
| solo verde | controller video (VOP2, `vop_init`) |
| nessuno | collegamento DSI, D-PHY o comandi al pannello (`rk_dsi_init`) |
| solo rosso | dominio di alimentazione del video, oppure finestra e retroilluminazione |

**Dopo un riavvio** (un aggiornamento, *Restart*, un kernel dalla rete; 2026-10-05): prima di
riavviare bm spegne retroilluminazione, pannello e modulo WiFi e i due LED (`quiet()` in
`plat_rk3566.c`), poi chiede il riavvio al firmware (PSCI SYSTEM_RESET) e, se il firmware torna
indietro, fa il reset globale del chip (`CRU_GLB_SRST_FST`, come Linux). Un riavvio del solo chip
lascia i binari del PMIC e i GPIO del PMU come erano: spenti prima, il pannello riparte come
dall'accensione. Dal 2026-10-10 (schermo nero dopo un aggiornamento, a posto spegnendo e
riaccendendo) il pannello ha un vero ciclo di alimentazione: allo spegnimento, se il collegamento
è su, riceve *display off* (DCS 0x28) e *sleep in* (0x10), poi reset basso, alimentazione
(GPIO0_C2) spenta e 300 ms per scaricarsi; all'avvio `rk_dsi_init` lo rimette comunque in reset e
spento per almeno 200 ms, lo riaccende, rilascia il reset e aspetta 120 ms prima dei comandi.
Dopo i comandi rilegge il *power mode* del pannello (DCS 0x0A): se non è acceso (o i comandi sono
falliti) rifà una volta tutto il ciclo. Il risultato è nel registro (`power mode 9c`,
`readback none`, `power cycle again`). Se lo schermo resta nero dopo il riavvio:
- LED spenti: bm non è ripartito (fermo nel firmware o in U-Boot), oppure è fermo nel DSI
  (tabella qui sopra): lo dice `bm/bootlog.txt`, da leggere sul PC **prima** di riaccendere (ogni
  avvio lo riscrive). La prima riga è la versione dell'avvio che l'ha scritto: se è ancora quella
  di prima, bm non è ripartito;
- rosso acceso, o le altre combinazioni della tabella: bm è ripartito e si è fermato lì.

Lo schermo: il bordo intorno a un'immagine più piccola del pannello (un gioco con `bm_scale=int`)
è il colore di sfondo del controller video (lo stesso blu-grigio scuro del menu). Tutto uniforme blu-grigio = pannello acceso ma finestra non
funzionante; nero = collegamento DSI o pannello; niente del tutto = retroilluminazione.

**`bm/bootlog.txt`**: a ogni avvio il kernel scrive sulla SD tutto quello che ha stampato
(versione, CPU, SD, cosa ha fatto il driver dello schermo passo per passo, menu). La SD si legge
prima di accendere lo schermo e il registro si scrive tre volte (prima dello schermo, dopo, e a
`ready`): anche se lo schermo blocca tutto, dal PC si vede fin dove è arrivato. Lo stesso registro
è nel menu, alla voce *Boot log*.

Mentre lo schermo parte, `bm/bootlog.txt` si riscrive a ogni passo, con righe `display: ...`:
`display: starting`, `display: DSI link and panel starting`, `display: panel power cycled`
(`... again` al secondo giro), `display: panel readback: on` / `not on` / `no answer`, poi
`display: panel on` o `display: panel failed`. L'ultima riga presente dice dove si è fermato.

**I registri della volta prima** (per capire dopo cosa è successo; FAT, nomi 8.3, tutti in `bm/`):

| File | Quando si scrive | Cosa contiene |
|---|---|---|
| `bootlog.txt` | a ogni avvio, più volte (vedi sopra) | il registro di questo avvio fino a `ready` |
| `bootprev.txt` | a ogni avvio, appena letta la SD (prima dello schermo) | il `bootlog.txt` dell'avvio prima, poi una riga su come è finito (dal record in RAM di `crumbs.c`) e le sue ultime righe stampate (4 KiB tenuti in RAM attraverso il riavvio; dopo uno spegnimento non ci sono: `(nothing in RAM)`) |
| `lastrun.txt` | prima di un riavvio o di uno spegnimento fatto da bm (*Restart*, *Shut down*, aggiornamento, kernel dalla rete: tutti passano da `plat_reset` / `plat_poweroff`), prima di spegnere schermo e audio | tutto quello che quella sessione ha stampato (i primi 64 KiB; se è pieno, in fondo le ultime righe) |

Non si scrive `lastrun.txt` da un'interruzione né mentre un altro trasferimento sta scrivendo
sulla SD (un kernel dal PC): in quel caso resta quello di prima. Non c'è (ancora) una copia
periodica dal menu: un blocco senza riavvio lascia solo `bootprev.txt` all'avvio dopo.

Per leggerli: spegnere la console, togliere la SD, metterla nel PC e aprire la cartella `bm/`
(con un editor di testo qualsiasi). Dopo uno schermo nero: `lastrun.txt` dice come è finita la
sessione prima del riavvio (deve finire con `restarting: this run's log in bm/lastrun.txt`),
`bootlog.txt` come è andato l'avvio nero (le righe `display: ...`), e se nel frattempo la console
è stata riaccesa `bootprev.txt` ha il registro dell'avvio nero.

## Cosa c'è (stato)

- Base a 64 bit: avvio da U-Boot (EL2 → EL1), MMU (RAM cache WB, framebuffer non-cacheable,
  periferiche Device), GICv3, timer generico a 1 kHz, eccezioni con registri a schermo, picolibc,
  Lua 5.4. **Provata in QEMU** (`make TARGET=rgb30 test`), anche attraverso U-Boot 2026.01.
- Schermo: VOP2 (video port 1, finestra Esmart0) → MIPI DSI0 → D-PHY Innosilicon → pannello
  ST7703, retroilluminazione PWM4. Valori di Linux (`rockchip_drm_vop2.c`, `dw-mipi-dsi.c`,
  `phy-rockchip-inno-dsidphy.c`, `panel-sitronix-st7703.c`). **Da provare sulla console.**
- Comandi: 18 tasti su GPIO3, levette su SARADC canale 3 con commutatore; pagina *Input test*.
  **Provati sulla console** (2026-10-03): tasti a posto; l'asse verticale delle levette era al
  contrario (il dts lo dà diverso), corretto.
- Conferma e indietro (decisione dell'utente, 2026-10-03): **B** (il tasto in basso) conferma e
  **A** torna indietro, come sull'RGB30; `confirm=a` in `bm/config.txt` li scambia. Dalla seriale e
  dalla console di rete Invio è conferma, Backspace/Esc indietro; i pad e le tastiere Bluetooth
  premono il tasto nella stessa posizione (la croce del DS4 è B, lo spazio della tastiera anche).
- SD: controller SDMMC0 (DesignWare MSHC) in PIO, 4 bit, 12 MHz; FAT dal codice del Pi.
- PMIC RK817 su I2C0: spegnimento, tensione della batteria, stato di carica.
- Modalità video pronte per la GPU (`src/rgb30/display.h`): ogni framebuffer ha la forma che la
  GPU Mali-G52 vuole per disegnarci (righe allineate a 64 byte, altezza a tessere da 16 pixel,
  pagine su confini di 64 KiB, fino a 3 pagine) nella memoria video e GPU, con il suo indirizzo
  fisico (`fb->bus`). Il controller video ingrandisce l'immagine sul pannello: un gioco può
  disegnare a 720×720 o a 360×360 mostrato ×2 (un quarto dei pixel per la GPU), nitido o
  sfumato; il menu è 360×360 ×2. Pagina **Display** nel menu: le modalità una dopo l'altra
  con un'immagine di prova (bordi, griglia delle tessere, barre di colore).
- **GPU Mali-G52, primo passo del driver** (M41, bm3d 6.0; `src/rgb30/mali.c`, *Dev > GPU test*):
  solo quando lo si chiede (l'avvio non tocca la GPU), un passo alla volta con una riga a schermo e
  un report `gpu`: vdd_gpu acceso (RK817 DCDC2, altrimenti si ferma subito), orologi del dominio
  PD_GPU (CRU) e dominio acceso (PMU, come i domini di Linux), GPU_ID e parti presenti, reset,
  accensione di L2, core e tiler, spazio di indirizzi 0 con le tabelle "Mali LPAE" (64 MiB della
  memoria video e GPU visti 1:1), un lavoro WRITE_VALUE sullo slot 1 e una catena di due. Ogni
  attesa ha un limite; il primo passo che non torna ferma la prova e dice perché (lo stato del
  lavoro, un errore dell'MMU con il suo indirizzo). Poi (bm3d 6.1) un lavoro di frammenti senza
  disegni che pulisce una superficie di 64×64 (controllata pixel per pixel) e un **quadrato verde
  in alto a destra** dello schermo: se si vede, la GPU ha scritto i pixel. Niente triangoli ancora. **Provato sul PC** con una GPU, un CRU e un PMU
  simulati (`make TARGET=rgb30 test-mali`); in QEMU la pagina dice che la GPU non c'è
  (`test_gpu_test`); **da provare sulla console.**
- Cartucce del Pi (`.bm`) nel menu e avviabili, per le prove: Yharnam nell'immagine SD (vedi
  sotto).
- Menu 360×360 ingrandito ×2 (riempie il pannello): dal 2026-10-04 è **lo stesso del Pi**
  (`src/kernel/menu_ui.c`: barra con le schede a pillola e le icone, copertine quadrate 88×88, tre per
  riga, pannelli, suggerimenti dei tasti). Schede **Market** (dal 2026-10-05, la prima, fuori
  dallo schermo a sinistra finché non è la scheda: lo stesso Market del Pi, ma solo i giochi
  `.b16` del catalogo; B chiede e scarica, il gioco va in `bm/` e si gioca da lì, X i dettagli:
  gioca, scarica di nuovo, cancella), **Games** (giochi `.b16` e i `.bm`, con la loro
  copertina; con `show_bm=0` dice quante cartucce sono nascoste), **Dev** (3D Bench, Render bench,
  Display, Input test, Boot log, Lua sulla seriale, GPU test) e **Settings**, l'ultima, una pagina a sé:
  dal 2026-10-04 le stesse sezioni del Pi (`src/kernel/settings.c`): Controllers
  (Bluetooth, abbinare pad, tastiere e mouse, prova dei tasti, *Confirm button*, layout della
  tastiera, icone), WiFi and network (rete salvata, indirizzo, console di rete, collegarsi,
  *Test the connection*), Screen and sound (i modi dello schermo, l'overlay delle prestazioni, il
  suono: Sound, Volume, *Test the sound*), Updates, Reports, System (versione, memoria, SD, batteria, il log, riavvio,
  spegnimento). Solo il controller: L1/R1 le schede (senza fare il giro, come sul Pi), la croce
  le copertine e le righe, **B** apre e **A** torna indietro (`confirm=a` li scambia, anche nei
  suggerimenti). A destra della barra solo la rete e la **batteria** (dal 2026-10-05; niente icone
  di controller, mouse e tastiere, decisione dell'utente): quattro
  tacche dal 75% in su, una in meno ogni 25%, vuota e rossa sotto il 10%, un fulmine sul
  caricatore; la carica viene dalla tensione (0% a 3,45 V, quando il LED avvisa, 100% a 4,18 V),
  scritta anche in *Settings > System > Battery*. Sul caricatore la tensione sale: lì la
  percentuale è solo un'indicazione. Le righe che lavorano (abbinare, collegarsi, aggiornare, bench, log) scrivono
  ancora sulla console di testo.
- **3D Bench** (scheda Dev, `src/rgb30/b3d_rgb30.c`): lo stesso banco di prova del Pi
  (`src/bm/b3d.c`), a 640×360 ingrandito sul pannello; tutte le prove 3D disegnate dall'ARM (la GPU
  Mali non ha ancora un driver: le colonne GPU restano vuote), i contatori del Cortex-A55
  (istruzioni, miss della cache dati L1, cicli; `start.S` li lascia a EL1 con `MDCR_EL2.HPMN`), il
  rapporto in `bm/bench/3DNNNN.TXT` sulla SD e sulla seriale. Sinistra/destra sfogliano le pagine
  dei risultati, il tasto indietro (A) torna al menu. In QEMU dura circa 40 s
  (`test_bench3d`).
- Bluetooth: RTL8821CS su UART1, protocollo H5 (`src/bt/h5.c`) e firmware Realtek
  (`src/bt/rtlbt.c`), poi lo stesso stack del Pi (controller e tastiere). H5 e firmware **provati
  sul PC** contro un chip simulato (`make TARGET=rgb30 test-bt`); **da provare sulla console.**
- WiFi (RTL8821CS su SDIO, port di rtw88): accensione, firmware, MAC dall'efuse, tabelle MAC/BB/RF,
  canali e potenza, calibrazione IQK; **scansione** (probe request e ascolto sui canali 1-13; a
  schermo i pacchetti per canale e le risposte al nostro MAC, che provano la trasmissione);
  **collegamento** a reti aperte e WPA2-PSK (autenticazione, associazione, handshake a 4 vie e
  rinnovo della chiave di gruppo in software, `src/rgb30/wpa.c`; le chiavi nella CAM del chip, che
  cifra e decifra in CCMP); dati 802.11 ↔ Ethernet per **lwIP** (DHCP, console di rete, invio di
  file: le stesse di M18); il collegamento è controllato dai beacon (8 s senza: perso) e dai
  deauth. 2,4 GHz, fino a 54 Mbit/s (niente 802.11n per ora). **Provato sul PC** con un chip e due
  access point simulati, DHCP e ping compresi (`make TARGET=rgb30 test-wifi`); **da provare sulla
  console.**

## Il suono

Dal 2026-10-05 (branch `claude/rgb30-audio`, verificato sulla console e unito il 2026-10-06) la
RGB30 suona come il Pi: lo stesso sintetizzatore
a 8 voci, i banchi dei giochi, la musica, gli effetti e nano8 (`src/audio/audio.c`). L'uscita è
`src/rgb30/rk_audio.c`: l'I2S1 del RK3566 manda 48 kHz a 16 bit al codec dentro il RK817 (il
chip della batteria), che suona dalle **cuffie** o dall'**altoparlante**: quando si infilano le
cuffie la console passa a loro da sola, e le cuffie sono mono. I valori vengono da Linux
(device tree `rk3566-powkiddy-rk2023.dtsi`, driver `rockchip_i2s_tdm.c` e `rk817_codec.c`).

- **Volume**: i tasti **+** e **−** sul lato, ovunque (menu, pagine, giochi): una barra
  "Volume" sopra lo schermo per un momento; tenuti continuano. Il livello si salva in
  `bm/config.txt` (`volume=`, 0–10, come sul Pi) quando i tasti restano fermi per 2 s. Anche
  *Settings > Screen and sound > Volume*.
- **All'avvio** un suonino di due note, come sul Pi: se si sente, il suono va.
- **Se non si sente niente**: *Settings > Screen and sound*: la riga *Sound* dice `on` o `off`
  e, scelta, il perché sotto; *Test the sound* scrive lo stato, i contatori dell'I2S
  (interrupt, pezzi, buchi) e il MCLK (deve essere 12288000 Hz), poi suona la melodia di prova
  (le sei forme d'onda, un accordo, glissando, vibrato, arpeggio). Una foto di quella pagina
  basta per capire dove si ferma.
- Prove sul PC: `make TARGET=rgb30 test-audio` (il driver su un chip simulato) e
  `test_sound` in QEMU (una sink che prende i 48 kHz al posto dell'I2S e dice che nota sente).

## Cartucce del Pi (`.bm`) per le prove

Per le prove (decisione dell'utente, 2026-10-03: "per il momento") la scheda Games elenca le
cartucce `.bm` di `bm/` e le **avvia**, senza impostazioni; `show_bm=0` in `bm/config.txt` le
nasconde. Il runtime è quello del Pi (`src/bm`), lo stesso codice compilato a
64 bit; quello che del Pi non c'è lo sostituiscono `src/rgb30/bm_port.c` (il 3D disegnato
dall'ARM, niente DMA) e `src/rgb30/bm_input.c` (i comandi); il suono è quello del Pi (sotto). Nell'immagine SD c'è
**Yharnam** (360×360 dal 2026-10-10, prima 256×256; dal branch `claude/yharnam`).

- Schermo: la cartuccia disegna alla sua risoluzione e il controller video la ingrandisce fino a
  riempire il pannello (256×256 → 720×720), nitida. `bm_scale=int`: solo multipli interi (256 ×2 =
  512×512, i pixel tutti uguali, con il bordo); `bm_smooth=1`: sfumata.
- Tasti: nei giochi valgono le lettere stampate sulla console (un gioco che scrive "A: start" vuole
  il tasto A); `game_buttons=position` li mette per posizione, come un DS4 sul Pi (il tasto in basso,
  B, diventa la A del gioco). Le levette sono la levetta sinistra e destra del gioco; Start +
  Select esce e torna al menu.
- In QEMU il gioco gira (test `test_bm_cartridge`), mostrato 1:1 (QEMU non ingrandisce e non ha il
  formato a 16 bit: lo converte `plat_virt.c`).

## WiFi: come si usa

In `bm/config.txt` sulla SD (dal PC):

```
wifi_ssid=NomeDellaRete
wifi_psk=password
```

Oppure già nell'immagine: `make TARGET=rgb30 image RGB30_CONFIG=$HOME/rgb30-config.txt` mette quel
file come `bm/config.txt` (tienilo fuori dal repository: contiene la password).

Nel menu, *WiFi*: **B** cerca le reti (elenco con segnale, canale, sicurezza), **X** entra nella
rete di `wifi_ssid`; poi DHCP e l'indirizzo IP sullo schermo, con la password della console di
rete (`python3 tools/bm_net.py <ip>`: i tasti w/a/s/d, Invio, Esc arrivano al menu come dalla
seriale). All'avvio la console entra da sola nella rete salvata, come il Pi (`wifi_boot=0` lo
spegne, anche da *Settings > WiFi and network*). Reti supportate: aperte e WPA2-PSK (anche
WPA2/WPA3 miste); non WPA3 sola, WPA1, WEP, enterprise.

## Report dei test

*Settings > Reports*: i report dei test (3D Bench, Render bench, il log) aspettano in
`bm/reports` sulla SD e vanno nel branch `reports` di `f-accomando/bm` con `github_token` in
`bm/config.txt` e il WiFi; *Send the reports* li manda, *Report the log* fa un report del log. Il nome dice kernel,
branch e scheda: `reports/<branch>/<data>_<tipo>_rgb30_<kernel>.txt`.

## Aggiornare bm

- **Dalla console** (WiFi collegato): *Settings > Updates* legge l'ultima release di GitHub
  (`manifest-rgb30.txt`, firmato con la chiave delle release che sta nel kernel), dice se è più
  nuova e quali file cambiano (`kernel8.img`, `bm/ca.pem`); *Install the update* la installa: scarica e
  controlla tutto prima di scrivere, tiene il kernel di prima in `bm/backup/kernel8.img`,
  scrive `kernel8.img` per ultimo e riavvia. `update_url=sd:/cartella/` in `bm/config.txt` per
  una release copiata sulla SD (le prove).
- **Dalla rete**: `python3 tools/bm_net.py <ip> --kernel build/rgb30/kernel8.img` (la password è
  quella che *WiFi* mostra quando la console entra nella rete), oppure `./easy_install.sh`, voce
  1 ([NET] update kernel), con un profilo di scheda RGB30.
- **Dal PC**: copiare `kernel8.img` sulla SD.

## Giochi dalla rete

`python3 tools/bm_net.py <ip> --send gioco.b16` (o `./easy_install.sh`, voce 5): un `.bm` /
`.b16` per `/carts` (il default) finisce in `bm/`, la cartella che il menu elenca. La console
risponde appena il file è arrivato intero (`QD`) e lo scrive sulla SD dal menu, un pezzo per
frame in una fibra: il gioco ha l'etichetta *Updating* e non parte finché non è scritto; la
barra in alto dice i KiB scritti. Se un gioco è aperto il file aspetta in memoria e va sulla SD
al ritorno nel menu. Un secondo file mentre il primo aspetta: `BY` (occupata), si rimanda dopo.

## File

- `rgb30.mk` — la build (incluso dal Makefile con `TARGET=rgb30`).
- `src/rgb30/` — tutto ciò che è specifico: `start.S`, `vectors.S`, `mmu.c`, `cache.c`, `gic.c`,
  `timer.c`, `exc.c`, `syscalls.c` (picolibc), `fb.c`, `glue.c` (le API dei driver del Pi),
  `pad.c` (comandi, anche dalla seriale), `ui.c` (menu), `main.c`;
  `plat_virt.c` + `sd_virt.c` (QEMU); `plat_rk3566.c`, `rk_gpio.c`, `rk_input.c`, `rk_board.c`
  (LED), `rk_display.c` (VOP2), `rk_dsi.c` (DSI, D-PHY, pannello), `rk_mmc.c` (DesignWare MSHC),
  `rk_sd.c`, `rk_pmic.c`; Bluetooth: `rk_wlbt.c` (alimentazione del modulo), `rk_btuart.c`,
  `rk_bt.c`; WiFi: `rk_sdio.c`, `rtw_io.c`, `rtw_mac.c` (accensione, firmware, efuse),
  `rtw_init.c` + `rtw8821c_table.c` (MAC e radio, CAM, comandi al firmware), `rtw_frame.c`
  (pacchetti, 802.11), `wpa.c` (WPA2), `rtw_sta.c` (le funzioni di `wifi/wifi.h`); GPU: `mali.c`
  (il driver, portabile), `gputest_rgb30.c` (la pagina *GPU test*).
- Codice in comune con il Pi: `gfx/`, `lib/printf.c`, `script/luavm.c` e `lib_bm.c`, `fs/fat.c`,
  `kernel/config.c`, `crumbs.c`, `version.c`, Lua.
- `tests/rgb30/qemu_test.py` — test in QEMU (avvio, EL2 e spostamento, schermo letto dai pixel,
  Lua, menu, `.bm` nascosti, input test, bootlog sulla SD).
- `tests/rgb30/rtw_frame_test.c`, `wpa_test.c` (vettori pubblicati e un handshake calcolato a
  parte da `wpa_vectors.py`), `wifi_sim_test.c` (tutta la stazione su un RTL8821C e due access
  point simulati): `make TARGET=rgb30 test-wifi`.
- `tests/rgb30/mali_test.c` — la prova della GPU su un Mali-G52, un CRU e un PMU simulati (tabelle
  delle pagine percorse come fa la GPU, lavori eseguiti, varianti che non tornano):
  `make TARGET=rgb30 test-mali`.
- `boot/rgb30/` — `extlinux.conf` e `LEGGIMI.txt` della scheda.
- `scripts/fetch-rgb30.sh` — bootloader e firmware; `scripts/mksd.py --start-mib --raw --active`.

## Mappa della memoria (RK3566)

| Da | A | Uso |
|---|---|---|
| 0x00000000 | 0x00200000 | TF-A (BL31): non mappato |
| 0x02000000 | | dove U-Boot carica `kernel8.img` (poi si sposta) |
| 0x10000000 | | kernel (testo, dati, bss, stack 1 MiB, tabelle MMU), poi l'heap |
| 0x3c000000 | 0x3fc00000 | memoria video, 60 MiB (non-cacheable): i framebuffer |
| 0x3fc00000 | 0x40000000 | memoria della GPU Mali, 4 MiB (non-cacheable): tabelle delle pagine, lavori (`PLAT_GPU_START`) |
| 0xfc000000 | 0xffffffff | periferiche (GIC 0xfd400000, CRU 0xfdd20000, VOP2 0xfe040000, DSI0 0xfe060000, SDMMC0 0xfe2b0000, UART2 0xfe660000, …) |

## Licenze dei file scaricati

- U-Boot: GPL-2.0+ (tag `v2026.01` + le patch ROCKNIX in
  `projects/ROCKNIX/devices/RK3566/packages/u-boot-Generic/patches` della release `20260901`).
- Inizializzazione DDR `rk3568_ddr_1056MHz_v1.23.bin` e BL31 `rk3568_bl31_v1.45.elf`: licenza
  rkbin di Rockchip (ridistribuzione permessa, niente reverse engineering).
- Firmware Realtek: `LICENCE.rtlwifi_firmware.txt` (ridistribuibile senza modifiche), copiata
  in `bm/` sulla scheda.
- Il driver WiFi (`src/rgb30/rtw*.c`, tabelle in `rtw8821c_table.c`) è un port di rtw88 di Linux,
  GPL-2.0 OR BSD-3-Clause, usato con la licenza BSD-3-Clause (Copyright Realtek Corporation).

Nessuno di questi file è nel repository: li scarica `make TARGET=rgb30 firmware`.
