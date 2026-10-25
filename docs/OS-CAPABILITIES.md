# What the bm OS can do, and its limits

This file describes bm as an operating system. It compares bm with Linux and Windows.
Read it to learn what a cartridge, a tool or the kernel can and cannot do.

## How to read this file

- Every fact comes from the source code. The `Source` column or the brackets give `file:line`.
- Line numbers are those of `bm-core` at commit `993ae1f7` (2026-10-22). They move when the code changes.
- The lines of section 2 and section 8 about `runtime.c` were checked again on 2026-10-24. Other `runtime.c` lines can be stale.
- Tags:
  - **code**: read in the source. Not run on hardware.
  - **measured**: a number taken from a document of this repository. It was not measured again for this file.
  - **estimate**: computed from the code or guessed. No measurement exists.
- **Nothing here was verified on hardware by the writer of this file.** Read "code" as "the code says so".
- **Proposed** means a pull request that is open and not merged. Open PRs at the time of writing: 80, 82, 83, 93, 94, 100, 101, 104, 106, 107, 108, 112 and others of the 80-112 range (`gh pr list`).
- The two machines are the **Pi Zero W** (BCM2835, ARM1176, the benchmark) and the **RGB30** (RK3566, Cortex-A55, AArch64).
- If the code and a document disagree, the section [Where code and documents disagree](#where-code-and-documents-disagree) lists it.

## 1. Short summary

- bm is a single-purpose bare-metal kernel. It boots to a game menu and runs one cartridge at a time.
- There are no processes, no users, no virtual memory, no preemption and no POSIX layer. [code]
- The kernel runs on one core in privileged mode. A cartridge is Lua 5.4 code in a sandbox that the kernel builds. [code]
- The network is IPv4 and TLS 1.2. HTTPS works for the kernel (Market, updates). A cartridge gets only UDP. [code]
- The 3D chip is driven on the Pi Zero W. On the RGB30 the 3D runs on the CPU. [code]

## 2. Languages and code execution

| Layer | Language | Where it runs | Source |
|---|---|---|---|
| Kernel, drivers, runtime | C11, and ARM assembly for boot, exceptions and fiber switch | on the device | `Makefile:53`, `src/boot/start.S`, `src/arch/fiber.S`, `src/rgb30/start.S` |
| Cartridge logic | Lua 5.4.7 | on the device | `third_party/lua/lua.h:19-27`, `third_party/lua/README.bm` |
| Kernel scripts (boot script, REPL) | Lua 5.4.7, full standard libraries | on the device | `src/script/luavm.c:66`, `third_party/lua/linit.c:43-52` |
| Host tools (Mkbm, Market, converters, tests) | Python 3 | on the PC only | `scripts/`, `tools/` |
| QPU programs (VideoCore IV) | QPU assembly, assembled by `tools/qpuasm.py` into C arrays | built into the Pi kernel | `tools/qpuprogs.py:1-16`, `src/gpu/qpuprog.h:1-8` |
| Data | `.bm`, `.b16`, `BMRES` resource files (sections: Lua, sheet, map, audio, mesh, anim, ...) | on the device | `src/bm/bm.h:154-167` |

- The Lua build has 64-bit integers and double floats. `LUA_32BITS` is 0. [code, `third_party/lua/luaconf.h:125`]
- Two changes to Lua exist: a cheaper count hook and a profiler hook for C functions. [code, `third_party/lua/README.bm`]
- **Libraries in a cartridge**: `base`, `table`, `string`, `math`, `coroutine`, `utf8`, and the bm API. [code, `src/bm/runtime.c:5802-5808`]
- **Libraries not in a cartridge**: `io`, `os`, `package`, `debug`. They are not opened. [code, `src/bm/runtime.c:5802-5808`]
- `dofile`, `loadfile` and `load` are set to nil in a cartridge. [code, `src/bm/runtime.c:5812`]
- `require` is the kernel's own. It loads only the libraries built into the kernel (`assist`, `bm3d`, `bmlib`, `bmnet`, `predict`, `words`, `padtype`, `riff`, `bmui`). [code, `src/bm/require.c:12-33`]
- **Lua heap**: 64 MB for all Lua states together. The allocator refuses growth above it. [code, `src/script/luavm.c:13`, `src/script/luavm.c:28`]
- **Time limit**: a callback that runs more than 20 million Lua instructions in one frame stops with "cart timeout". [code, `src/bm/runtime.c:79-80`, `src/bm/runtime.c:5776`]
- **Garbage collector**: generational. [code, `src/bm/runtime.c:5872`]
- **Stack**: the kernel stack is 1 MB on both machines. The Lua parser and the VM use it. [code, `linker.ld:4`, `src/rgb30/kernel.ld:9`]
- **Native code from a cartridge**: there is no loader, no `dlopen` and no machine-code section in the format. [code, `src/bm/bm.h:154-167`]
  - The QPU programs are built into the kernel. A cartridge cannot supply one. [code]
  - See [the bytecode question](#8-security) in section 8. The runtime loads a cartridge's code as text only. A bytecode chunk is refused.
- The machine code of the kernel is fixed at build time. `kernel.img` is replaced as a whole file.

## 3. Network

### 3.1 Radio and cable

| Item | Pi Zero W | RGB30 | Source |
|---|---|---|---|
| Wi-Fi chip | Broadcom BCM43430 behind SDIO. The Zero 2 W has a CYW43436. | Realtek RTL8821CS behind SDIO. The driver is a port of the parts of rtw88 that a station needs. | `src/wifi/wifi.c:2-4`, `src/rgb30/rtw.h:1-8` |
| Security modes | open, WPA-PSK, WPA2-PSK. WEP gives an error. The chip firmware does the handshake. | WPA2-PSK. The kernel does the 4-way handshake in software. The chip does CCMP. | `src/wifi/wifi.c:962-991`, `src/rgb30/wpa.h:1-8` |
| WPA3, enterprise (802.1X) | not in the code | not in the code | grep of `src/wifi`, `src/rgb30` |
| Band | 2.4 GHz chip | the code sets 2.4 GHz channels only. No 5 GHz path was found. | `src/rgb30/rtw_init.c:287-345` |
| Cable | Only USB devices with an SMSC LAN95xx id (0424:EC00, 9500, 9505, 9E00, 9E01). Other adapters are ignored. | no | `src/usb/smsc95xx.c:79-83`, `src/usb/usb.c:383-386` |
| Bluetooth | BCM43438. Classic HID pads (DualShock 4) and LE keyboard and mouse. Up to 4 pads. | RTL8821CS on UART1 with an H5 link. The same `bt.c` runs on top. | `src/bt/bt.h:1-6`, `src/bt/bt.h:12-14`, `src/rgb30/rk_bt.c:1-8` |

- **Other Pi boards** (`kernel.img` is one build for the BCM2835 boards): [code, `boot/config.txt:3-8`]
  - **Pi Zero 2 W**: a separate build (`kernel7.img`) exists. It is **off by default** (`ZERO2=0`). [code, `Makefile:45-47`]
  - **Pi 1 B and B+**: the board is detected. These boards have no radio. The built-in SMSC95xx Ethernet is used. The MAC address is made up by the kernel. [code, `src/drivers/board.c:95-103`, `src/usb/smsc95xx.c:170-178`]
  - Pi 1 A and A+ have no radio and no Ethernet. The plain Zero has no radio (the Wi-Fi probe fails quietly). [code, `src/drivers/board.c:95-103`]

### 3.2 IP stack

- **lwIP 2.2.0** in `NO_SYS` mode. The main loop polls it. There is no network thread. [code, `third_party/lwip/src/include/lwip/init.h:53-57`, `src/net/lwipopts.h:6`]
- **IPv4 only.** `LWIP_IPV6` is 0. [code, `src/net/lwipopts.h:11`]
- **No BSD sockets** and no netconn API in the kernel. The kernel uses the lwIP raw API. [code, `src/net/lwipopts.h:8-9`]
- **Address**: DHCP only. No static address code was found (`grep netif_set_addr src` is empty). [code, `src/net/lwipopts.h:18`]
- **DNS**: lwIP resolver, 2 servers. [code, `src/net/lwipopts.h:53`]
- **Time**: SNTP from `pool.ntp.org`, one update per hour. TLS waits up to 8 s for it. [code, `src/net/lwipopts.h:47-52`, `src/net/tls.c:177`]
- **Limits**: 8 TCP PCBs, 4 listening, 6 UDP PCBs, window 8 x 1460 bytes. [code, `src/net/lwipopts.h:34-41`]
- ICMP is on (the console answers ping). IGMP is off. There is no routing and no firewall. [code, `src/net/lwipopts.h:14,23`]

### 3.3 TLS and HTTP (kernel only)

| Item | Value | Source |
|---|---|---|
| Library | mbedTLS 3.6.2 | `third_party/mbedtls/include/mbedtls/build_info.h:37` |
| Protocol | TLS 1.2 client only. No TLS 1.3. No server mode. No DTLS. | `src/net/bm_mbedtls.h:66-68` |
| Key exchange | ECDHE-ECDSA and ECDHE-RSA. Curves P-256, P-384, X25519. | `src/net/bm_mbedtls.h:46-48,73-74` |
| Ciphers | AES-GCM and ChaCha20-Poly1305. No CBC mode is enabled. | `src/net/bm_mbedtls.h:32-38` |
| Hashes | SHA-1 (old chains), SHA-224/256/384/512 | `src/net/bm_mbedtls.h:27-31` |
| Certificate check | required. Chain, host name and dates are checked. | `src/net/tls.c:125`, `src/net/tls.c:202` |
| Trust store | `bm/ca.pem` on the SD card. The repository file has 21 root certificates. | `src/net/tls.c:88-100`, `boot/ca.pem` |
| Revocation (CRL, OCSP) | none. CRL parsing is not enabled. | `src/net/bm_mbedtls.h` |
| SNI | yes | `src/net/bm_mbedtls.h:69`, `src/net/tls.c:202` |
| Session resume, tickets | not enabled | `src/net/bm_mbedtls.h` |
| Entropy | the hardware random generator | `src/net/tls.c:29-35` |
| Time to connect | 10 s TCP, 15 s read by default | `src/net/tls.c:192-194` |
| Roots missing | the handshake fails with a message that names the missing root | `src/net/tls.c:217` |

- **HTTP client**: HTTP/1.1, one request per connection, `Connection: close`. No keep-alive. [code, `src/net/http.c:1-4`, `src/net/http.c:179-180`]
- The client follows 301, 302, 303, 307 and 308. The limit is 5 redirects. [code, `src/net/http.c:166`, `src/net/http.c:226-228`]
- It reads bodies with `Content-Length`, with chunked encoding, or until the server closes. [code, `src/net/http.c:106-110`]
- **No `Range` header support.** No code sends `Range` or handles status 206. [code, `grep Range src` is empty]
- No gzip, no cookies, no HTTP/2. The request sends only `Accept: */*`. [code, `src/net/http.c:179-180`]
- URL limit: 2048 characters. Default timeout 15 s. [code, `src/net/http.c:71`, `src/net/http.c:198`]
- The Market refreshes its catalog after 10 minutes. [code, `src/kernel/market.c:31`]

### 3.4 What a cartridge can use

- A cartridge has **UDP only**. There is no TCP, no HTTP and no raw socket in the Lua API. [code, `src/bm/runtime.c:4824-4825`]
  - `udp_open`, `udp_send`, `udp_recv`, `udp_close`, `net_ip`, `net_resolve`.
  - 2 sockets. 48 packets queued per socket. 1024 bytes per packet. [code, `src/net/cartnet.h:13-15`]
  - Address `"*"` is the LAN broadcast. [code, `src/bm/runtime.c:3322`]
- The player must allow the network for each game. The answer is stored in `bm/config.txt`. [code, `src/bm/runtime.c:3243`, `src/bm/runtime.c:3297`, `src/bm/runtime.c:7951-7961`]
- The library `bmnet` builds matches on this UDP. It works on the LAN or through a relay on the internet. The traffic is not encrypted or authenticated. [code, `src/script/bmnet.lua:5`, `src/script/bmnet.lua:114`]
- Internet access for tools: `picture3d` downloads an image from a URL. It works only in tool mode. [code, `src/bm/runtime.c:6418`]
- A game can send a report to GitHub with the console's token if the player allows it. [code, `src/bm/runtime.c:3243-3255`]

### 3.5 Servers on the device

| Port | Service | Protection | Source |
|---|---|---|---|
| TCP 3333 | network console (text console and keys) | one password, 3 tries | `src/net/netcon.h:10`, `src/net/netcon.c:84-105` |
| TCP 3334 | file transfer (`S` save, `P` play, `K` kernel, `C` config, `T` screenshot, `G` get file) | the same password. No encryption. No signature on the file. | `src/net/netxfer.h:23`, `src/net/netxfer.c:148`, `src/net/netxfer.c:162` |
| UDP and TCP (LAN) | console to console: hello, cartridge offer | the player answers a prompt first | `src/net/lan.c:1-6`, `src/net/lan.c:22-24` |

- Both servers start after the network comes up. [code, `src/net/net.c:95`]

## 4. Multitasking and concurrency

- **No processes and no threads.** The kernel has one flow of control, plus interrupt handlers. [code, `src/kernel/fiber.h:2-8`]
- **No preemptive scheduler.** Nothing takes the CPU away from a running function, except an interrupt handler. [code]
- **Fibers**: cooperative stacks used by the menu for background work. A fiber runs only inside `fiber_resume`. It gives the CPU back with `fiber_yield`, or when its time slice ends. [code, `src/kernel/fiber.h:31-47`]
  - Network waits (`net_wait_step`) and SD reads (`fat_load_tick`) are the yield points. [code, `src/kernel/fiber.h:6-8`, `src/kernel/fiber.h:42-46`]
  - A guard of 16 words at the bottom of each stack is checked. An overflow stops the kernel with a message. [code, `src/kernel/fiber.c:30-31`, `src/kernel/fiber.c:65-67`]
- **Jobs that run as fibers** (menu only): [code]
  - Market: 128 KB stack. `src/kernel/market.c:30`
  - Market publish: 128 KB. `src/kernel/publish.c:41`
  - Kernel update: 128 KB. `src/kernel/update.c:74`
  - Report sender: 128 KB. `src/kernel/carts.c:312`
  - Transfer write to SD: 64 KB. `src/net/netxfer.c:77`
  - Wi-Fi auto join: 64 KB. `src/net/wifi_auto.c:36`
  - Cover loader, SD map scan: 16 KB each. `src/kernel/carts.c:278`, `src/kernel/sdmap.c:87`
- **Network polling** runs from the input loops (`net_poll` in `src/kernel/input.c`). lwIP runs while the menu, the monitor or a game polls input. [code, `src/kernel/input.c:65-216`]
- **A game runs exclusively.** The runtime owns the main loop until the game ends or is suspended. The menu fibers do not run. A transfer waits in memory and is written when the menu returns. [code, `src/bm/runtime.c:9073`, `src/net/netxfer.c:6-12`]
- **Suspend**: one cartridge can stay suspended in memory. A new cartridge closes the old one. [code, `src/bm/runtime.c:9266`, `src/bm/runtime.c:9362`]
- **Interrupts**: a 1 kHz tick and the audio. [code, `src/kernel/main.c:75`, `src/audio/audio.h:1-12`]
  - The audio mixer runs inside the output interrupt, 256 samples (5.3 ms) at a time. It never waits for the game loop.
  - Pi: the HDMI audio FIFO is fed by DMA. RGB30: an I2S FIFO interrupt feeds the codec.
- **Watchdog**: the Pi arms the BCM2835 watchdog for 3 s. [code, `src/kernel/main.c:255`]
- **Frame pacing**: 16 667 us per frame (60 Hz). [code, `src/bm/runtime.c:67`, `src/bm/runtime.c:7304-7309`]

### 4.1 Memory management

- **No virtual memory, no swap, no paging, no protection between components.** [code]
- **Pi MMU**: a flat 1:1 map of 1 MB sections. ARM memory is cached. The GPU part of RAM is uncached. Peripherals are device memory. Other addresses are unmapped. [code, `src/arch/mmu.c:13-16`, `src/arch/mmu.c:41-49`]
  - The first section (address 0) is mapped as ordinary RAM. A null pointer access does not fault on the Pi. [code, `src/arch/mmu.c:41-45`]
- **RGB30 MMU**: a 1:1 map of 2 MB blocks. RAM is cached. Frame buffers are uncached. Peripherals are device memory. The rest is unmapped. [code, `src/rgb30/mmu.c:2-10`]
- **Privilege**: all code runs privileged (SVC on the Pi, EL1 on the RGB30). There is no user mode. [code, `src/boot/start.S:71`, `src/rgb30/start.S:111`]
- **Heap**: a bump allocator (`_sbrk`) feeds newlib `malloc` on the Pi and picolibc on the RGB30. Freed memory is reused by `malloc`. It never returns to the system. [code, `src/lib/syscalls.c:28-60`]
- **Cores**: the Pi Zero W has one core. On the RGB30 one core runs bm. No call to `PSCI CPU_ON` exists. PSCI is used only for reset and power off. [code, `src/rgb30/plat_rk3566.c:56-93`, `grep 0xc4000003 src` is empty]
  - On the ARMv7 build (Zero 2 W) every core except core 0 parks at start. [code, `src/boot/start.S:44-50`]
- **Multicore, preemption and background jobs during games**: proposed. See PRs 100, 106 and 108. [not merged]

## 5. Screen

| Item | Pi Zero W | RGB30 | Source |
|---|---|---|---|
| Output | HDMI | MIPI DSI, panel Sitronix ST7703 | `boot/config.txt:21-26`, `src/rgb30/rk_dsi.c:1-8` |
| Menu and console framebuffer | 640 x 360, 32-bit, 2 pages | 360 x 360 | `src/kernel/main.c:62-63,223`, `src/rgb30/main.c:55-56` |
| Panel or HDMI mode | 1080p60 forced. The GPU scales the framebuffer. Nearest neighbour (`scaling_kernel=8`). | 720 x 720 at 59.97 Hz | `boot/config.txt:22,28-29`, `src/rgb30/plat.h:56-57`, `src/rgb30/rk_display.c:5-6` |
| Cartridge pixel format | RGB565, 3 pages | RGB565, 3 pages | `src/bm/runtime.c:7215`, `src/rgb30/fb.c:95` |
| Cartridge sizes | 320x180, 256x256, 360x360, 480x270, 640x360, 720x720 | the same sizes | `src/bm/runtime.c:5640-5643` |
| Square carts | boxed in a 16:9 frame (256 in 480x270, 360 in 640x360, 720 in 1280x720) | shown whole. The display scales 1 to 8 times, or fits the panel. | `src/bm/runtime.c:7207-7215`, `src/rgb30/display.h:6-29` |
| 8-bit palette sheets | yes (`SHEET8`, up to 4096 x 4096) | yes | `src/bm/bm.h:158`, `src/bm/bm.h:180` |
| Vsync | mailbox `WAIT_FOR_VSYNC`, probed at start | VOP2 frame start flag | `src/drivers/fb.c:16,142-160`, `src/rgb30/rk_display.c:298-302` |
| Refresh | 60 Hz (mode 16) | 59.97 Hz | `boot/config.txt:28-29`, `src/rgb30/rk_display.c:6` |

- A cartridge picks one of the sizes above in its header, or calls `screen()` to change it. [code, `src/bm/runtime.c:3102`, `src/bm/runtime.c:5640-5643`]
- `bm_scale=int` and `bm_smooth=1` in `bm/config.txt` set the RGB30 scaling. [code, `src/rgb30/fb.c:85-95`]
- The Pi menu uses 32-bit colour. Games use RGB565. [code, `src/kernel/main.c:223`, `src/bm/runtime.c:7215`]

### 5.1 Screenshots

- A game calls `shot([prefix])`, or the player presses a key, or the PC sends `T` on port 3334. [code, `src/bm/runtime.c:3576`, `src/net/netxfer.c:15-17`]
- The frame is saved **before the overlays** as a PNG in `bm/shots/<NAME><NNNN>.PNG`. The name has up to 4 letters from the title and a number 0000-9999. [code, `src/bm/runtime.c:3543`, `src/bm/runtime.c:7315-7330`, `src/bm/shot.c:19-35`]
- Format: PNG, 8-bit RGB, filter 0. The PNG encoder is the kernel's own. It makes one fixed-Huffman block. Width and height are 1 to 4096. [code, `src/bm/shot.c:171-190`, `src/bm/shot.h:1-6`]
- The size on the SD card is the game's own size, not the scaled size. [code, `src/bm/runtime.c:7323`]

### 5.2 Video recording

- The kernel has a `.bmv` reader (`src/bm/vid.c`, `src/bm/runtime.c` section `video clips (.bmv)`) and a recorder (`src/bm/rec.c`, key `rec=on`). A clip is read whole into memory, 48 MB at most. [code, `src/bm/runtime.c:5520-5526`]
- Screenshots are the other capture feature. The PC can fetch a PNG with `G` over the network. [code, `src/net/netxfer.c:17-18`]
- The speed of clips and of the recorder is **not verified on hardware**.

## 6. Storage

| Item | Value | Source |
|---|---|---|
| File systems | FAT16 and FAT32. FAT12 is refused. No exFAT, no NTFS, no ext4. | `src/fs/fat.c:2`, `src/fs/fat.c:108` |
| Partition table | MBR. The first partition of type 04, 06, 0E, 0B or 0C. Or a card with no partition table. No GPT. | `src/fs/fat.c:62-90` |
| Volumes | one volume. No mount points. | `src/fs/fat.h:2-4` |
| Long names | read. Written files and directories get 8.3 names. | `src/fs/fat.h:3-5`, `src/fs/fat.c:3` |
| Write path | whole files are written. An existing file keeps its name. The write can run in a fiber in slices. | `src/fs/fat.h:5-7` |
| File dates | the SNTP time (UTC) when the network has set it. Else fixed at 2026-01-01. The kernel registers the clock with `fat_set_clock(net_time)` after the card is mounted. | `src/fs/fat.c` (`fat_stamp`), `src/kernel/carts.c` (`carts_init`), `src/rgb30/main.c` |
| Reading a file | the whole file goes into a `malloc` buffer. | `src/fs/fat.c:409` |
| File size | 32-bit size field in FAT. A transfer is limited to 100 MB. A Market file is limited to 100 MB. | `src/net/netxfer.c:44`, `src/net/catalog.h:19` |
| `.b16` size | no limit. The 8 MB build check is removed. | `docs/BM-FORMAT.md` (File size) |
| `.bm` size | no limit in the loader except RAM and the 100 MB transfer limit | `src/net/netxfer.c:44` |
| Nano8 cart | 4 MB | `src/bm/runtime.c:4904` |
| Save | `/bm/save`, 32 KB per slot, up to 8 slots (`BM_SAVE_SLOTS`) | `src/bm/runtime.c:3382-3384`, `src/bm/runtime.h:150` |
| Documents | `/docs`, 4 MB per file, 8.3 names, no folders, needs the player's permission | `src/bm/runtime.c:3711-3714`, `src/bm/runtime.c:3747` |

### 6.1 SD drivers

- **Pi**: the BCM2835 SDHOST controller. Polled PIO. 4-bit bus at 25 MHz. The Arasan EMMC controller is the fallback. [code, `src/drivers/sdhost.c:1-3`, `src/drivers/sd_emmc.c:1-5`]
- **RGB30**: the DesignWare MSHC (SDMMC0). Polled PIO. 4-bit bus. 400 kHz for identification, then 12 MHz. [code, `src/rgb30/rk_sd.c:1-8`]
- No DMA for the SD card on either machine. [code]
- A speed in MB/s is **not recorded in any document** of this repository. The 25 MHz 4-bit bus gives at most 12.5 MB/s. This is an **estimate** (computed from the bus, not measured).

### 6.2 Software distribution (the "package manager")

- **Market**: the kernel downloads a signed catalog, then the files. Each file is checked with SHA-256. [code, `src/net/catalog.c:55-67`, `src/net/catalog.c:294-310`]
- **Updates**: the kernel downloads a signed release manifest from GitHub, then each file. SHA-256 and size are checked. [code, `src/kernel/update.c:24`, `src/kernel/update.c:264-276`, `src/net/release.c:227-230`]
- The Market is a tool for cartridges. It has no dependencies, no versions per package and no uninstall solver. [code, `src/net/catalog.c`]
- A game may write only new `.bm` and `.bme` files in `/carts`. [code, `src/bm/runtime.c:5545-5553`]

## 7. Audio, input, USB, time

### 7.1 Audio

- Synthesizer with 8 voices, plus a bank player and the nano8 channels. Mixed in floating point. 48 kHz. [code, `src/audio/audio.c:1-9`, `src/audio/audio.h:20-23`]
- **Pi output: HDMI audio only.** The Pi Zero W has no analogue output in the code. [code, `src/audio/audio.h:5-8`]
- **RGB30 output**: I2S into the RK817 codec. Speaker and headphone jack. The headphones are mono. [code, `src/rgb30/rk_audio.c:1-12`]
- Volume 0 to 10. Sample depth 16, 24 or 32 bits. [code, `src/audio/audio.h:23`, `src/rgb30/rk_audio.c:9-13`]

### 7.2 Input

| Device | Pi Zero W | RGB30 | Source |
|---|---|---|---|
| USB keyboard, mouse, gamepad | yes: HID class | no. The USB port is not driven. | `src/usb/usb.c:290-319`, `src/rgb30/bm_port.c:185` |
| Xbox 360 wired pad | yes | no | `src/usb/usb.c:315` |
| DualShock 4 | USB and Bluetooth | Bluetooth | `src/usb/usb.c:317`, `src/bt/bt.h:1-6` |
| Bluetooth keyboard and mouse | LE keyboard, LE and classic mouse | the same code | `src/bt/bt.h:22-37` |
| Built-in buttons | none in the code | D-pad, ABXY, L1 R1 L2 R2, Select, Start, stick clicks, volume. All on GPIO3 pins. Two sticks on an analog mux. | `src/rgb30/rk_input.c:1-9`, `src/rgb30/pad.h:1-9` |
| GPIO pads on the Pi | none found (`grep -i "gpio pad" src` is empty) | not applicable | not in the code |
| Players | up to 4 | up to 4 | `src/kernel/input.h:54-63` |

### 7.3 USB host limits (Pi)

- One root port. At most 8 devices. One hub, directly on the root port. [code, `src/usb/usb.c:24,40`, `src/usb/dwc2.h:3`]
- The driver is polled. It has no interrupts. [code, `src/usb/dwc2.h:3`]
- **No hot-plug.** The kernel enumerates the bus at boot. A menu entry "Scan the USB again" repeats it. [code, `src/kernel/main.c:275`, `src/kernel/settings.c:877-883`]
- Classes handled: hub, HID, Xbox 360 vendor class, SMSC95xx Ethernet. **No mass storage, no audio class, no serial.** [code, `src/usb/usb.c:290-319,388`]

### 7.4 Time

- The timer is a free-running 1 MHz counter (32-bit, wraps after about 71.6 minutes). [code, `src/drivers/timer.h:7`; the wrap time is computed]
- The kernel tick is 1000 Hz. [code, `src/kernel/main.c:75`]
- **No battery-backed RTC is used.** The wall clock comes from SNTP after the network is up. Without a network the clock is not set, and TLS refuses to connect. [code, `src/net/tls.c:177-186`]
- Time zones and calendars: none. `net_time` is a UNIX time. [code, `src/net/lwipopts.h:49`]

## 8. Security

- **No user accounts, no file permissions, no capabilities, no secure boot.** All code is trusted kernel code. [code]
- **Boot chain**: the Pi firmware loads `kernel.img` from the SD card. U-Boot loads the RGB30 kernel. The kernel does not check its own signature at boot. [code, `boot/config.txt:3-8`, `src/rgb30/start.S`]
- **Signed items**: [code]
  - The Market catalog: ECDSA P-256 over SHA-256. `src/net/catalog.c:1-3,55-67`
  - The release manifest: ECDSA P-256 over SHA-256. `src/net/release.c:1-3,55-67`
  - The public keys are built into the kernel (`keys/market-pub.pem`, `keys/release-pub.pem`).
  - A second key can be added from the SD card (`bm/market.pem`, `bm/release.pem`). Anyone who can write the card can add a trusted key. `src/net/catalog.c:20-22`, `src/net/release.c:20-22`
- **Not signed**: cartridges installed by hand, files sent over TCP 3334, a kernel sent with `K`. [code, `src/net/netxfer.c` has no signature check]
- **Secrets** live in `bm/config.txt` on the SD card in plain text: the Wi-Fi password (`wifi_psk`), `net_password`, tokens. [code, `src/wifi/wifi.c:1125-1126`, `src/net/netcon.c:178-184`]
  - The kernel masks known secrets in logs and reports. [code, `src/kernel/reports.c:182-189`]
  - The network password is printed on the console at start. [code, `src/net/net.c:96-98`]

### 8.1 Sandbox of a cartridge

A cartridge **can**: [code]
- Draw, play sound, read input, read the clock. Use the 3D API.
- Save its own data in `/bm/save` (32 KB per slot).
- Read and write documents in `/docs` if the player allows it. `src/bm/runtime.c:3747`
- Fetch `https://` pages with `web_get` if the player allows the network. Write files in `/bm/web` with `file_write` (a bare 8.3 name, never `.bm`, `.bme` or `.b16`, at most 128 files). `src/bm/runtime.c` (`l_file_write`), `src/bm/webpolicy.c`
- Write a **model** with `file_write("NAME.bmm", bytes)`: the file goes to `/bmm/NAME.bmm`. The kernel checks the bytes as a BMRES model first. It never replaces a model that is there. It asks the player nothing more. `src/bm/runtime.c` (`file_write_model`)
- Use UDP if the player allows it. `src/bm/runtime.c:3297`
- Send a report to GitHub if the player allows it. `src/bm/runtime.c:3255`
- **List any directory of the SD card** with `ls(dir)`. It needs no permission. `src/bm/runtime.c` (`l_ls`)
- Read the title, author and Lua code of any `.bm` file with `cart_read`. It needs no permission. `src/bm/runtime.c` (`l_cart_read`)
- Create new `.bm` or `.bme` files in `/carts`. It cannot change a file that was there before. `src/bm/runtime.c:5561-5570`
- Quit, which returns to the menu.

A cartridge **cannot**: [code]
- Open arbitrary files. `io`, `os`, `package`, `debug`, `load`, `loadfile` and `dofile` are removed.
- Use TCP, HTTP or a raw socket.
- Run longer than 20 million Lua instructions in one callback. `src/bm/runtime.c:79-80`
- Use more than 64 MB of Lua memory. `src/script/luavm.c:13`

**Reading the SD card (`ls`, `cart_read`): finding kept, behaviour not changed.**
- `ls` and `cart_read` have no `tool_mode` check and no prompt. A game can list the files of the card and read the code, the title and the author of every `.bm` on it. It cannot read other files, and it cannot change a file that was there before. [code, `src/bm/runtime.c` (`l_ls`, `l_cart_read`); `docs/APP-ARCHITECTURE.md` says the same for `cart_read`]
- Why it stays: the editors and the SDK of bm (bm Code, bm Pixel, the Studio) are cartridges. They call `ls` and `cart_read` to open files and to keep several files open (`docs/api/tools.md`). The `.bm` files are public by design: the code of a game is open and the Market lists the same data. The `save` data and the network keys are not in a `.bm`, so `cart_read` does not give them. `bm/config.txt` (Wi-Fi password, `net_password`) is **not** readable by `cart_read`, because `bm_parse` refuses a file that is not a cartridge. `ls` shows the name and size of `config.txt`, not its text.
- Options, not built: (1) a prompt on the first `ls` or `cart_read` of each game, like the prompts for UDP and documents (a small change in the code, but the tools would ask on every start, so they need an allow-list of the bm tools first); (2) a per-cartridge permission flag in the INFO section, set by the author and shown in the menu (needs a format change and a spec revision); (3) limit `ls` to `/carts`, `/docs` and the cartridge's own folder (breaks the file browsers of the tools). Pick (1) with a list of the bm tools if a game that scans the card for private data is ever found.

**The bytecode question (closed).**
- The runtime loads `main.lua` of a cartridge with `lua_load(..., "t")`. Only text chunks load. A chunk that starts with the bytecode signature stops with "attempt to load a binary chunk (mode is 't')". [code, `src/bm/runtime.c` (`bm_run`)]
- The capture of a cartridge's 3D data (`bm_meshcap`) loads the code with mode `"t"` too. [code, `src/bm/meshcap.c`]
- The library loader, the save loader and the riff loader use mode `"t"`. [code, `src/bm/require.c:50`, `src/bm/require.c:76`, `src/bm/runtime.c:3862`]
- The kernel scripts (`luavm_run`, the REPL) and the embedded library loads in `meshcap.c` use mode `"t"` too. The embedded libraries are `.lua` text files (`src/rgb30/bm_embed.S`). [code, `src/script/luavm.c:89`, `src/script/repl.c:132,138`, `src/bm/meshcap.c:336,533`]
- Cartridges store Lua as source text (the token count in `lua_tokens` reads text). No tool in the repository writes bytecode into a cartridge, so the restriction removes no valid cartridge. [code, `scripts/mkbm.py`]
- Why: Lua does not verify precompiled chunks. A crafted chunk can corrupt memory, and the kernel has no memory protection.
- Test: `make test-luavm-text` runs `luavm_run` on a binary chunk and on a text chunk. The chunk is refused and the text runs. The test fails on the old `luaL_loadbuffer` call.
- Test: `make test-luatext` packs a cartridge whose code starts with the bytecode signature. The runtime must refuse it. The test fails on the old `NULL` mode (the chunk is read as bytecode and fails with "bad binary format" instead).
- Not covered: the `string.dump` function is still present in the cartridge sandbox. It only makes bytecode. Nothing in the sandbox can load it, because `load`, `loadfile` and `dofile` are removed.

**The file-transfer service (TCP 3334).**
- The password is 6 digits by default. It comes from the hardware random generator (`rng_read`, `src/drivers/rng.c` on the Pi, `src/rgb30/rng.c` on the RGB30). If the generator gives nothing, it comes from a hash of the timer, a counter and a stack address. The old source was `rand()` seeded with `timer_ticks()`, which gave about the same password after each boot. [code, `src/net/netcon.c` (`netcon_make_password`)]
- The length stays at 6 digits, so that the user types it easily. A user can set any text in `net_password` of `config.txt`.
- The check is `memcmp`. The traffic is clear text. There is no rate limit for the transfer port. [code, `src/net/netxfer.c:162`]
- The `K` operation writes a new kernel. The `C` operation changes `config.txt`. [code, `src/net/netxfer.c:1-5`]
- A device on the same network that guesses the password controls the console. This is **from code, not tested**. Six digits are 1 000 000 values. With no rate limit on the transfer port, a guess loop on the LAN is possible.
- **Accepted risk: the plaintext protocol stays.** The tools `tools/bm_net.py` and the bridge send the password and the file bytes in the clear, and the protocol is part of their contract. TLS server mode is not built into the kernel (the TLS library is client only, `src/net/bm_mbedtls.h`). A change would need a new server stack and new versions of every tool. The expected network is a private home LAN. Do not open ports 3333 and 3334 to the internet. A user on a shared network can set a long `net_password`, or keep the Wi-Fi off.
- Options that were not built: a rate limit per address on port 3334 (small), a challenge and response with a hash of the password (changes the tools), TLS with a pre-shared key (large).

## 9. Graphics and 3D

| Item | Pi Zero W | RGB30 | Source |
|---|---|---|---|
| 3D hardware | VideoCore IV V3D, driven by the kernel (binner, tile renderer, TMU) | Mali-G52 (Bifrost). Probe only. | `src/gpu/v3d.c`, `src/rgb30/mali.h:1-12` |
| 3D in cartridges | V3D by default. The ARM rasterizer when the V3D is missing or fails. | **ARM software rasterizer only.** `gpu3d_init` returns -1. | `src/gpu/gpu3d.h:14-17`, `src/rgb30/bm_port.c:26-27` |
| GPU shaders | NV shader state, 17 fragment shaders and 7 vertex shader kinds | none for games | `src/gpu/gpu3d.c:60-70` |
| MSAA | 4x, optional | no | `src/gpu/gpu3d.c:14` |
| Depth | 24-bit | software z-buffer | `src/gpu/gpu3d.h:6-11` |

- The ARM transforms, lights, culls and clips. The V3D rasterizes. [code, `src/gpu/gpu3d.h:3-9`]
- **Vertices of a mesh**: 65535. **Faces**: 65535. **Bones**: 64. **Clips**: 255. **Models in a file**: 256. [code, `src/bm/bm.h:186-191`, `src/bm/r3d.c:1058`]
- **One GPU batch**: 65 532 vertices. [code, `src/gpu/gpu3d.c:40`]
- **GPU job memory**: 4 MB for vertices. A vertex takes 24 bytes (3 varyings) or 44 bytes (8 varyings). That is about 95 000 to 175 000 vertices per job. [code, `src/gpu/gpu3d.c:23,52-55`; the count is computed, an **estimate**]
- **Pixel and triangle rate**: 811 Mpixel/s and 3 M triangles/s on the Pi. 182 spheres at 60 fps. [measured: `docs/PERFORMANCE.md:21,161`; not run again]
- **QPU**: a driver (`v3d_qpu_run`), an assembler and two programs (particles, fade) exist. [code, `src/gpu/v3d.c:395`, `src/gpu/qpu.c:15-20`]
  - **No function of the kernel or the runtime calls them.** Only a host test uses `qpu.h`. [code, `grep qpu_ src` shows only `src/gpu`]
  - PRs 93 and 94 would wire them in. They are **proposed, not merged**.
- **Mali state**: probe steps only (power, reset, MMU, job manager, a clear, one triangle). It draws nothing for games. [code, `src/rgb30/mali.h:1-26`, `src/rgb30/b3d_rgb30.c:5-8`]
- Triangle throughput on the RGB30 is the Cortex-A55 software rate. **Not verified on hardware.**

## 10. bm compared with a conventional OS

Legend: **measured**, **from code**, **estimate**.

### 10.1 What bm does not have

| Feature | Linux / Windows | bm | Basis |
|---|---|---|---|
| Processes | yes | no | from code |
| Threads and preemption | yes | no. Cooperative fibers in the menu only. | from code (`src/kernel/fiber.h:2-8`) |
| Virtual memory, swap | yes | no. A flat 1:1 map. | from code (`src/arch/mmu.c`) |
| Memory protection | yes | no. One privilege level. | from code |
| User mode and system calls | yes | no. Lua is the only boundary. | from code |
| POSIX, libc files, `fork` | yes | no. Syscalls return `ENOSYS`. | from code (`src/lib/syscalls.c:92-95`) |
| Dynamic loading, `.so` or `.dll` | yes | no. Cartridges are data plus Lua. | from code |
| Hot-plug and a driver model | yes | no. Fixed drivers. USB is scanned at boot. | from code (`src/usb/usb.c`) |
| Multi-user, ACLs | yes | no | from code |
| ext4, NTFS, exFAT | yes | no. FAT16/FAT32 only. | from code (`src/fs/fat.c`) |
| IPv6 | yes | no | from code (`src/net/lwipopts.h:11`) |
| TLS 1.3, HTTP/2, keep-alive | yes | no. TLS 1.2, HTTP/1.1 with close. | from code |
| Certificate revocation | yes | no | from code |
| Containers and namespaces | yes | no. A Lua sandbox only. | from code |
| Multicore | yes | no. One core. | from code (`src/boot/start.S:44-50`) |
| Real-time clock, time zones | yes | no. SNTP only. | from code |
| Disk quotas and journaling | yes | no. A FAT write has no journal. | from code |
| Package manager | apt, winget | Market and signed updates. No dependency solver. | from code |

### 10.2 What bm does better or differently

| Property | bm | Basis |
|---|---|---|
| Determinism | no other process takes CPU time. Interrupts are the tick and the audio only. A game owns the loop. | from code |
| Frame pacing | one fixed target of 16 667 us per frame | from code (`src/bm/runtime.c:67`) |
| Boot time | the README says about 2 seconds. **No measurement exists in the documents.** The USB and the network start-up add fixed waits of 100 ms to 1 s each. Treat the claim as unproven. | **estimate** (`README.md:26`, `src/usb/dwc2.c:93-175`) |
| Small attack surface | no shell, no package scripts, no browser. Two TCP ports, both password-protected. | from code |
| Sandbox that cannot reach files | `io`, `os`, `load` removed | from code (bytecode is refused, see section 8.1) |
| Signed distribution | catalog and releases carry ECDSA signatures | from code |
| License-clean sources | Lua (MIT), mbedTLS, lwIP (BSD), Mesa-derived structures, rtw88 (BSD-3 option) | from code (`third_party/*/LICENSE`, `src/rgb30/rtw.h:1-8`) |
| Image size | a kernel of about 1.1 MB with the tools inside | **measured** (`docs/HARDWARE.md:34`; not run again) |
| Crash handling | the exception screen, a report saved for the next boot, a 3 s watchdog | from code (`src/kernel/exceptions.c`, `src/kernel/main.c:255`) |

## 11. Memory

| Item | Pi Zero W | RGB30 | Source |
|---|---|---|---|
| Total RAM | 512 MB (shared) | 1 GB LPDDR4 | `src/kernel/main.c:134-139`, `src/rgb30/plat.h:14` |
| ARM share | asked from the firmware by mailbox. 448 MB with `gpu_mem=64`. | from the kernel base to `0x3c000000` | `src/kernel/main.c:125-131`, `src/rgb30/plat.h:19-20` |
| GPU or display share | the firmware sets it. `boot/config.txt` does not set `gpu_mem`. | the top 64 MB. The last 4 MB belong to the GPU. | `boot/config.txt`, `src/rgb30/plat.h:19,39` |
| Kernel load address | `0x8000` | `0x10000000` | `linker.ld:12`, `rgb30.mk:20` |
| Main stack | 1 MB (SVC) | 1 MB | `linker.ld:4`, `src/rgb30/kernel.ld:9` |
| Exception stacks | 4 KB each: IRQ, FIQ, abort, undefined | one stack (not checked per mode) | `linker.ld:5-8` |
| Heap | from the kernel end to the end of the ARM share | from the kernel end to `0x3c000000` (about 704 MB before the kernel image size is taken off; computed) | `src/lib/syscalls.c:28-33`, `src/kernel/main.c:194-197`, `src/rgb30/main.c:209` |
| Lua heap | 64 MB | 64 MB | `src/script/luavm.c:13` |

- The Lua limit counts all Lua states together, one counter. [code, `src/script/luavm.c:15,28`]
- A cartridge file is read whole into RAM. The Lua section and the sheets are copied. [code, `src/fs/fat.c:409`]
- Memory is not the bottleneck. The bottleneck is the time per frame and the RAM bandwidth (memcpy about 100 MB/s, fill about 430 MB/s on the Pi). [measured: `docs/HARDWARE.md:41`; not run again]

## Where code and documents disagree

- `docs/progress/b16.md` section 1 said the `.b16` Lua heap is **4 MB**, the save **64 KB** and that there is a sandbox. The code gives every cartridge **64 MB**, saves of **32 KB**, and no sandbox (`src/script/luavm.c:13`, `src/bm/runtime.c:3566`). The section is fixed from the code.
- `README.md:26` and `README.md:35` say the console boots in about 2 seconds. No document records a measurement. The code has fixed waits in USB start-up (`src/usb/dwc2.c:93-175`, `src/usb/usb.c:453,513`). The claim stays an estimate.
- `README.md:34` says "448 MB RAM". The code reads the real split from the firmware (`src/kernel/main.c:125-139`). `boot/config.txt` sets no `gpu_mem`. `docs/HARDWARE.md:34` gives 448 MB with `gpu_mem=64`. The number depends on the firmware default of the card used.
- The 8 MB `.b16` check is removed. `BM_B16_MAX` no longer exists.
- The user-facing description of the RGB30 as "720x720" is the panel size. The menu framebuffer is **360 x 360** (`src/rgb30/main.c:55-56`). Games choose their own size and the display scales it.
- `README.md:40` says "FAT32 SD card file system". The code also handles FAT16 (`src/fs/fat.c:2`).
- `CLAUDE.md` says the `kernel7` target is disabled. The Makefile agrees (`Makefile:45-47`).
- `docs/api/bmnet.md:9` says carts reach the internet "through a relay". This is UDP through a relay program. A cartridge has no other internet path (`src/bm/runtime.c:4824`).

## Unverified items

- All hardware behavior in this file is **not verified on hardware** by the writer.
- The bytecode refusal is tested on the host only (`make test-luatext`, `make test-luavm-text`). It is **not verified on hardware**.
- The password guessing risk in section 8.1 was **not tested**. The new password source (`rng_read`) is tested on the host with a fake generator. It is **not verified on hardware**.
- The file dates from SNTP are tested on the host (`make test-fat`, a fake clock). They are **not verified on hardware**. The dates are UTC. There is no time zone.
- No SD speed, no boot time and no RGB30 3D rate is documented. They are **not verified**.
- Open PRs (80, 82, 83, 93, 94, 100, 101, 104, 106, 107, 108, 112 and others) describe **proposed** features. They are not in `bm-core`.
