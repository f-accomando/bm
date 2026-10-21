title: Installing bm on an SD card
accent: SD card
lang: en
kicker: A bm how-to
subtitle: Put bm on a microSD card for the Raspberry Pi Zero W or the PowKiddy RGB30, start it, update it and fill it with games.
footer: bm manuals - install-sd

::: readme
**What this is.** The way from an empty microSD card to the bm menu on a Pi Zero W or on a PowKiddy RGB30, and how to keep the card up to date.

**How to use it.** Pick your console (chapter 2 or 3) and follow the numbered steps in order. Anything in a bordered box with a shadow is meant to be copied as it is.

**What is missing.** A manual marks every gap with a highlighted TO FILL tag; nothing is invented.
:::

# What you need

bm has no installer you download and double-click: you build the card from the bm repository on your PC, then write it to the microSD.

| You need | For |
|---|---|
| A PC with Ubuntu or Debian, or Windows with WSL/Ubuntu | building the card's files |
| A microSD card and a reader | the console's disk |
| Raspberry Pi Zero W, an HDMI cable and a screen | the Pi |
| A USB keyboard or gamepad and an OTG adapter | the Pi's controls |
| A PowKiddy RGB30 and a **spare** microSD card | the RGB30 |
| A program that writes disk images: Raspberry Pi Imager, balenaEtcher (the RGB30 also Rufus), or `dd` | writing the image |

## Good to know

- The Pi Zero W starts only from a FAT card. bm uses FAT32.
- The Pi and the RGB30 use **different** cards: never put one board's card in the other.
- The RGB30 keeps its own system (ROCKNIX) on its other card. bm goes on a spare card, so you can go back by putting the old card back.
- The `.bm` games are for the Pi; the RGB30 plays `.b16` games and, for testing, the Pi's `.bm`.

## Prepare the PC

1. Open a terminal on the PC (on Windows: the Ubuntu terminal of WSL).
2. Install the tools. You should see `apt` finish with no error.
3. Get the repository. You should see a new folder `bm`.
4. Go into it and take the main branch (this step is for building from source). You should see `bm-core` as the answer of `git branch`.

::: copy
sudo apt-get install -y git curl make python3 python3-numpy python3-pil dosfstools mtools \
  gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi
git clone https://github.com/f-accomando/bm
cd bm
git checkout bm-core
git branch
:::

For the RGB30 you also need its compiler:

::: copy
sudo apt install gcc-aarch64-linux-gnu picolibc-aarch64-linux-gnu mtools dosfstools python3
:::

[TO FILL: a ready-made image to download without building; the repository does not say where one is published]

[TO FILL: the same steps on Windows without WSL; the repository documents WSL/Ubuntu only]

# Installing on the Pi Zero W

You make one disk image, `dist/bm.img` (64 MiB), and write it to the card. The image has one FAT32 partition labelled `BM` with the Pi's boot files, `config.txt`, `kernel.img`, the games in `carts/` and `bm/`.

## Step by step

1. In the terminal, in the `bm` folder, download the Pi's closed boot files and the WiFi and Bluetooth firmware. You should see a line `fetching ...` for each file, and the folder `firmware/` with `bootcode.bin`, `start.elf` and `fixup.dat` in it.
2. Build the image. You should see the file `dist/bm.img` when it ends.
3. Put the microSD card in the PC's reader. Everything on it will be erased by the next steps.
4. Open Raspberry Pi Imager, choose **Use custom** and select `dist/bm.img`. balenaEtcher or `dd` do the same.
5. Select the card and write. When it finishes, the program says it is done.
6. Take the card out of the PC (eject it first in Windows).
7. Plug the HDMI cable into the Pi and the screen, and the keyboard or gamepad into the Pi's USB port with the OTG adapter.
8. Put the card in the Pi.
9. Power the Pi on. You should see the bm menu on the screen.

![The card as the PC shows it after writing the image](shots/install-sd-pi-files.png)

::: copy
make firmware
make image
:::

For `dd` the command is the usual one, with your card's device in place of `/dev/sdX`. Check the device name twice: `dd` erases whatever disk you name.

::: copy
sudo dd if=dist/bm.img of=/dev/sdX bs=4M conv=fsync status=progress
:::

::: warn
Writing an image erases the whole card. Check that you picked the card and not a disk of the PC.
:::

## What you should see

The screen is driven at 1080p60: the kernel draws 640x360 and the Pi's GPU scales it up with sharp pixels. The menu has the tabs **Market**, **Games**, **Dev** and **Settings**.

![The bm menu on the Pi](shots/install-sd-pi-menu.png)

[TO FILL: what appears between power-on and the menu on the Pi Zero W (splash, how long), and what its LED does; not recorded in the repository]

## Alternative: easy_install.sh

On WSL there is a script that does the same with a menu. The first time it asks for the drive letter of the SD card in Windows and installs what the build needs.

1. In the `bm` folder, start the script. You should see the branch, the repository folder and the card's drive.
2. Answer the questions of the first run: the drive letter (for example `D`). It must not be `C`.
3. Put the card in the PC. Choose **4** (**[SD] disk image**) to build the image, format the card FAT32 and copy the image's files onto it, or **3** (**[SD] full install**) to copy the files onto a card that is already FAT32.
4. For **4**, type the drive letter when asked. Windows asks for administrator rights: answer Yes. If Windows offers to format the disk, press Cancel: the script formats it.
5. Wait for the line `You can take the card out of the reader.` and for `kernel: ... -> ...`.

::: copy
./easy_install.sh
:::

Option **4** keeps your settings, saves and games if you say yes when it asks: it copies `bm/` and `carts/` to the PC first and puts them back. It runs `make image`, formats the card FAT32 (up to 31 GB) and copies the image's files onto it; it does not write the image file itself. It refuses an RGB30 card.

::: try
After the first boot, press the keyboard or the pad and move through the four tabs of the menu. Then switch off, put the card in the PC and look at the folders `bm/` and `carts/`.
:::

# Installing on the RGB30

You make `dist/rgb30/bm-rgb30.img` (256 MiB), with a hidden bootloader in front of the partition, and write it to a spare microSD.

## Step by step

1. In the terminal, in the `bm` folder, download the bootloader and the Bluetooth firmware. The files are checked against their SHA-256.
2. Build the image. You should see `dist/rgb30/bm-rgb30.img` (and a `.img.gz` of about 1.1 MB).
3. Put the **spare** microSD card in the PC. Everything on it will be erased.
4. Write `dist/rgb30/bm-rgb30.img` with balenaEtcher, Raspberry Pi Imager (**Use custom**) or Rufus. WSL does not write to cards directly: use the Windows program.
5. When it ends, eject the card in Windows. Windows shows a drive called `BM` with `extlinux/`, `kernel8.img`, `LEGGIMI.txt` and `bm/`.
6. Switch the RGB30 off and put the card in slot **TF1**, the one ROCKNIX boots from.
7. Turn the console on with the **power button**. Do not plug in the charger to turn it on: U-Boot shuts down again.
8. Watch the LED: red on means it is booting; solid green means bm is running.
9. You should see the bm logo, hear a short two-note sound, and then see the menu with the tabs **Market**, **Games**, **Dev** and **Settings**.

![The card of the RGB30 as the PC shows it](shots/install-sd-rgb30-files.png)

::: copy
make TARGET=rgb30 firmware
make TARGET=rgb30 image
:::

::: warn
Do not format or repartition the RGB30's card: its bootloader sits before the partition. To redo the card, build the image again and write it again.
:::

## Controls

- **B** confirms and **A** goes back. `confirm=a` in `bm/config.txt` swaps them.
- **L1** and **R1** change tab.
- Start + Select leaves a game and returns to the menu.

![The bm menu on the RGB30](shots/install-sd-rgb30-menu.png)

## What the LEDs say

| LED | Meaning |
|---|---|
| red on | booting; if it stays so, the kernel hung early |
| solid green | bm is running and nothing is wrong |
| green blinking slowly | something else: boot not finished, the SD missing or unreadable, battery low (below 3.45 V, without charger), a kernel arriving or an update being installed |
| solid red + green blinking | the screen had problems: read `bm/bootlog.txt` on the PC |
| red blinking N times, pause | fatal error N (1 synchronous, 4 SError, 9 panic) |

[TO FILL: check on the console that the LED is solid green when bm runs; the `LEGGIMI.txt` on the card says blinking, `docs/RGB30.md` says solid]

[TO FILL: which microSD sizes and brands were tried with the Pi and with the RGB30]

# The folders on the card

What is on the card depends on the board. The text in `LEGGIMI.txt` on the RGB30's card is in Italian.

| Path | Board | What it holds |
|---|---|---|
| `bootcode.bin`, `start.elf`, `fixup.dat` | Pi | the closed boot files of the Pi |
| `config.txt` | Pi | the Pi firmware's settings: which kernel, the HDMI mode |
| `kernel.img` | Pi | the bm kernel |
| `kernel7.img` | Pi Zero 2 W | present only if the image was built for it (`ZERO2=1`) |
| `carts/` | Pi | the games, `.bm`; `carts/nano8/` has the nano8 ROMs |
| `kernel8.img` | RGB30 | the bm kernel |
| `extlinux/` | RGB30 | the instructions for U-Boot: do not touch |
| `LEGGIMI.txt` | RGB30 | a note about the card |
| `bm/` | both | everything bm writes and reads for itself; on the RGB30 also the games |
| `bm/config.txt` | both | bm's settings, `key=value` lines |
| `bm/ca.pem` | both | the certificates for the network |
| `bm/save/` | both | the save files of the games (8 slots each, up to 32 KiB a slot) |
| `bm/shots/` | both | the screenshots (`PONG0001.PNG`) |
| `bm/lib/` | both | resources (`.bmm`, `.bmi`, `.bms`...) the apps and games can share |
| `bm/backup/` | both | the previous kernel, kept by every update |
| `bm/reports/` | both | test reports waiting to be sent |
| `bm/bench/` | both | reports of the 3D bench (`3D0001.TXT`) |
| `bm/bootlog.txt`, `bm/bootprev.txt`, `bm/lastrun.txt` | RGB30 | what the kernel printed at this boot, at the one before, and in the last session |
| `docs/` | both | documents of the apps that ask for them, such as bm Write (`LETTER.BMD`) |

The WiFi and Bluetooth firmware is in `bm/` too: on the Pi the files `BCM43430A1.hcd` and `brcmfmac43430-sdio.*`; on the RGB30 `rtl8821cs_fw.bin` and `rtl8821cs_config.bin`.

## Good to know

- Names on the card are 8.3: eight letters, a dot, three.
- A card from before bm was renamed may have a folder `bm33`; `make install` moves its content to `bm/`.
- Settings and saves (`bm/config.txt`, `bm/save/`) are never touched by `make install`.

## Look at the card

1. Switch the console off.
2. Take the card out and put it in the PC.
3. Open the drive (on the RGB30 it is called `BM`). You should see the files of the table above.
4. Open `bm/`. You should see the files and folders of the table that belong to your board.
5. Eject the card before taking it out.

# Updating the kernel

The kernel is the file `kernel.img` on the Pi and `kernel8.img` on the RGB30. Updating it keeps your games, settings and saves. The previous kernel stays in `bm/backup/`.

## From the console

Needs WiFi (see the chapter on `config.txt`).

1. Open **Settings** and then **Updates**. The console reads the latest GitHub release and says whether it is newer and which files change.
2. Choose **Install the update**. It downloads and checks everything before writing anything, writes the kernel last and restarts.
3. After the restart, open **Settings > System**. You should see the new version.

The release is signed; the console checks the signature with a key built into the kernel.

## From the PC, with the card in the reader

1. Put the card in the PC and, on WSL, start the script.
2. Choose **2** (**[SD] update kernel**). The script builds the kernel, copies the old one to `bm/backup`, writes the new one and checks it.
3. Wait for the green line `kernel: <old> -> <new>` and for `You can take the card out of the reader.`
4. Put the card back in the console.

::: copy
./easy_install.sh kernel
:::

On a card that has `extlinux/` or `kernel8.img` the script knows it is the RGB30's card and writes `kernel8.img`. Choose **3** (**[SD] full install**) to copy also the boot files, the games and `bm/`.

Without the script, from a mounted card (`SD=` is the folder where the card is mounted, `/mnt/d` by default):

::: copy
make install SD=/mnt/d
make TARGET=rgb30 sdcard SD=/mnt/d
:::

The first line is for the Pi, the second for the RGB30. On the RGB30 the script `scripts/copy-sd-rgb30.sh` checks the card first and says what is wrong: not mounted, read-only, or not the RGB30's card.

## From the network

1. On the console, join the WiFi. The console shows its IP and a 6-digit Console password (on the Pi in **Settings > WiFi and network**, on the RGB30 in the WiFi page).
2. On the PC start `./easy_install.sh` and choose **1** (**[NET] update kernel**).
3. Choose **n** for a new console, then type a name, the IP, the 6 digits and the board. You should see `bm <version>` answered by the console.
4. The script builds the kernel and sends it. You should see the green line `kernel (<name>): <old> -> <new>`.

## An older version

Choose **o** (**older release**) in the script, or run:

::: copy
./easy_install.sh rollback v0.1.0
:::

The script checks the signed manifest of that release and every file before touching the card. The old kernels go to `bm/backup`.

::: warn
An RGB30 kernel from before 2026-10-04 writes a kernel it receives from the network as `kernel.img`, which U-Boot does not start. Put `kernel8.img` on its card once, from the PC.
:::

# The Market and getting games

The Market is the catalogue of free games. There are no accounts and no payments.

## Step by step, on the console

1. Connect the console to WiFi.
2. Open the **Market** tab: it is the first one in the menu.
3. Choose a game. On the RGB30, **B** downloads it and **X** opens its details.
4. The console checks the signature of the catalogue and the SHA-256 of each file before it writes anything to the card. Wait until the game is written.
5. Open the **Games** tab. You should see the game; select it to play.

On the Pi the game goes to `carts/`. On the RGB30 it goes to `bm/`, the folder its menu lists. The Pi lists the `.bm` games of the Market and the RGB30 the `.b16` ones.

## From the PC

1. Open the catalogue at https://f-accomando.github.io/bm-market/ and download a game.
2. Send it to a console that is on the network. You should see `sent: /carts/<NAME> on <console>` in the script.
3. The console writes it from its menu and shows it in **Games** (if a game is open, when you close it).

::: copy
./easy_install.sh send game.bm
python3 tools/bm_net.py <ip> --send game.bm
:::

Or copy the file by hand: a `.bm` into `carts/` on the Pi's card, into `bm/` on the RGB30's card.

## Good to know

- Names are made into 8.3 on the card (`PONG.BM`): the script proposes one.
- Resources (`.bmm`, `.bmi`, `.bms`, `.bmt`, `.bmc`, `.bmk`) go to `bm/lib` by default.
- A Market game runs in a sandbox: it writes only its own save, and the network, reports and `docs/` only after you say yes.

# The config.txt keys that matter

There are two files with this name. Do not mix them up.

| File | Who reads it |
|---|---|
| `config.txt` in the root of the Pi's card | the Pi firmware |
| `bm/config.txt` | bm; lines `key=value` |

## config.txt in the root (Pi)

The file the repository ships already works. These are the lines you may want to change:

| Key | What it does |
|---|---|
| `kernel=kernel.img` | which kernel starts (`kernel7.img` for the Pi Zero 2 W) |
| `hdmi_group=1`, `hdmi_mode=16` | always 1080p60; for 720p60 use `hdmi_mode=4` |
| `scaling_kernel=8` | sharp pixels; 6 is a soft enlargement |
| `hdmi_force_hotplug=1` | HDMI is driven even if no screen is detected |

If an ultrawide monitor stretches the picture, set its aspect option to "original" or "1:1".

## bm/config.txt

The console writes it when you change a setting. You can also edit it on the PC, or with `./easy_install.sh` (option **8** on the card, **7** on the network). Keys of at most 23 letters, digits and `_`, values of at most 127 characters.

| Key | What it does |
|---|---|
| `wifi_ssid` | the WiFi network |
| `wifi_psk` | its password |
| `wifi_boot` | `0`: no WiFi at the start; `1`: the console joins at every start |
| `net_password` | the 6-digit Console password, used from the next start |
| `layout` | `it` or `us`: the keyboard |
| `volume` | 0 to 10 |
| `confirm` | `a`: A confirms (RGB30) |
| `show_bm` | `0`: no `.bm` in the RGB30's Games tab (they are shown by default, for tests) |
| `market_bm` | `1`: the RGB30's Market also lists the `.bm` cartridges (by default it lists only `.b16`) |
| `menu_scale` | `3`: the Pi's menu at 1920x1080 |
| `github_token` | a GitHub token for the test reports |

## Set up WiFi from the PC

1. Switch the console off, take the card out and put it in the PC.
2. Open `bm/config.txt` with a text editor. If it is missing, create it.
3. Add the two lines with your network.
4. Save, eject the card, put it back and start the console. You should see the console join the network: the IP and the Console password appear in the WiFi page.

::: copy
wifi_ssid=MyNetwork
wifi_psk=mypassword
:::

Networks supported on the RGB30: open and WPA2-PSK (also mixed WPA2/WPA3). Not WPA3 only, WPA1, WEP or enterprise. The RGB30 joins at 2.4 GHz.

::: warn
`wifi_psk` and `github_token` are secrets. Never put them in a file you share or commit.
:::

To have the password already in the RGB30's image, keep the file outside the repository and build with it:

::: copy
make TARGET=rgb30 image RGB30_CONFIG=$HOME/rgb30-config.txt
:::

[TO FILL: whether the Pi's WiFi accepts the same networks as the RGB30; the repository gives the list for the RGB30 only]

# Troubleshooting

Only problems the repository documents.

| What you see | What to do |
|---|---|
| `Run 'make firmware' first` | run `make firmware` (RGB30: `make TARGET=rgb30 firmware`) and build again |
| `make firmware did not fetch start.elf` | check that the PC is on the network and run it again |
| `... is not mounted` or the script cannot find the card | in WSL the card must be mounted; see below |
| `cannot write to ...` | the SD adapter's lock switch is down (towards LOCK): slide it up |
| `... is the Raspberry Pi's card, not the RGB30's` | you chose the wrong drive: the RGB30's is the one called `BM` |
| Windows offers to format the card | press Cancel; the script formats it |
| The Pi's picture is stretched on an ultrawide monitor | set the monitor's aspect to "original" or "1:1" |
| The RGB30 turns off again when you plug the charger | turn it on with the power button |
| The RGB30's screen stays black | read `bm/bootlog.txt` on the PC before you power on again |

## The card is not mounted in WSL

1. In Windows' File Explorer find the card's drive letter (for the RGB30: the drive called `BM`).
2. In the terminal, mount it. You should see no error.
3. Run the `make` command again, or the script.

::: copy
sudo mkdir -p /mnt/d && sudo mount -t drvfs D: /mnt/d -o uid=$(id -u),gid=$(id -g)
:::

Replace `D` and `d` with your drive's letter. Windows must show the drive as that letter.

## The RGB30 does not show a picture

1. Switch the console off and take the card out.
2. Put the card in the PC and open `bm/bootlog.txt`. The first line is the version of the boot that wrote it; if it is the previous one, bm did not restart.
3. Look at the last line. If it is `display: starting`, the steady LEDs say where it stopped: red and green, the video clock; green only, the video controller; none, the DSI link or the panel; red only, the video power domain or the backlight.
4. Open `bm/lastrun.txt` to see how the session before ended. It must end with `restarting: this run's log in bm/lastrun.txt`.

[TO FILL: a fix for a black screen on the RGB30 beyond reading the logs; the repository only explains how to read them]
