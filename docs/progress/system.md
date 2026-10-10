# System & Hardware Subsystem

This document covers low-level boot orchestration, processor core setup, memory management, interrupt handling, and hardware device drivers in **bm**.

---

## 1. Bootstrapping & Kernel Entry

bm boots bare-metal without an intermediate operating system or Linux kernel. On the Raspberry Pi family, the GPU VideoCore bootloader executes first from the FAT32 boot partition of the SD card (`bootcode.bin` and `start.elf`), loads `kernel.img` into physical RAM at address `0x8000`, and starts the primary ARM core.

### Boot Sequence (`src/boot/start.S`, `src/kernel/main.c`)
1. **Reset & Core Verification**:
   - `start.S` verifies the core ID. On multi-core chips (Pi Zero 2 W / Pi 2/3), secondary cores are directed to spin stubs or firmware parking loops; only core 0 continues.
   - Sets processor status register (CPSR) into Supervisor mode (`SVC_MODE`, `0x13`) and disables IRQ and FIQ interrupts.
2. **Stack & BSS Initialization**:
   - Sets stack pointer `sp` below the kernel load address (`0x8000`).
   - Zeroes the `.bss` segment defined by the linker script ([`linker.ld`](../../linker.ld)).
   - Preserves boot register values (`r0 = 0`, `r1 = machine_type`, `r2 = atags/dtb_pointer`).
3. **Hardware Initialization (`main.c`)**:
   - Initializes PL011 UART (`src/drivers/uart.c`) for early console diagnostic output.
   - Sets up the exception vector table at `0x0000` via VBAR / CP15 register `c12`.
   - Queries hardware properties from VideoCore firmware using the mailbox interface (`src/drivers/mbox.c`, `src/drivers/prop.c`): board revision, MAC address, base clock rates.
   - Raises ARM CPU clock from stock 700 MHz to **1000 MHz (1 GHz)** on the Pi Zero W.
   - Enables hardware floating point (VFPv2: CP15 coprocessor access register `c1`, enabling CP10/CP11 full access, followed by `fmxr fpexc, #0x40000000`).
   - Configures MMU page tables and data/instruction caches (`src/arch/mmu.c`, `src/arch/cache.c`).
   - Initializes system timer, interrupt controller, USB host, SD card storage, and launches the runtime.

---

## 2. ARM Architecture & Memory Management

### CPU Profile
* **Target CPU**: ARM1176JZF-S (ARMv6 architecture).
* **Clock Frequency**: 1 GHz (governed by mailbox property tags).
* **L1 Cache**: 16 KiB 4-way set associative Instruction Cache, 16 KiB 4-way set associative Data Cache.
* **Vector Floating Point**: VFPv2 with 32 single-precision / 16 double-precision registers. Hard-float ABI enabled (`-mfloat-abi=hard -mfpu=vfp`).
* **Branch Prediction**: Enabled via CP15 Control Register (bit 11).

### Memory Map & MMU Configuration (`src/arch/mmu.c`)
The MMU uses a single-level page directory mapping the entire 4 GB physical address space in **1 MB sections**:

```
+---------------------------+ 0xFFFFFFFF
| VideoCore Registers & MMIO| (Identity mapped: Device / Strongly-Ordered)
| Base: 0x20000000          | (Pi Zero / Pi 1: 0x20000000; Zero 2: 0x3F000000)
+---------------------------+ 0x20000000
| Uncached DMA Buffers      | (Buffer sharing with GPU / V3D)
+---------------------------+ 0x1C000000 (448 MiB)
| GPU Shared Memory (64 MiB)| (Framebuffer, textures, VideoCore control lists)
+---------------------------+
| Heap (Newlib _sbrk)       | (Grows upward)
+---------------------------+
| Kernel .text, .data, .bss | (Loaded at 0x8000)
+---------------------------+ 0x00008000
| Stack & Vectors           | (Grows downward to 0x0)
+---------------------------+ 0x00000000
```

* **Section Attributes**:
  * **System RAM (`0x00000000`–`0x1C000000`)**: Normal memory, Outer/Inner Cacheable, Write-Back, Write-Allocate.
  * **Framebuffer (`0x1C000000`–`0x20000000`)**: Write-through or coherent memory to ensure frame scans are visible without flushing penalty.
  * **Peripherals / MMIO (`0x20000000`–`0x20FFFFFF`)**: Device / Strongly-Ordered memory, non-cacheable, non-executable, buffered writes disabled.
* **Heap Allocation**: Implemented in `src/kernel/main.c` through standard `_sbrk()` providing dynamic memory to the embedded Newlib libc and Lua 5.4 runtime.

---

## 3. Interrupts, Timers & Exception Handling

### Interrupt Controller (`src/kernel/irq.c`)
The BCM2835 interrupt controller manages peripheral interrupts routed through two GPU interrupt registers and one Basic Interrupt register:
* **Timer IRQ**: System timer channel 1 generates periodic interrupts for the 1 kHz system tick.
* **USB IRQ**: DWC2 host controller signals transfer completion and SOF (start-of-frame) events.
* **DMA IRQ**: Audio DMA channel notifications for IEC958 / HDMI audio buffer cycling.
* **UART IRQ**: Receive and transmit FIFO interrupts for serial console communications.

### Pacing & Tick (`src/kernel/tick.c`, `src/drivers/timer.c`)
* The 1 MHz BCM2835 free-running counter provides microsecond-accurate timekeeping (`timer_get_ticks()`).
* Frame pacing loop locks cartridge execution to 60.0 Hz (16,666 µs per frame).
* Background tasks and cooperative multitasking use a lightweight fiber mechanism ([`src/kernel/fiber.c`](../../src/kernel/fiber.c)) scheduled during idle frame budgets.

### Exception Vectors & Crash Diagnostics (`src/kernel/vectors.S`, `src/kernel/exceptions.c`)
All ARM hardware exceptions (Undefined Instruction, Prefetch Abort, Data Abort) are caught:
* Register dump (`r0`–`r15`, `CPSR`, `SPSR`, `FAR`, `DFSR`) printed to UART and displayed on-screen via emergency text console.
* Call stack unwinding reconstructs return addresses (`called from:` trace).
* Watchdog reset triggers after 60 seconds of stall; panic breadcrumbs saved in `.noinit` memory ([`src/kernel/crumbs.c`](../../src/kernel/crumbs.c)) survive reset and generate automated crash reports sent to GitHub on the next boot.

---

## 4. Hardware Peripherals & Device Drivers

### Custom DWC2 USB Host Stack (`src/usb/`)
Built from scratch without third-party dependencies (no USPi):
* Controls the Synopsys DesignWare Hi-Speed USB 2.0 OTG core in Host Mode.
* Root hub port resets, detects low-speed (1.5 Mbps), full-speed (12 Mbps), and high-speed (480 Mbps) devices.
* **HID Keyboard**: Boot protocol support with key repeat, modifier tracking, and Italian/US layouts.
* **HID Mouse**: Relative motion parsing, wheel scroll, and 3-button status.
* **Gamepads**: Generic USB HID descriptor parsing + dedicated Xbox 360 controller decoding.

### SD Card Storage & FAT32 Filesystem (`src/drivers/sd.c`, `src/fs/`)
* **Controller**: Arasan EMMC / SDHCI controller running in PIO mode over a 4-bit bus clocked at 25 MHz.
* **Filesystem Engine**: Custom-written FAT16 and FAT32 driver (independent from FatFs):
  * Supports Long File Names (LFN) and 8.3 short aliases.
  * Multi-sector cluster read/write optimizations.
  * Cluster chain lookups accelerated via memory-mapped FAT tables.
  * Supports hot-reloading cartridges from `/carts/` and persistent user saves in `/bm/`.

### Bluetooth HCI Stack (`src/bt/`)
* Uses UART interface to communicate with the onboard Broadcom BCM43438 Bluetooth controller.
* Downloads firmware patchram files during initialization.
* Implements HCI (Host Controller Interface) packet framing, L2CAP connection management, and HID protocol.
* Connects up to **4 Sony DualShock 4** wireless controllers:
  * Reads analog sticks, digital D-pad, face buttons, and triggers.
  * Controls RGB lightbar per-player color assignments (`controller(p).color`).
  * Supports Bluetooth Low Energy (BLE) wireless keyboards and mice.

### WiFi & Network Stack (`src/wifi/`, `src/net/`)
* BCM43438 FullMAC driver operating over the SDIO bus.
* Software-implemented 4-way WPA2-PSK handshake (`src/wifi/wpa.c`).
* Integrated with **lwIP** (lightweight TCP/IP stack) and **mbedTLS** for secure communications:
  * DHCP client for automatic IPv4 address configuration.
  * SNTP client for real-time clock synchronization.
  * HTTPS client powering OTA firmware updates and the official game market.
  * UDP lockstep network engine for multiplayer gaming ([`bmnet`](../../src/script/bmnet.lua)).

- Splash: the bm suite (editor, Sound, Studio, Animator, Mesh, Pixel, projects, Lib openings) opens with no splash; only games show it (`src/kernel/carts.c`, `suite`).
- Menu bar (2026-10-10): the controller, keyboard and mouse icons have no player number any more, on USB and on Bluetooth (Bluetooth keeps only its blue dot, `ICON_DOT`; `status_icons()` in `src/kernel/menu_ui.c`).
- WiFi (2026-10-10): the console now retries the connection by itself. Two tries at boot, then in the menu's idle time after 5 s, 15 s, 30 s, 1 min and every 2 min while the link is down (`src/net/wifi_auto.c`, Pi and RGB30); `wifi_boot=0` turns it off. Before every reboot (update, Settings, network kernel, monitor) the console now says goodbye to the access point with `wifi_leave()`. The cause of the missing connection was a single try at boot; the stale association after a reboot is a hypothesis, to confirm from the boot log on the device.
