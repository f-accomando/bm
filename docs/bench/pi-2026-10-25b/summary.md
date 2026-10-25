# Hardware Run Summary: Raspberry Pi Zero W (25 Oct 2026, Run 2)

Date: 2026-10-25  
Target: Raspberry Pi Zero W (BCM2835 rev 9000c1)  
Branch: `claude/pi-hw-results-2-25oct`  
Base commit: `a2e63306dadadb5ee4a3bdb8cea6fd5c0a3e27eb`  
Exact kernel string: `bm kernel v0.2.5-1654-ga2e63306 - Raspberry Pi Zero W (BCM2835, revision 9000c1)`

## Precondition Check
- `git log --oneline | grep -ci "key chips"`: 3 commits (`602ae386`, `69f1e3c9`, `fd173f5c`). Precondition passed.

---

## Test Results: Pi Zero W

### P1: Bottom Bar with Key Chips Only & "Not Done" Notice
- **P1.a (Chips only in bottom bar)**: **OK**  
  Walked Market, Games, Dev, Settings (Screen, Sound, System, Updates), game options, Market list. Bottom bar (`FOOT_Y = 316`) shows key chips only. No detail strings, last game text, or counters appear.
- **P1.b (Key chips functionality)**: **OK**  
  Key chips (A, B, X, Y, Monitor) respond correctly across all screens.
- **P1.c (Error notice on network drop)**: **NON VERIFICABILE**  
  Remote session over WiFi without serial prevents cutting network connection. Code path in `src/kernel/market.c:799` verified (`notice_fail` displays "No network" for 4000 ms).
- **P1.d (Error notice on missing SD card file)**: **NON VERIFICABILE**  
  Cannot safely remove file remotely during active menu loop. Code path in `src/kernel/carts.c:778` verified (`notice_fail` displays "... is not on the SD card").
- **P1.e (Pong launch and return)**: **OK**  
  Pong started and ran 1,321 frames at 59.8 fps. Exited cleanly back to menu on Ctrl-Esc (`0x1c`).

---

### P2: Defaults of later, sort, qpu
- **P2.a (later profile comparison)**: **OK**  
  - Command: `:b3d tests=herotris,heroes profiles=GPU+VS+Q,GPU+VS+Q2; send`  
  - Herotris `GPU+VS+Q`: 50,068 triangles @ 60 fps, 13.7 ms/frame.  
  - Herotris `GPU+VS+Q2`: 50,284 triangles @ 60 fps, 13.3 ms/frame.  
  - Heroes `GPU+VS+Q`: 75,502 triangles @ 60 fps, 18.0 ms/frame.  
  - Heroes `GPU+VS+Q2`: 83,161 triangles @ 60 fps, 17.0 ms/frame.  
  - Repeating with `:set gpu3d_later=1` yields identical numbers (Herotris `GPU+VS+Q2`: 50,315 triangles @ 60 fps, 13.3 ms/frame) because `GPU+VS+Q2` already sets `queue=2` and measures later pipelining.
- **P2.b (sort profile comparison)**: **OK**  
  - Command: `:set gpu3d_later=0; b3d tests=heroes profiles=GPU+VS,GPU+VS+S; send`  
  - Heroes `GPU+VS`: 104,945 triangles @ 60 fps, 15.9 ms/frame.  
  - Heroes `GPU+VS+S`: 95,089 triangles @ 60 fps, 14.2 ms/frame.  
  - Shaded pixels (`pixels` field in R line): 0 for both profiles.
- **P2.c (qpu enable and Yharnam run)**: **OK**  
  - Configured `gpu3d_qpu=1` in `bm/config.txt` and rebooted.  
  - Fade test command: `:b3d tests=fade; send`  
  - QPU fade row: `qpufade key=1 path=qpu frames_qpu=98 frames_arm=0 n60_qpu=15.8 ms_qpu=8.7 n60_arm=-1 ms_arm=20.4`  
  - Diagnostic output: `QPU PASS: particles PASS (1024: ARM 163 us, QPU 50 us), fade PASS (1024 px: ARM 81 us, QPU 38 us)`  
  - Yharnam gameplay: 8,016 frames, 59.8 fps, 0 freezes, 0 black screen, 0 glitch, avg update+draw 8.86 ms, max 84.2 ms.  
  - Restored `gpu3d_qpu=0` in `bm/config.txt`.

---

### P3: Multitasking Web Benchmark
- Cartridge built: `python3 scripts/mkbm.py -o build/webbench.bm --lua tests/web/pi_bench.lua --title "Web bench" --author bm --res 640x360`
- Target URL: `https://github.com/f-accomando/bm/releases/download/v0.2.5/kernel.img` (4,040,264 bytes)
- Execution log (`docs/bench/pi-2026-10-25b/WEBBENCH.TXT`):
  - Synchronous write (2048 KB): 656 ms (`true`).
  - Asynchronous write (2048 KB): worst frame 682.0 ms, finished in 1240 ms.
  - Queued write with save: save `true`, write state `writing`, finished with state `done`.
  - Resume 1: cancelled after 1,062,128 bytes in 4 s (`resumed nil`).
  - Resume 2: complete 4,040,264 bytes in 12 s (`resumed 1062128`, HTTP status 206).
- TLS handshake statistics (console `menu stalls:`):
  - Handshakes: 4x, last 142.9 ms, worst 353.4 ms.

---

### P4: Pointer Jump Over Covers
- **P4**: **NON VERIFICABILE**  
  Testing conducted remotely without physical USB mouse hardware connected (Apple Magic Keyboard attached). Cover loader runs on idle background fiber; menu frame render time averages 6.5 ms (`draw 6530 us`), well inside the 16.7 ms frame budget.

---

## Test Results: PowKiddy RGB30
- **R1–R5**: **NON VERIFICABILE**  
  RGB30 hardware not active (per instructions: "PROVE SULLA RGB30 (ignora, la console non è attiva)").
