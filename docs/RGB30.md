# bm su PowKiddy RGB30

Versione bare metal di bm per la **PowKiddy RGB30**: SoC Rockchip **RK3566** (4 × Cortex-A55 a
64 bit, 1 GiB LPDDR4), schermo IPS **720×720** su MIPI-DSI (pannello Sitronix ST7703), due slot
microSD, WiFi + Bluetooth Realtek **RTL8821CS**, PMIC Rockchip RK817. Il codice vive nel branch
`rgb30-powkiddy`; la build del Pi non cambia.

Decisioni (2026-10-01, utente):

- **bare metal vero**, come sul Pi: niente Linux; U-Boot avvia il kernel dalla SD;
- **formato 1:1**: il menu è **512×512**, al centro del pannello senza ingrandimento (sarà
  riadattato in seguito);
- giochi e app dell'RGB30 useranno un formato nuovo, **`.s16`**, ancora da definire: il menu li
  elenca ma non li avvia;
- le cartucce **`.bm` del Pi sono nascoste** (blocco "morbido": `show_bm=1` in `bm/config.txt`
  le elenca, ma non partono);
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
5. Aggiornare bm: Windows vede la partizione "BM"; basta copiarci sopra
   `dist/rgb30/kernel8.img` (da WSL: `/mnt/<lettera>/kernel8.img`).

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
| verde che lampeggia (1 Hz) | bm funziona (interrupt del timer) |
| rosso fisso + verde che lampeggia | lo schermo ha avuto problemi: leggere `bm/bootlog.txt` |
| rosso che lampeggia N volte, pausa | eccezione fatale N (1 sincrona, 4 SError, 9 panic) |

Se si ferma mentre accende lo schermo (`bm/bootlog.txt` finisce con `display: starting`), i LED
fermi dicono dove:

| LED fermi | Bloccato in |
|---|---|
| rosso e verde | clock del video (VPLL, `clocks_on`) |
| solo verde | controller video (VOP2, `vop_init`) |
| nessuno | collegamento DSI, D-PHY o comandi al pannello (`rk_dsi_init`) |
| solo rosso | dominio di alimentazione del video, oppure finestra e retroilluminazione |

Lo schermo: il bordo intorno al menu 512×512 è il colore di sfondo del controller video (lo stesso
blu-grigio scuro del menu). Tutto uniforme blu-grigio = pannello acceso ma finestra non
funzionante; nero = collegamento DSI o pannello; niente del tutto = retroilluminazione.

**`bm/bootlog.txt`**: a ogni avvio il kernel scrive sulla SD tutto quello che ha stampato
(versione, CPU, SD, cosa ha fatto il driver dello schermo passo per passo, menu). La SD si legge
prima di accendere lo schermo e il registro si scrive tre volte (prima dello schermo, dopo, e a
`ready`): anche se lo schermo blocca tutto, dal PC si vede fin dove è arrivato. Lo stesso registro
è nel menu, alla voce *Boot log*.

## Cosa c'è (stato)

- Base a 64 bit: avvio da U-Boot (EL2 → EL1), MMU (RAM cache WB, framebuffer non-cacheable,
  periferiche Device), GICv3, timer generico a 1 kHz, eccezioni con registri a schermo, picolibc,
  Lua 5.4. **Provata in QEMU** (`make TARGET=rgb30 test`), anche attraverso U-Boot 2026.01.
- Schermo: VOP2 (video port 1, finestra Esmart0) → MIPI DSI0 → D-PHY Innosilicon → pannello
  ST7703, retroilluminazione PWM4. Valori di Linux (`rockchip_drm_vop2.c`, `dw-mipi-dsi.c`,
  `phy-rockchip-inno-dsidphy.c`, `panel-sitronix-st7703.c`). **Da provare sulla console.**
- Comandi: 18 tasti su GPIO3, levette su SARADC canale 3 con commutatore; pagina *Input test*.
- SD: controller SDMMC0 (DesignWare MSHC) in PIO, 4 bit, 12 MHz; FAT dal codice del Pi.
- PMIC RK817 su I2C0: spegnimento, tensione della batteria, stato di carica.
- Modalità video pronte per la GPU (`src/rgb30/display.h`): ogni framebuffer ha la forma che la
  GPU Mali-G52 vuole per disegnarci (righe allineate a 64 byte, altezza a tessere da 16 pixel,
  pagine su confini di 64 KiB, fino a 3 pagine) nella memoria video e GPU, con il suo indirizzo
  fisico (`fb->bus`). Il controller video ingrandisce l'immagine sul pannello: un gioco può
  disegnare a 720×720 o a 360×360 mostrato ×2 (un quarto dei pixel per la GPU), nitido o
  sfumato; il menu resta 512×512 1:1. Pagina **Display** nel menu: le modalità una dopo l'altra
  con un'immagine di prova (bordi, griglia delle tessere, barre di colore). La GPU stessa (driver
  Mali) non c'è ancora.
- Menu 512×512: giochi `.s16`, strumenti (Input test, System, Bluetooth, WiFi, Display, Boot log, Lua sulla
  seriale, Reboot, Power off).
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

## WiFi: come si usa

In `bm/config.txt` sulla SD (dal PC):

```
wifi_ssid=NomeDellaRete
wifi_psk=password
wifi_boot=1
```

Oppure già nell'immagine: `make TARGET=rgb30 image RGB30_CONFIG=$HOME/rgb30-config.txt` mette quel
file come `bm/config.txt` (tienilo fuori dal repository: contiene la password).

Nel menu, *WiFi*: **A** cerca le reti (elenco con segnale, canale, sicurezza), **X** entra nella
rete di `wifi_ssid`; poi DHCP e l'indirizzo IP sullo schermo, con la password della console di
rete (`python3 tools/bm_net.py <ip>`: i tasti w/a/s/d, Invio, Esc arrivano al menu come dalla
seriale). Con `wifi_boot=1` la console entra nella rete da sola all'avvio (senza, solo dal menu:
finché il WiFi dell'RGB30 è nuovo, all'avvio è spento). Reti supportate: aperte e WPA2-PSK (anche
WPA2/WPA3 miste); non WPA3 sola, WPA1, WEP, enterprise.

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
  (pacchetti, 802.11), `wpa.c` (WPA2), `rtw_sta.c` (le funzioni di `wifi/wifi.h`).
- Codice in comune con il Pi: `gfx/`, `lib/printf.c`, `script/luavm.c` e `lib_bm.c`, `fs/fat.c`,
  `kernel/config.c`, `crumbs.c`, `version.c`, Lua.
- `tests/rgb30/qemu_test.py` — test in QEMU (avvio, EL2 e spostamento, schermo letto dai pixel,
  Lua, menu, `.bm` nascosti, input test, bootlog sulla SD).
- `tests/rgb30/rtw_frame_test.c`, `wpa_test.c` (vettori pubblicati e un handshake calcolato a
  parte da `wpa_vectors.py`), `wifi_sim_test.c` (tutta la stazione su un RTL8821C e due access
  point simulati): `make TARGET=rgb30 test-wifi`.
- `boot/rgb30/` — `extlinux.conf` e `LEGGIMI.txt` della scheda.
- `scripts/fetch-rgb30.sh` — bootloader e firmware; `scripts/mksd.py --start-mib --raw --active`.

## Mappa della memoria (RK3566)

| Da | A | Uso |
|---|---|---|
| 0x00000000 | 0x00200000 | TF-A (BL31): non mappato |
| 0x02000000 | | dove U-Boot carica `kernel8.img` (poi si sposta) |
| 0x10000000 | | kernel (testo, dati, bss, stack 1 MiB, tabelle MMU), poi l'heap |
| 0x3c000000 | 0x40000000 | memoria video e GPU, 64 MiB (non-cacheable): framebuffer, poi ciò che legge e scrive la GPU Mali |
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
