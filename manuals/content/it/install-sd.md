title: Installare bm su una scheda SD
accent: scheda SD
lang: it
kicker: Una guida bm
subtitle: Metti bm su una microSD per il Raspberry Pi Zero W o per il PowKiddy RGB30, avvialo, aggiornalo e riempilo di giochi.
footer: manuali bm - install-sd

::: readme
**Cos'è.** Il percorso da una microSD vuota al menu di bm su un Pi Zero W o su un PowKiddy RGB30, e come tenere aggiornata la scheda.

**Come si usa.** Scegli la tua console (capitolo 2 o 3) e segui i passi numerati nell'ordine. Tutto ciò che sta in un riquadro con bordo e ombra va copiato così com'è.

**Cosa manca.** Un manuale segna ogni lacuna con un'etichetta evidenziata DA COMPLETARE; nulla è inventato.
:::

# Cosa ti serve

bm non ha un installatore da scaricare e avviare con un doppio clic: la scheda si costruisce dal repository di bm sul tuo PC, poi si scrive sulla microSD.

| Ti serve | Per |
|---|---|
| Un PC con Ubuntu o Debian, oppure Windows con WSL/Ubuntu | costruire i file della scheda |
| Una scheda microSD e un lettore | il disco della console |
| Raspberry Pi Zero W, un cavo HDMI e uno schermo | il Pi |
| Una tastiera o un gamepad USB e un adattatore OTG | i comandi del Pi |
| Un PowKiddy RGB30 e una microSD **di scorta** | l'RGB30 |
| Un programma che scrive immagini disco: Raspberry Pi Imager, balenaEtcher (per l'RGB30 anche Rufus), oppure `dd` | scrivere l'immagine |

## Da sapere

- Il Pi Zero W parte solo da una scheda FAT. bm usa FAT32.
- Il Pi e l'RGB30 usano schede **diverse**: non mettere mai la scheda di una scheda madre nell'altra.
- L'RGB30 tiene il suo sistema (ROCKNIX) sull'altra scheda. bm va su una scheda di scorta, così puoi tornare indietro rimettendo la vecchia scheda.
- I giochi `.bm` sono per il Pi; l'RGB30 gioca i `.b16` e, per prova, i `.bm` del Pi.

## Prepara il PC

1. Apri un terminale sul PC (su Windows: il terminale Ubuntu di WSL).
2. Installa gli strumenti. Dovresti vedere `apt` finire senza errori.
3. Scarica il repository. Dovresti vedere una nuova cartella `bm`.
4. Entraci e prendi il ramo principale (questo passo serve per compilare dai sorgenti). Dovresti vedere `bm-core` come risposta di `git branch`.

::: copy
sudo apt-get install -y git curl make python3 python3-numpy python3-pil dosfstools mtools \
  gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi
git clone https://github.com/f-accomando/bm
cd bm
git checkout bm-core
git branch
:::

Per l'RGB30 serve anche il suo compilatore:

::: copy
sudo apt install gcc-aarch64-linux-gnu picolibc-aarch64-linux-gnu mtools dosfstools python3
:::

[DA COMPLETARE: un'immagine già pronta da scaricare senza costruirla; il repository non dice dove ne sia pubblicata una]

[DA COMPLETARE: gli stessi passi su Windows senza WSL; il repository documenta solo WSL/Ubuntu]

# Installare sul Pi Zero W

Si crea una sola immagine disco, `dist/bm.img` (64 MiB), e la si scrive sulla scheda. L'immagine ha una partizione FAT32 con etichetta `BM` che contiene i file di avvio del Pi, `config.txt`, `kernel.img`, i giochi in `carts/` e `bm/`.

## Passo per passo

1. Nel terminale, nella cartella `bm`, scarica i file di avvio chiusi del Pi e il firmware WiFi e Bluetooth. Dovresti vedere una riga `fetching ...` per ogni file, e la cartella `firmware/` con dentro `bootcode.bin`, `start.elf` e `fixup.dat`.
2. Costruisci l'immagine. Alla fine dovresti vedere il file `dist/bm.img`.
3. Metti la microSD nel lettore del PC. Tutto ciò che contiene verrà cancellato dai passi successivi.
4. Apri Raspberry Pi Imager, scegli **Use custom** e seleziona `dist/bm.img`. balenaEtcher o `dd` fanno lo stesso.
5. Seleziona la scheda e scrivi. Quando finisce, il programma dice che è fatto.
6. Togli la scheda dal PC (prima espellila in Windows).
7. Collega il cavo HDMI al Pi e allo schermo, e la tastiera o il gamepad alla porta USB del Pi con l'adattatore OTG.
8. Metti la scheda nel Pi.
9. Accendi il Pi. Dovresti vedere sullo schermo il menu di bm.

![La scheda come la mostra il PC dopo aver scritto l'immagine](shots/install-sd-pi-files.png)

::: copy
make firmware
make image
:::

Per `dd` il comando è quello solito, con il dispositivo della tua scheda al posto di `/dev/sdX`. Controlla due volte il nome: `dd` cancella qualunque disco gli indichi.

::: copy
sudo dd if=dist/bm.img of=/dev/sdX bs=4M conv=fsync status=progress
:::

::: warn
Scrivere un'immagine cancella tutta la scheda. Controlla di aver scelto la scheda e non un disco del PC.
:::

## Cosa dovresti vedere

Lo schermo è pilotato a 1080p60: il kernel disegna 640x360 e la GPU del Pi lo ingrandisce con pixel netti. Il menu ha le schede **Market**, **Games**, **Dev** e **Settings**.

![Il menu di bm sul Pi](shots/install-sd-pi-menu.png)

[DA COMPLETARE: cosa appare tra l'accensione e il menu sul Pi Zero W (splash, quanto dura) e cosa fa il suo LED; non registrato nel repository]

## In alternativa: easy_install.sh

Su WSL c'è uno script che fa lo stesso con un menu. La prima volta chiede la lettera dell'unità della scheda SD in Windows e installa ciò che serve alla build.

1. Nella cartella `bm`, avvia lo script. Dovresti vedere il ramo, la cartella del repository e l'unità della scheda.
2. Rispondi alle domande della prima esecuzione: la lettera dell'unità (per esempio `D`). Non può essere `C`.
3. Metti la scheda nel PC. Scegli **4** (**[SD] disk image**) per costruire l'immagine, formattare la scheda in FAT32 e copiarci i file dell'immagine, oppure **3** (**[SD] full install**) per copiare i file su una scheda già FAT32.
4. Con **4**, scrivi la lettera dell'unità quando richiesto. Windows chiede i diritti di amministratore: rispondi Sì. Se Windows propone di formattare il disco, premi Annulla: lo formatta lo script.
5. Aspetta la riga `You can take the card out of the reader.` e la riga `kernel: ... -> ...`.

::: copy
./easy_install.sh
:::

L'opzione **4** conserva impostazioni, salvataggi e giochi se rispondi di sì quando lo chiede: copia prima `bm/` e `carts/` sul PC e poi li rimette. Esegue `make image`, formatta la scheda in FAT32 (fino a 31 GB) e ci copia i file dell'immagine; non scrive il file immagine in sé. Rifiuta una scheda dell'RGB30.

::: try
Dopo il primo avvio, premi la tastiera o il pad e passa da una all'altra delle quattro schede del menu. Poi spegni, metti la scheda nel PC e guarda le cartelle `bm/` e `carts/`.
:::

# Installare sull'RGB30

Si crea `dist/rgb30/bm-rgb30.img` (256 MiB), con un bootloader nascosto davanti alla partizione, e lo si scrive su una microSD di scorta.

## Passo per passo

1. Nel terminale, nella cartella `bm`, scarica il bootloader e il firmware Bluetooth. I file vengono controllati con il loro SHA-256.
2. Costruisci l'immagine. Dovresti vedere `dist/rgb30/bm-rgb30.img` (e un `.img.gz` di circa 1,1 MB).
3. Metti nel PC la microSD **di scorta**. Tutto ciò che contiene verrà cancellato.
4. Scrivi `dist/rgb30/bm-rgb30.img` con balenaEtcher, Raspberry Pi Imager (**Use custom**) o Rufus. WSL non scrive direttamente sulle schede: usa il programma di Windows.
5. Quando finisce, espelli la scheda in Windows. Windows mostra un'unità chiamata `BM` con `extlinux/`, `kernel8.img`, `LEGGIMI.txt` e `bm/`.
6. Spegni l'RGB30 e metti la scheda nello slot **TF1**, quello da cui si avvia ROCKNIX.
7. Accendi la console con il **tasto di accensione**. Non collegare il caricatore per accenderla: U-Boot si spegne di nuovo.
8. Guarda il LED: rosso acceso vuol dire avvio in corso; verde fisso vuol dire che bm funziona.
9. Dovresti vedere il logo di bm, sentire un breve suono di due note, poi vedere il menu con le schede **Market**, **Games**, **Dev** e **Settings**.

![La scheda dell'RGB30 come la mostra il PC](shots/install-sd-rgb30-files.png)

::: copy
make TARGET=rgb30 firmware
make TARGET=rgb30 image
:::

::: warn
Non formattare e non ripartizionare la scheda dell'RGB30: il suo bootloader sta prima della partizione. Per rifarla, costruisci di nuovo l'immagine e riscrivila.
:::

## I comandi

- **B** conferma e **A** torna indietro. `confirm=a` in `bm/config.txt` li scambia.
- **L1** e **R1** cambiano scheda.
- Start + Select esce da un gioco e torna al menu.

![Il menu di bm sull'RGB30](shots/install-sd-rgb30-menu.png)

## Cosa dicono i LED

| LED | Significato |
|---|---|
| rosso acceso | avvio in corso; se resta così, il kernel si è bloccato presto |
| verde fisso | bm funziona e va tutto bene |
| verde che lampeggia piano | altro: avvio non finito, SD mancante o illeggibile, batteria scarica (sotto 3,45 V, senza caricatore), un kernel in arrivo o un aggiornamento in corso |
| rosso fisso + verde che lampeggia | problemi allo schermo: leggi `bm/bootlog.txt` dal PC |
| rosso che lampeggia N volte, pausa | errore fatale N (1 sincrono, 4 SError, 9 panic) |

[DA COMPLETARE: verificare sulla console che il LED sia verde fisso quando bm funziona; il `LEGGIMI.txt` sulla scheda dice lampeggiante, `docs/RGB30.md` dice fisso]

[DA COMPLETARE: quali dimensioni e marche di microSD sono state provate con il Pi e con l'RGB30]

# Le cartelle sulla scheda

Cosa c'è sulla scheda dipende dalla scheda madre. Il testo di `LEGGIMI.txt` sulla scheda dell'RGB30 è in italiano.

| Percorso | Scheda madre | Cosa contiene |
|---|---|---|
| `bootcode.bin`, `start.elf`, `fixup.dat` | Pi | i file di avvio chiusi del Pi |
| `config.txt` | Pi | le impostazioni del firmware del Pi: quale kernel, il modo HDMI |
| `kernel.img` | Pi | il kernel di bm |
| `kernel7.img` | Pi Zero 2 W | presente solo se l'immagine è stata costruita per lui (`ZERO2=1`) |
| `carts/` | Pi | i giochi, `.bm`; `carts/nano8/` ha le ROM nano8 |
| `kernel8.img` | RGB30 | il kernel di bm |
| `extlinux/` | RGB30 | le istruzioni per U-Boot: non toccare |
| `LEGGIMI.txt` | RGB30 | una nota sulla scheda |
| `bm/` | entrambe | tutto ciò che bm scrive e legge per sé; sull'RGB30 anche i giochi |
| `bm/config.txt` | entrambe | le impostazioni di bm, righe `chiave=valore` |
| `bm/ca.pem` | entrambe | i certificati per la rete |
| `bm/save/` | entrambe | i salvataggi dei giochi (8 slot ciascuno, fino a 32 KiB a slot) |
| `bm/shots/` | entrambe | gli screenshot (`PONG0001.PNG`) |
| `bm/lib/` | entrambe | risorse (`.bmm`, `.bmi`, `.bms`...) che app e giochi possono condividere |
| `bm/backup/` | entrambe | il kernel precedente, conservato da ogni aggiornamento |
| `bm/reports/` | entrambe | rapporti di test in attesa di invio |
| `bm/bench/` | entrambe | rapporti del bench 3D (`3D0001.TXT`) |
| `bm/bootlog.txt`, `bm/bootprev.txt`, `bm/lastrun.txt` | RGB30 | cosa ha scritto il kernel in questo avvio, in quello prima e nell'ultima sessione |
| `docs/` | entrambe | i documenti delle app che li chiedono, come bm Write (`LETTER.BMD`) |

Anche il firmware WiFi e Bluetooth sta in `bm/`: sul Pi i file `BCM43430A1.hcd` e `brcmfmac43430-sdio.*`; sull'RGB30 `rtl8821cs_fw.bin` e `rtl8821cs_config.bin`.

## Da sapere

- I nomi sulla scheda sono 8.3: otto lettere, un punto, tre.
- Una scheda di prima che bm cambiasse nome può avere una cartella `bm33`; `make install` ne sposta il contenuto in `bm/`.
- Impostazioni e salvataggi (`bm/config.txt`, `bm/save/`) non vengono mai toccati da `make install`.

## Guarda la scheda

1. Spegni la console.
2. Togli la scheda e mettila nel PC.
3. Apri l'unità (sull'RGB30 si chiama `BM`). Dovresti vedere i file della tabella qui sopra.
4. Apri `bm/`. Dovresti vedere i file e le cartelle della tabella che appartengono alla tua scheda madre.
5. Espelli la scheda prima di toglierla.

# Aggiornare il kernel

Il kernel è il file `kernel.img` sul Pi e `kernel8.img` sull'RGB30. Aggiornarlo conserva giochi, impostazioni e salvataggi. Il kernel precedente resta in `bm/backup/`.

## Dalla console

Serve il WiFi (vedi il capitolo su `config.txt`).

1. Apri **Settings** e poi **Updates**. La console legge l'ultima release su GitHub e dice se è più nuova e quali file cambiano.
2. Scegli **Install the update**. Scarica e controlla tutto prima di scrivere qualcosa, scrive il kernel per ultimo e riavvia.
3. Dopo il riavvio, apri **Settings > System**. Dovresti vedere la nuova versione.

La release è firmata; la console controlla la firma con una chiave incorporata nel kernel.

## Dal PC, con la scheda nel lettore

1. Metti la scheda nel PC e, su WSL, avvia lo script.
2. Scegli **2** (**[SD] update kernel**). Lo script costruisce il kernel, copia il vecchio in `bm/backup`, scrive il nuovo e lo controlla.
3. Aspetta la riga verde `kernel: <vecchio> -> <nuovo>` e `You can take the card out of the reader.`
4. Rimetti la scheda nella console.

::: copy
./easy_install.sh kernel
:::

Su una scheda che ha `extlinux/` o `kernel8.img` lo script capisce che è quella dell'RGB30 e scrive `kernel8.img`. Scegli **3** (**[SD] full install**) per copiare anche i file di avvio, i giochi e `bm/`.

Senza lo script, da una scheda montata (`SD=` è la cartella dove la scheda è montata, `/mnt/d` di default):

::: copy
make install SD=/mnt/d
make TARGET=rgb30 sdcard SD=/mnt/d
:::

La prima riga è per il Pi, la seconda per l'RGB30. Sull'RGB30 lo script `scripts/copy-sd-rgb30.sh` controlla prima la scheda e dice cosa non va: non montata, in sola lettura, o non è la scheda dell'RGB30.

## Dalla rete

1. Sulla console, collegati al WiFi. La console mostra il suo IP e una Console password di 6 cifre (sul Pi in **Settings > WiFi and network**, sull'RGB30 nella pagina WiFi).
2. Sul PC avvia `./easy_install.sh` e scegli **1** (**[NET] update kernel**).
3. Scegli **n** per una nuova console, poi scrivi un nome, l'IP, le 6 cifre e la scheda madre. Dovresti vedere `bm <versione>` come risposta della console.
4. Lo script costruisce il kernel e lo invia. Dovresti vedere la riga verde `kernel (<nome>): <vecchio> -> <nuovo>`.

## Una versione precedente

Scegli **o** (**older release**) nello script, oppure esegui:

::: copy
./easy_install.sh rollback v0.1.0
:::

Lo script controlla il manifest firmato di quella release e ogni file prima di toccare la scheda. I vecchi kernel vanno in `bm/backup`.

::: warn
Un kernel RGB30 precedente al 2026-10-04 scrive un kernel ricevuto dalla rete come `kernel.img`, che U-Boot non avvia. Metti una volta `kernel8.img` sulla sua scheda, dal PC.
:::

# Il Market e come avere i giochi

Il Market è il catalogo dei giochi gratuiti. Niente account e niente pagamenti.

## Passo per passo, sulla console

1. Collega la console al WiFi.
2. Apri la scheda **Market**: è la prima del menu.
3. Scegli un gioco. Sull'RGB30 lo scarica **B** e **X** apre i dettagli.
4. La console controlla la firma del catalogo e lo SHA-256 di ogni file prima di scrivere qualcosa sulla scheda. Aspetta che il gioco sia scritto.
5. Apri la scheda **Games**. Dovresti vedere il gioco; selezionalo per giocare.

Sul Pi il gioco va in `carts/`. Sull'RGB30 va in `bm/`, la cartella che il suo menu elenca. Il Pi elenca i giochi `.bm` del Market e l'RGB30 quelli `.b16`.

## Dal PC

1. Apri il catalogo su https://f-accomando.github.io/bm-market/ e scarica un gioco.
2. Invialo a una console che è in rete. Dovresti vedere `sent: /carts/<NOME> on <console>` nello script.
3. La console lo scrive dal suo menu e lo mostra in **Games** (se un gioco è aperto, quando lo chiudi).

::: copy
./easy_install.sh send game.bm
python3 tools/bm_net.py <ip> --send game.bm
:::

Oppure copia il file a mano: un `.bm` in `carts/` sulla scheda del Pi, in `bm/` sulla scheda dell'RGB30.

## Da sapere

- I nomi vengono portati a 8.3 sulla scheda (`PONG.BM`): lo script ne propone uno.
- Le risorse (`.bmm`, `.bmi`, `.bms`, `.bmt`, `.bmc`, `.bmk`) vanno di default in `bm/lib`.
- Un gioco del Market gira in una sandbox: scrive solo il proprio salvataggio, e la rete, i rapporti e `docs/` solo dopo che dici di sì.

# Le chiavi di config.txt che contano

Ci sono due file con questo nome. Non confonderli.

| File | Chi lo legge |
|---|---|
| `config.txt` nella radice della scheda del Pi | il firmware del Pi |
| `bm/config.txt` | bm; righe `chiave=valore` |

## config.txt nella radice (Pi)

Il file che il repository fornisce funziona già. Queste sono le righe che potresti voler cambiare:

| Chiave | Cosa fa |
|---|---|
| `kernel=kernel.img` | quale kernel parte (`kernel7.img` per il Pi Zero 2 W) |
| `hdmi_group=1`, `hdmi_mode=16` | sempre 1080p60; per 720p60 usa `hdmi_mode=4` |
| `scaling_kernel=8` | pixel netti; 6 è un ingrandimento morbido |
| `hdmi_force_hotplug=1` | l'HDMI è pilotato anche se non rileva uno schermo |

Se un monitor ultrawide stira l'immagine, imposta la sua opzione di aspetto su "original" o "1:1".

## bm/config.txt

La console lo scrive quando cambi un'impostazione. Puoi anche modificarlo dal PC, o con `./easy_install.sh` (opzione **8** sulla scheda, **7** in rete). Chiavi di al massimo 23 lettere, cifre e `_`, valori di al massimo 127 caratteri.

| Chiave | Cosa fa |
|---|---|
| `wifi_ssid` | la rete WiFi |
| `wifi_psk` | la sua password |
| `wifi_boot` | `0`: niente WiFi all'avvio; `1`: la console si collega a ogni avvio |
| `net_password` | la Console password di 6 cifre, valida dal prossimo avvio |
| `layout` | `it` o `us`: la tastiera |
| `volume` | da 0 a 10 |
| `confirm` | `a`: A conferma (RGB30) |
| `show_bm` | `0`: niente `.bm` nel menu dell'RGB30 |
| `menu_scale` | `3`: il menu del Pi a 1920x1080 |
| `github_token` | un token GitHub per i rapporti di test |

## Imposta il WiFi dal PC

1. Spegni la console, togli la scheda e mettila nel PC.
2. Apri `bm/config.txt` con un editor di testo. Se manca, crealo.
3. Aggiungi le due righe con la tua rete.
4. Salva, espelli la scheda, rimettila e avvia la console. Dovresti vedere la console collegarsi alla rete: l'IP e la Console password compaiono nella pagina WiFi.

::: copy
wifi_ssid=MyNetwork
wifi_psk=mypassword
:::

Reti supportate sull'RGB30: aperte e WPA2-PSK (anche WPA2/WPA3 misto). Non solo WPA3, WPA1, WEP o enterprise. L'RGB30 si collega a 2,4 GHz.

::: warn
`wifi_psk` e `github_token` sono segreti. Non metterli mai in un file che condividi o di cui fai il commit.
:::

Per avere la password già nell'immagine dell'RGB30, tieni il file fuori dal repository e costruisci con quello:

::: copy
make TARGET=rgb30 image RGB30_CONFIG=$HOME/rgb30-config.txt
:::

[DA COMPLETARE: se il WiFi del Pi accetta le stesse reti dell'RGB30; il repository dà l'elenco solo per l'RGB30]

# Risoluzione dei problemi

Solo i problemi che il repository documenta.

| Cosa vedi | Cosa fare |
|---|---|
| `Run 'make firmware' first` | esegui `make firmware` (RGB30: `make TARGET=rgb30 firmware`) e costruisci di nuovo |
| `make firmware did not fetch start.elf` | controlla che il PC sia in rete e rilancia il comando |
| `... is not mounted` o lo script non trova la scheda | in WSL la scheda deve essere montata; vedi sotto |
| `cannot write to ...` | l'interruttore di blocco dell'adattatore SD è abbassato (verso LOCK): alzalo |
| `... is the Raspberry Pi's card, not the RGB30's` | hai scelto l'unità sbagliata: quella dell'RGB30 è la `BM` |
| Windows propone di formattare la scheda | premi Annulla; la formatta lo script |
| L'immagine del Pi è stirata su un monitor ultrawide | imposta l'aspetto del monitor su "original" o "1:1" |
| L'RGB30 si rispegne quando colleghi il caricatore | accendilo con il tasto di accensione |
| Lo schermo dell'RGB30 resta nero | leggi `bm/bootlog.txt` dal PC prima di riaccendere |

## La scheda non è montata in WSL

1. Nell'Esplora file di Windows trova la lettera dell'unità della scheda (per l'RGB30: l'unità chiamata `BM`).
2. Nel terminale, montala. Non dovresti vedere errori.
3. Esegui di nuovo il comando `make`, o lo script.

::: copy
sudo mkdir -p /mnt/d && sudo mount -t drvfs D: /mnt/d -o uid=$(id -u),gid=$(id -g)
:::

Sostituisci `D` e `d` con la lettera della tua unità. Windows deve mostrare l'unità con quella lettera.

## L'RGB30 non mostra l'immagine

1. Spegni la console e togli la scheda.
2. Metti la scheda nel PC e apri `bm/bootlog.txt`. La prima riga è la versione dell'avvio che l'ha scritto; se è quella precedente, bm non è ripartito.
3. Guarda l'ultima riga. Se è `display: starting`, i LED fermi dicono dove si è bloccato: rosso e verde, il clock video; solo verde, il controller video; nessuno, il collegamento DSI o il pannello; solo rosso, il dominio di alimentazione video o la retroilluminazione.
4. Apri `bm/lastrun.txt` per vedere come è finita la sessione prima. Deve finire con `restarting: this run's log in bm/lastrun.txt`.

[DA COMPLETARE: una soluzione per lo schermo nero dell'RGB30 oltre a leggere i log; il repository spiega solo come leggerli]
