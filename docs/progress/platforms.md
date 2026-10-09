# Supported Platforms & Hardware Targets

This document outlines the hardware specifications, architectural configurations, peripheral layouts, and development status across all supported platforms in **bm**.

---

## 1. Platform Comparison Matrix

| Platform | SoC | Architecture / CPU | RAM (ARM / Total) | GPU | Primary Display | Audio Output | Binary Target |
|---|---|---|---|---|---|---|---|
| **Raspberry Pi Zero W** | BCM2835 | ARM1176JZF-S (ARMv6) @ 1.0 GHz | 448 MiB / 512 MiB | VideoCore IV | 640×360 / 1080p HDMI | HDMI IEC958 | `kernel.img` |
| **Raspberry Pi 1 (A/B/+)** | BCM2835 | ARM1176JZF-S (ARMv6) @ 1.0 GHz | 448 MiB / 512 MiB | VideoCore IV | 640×360 / 1080p HDMI | HDMI IEC958 | `kernel.img` |
| **Raspberry Pi Zero 2 W** | BCM2710A1| Cortex-A53 (ARMv7 32-bit) @ 1.0 GHz| 448 MiB / 512 MiB | VideoCore IV | 640×360 / 1080p HDMI | HDMI IEC958 | `kernel7.img` |
| **PowKiddy RGB30** | RK3566 | Quad Cortex-A55 (AArch64) @ 1.8 GHz | 1–2 GiB | Mali-G52 | 720×720 DSI IPS | I2S RK817 Codec | `kernel8.img` |
| **PC Host (`bmhost`)** | Native x86_64 | Host CPU execution | Host RAM | Software / Emu | Windowed / Headless | WAV / Virtual Sink | `build/host/bmhost` |
| **QEMU Simulation** | Emulated | `-M raspi0` / `-M virt` | Configured RAM | Software / V3D Emu | Framebuffer Dump | Headless Capture | CI runners |

---

## 2. Raspberry Pi Zero W & Pi 1 (Primary Target)

The primary reference hardware for bm:
* **Processor Core**: Broadcom BCM2835 featuring an ARM1176JZF-S core with 16 KiB instruction cache, 16 KiB data cache, and VFPv2 hardware floating point.
* **Overclocking**: The bootloader queries the mailbox interface to elevate core clock from 700 MHz to **1000 MHz (1 GHz)**.
* **Memory Map**: MMIO registers reside at base address `0x20000000`. Memory split gives 448 MiB to ARM and 64 MiB to GPU.
* **Storage**: Arasan EMMC controller operates in PIO mode over a 4-bit bus clocked at 25 MHz.
* **USB Host**: Single OTG micro-USB port connected to the DWC2 host controller. On the Pi 1 Model B/B+, an SMSC LAN9512/LAN9514 USB hub/Ethernet controller is supported.
* **Wireless**: Broadcom BCM43438 providing 2.4 GHz 802.11n WiFi and Bluetooth 4.1.
* **Boot Image**: Standard single-stage bare-metal binary: [`build/kernel.img`](../../build/kernel.img).

---

## 3. Raspberry Pi Zero 2 W (Secondary Target)

Maintained as a dual-build companion target sharing the same unified codebase:
* **SoC**: Broadcom BCM2710A1 containing four ARM Cortex-A53 cores.
* **Execution Mode**: Compiled in ARMv7 32-bit mode (`-DBM_ZERO2`, `ARCH7` flags in Makefile).
* **Multi-Core Boot Handling**: Boots into HYP mode. Core 0 drops to SVC mode and runs the kernel; secondary cores 1–3 are preserved in firmware parking stubs at physical address `0x00000000`.
* **Hardware Differences**:
  * MMIO peripheral base address shifts to `0x3F000000`.
  * Status ACT LED mapped to **GPIO 29** (GPIO 47 is dedicated to I2C power regulator).
  * Wireless chip updated to Cypress CYW43436.
* **Dual-Boot SD Architecture**: Both `kernel.img` (for Pi Zero W) and `kernel7.img` (for Pi Zero 2 W) live side-by-side on the root partition. The Raspberry Pi firmware automatically launches the matching kernel for the detected board revision.
* **Build Command**: `make ZERO2=1` produces `build/kernel7.img`.

---

## 4. PowKiddy RGB30 (Handheld Target)

A dedicated port adapting bm to a modern portable form factor ([`docs/RGB30.md`](../RGB30.md)):
* **SoC & Toolchain**: Rockchip RK3566 (quad Cortex-A55) compiled with `aarch64-linux-gnu-gcc` and **picolibc**.
* **Display**:
  * 4.0-inch 720×720 square IPS panel driven via Rockchip VOP2 display processor through DSI0 and an ST7703 driver chip.
  * System menu runs at native 360×360 and scales 2× onto the 720×720 screen.
* **Integrated Controls**:
  * Digital D-pad, face buttons (B confirms, A cancels), shoulder buttons (L1/R1, L2/R2) read via GPIO.
  * Dual analog sticks read via SARADC and analog multiplexer.
* **Audio & Power**:
  * Sound driven via RK3566 I2S1 to Rockchip RK817 audio codec and headphone amplifier.
  * Hardware volume buttons (+ / −) with instant onscreen indicators.
  * Battery percentage and charging status read directly from RK817 PMIC registers.
* **Wireless**: Realtek RTL8821CS SDIO WiFi and UART Bluetooth.
* **Boot Flow**: Boots as an uncompressed AArch64 Image via U-Boot extlinux configuration (`kernel8.img`).
* **Build Command**: `make TARGET=rgb30` produces `build/kernel8.img`.

---

## 5. Host Simulation & Automated Testing

To ensure rapid test iteration without physical SD card swapping:

### 1. `bmhost` Native PC Runtime ([`tests/host/`](../../tests/host/))
* Compiles the cartridge runtime, graphics engine, and sound synthesizer natively for Linux / WSL / macOS.
* Runs `.bm` cartridges headlessly or windowed.
* Supports scripted inputs, deterministic snapshot dumps (`--shots`), and video/WAV recording.
* Includes cycle-scaled clock emulation (`--clock-scale 21`) to accurately estimate Pi Zero frame times directly on a PC.

### 2. QEMU Automated CI Testsuite
* **Raspberry Pi Zero W**: Tested using `qemu-system-arm -M raspi0 -kernel build/kernel.elf`.
* **PowKiddy RGB30**: Tested using `qemu-system-aarch64 -M virt -cpu cortex-a55`.
* Runs comprehensive end-to-end regression suites including filesystem reads, menu navigation, cartridge launches, audio synthesis, and framebuffer verification.
