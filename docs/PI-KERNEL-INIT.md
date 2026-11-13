# Raspberry Pi Bare-Metal Kernel: Initialization Hierarchy & Architecture

This document describes the initialization sequence and structural hierarchy of the **`bm`** bare-metal operating environment and fantasy console on Raspberry Pi boards (BCM2835 ARMv6 for Pi Zero / Pi 1 in `kernel.img`, and BCM2710 Cortex-A53 ARMv7 in `kernel7.img` for Pi Zero 2 W).

---

## 1. Overview & Boot Flow

The Pi does not run Linux or an existing general-purpose OS; the GPU firmware (`start.elf`) sets up system clocks and basic hardware according to `core/boot/config.txt`, loads the kernel binary into memory, and jumps directly to `0x8000` with ARM registers prepared (`r0 = 0`, `r1 = machine_type`, `r2 = atags/dtb_ptr`).

The initialization follows a strict chronological order across 11 main phases:

```text
📦 BM Kernel Boot Sequence (Raspberry Pi)
 ┣━━ 1. Bootloader & Assembly Low-Level Entry (0x8000)
 ┃    ┣━━ core/boot/config.txt           (Firmware VideoCore IV boot configuration)
 ┃    ┣━━ core/linker.ld                 (Linker script & memory section layout)
 ┃    ┣━━ core/src/boot/start.S          (Low-level assembly entry: HYP->SVC, stacks, VFP, zero BSS)
 ┃    ┣━━ core/src/kernel/vectors.S      (ARM exception vector table & context stubs)
 ┃    ┗━━ core/src/kernel/exceptions.c   (Unhandled exception & crash panic handler)
 ┃
 ┣━━ 2. Kernel Orchestration
 ┃    ┗━━ core/src/kernel/main.c         (kernel_main entry point: boot pipeline orchestration)
 ┃
 ┣━━ 3. Memory Architecture & VideoCore Mailbox
 ┃    ┣━━ core/src/drivers/mmio.h        (Peripherals base address definitions)
 ┃    ┣━━ core/src/drivers/mbox.c        (VideoCore mailbox channel communication)
 ┃    ┣━━ core/src/drivers/prop.c        (Mailbox property tags: RAM layout, clocks)
 ┃    ┣━━ core/src/lib/syscalls.c        (heap_init, dynamic sbrk memory allocator)
 ┃    ┣━━ core/src/arch/mmu.c            (L1 page table translation & MMU activation)
 ┃    ┗━━ core/src/arch/cache.c          (L1 instruction and data cache control)
 ┃
 ┣━━ 4. Basic I/O, Serial Diagnostics & Display Subsystem
 ┃    ┣━━ core/src/drivers/board.c       (Raspberry Pi hardware board revision query)
 ┃    ┣━━ core/src/drivers/gpio.c        (BCM2835/2710 GPIO register control)
 ┃    ┣━━ core/src/drivers/led.c         (Status ACT LED hardware driver)
 ┃    ┣━━ core/src/kernel/ledstate.c     (LED blink machine: solid during boot, heartbeat / panic blinks)
 ┃    ┣━━ core/src/drivers/uart.c        (PL011 / mini-UART serial console driver)
 ┃    ┣━━ core/src/lib/printf.c          (Autonomous kprintf logging formatter)
 ┃    ┣━━ core/src/drivers/fb.c          (Mailbox 640x360 32-bit double-buffered framebuffer)
 ┃    ┣━━ core/src/gfx/console.c         (Virtual 80x22 ANSI terminal emulator)
 ┃    ┣━━ core/src/gfx/font8x16.c        (Bitmap 8x16 font data for early console)
 ┃    ┗━━ core/src/kernel/splash.c       (Early graphical splash screen & logo)
 ┃
 ┣━━ 5. Interrupts, System Timing & Cooperative Multitasking (Fibers)
 ┃    ┣━━ core/src/kernel/irq.c          (BCM interrupt controller routing & CPSR enable)
 ┃    ┣━━ core/src/drivers/timer.c       (1 MHz free-running microsecond hardware timer)
 ┃    ┣━━ core/src/kernel/tick.c         (1 kHz periodic tick & heartbeat timer)
 ┃    ┗━━ core/src/kernel/fiber.c        (Cooperative lightweight coroutine scheduler & fiber.S switch)
 ┃
 ┣━━ 6. Safety, Watchdog & Direct Memory Access (DMA)
 ┃    ┣━━ core/src/drivers/rng.c         (Hardware True Random Number Generator)
 ┃    ┣━━ core/src/drivers/watchdog.c    (BCM2835 hardware watchdog at 3000 ms anti-freeze)
 ┃    ┣━━ core/src/kernel/crumbs.c       (Crash & reboot breadcrumb diagnostics)
 ┃    ┗━━ core/src/drivers/dma.c         (Hardware DMA channel manager)
 ┃
 ┣━━ 7. Audio & USB / Input Controller Subsystems
 ┃    ┣━━ core/src/audio/audio.c         (Stereo audio engine, boot chime chord generator)
 ┃    ┣━━ core/src/audio/pwm_audio.c     (PWM DMA dual-channel audio driver)
 ┃    ┣━━ core/src/usb/dwc2.c            (Synopsys DWC2 bare-metal USB host controller driver)
 ┃    ┣━━ core/src/usb/usb.c             (USB bus enumeration & transfer management)
 ┃    ┣━━ core/src/usb/hid.c             (USB HID parser for gamepads & keyboards)
 ┃    ┗━━ core/src/kernel/input.c        (Unified input event subsystem & hotkeys)
 ┃
 ┣━━ 8. SD Storage, FAT Filesystem & Configuration
 ┃    ┣━━ core/src/drivers/sd.c          (Raspberry Pi EMMC / SD card controller driver)
 ┃    ┣━━ core/src/fs/fat.c              (FAT16 / FAT32 filesystem driver with LFN support)
 ┃    ┣━━ core/src/kernel/carts.c        (carts_init: SD mount, cart scanner, catalog)
 ┃    ┣━━ core/src/kernel/config.c       (Parsing and applying /bm/config.txt options)
 ┃    ┗━━ core/src/kernel/sdlog.c        (Persistent log file writing to SD)
 ┃
 ┣━━ 9. 3D Graphics Acceleration (VideoCore IV V3D / QPU)
 ┃    ┣━━ core/src/gpu/v3d.c             (V3D hardware register control & clock scaling)
 ┃    ┣━━ core/src/gpu/gpu3d.c           (3D graphics pipeline, binning & draw dispatch)
 ┃    ┗━━ core/src/gpu/qpu.c             (Quad Processor Unit microcode execution & hardware fade)
 ┃
 ┣━━ 10. Networking, Wi-Fi, Ethernet & Bluetooth
 ┃    ┣━━ core/src/wifi/sdio.c           (Broadcom SDIO host bus interface)
 ┃    ┣━━ core/src/wifi/wifi.c           (BCM43438 / CYW43436 Wi-Fi firmware loading & driver)
 ┃    ┣━━ core/src/net/wifi_auto.c       (Automatic Wi-Fi network joining)
 ┃    ┣━━ core/src/net/net.c             (lwIP network stack integration & SNTP time sync)
 ┃    ┣━━ core/src/usb/smsc95xx.c        (SMSC95xx USB Ethernet driver for Pi 1 Model B/B+)
 ┃    ┗━━ core/src/bt/bt.c               (Bluetooth HCI stack over UART for wireless pads)
 ┃
 ┗━━ 11. BM Runtime, Lua VM, Menu & Interactive Monitor Shell
      ┣━━ core/src/script/luavm.c        (Lua 5.4 VM engine for autoexec.lua and cart scripts)
      ┣━━ core/src/bm/runtime.c          (Full BM fantasy console API: 2D/3D graphics, audio, maths)
      ┣━━ core/src/kernel/home.c         (System Home UI, menu switcher, and settings)
      ┗━━ core/src/kernel/monitor.c      (Interactive serial/console monitor & debug CLI)
```

---

## 2. Detailed Component Breakdown & Metrics

The table below lists all 103 source and configuration files participating in the Raspberry Pi bare-metal kernel initialization, organized strictly by their chronological invocation.

| Phase | Subsystem | Source / Header File | Description | Lines | Size (KB) |
| :--- | :--- | :--- | :--- | :---: | :---: |
| **1. Boot & Low-Level** | Firmware Config | `core/boot/config.txt` | VideoCore boot flags (`gpu_mem`, HDMI scaling, UART) | 34 | 1.39 KB |
| | Linker | `core/linker.ld` | Memory section map (`0x8000`, `.text`, stacks, heap) | 52 | 1.43 KB |
| | Assembly Entry | `core/src/boot/start.S` | `_start`: HYP->SVC mode, stack setup, VFP, zero BSS | 139 | 4.62 KB |
| | Trap Vectors | `core/src/kernel/vectors.S` | ARM vector table and register save/restore stubs | 94 | 2.79 KB |
| | Exception Handler | `core/src/kernel/exceptions.c`<br>`core/src/kernel/exceptions.h` | Undefined, abort & panic handlers with console register dump | 157<br>25 | 6.15 KB<br>0.79 KB |
| **2. Kernel Entry** | Core Orchestrator | `core/src/kernel/main.c` | `kernel_main()`: Master sequence orchestrator for all drivers | 290 | 10.33 KB |
| **3. Memory & Mailbox** | Hardware MMIO | `core/src/drivers/mmio.h` | Base addresses for BCM2835 and BCM2710 peripherals | 39 | 1.35 KB |
| | Mailbox | `core/src/drivers/mbox.c`<br>`core/src/drivers/mbox.h` | VideoCore mailbox interface driver | 75<br>25 | 2.84 KB<br>1.30 KB |
| | Property Tags | `core/src/drivers/prop.c`<br>`core/src/drivers/prop.h` | Mailbox tags: RAM boundary queries, clock rates | 52<br>33 | 1.64 KB<br>1.44 KB |
| | Heap / Allocator | `core/src/lib/syscalls.c`<br>`core/src/lib/heap.h` | Newlib system calls, `heap_init`, `_sbrk` heap expansion | 146<br>9 | 3.09 KB<br>0.28 KB |
| | MMU | `core/src/arch/mmu.c`<br>`core/src/arch/mmu.h` | L1 1MB section table: Cached RAM, Device MMIO | 109<br>25 | 4.18 KB<br>1.26 KB |
| | Cache | `core/src/arch/cache.c`<br>`core/src/arch/cache.h` | L1 Instruction/Data Cache invalidate/clean & barriers | 69<br>42 | 2.42 KB<br>1.61 KB |
| **4. Basic I/O & Video** | GPIO | `core/src/drivers/gpio.c`<br>`core/src/drivers/gpio.h` | BCM2835 General Purpose I/O pin control | 39<br>14 | 1.18 KB<br>0.47 KB |
| | Activity LED | `core/src/drivers/led.c`<br>`core/src/drivers/led.h` | ACT LED GPIO driver | 55<br>9 | 1.54 KB<br>0.29 KB |
| | LED State Machine | `core/src/kernel/ledstate.c`<br>`core/src/kernel/ledstate.h` | Solid during boot, 1Hz heartbeat, error blink patterns | 28<br>22 | 0.81 KB<br>1.15 KB |
| | Serial UART | `core/src/drivers/uart.c`<br>`core/src/drivers/uart.h` | Mini-UART / PL011 driver at 115200 baud | 160<br>21 | 4.59 KB<br>0.73 KB |
| | Kernel Printf | `core/src/lib/printf.c`<br>`core/src/lib/printf.h` | Fast formatted string output multiplexed to UART & console | 268<br>31 | 7.30 KB<br>1.53 KB |
| | Framebuffer | `core/src/drivers/fb.c`<br>`core/src/drivers/fb.h` | Double-buffered 640x360 32bpp framebuffer via mailbox | 148<br>44 | 5.35 KB<br>2.10 KB |
| | Console Terminal | `core/src/gfx/console.c`<br>`core/src/gfx/console.h` | ANSI terminal text renderer over framebuffer | 333<br>37 | 9.35 KB<br>1.58 KB |
| | Bitmap Font | `core/src/gfx/font8x16.c`<br>`core/src/gfx/font.h` | 8x16 bitmap font tables | 268<br>18 | 28.88 KB<br>0.79 KB |
| | Splash Screen | `core/src/kernel/splash.c`<br>`core/src/kernel/splash.h` | Early boot logo rendering | 33<br>18 | 1.43 KB<br>0.79 KB |
| **5. IRQ, Timers & Fibers**| Board Identification| `core/src/drivers/board.c`<br>`core/src/drivers/board.h` | Model detection: Zero, Zero W, Zero 2 W, Pi 1 | 128<br>49 | 4.63 KB<br>1.99 KB |
| | Interrupt Controller | `core/src/kernel/irq.c`<br>`core/src/kernel/irq.h` | BCM2835 interrupt controller dispatcher | 94<br>51 | 2.51 KB<br>1.73 KB |
| | Hardware Timer | `core/src/drivers/timer.c`<br>`core/src/drivers/timer.h` | Free-running 1 MHz 64-bit microsecond counter | 18<br>7 | 0.35 KB<br>0.20 KB |
| | System Tick | `core/src/kernel/tick.c`<br>`core/src/kernel/tick.h` | 1000 Hz timer interrupt hook & heartbeat driver | 56<br>11 | 1.61 KB<br>0.42 KB |
| | Cooperative Fibers | `core/src/kernel/fiber.c`<br>`core/src/kernel/fiber.h`<br>`core/src/arch/fiber.S` | Lightweight cooperative coroutines and assembly context switcher | 241<br>117<br>29 | 7.89 KB<br>6.48 KB<br>0.94 KB |
| **6. Safety & DMA** | Hardware RNG | `core/src/drivers/rng.c`<br>`core/src/drivers/rng.h` | Broadcom hardware True Random Number Generator | 41<br>14 | 1.32 KB<br>0.48 KB |
| | Watchdog | `core/src/drivers/watchdog.c`<br>`core/src/drivers/watchdog.h` | 3-second hardware watchdog timer with anti-freeze check | 131<br>40 | 4.01 KB<br>2.16 KB |
| | Crash Crumbs | `core/src/kernel/crumbs.c`<br>`core/src/kernel/crumbs.h` | Non-volatile crash logging and diagnostics across boots | 257<br>31 | 8.38 KB<br>1.72 KB |
| | Direct Memory Access | `core/src/drivers/dma.c`<br>`core/src/drivers/dma.h` | BCM DMA channel allocator and transfer routines | 153<br>28 | 4.36 KB<br>1.29 KB |
| **7. Audio & USB / Input** | Audio Engine | `core/src/audio/audio.c`<br>`core/src/audio/audio.h` | Audio synthesis, chime generator, channel manager | 535<br>120 | 16.35 KB<br>6.15 KB |
| | PWM Audio | `core/src/audio/pwm_audio.c`<br>`core/src/audio/pwm_audio.h` | Dual-channel PWM audio generation with DMA | 212<br>12 | 7.69 KB<br>0.44 KB |
| | USB Host Controller | `core/src/usb/dwc2.c`<br>`core/src/usb/dwc2.h` | Synopsys DWC2 USB host driver | 397<br>43 | 14.85 KB<br>2.02 KB |
| | USB Subsystem | `core/src/usb/usb.c`<br>`core/src/usb/usb.h` | Core USB device enumeration and endpoint polling | 641<br>55 | 24.13 KB<br>2.60 KB |
| | HID Parser | `core/src/usb/hid.c`<br>`core/src/usb/hid.h` | Gamepad (PS4/PS5, Xbox, DirectInput) & keyboard parser | 1047<br>160 | 40.56 KB<br>8.54 KB |
| | Unified Input | `core/src/kernel/input.c`<br>`core/src/kernel/input.h` | Central button, key event dispatcher & system hotkeys | 369<br>95 | 10.70 KB<br>5.05 KB |
| **8. SD Storage & FAT** | SD Card Driver | `core/src/drivers/sd.c`<br>`core/src/drivers/sd.h` | Broadcom EMMC/SD bus communication | 80<br>30 | 2.98 KB<br>1.36 KB |
| | FAT File System | `core/src/fs/fat.c`<br>`core/src/fs/fat.h` | Complete FAT16/FAT32 driver with long file name support | 1041<br>102 | 35.18 KB<br>5.41 KB |
| | Cartridge System | `core/src/kernel/carts.c`<br>`core/src/kernel/carts.h` | `carts_init()`: Mounts SD, builds library, cart launcher | 1440<br>48 | 59.01 KB<br>2.46 KB |
| | Configuration | `core/src/kernel/config.c`<br>`core/src/kernel/config.h` | Parser and storage for `/bm/config.txt` key-values | 345<br>54 | 11.88 KB<br>2.76 KB |
| | SD Boot Log | `core/src/kernel/sdlog.c`<br>`core/src/kernel/sdlog.h` | Flushes kernel boot log to SD card | 94<br>6 | 2.71 KB<br>0.12 KB |
| **9. 3D Graphics (GPU)** | VideoCore 3D | `core/src/gpu/v3d.c`<br>`core/src/gpu/v3d.h` | Hardware register management & max clock boost for V3D | 468<br>166 | 14.72 KB<br>9.14 KB |
| | 3D Graphics Pipeline | `core/src/gpu/gpu3d.c`<br>`core/src/gpu/gpu3d.h` | Binner memory allocation, draw calls, vertex pipelines | 4201<br>225 | 183.38 KB<br>13.66 KB |
| | QPU Vector Units | `core/src/gpu/qpu.c`<br>`core/src/gpu/qpu.h` | Assembly microcode generator for QPUs (e.g., accelerated fade) | 451<br>89 | 15.23 KB<br>4.71 KB |
| **10. Connectivity** | SDIO Host Bus | `core/src/wifi/sdio.c`<br>`core/src/wifi/sdio.h` | Broadcom SDIO bus protocol for Wi-Fi chipset | 308<br>22 | 10.37 KB<br>0.97 KB |
| | Wi-Fi Subsystem | `core/src/wifi/wifi.c`<br>`core/src/wifi/wifi.h` | BCM43438 firmware download, association & transmission | 1161<br>39 | 42.55 KB<br>1.90 KB |
| | Auto Wi-Fi | `core/src/net/wifi_auto.c`<br>`core/src/net/wifi_auto.h` | Background network connection attempt using saved credentials | 123<br>24 | 4.04 KB<br>1.09 KB |
| | Network & lwIP | `core/src/net/net.c`<br>`core/src/net/net.h` | lwIP integration layer, DHCP client, and SNTP time synchronization | 240<br>48 | 6.74 KB<br>2.16 KB |
| | USB Ethernet | `core/src/usb/smsc95xx.c`<br>`core/src/usb/smsc95xx.h` | Driver for LAN9512/SMSC95xx on Pi 1 Model B/B+ | 356<br>34 | 11.75 KB<br>1.51 KB |
| | Bluetooth | `core/src/bt/bt.c`<br>`core/src/bt/bt.h` | Bluetooth HCI layer over serial for pairing wireless gamepads | 1331<br>57 | 47.05 KB<br>2.70 KB |
| **11. Runtime & Launcher** | Lua 5.4 VM Engine | `core/src/script/luavm.c`<br>`core/src/script/luavm.h` | Lua VM initialization, executes `/bm/autoexec.lua` | 96<br>24 | 2.43 KB<br>1.05 KB |
| | Fantasy Console API | `core/src/bm/runtime.c`<br>`core/src/bm/runtime.h` | Full BM engine: 2D sprite draw, 3D render loop, math, audio | 4055<br>139 | 169.78 KB<br>7.95 KB |
| | Dashboard / Home | `core/src/kernel/home.c`<br>`core/src/kernel/home.h` | System Home screen, cartridge selector, and settings menu | 236<br>109 | 7.33 KB<br>6.15 KB |
| | Interactive Shell | `core/src/kernel/monitor.c`<br>`core/src/kernel/monitor.h` | Interactive serial and on-screen CLI shell / debug monitor | 991<br>5 | 37.37 KB<br>0.16 KB |

---

## 3. Grand Totals

- **Total Participating Files**: **`103`** (including C sources, headers, assembly files, linker script, and firmware config)
- **Total Lines of Code**: **`26,381` lines**
- **Total Uncompressed Size**: **`1,040.09 KB`** (~**`1.02 MB`**)
