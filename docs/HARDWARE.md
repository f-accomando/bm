# Raspberry Pi Zero W v1.1: hardware resources and how bm uses them

Italian version: [HARDWARE-IT.md](HARDWARE-IT.md).

(The Pi Zero 2 W, with its kernel `kernel7.img`, is in section 8.)

Purpose: to see **how much of the hardware we use** and **how much headroom is left**. The status and
the technical choices with their measurements are also in [`PRESTAZIONI.md`](PRESTAZIONI.md).
Columns:

- **Pi Zero W** — hardware capability (datasheet or measured on our Pi);
- **bm today** — what the current implementation uses;
- **Use** — share of the total resource.

Source legend: *(M)* measured on a real Pi Zero W, *(Q)* measured in QEMU,
*(D)* Raspberry Pi datasheet/documentation.

## 1. Processor

| Resource | Pi Zero W | bm today | Use / headroom |
|---|---|---|---|
| CPU | ARM1176JZF-S (ARMv6), 1 core, 700 MHz at boot → **1000 MHz** *(M)* | 1 core at 1000 MHz, MMU + I/D caches + branch prediction on *(M)* | 1 core of 1 (100% available to us, no OS) |
| FPU | VFPv2, double in hardware *(D)* | used by Lua (double), by the 3D and by the synthesizer; RunFast on | — |
| Cache | L1 16 KiB instructions + 16 KiB data; L2 128 KiB in the VideoCore *(D)* | L1 on | — |
| Integer division | not in hardware (libgcc) *(D)* | — | divisions are expensive, avoid them in hot loops |
| Frame budget | 16.7 ms at 60 fps | 640×360 fill: 2.2 ms; C demo: 2.7 ms of drawing *(M)* | ~84% of the frame free in the C demo |
| Lua | — | ~100 ns per simple operation: fib(25) 83 ms, 1M additions 104 ms *(M)* | ~140 000 Lua operations per frame at 60 fps |

## 2. Memory

| Resource | Pi Zero W | bm today | Use |
|---|---|---|---|
| Total RAM | **512 MiB** LPDDR2, shared CPU/GPU *(D)* | — | — |
| ARM RAM | 448 MiB (with `gpu_mem=64`) *(M)* | kernel ~1.1 MB (with SDK, Sound editor and TLS inside) + stack 1.1 MiB + heap ~447 MiB | all available |
| GPU RAM | 64 MiB *(M)* | framebuffer 640×360×4×2 = 1.8 MiB | 2.8% of the GPU memory |
| Memory for Lua | — | limit **64 MiB** *(M)*; boot.lua uses 84 KiB of it, peak 3.4 MiB | 14% of the ARM RAM as a ceiling |
| Sound bank | — | 2 copies of ~140 KiB (the one playing and the one the new one is read into) | ~0 |
| Cartridge | microSD (GB) | the largest, Titan Clash, 1.7 MB; read entirely into RAM | negligible |

In practice **memory is not a constraint**; the time per frame and the bandwidth to
RAM are (memcpy ~100 MB/s, fill ~430 MB/s *(M)*).

## 3. Video

| Resource | Pi Zero W | bm today | Use |
|---|---|---|---|
| Output | mini-HDMI up to 1920×1080 at 60 Hz; composite (TV pad) *(D)* | HDMI *(M)* | — |
| GPU | VideoCore IV, core 250 MHz *(M)*, OpenGL ES 2.0, hardware scaler (HVS) *(D)*; 3D (V3D): 12 QPUs in 3 slices, filtered textures, 64×64 tiles with 24-bit z on chip *(D)* | the scaler (framebuffer enlarged by the GPU); the cartridges' 3D (M33: our own V3D driver, 811 Mpixel/s, ARM as fallback) | 3D GPU used by the games' 3D |
| Logical resolution | any, scaled by the GPU | console and menu 640×360 (32 bit); **cartridges 640×360, 480×270 or 320×180 RGB565** | 640×360 = 11% of the pixels of 1080p |
| Colours | 32-bit framebuffer (16.7 million) *(M)* | 32 bit for the console; 16-bit RGB565 (65 536 colours) for the cartridges | — |
| Sprites | no hardware limit (software drawing) | 256 sprites 16×16 ≈ 0.9 ms *(M)* | — |
| Refresh rate | 60 Hz (firmware vsync **not available** on Pi Zero, tag not supported *(M)*) | 60 fps from the timer, 0 frames lost *(M)* | — |
| Double buffer | yes (virtual offset) *(M)* | on | — |

## 4. Audio

| Resource | Pi Zero W | bm today | Use |
|---|---|---|---|
| Outputs | **HDMI** audio; no jack; 2 PWM channels on GPIO (needs an RC filter) *(D)* | **HDMI** 48 kHz, mono on both channels: MAI FIFO with IEC 958 samples from a DMA channel *(M)* | 1 DMA channel |
| Synthesis | software (CPU) + DMA for the output | 8 voices: square (duty), triangle, sawtooth, sine, two noises; ADSR; master volume; soft limiter | the cost per block is shown by the `a` command |
| Sequencer | — | the cartridge's bank (AUDIO section): sound effects, 8-track patterns, songs, effects on steps; every 64 samples (1.3 ms) in the audio interrupt | — |

## 5. Input and peripherals

| Resource | Pi Zero W | bm today | Use |
|---|---|---|---|
| USB | 1 × micro-USB OTG (USB 2.0, DWC controller) *(D)* | DWC2 host: **1 HID device** (keyboard or gamepad) in use plus a **mouse** (M32), also behind a hub *(M7b, M29)* | 1 port |
| Bluetooth | BT 4.1 / BLE (BCM43438) *(D)* | up to **4 DualShock 4** (M12, M16), a BLE keyboard (M28) and a BLE or classic mouse (M32) *(M)* | — |
| Wi-Fi | 802.11 b/g/n 2.4 GHz (BCM43438) *(D)* | WiFi, network console, sending kernels and cartridges, HTTPS (M18, M19) *(M)* | — |
| GPIO | 40-pin header (28 GPIO, to be soldered on the Zero W) *(D)* | GPIO14/15 UART, GPIO47 LED | 2 of 28 |
| UART | PL011 + mini UART *(D)* | console on the serial port (on the mini UART when the PL011 goes to Bluetooth) | — |
| Timer | system timer 1 MHz, 4 comparators (2 free for the ARM) *(D)* | comparator 1 at 1 kHz *(M)* | 1 of 2 |
| DMA | 16 channels *(D)* | HDMI audio; frame copies when enabled | 1–2 |
| Camera | CSI connector *(D)* | not used | 0 |
| Storage | microSD *(D)* | FAT16/32 read and write (SDHOST): cartridges, saves, settings, sound packs; a file is rewritten without losing it if power fails | — |

## 6. What the numbers tell us

- **Memory**: huge headroom.
- **Time per frame**: this is the real resource. Drawing in C leaves over 80% of the frame;
  music and sound effects run in the audio interrupt and do not touch it.
- **Memory bandwidth**: the bottleneck for graphics and copies (slow memcpy, DMA
  helps).
- **Not used**: 3D GPU, camera, PWM audio (it would need the RC filter).

## 7. Raspberry Pi 1 B / B+ (M29)

Same SoC (BCM2835), same kernel; the differences that matter for bm *(D)*:

| Resource | Pi 1 B (rev 2.0) / B+ | Pi Zero W | bm |
|---|---|---|---|
| CPU | ARM1176 at **700 MHz** | 1 GHz | clock at the maximum the firmware allows |
| RAM | 512 MiB (B rev 1.0: 256) | 512 MiB | read from the firmware |
| USB | 2 ports (B+: 4) behind the hub of the **LAN9512** (B+: LAN9514), high speed | 1 OTG | hub + split transactions, 1 HID device in use |
| Network | **Ethernet 10/100** (LAN951x, USB 0424:ec00, hub port 1) | WiFi (BCM43438) | `smsc95xx.c` driver, lwIP as with WiFi |
| Wi-Fi / Bluetooth | absent | BCM43438 | `W` and `T` say they are not there |
| ACT LED | GPIO 16 active low (B+: GPIO 47 active high) | GPIO 47 active low | chosen from the revision code |
| Video | full-size HDMI (+ composite) | mini-HDMI | same |
| SD | SD (B) / microSD (B+) | microSD | same (SDHOST) |

## 8. Raspberry Pi Zero 2 W (M31)

Another SoC, the **BCM2710A1** (in the RP3A0 module, like the Pi 3), and therefore another
kernel: **`kernel7.img`**, the same sources compiled for 32-bit ARMv7
(`-DBM_ZERO2`). Both fit on the same SD: `config.txt` boots
`kernel7.img` on the Zero 2 W (`[pi02]`) and `kernel.img` on the other boards. Differences
that matter for bm *(D)*:

| Resource | Pi Zero 2 W | Pi Zero W | bm (`kernel7.img`) |
|---|---|---|---|
| CPU | **4 × Cortex-A53** (ARMv8) at 1 GHz, started by the firmware in HYP mode | 1 × ARM1176 (ARMv6) at 1 GHz | 32 bit (ARMv7: integer division in hardware, VFPv4 with 32 registers, NEON); switches from HYP to SVC; **1 core**, the other 3 stay parked in the firmware stub |
| Cache | L1 32 KiB + 32 KiB per core, **L2 512 KiB**, 64-byte lines | L1 16 + 16 KiB, 32-byte lines | ARMv7 operations (by address up to the point of coherency; "whole cache" by set/way, L1 and L2) |
| Peripherals | at **0x3F000000** (+ the ARM ones at 0x40000000) | at 0x20000000 | same drivers (UART, GPIO, timer, DMA, SDHOST, USB DWC2, HDMI, mailbox) |
| Addresses for GPU and DMA | uncached alias 0xC0000000 | L2 alias 0x40000000 | `ARM_TO_BUS` in `src/drivers/mmio.h` |
| RAM | 512 MiB LPDDR2 | 512 MiB | same |
| ACT LED | **GPIO 29** active low | GPIO 47 active low | on the Zero 2 W GPIO 47 is the power supply's I2C: never touched by `kernel7.img` |
| Wi-Fi | **CYW43436** (reports chip 43430; rev 2+ = 43436, rev 1 = 43436s) on the same pins (SDIO GPIO 34–39, WL_ON GPIO 41) | BCM43438 | firmware `brcmfmac43436-sdio.*` or `brcmfmac43436s-sdio.*` in `bm/` |
| Bluetooth | same chip, UART GPIO 30–33, **BT_ON GPIO 42** | BT_ON GPIO 45 | patch `SYN43430B0.hcd` or `SYN43430A1.hcd` in `bm/`, chosen from the LMP subversion |
| USB, video, audio, SD | like the Zero W (micro-USB OTG, mini-HDMI, HDMI audio, microSD) | | same |

- Boot: the firmware loads `kernel7.img` at 0x8000 in HYP mode; `start.S` disables the
  traps to HYP, switches to SVC and puts the exception vectors where they are (VBAR),
  because at 0x0 there is the stub where the other three cores wait. MMU and caches start before
  everything else: without the MMU the Cortex-A53 sees RAM as "device" memory, where the
  unaligned accesses that ARMv7 code makes freely are exceptions.
- The first line on screen gives the board and the SoC (`Raspberry Pi Zero 2 W (BCM2710A1,
  revision 902120)`), the second the processor and the boot mode (`Cortex-A53 from HYP`).
- QEMU does not have the Zero 2 W: `kernel7.img` is tested in `raspi2b` (Pi 2 B: the same
  peripherals as the BCM2710, a Cortex-A7, no radio), `tests/qemu_test.py --kernel7`.

Update this document when the measurements or the implementation change.
