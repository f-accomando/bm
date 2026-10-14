# Tropic Trail: final report (2026-10-14)

Eight English videos that teach the in-console editors of bm through a PSX-style 3D platformer (hero Pico, a mint gecko). Operators: SPV (Claude local, supervisor), CLD (Claude cloud, QEMU recordings), AGY (Antigravity), OP4 (a fourth operator on a second Windows machine, joined late), Grok (removed early). All eight mp4 (1920x1080, 60 fps) are made and were received by the owner; the upload to YouTube is the owner's.

Times are the ones in the logs (Rome time), rounded; where a number was not measured it says so. The lessons on the sync protocol are in [`sync-workflow.md`](sync-workflow.md); the protocol as it ended is in [`sync.md`](sync.md).

## What was delivered

**Videos** (`video/3d/NN-*/record.py` + `verify.py` per video, `docs/video/0N-*/` script, shot list, input, YouTube text): 1 Studio, 2 Pixel, 3 Animator, 4 Map, 5 Sound, 6 SDK (light, fog, shadow), 7 Code (the game logic with the assistant), 8 The game played by a bot. Lengths: 180 s (6), 242 s (7), 95 s (8); the others are screen recordings of the editors in QEMU.

**Features of bm** (all in `bm-core`):
- the 3D map engine `map3d_build/draw/height/blocked/objects` (AGY-01..06) and the 3D map editor (layers, 2x2 stamps, path, apples, crates, lamps, a 3D preview, undo);
- the editor places the **start** and the **goal** of a level (`Shift+S`, `Shift+G`) and the lamp slot bug (`lamp3d(0)` crash of the preview) is fixed;
- bm SDK in a 720x720 adaptive layout (AGY-09, R39) and the build variable `SDK_B16_RES`;
- the monitor line `err` in the network console (AGY-10, R-NET1): the last error of the cartridge;
- the assistant: English words (`third`, `person`, `behind`, `follow`, `first`) so a short English question is answered in English; the guides are found in the guide mode (Tab x4).

**Tools**: `tools/pi_bridge.py`, `pi.py`, `pi_test.py` (test the Pi Zero W from the PC; requests R-NET2/R-NET3 written, not built); `rec_qemu.py --take r1|r3` and `--tool`, serial log of the kernel in every recording; `montage.py` (6), `montage7.py`, `montage8.py` (tested, a ffmpeg hang fixed); `bot.py` (a bot that plays the game and writes the button script); `video/3d/check_series.py`.

**Sync protocol** (template on the branch `sync-handshake`, live copy on `bm-core`): status block (`sync_hdr.py`), `sync_check.py` (an objective check), inboxes, `DELIVERED` and `SIGNOFF` rows, short heartbeats, `sync_watch.py` (a heartbeat with no model tokens), the handshake in `CLAUDE.md` section 6.

**Skills and agents**: the series skills (`bm-video-series`, `bm-video-tutorial`) and the agent set of `CLAUDE.md` section 3 were used; drafts of a `bm-reviewer` agent, a review script and a "barriers" section are in `docs/design/agent-workflow.md`, not yet adopted.

## Review of the operators (1 to 5)

| Operator | Grade | Why |
|---|---|---|
| **CLD** (cloud) | **4** | Recorded and verified videos 1 to 8 in QEMU (about 15 to 19 times real time), built `rec_qemu` (take, tool, serial log), ran two recordings in parallel, found the root cause of the editor crash with a measurement that disproved the supervisor's hypothesis, found the `montage8` hang and fixed it. Lost: it read the urgent row (Shift+S/G) 85 minutes late and delivered a video 4 without start and goal (70 minutes redone); it has no timer, so its heartbeats depended on the owner; its file stayed in the old format. |
| **SPV** (local, me) | **3** | Planned, wrote the recorders and checks of videos 4, 6, 7, 8, found three defects with short probes, merged and kept the sync. Lost: the first video 4 was assigned as a draft that recorded an empty map (70 minutes of CLD wasted); sent a hypothesis as an instruction ("your kernel has no `map3d`", wrong); reported predicted states; wrote rows up to 2016 characters (limit 600); flagged OP4 IDLE on a stale read; assigned AGY-13 and a sign-off almost together; did not notice early that the level of video 4 is a corridor of 18 units, so the finale plays only about 9 s instead of the planned 51 s. |
| **AGY** (Antigravity) | **4** | The 3D engine, the 720 layout, `err`, `montage7` and `montage8` (each in 15 to 30 minutes, tested with ffprobe), and a clean review of the sync tools. Lost: the first video 4 draft did not work; a 64-minute silence with a "subagent active" declared; it described its tool set wrongly (ffmpeg "absent", SPV "local Linux") and its first report on the sync was mostly positive. |
| **OP4** | **4** | Set up WSL, QEMU and the kernel in about 20 minutes, found a real bug (the recorder opened the wrong tool for the SDK take) and delivered a verified take. Its work was redundant (CLD had the same fix) and its first acknowledgement came after 64 minutes. |
| **Grok** | **2** | Only GROK-01: eight commits correcting the docs of videos 2, 3 and 5 from the verified NOTES; the review asked for two fixes (a `×` encoding and a header). One of its commits wiped `SYNC.md` with a literal `$(cat ...)`. A single task: the grade has little weight. |

## Estimated against actual

| Phase | Estimate | Actual |
|---|---|---|
| AGY-05 (three fixes) | 1 to 2 h | about 40 min |
| AGY-09 steps 3 to 6 (SDK layout) | 45 min per step | about 50 min for steps 3 to 5; step 6 (the real fix, found in review) came after |
| AGY-10 (`err`), AGY-11, AGY-12, AGY-13 | 45, 45, 45, 20 min | about 14, 15, 28, 14 min |
| One 3D recording in QEMU | "about 15 min" | 68 to 72 min (the clock of QEMU runs 15 to 19 times slower) |
| Video 4 | one recording | four: draft (wasted), crash at the preview (wasted), without start and goal (replaced), final: about 280 min of QEMU for one video |
| Video 8, played part | 51 s | about 9 s (the level of video 4 is short); the montage is 95 s |
| End of the project (my ETA of the afternoon) | 19:00 to 20:00 (+-1.5 h) | mp4 delivered at about 20:20; the sync closed at about 21:00 |

## Lessons on the use of SYNC

**What worked**: one writer per file (no merge conflict in `sync/`), short work orders with a verification command, `READY` and `DELIVERED` rows as unambiguous signals, short probes before long runs (they found the three defects that would have shown after 70 minutes), a log that allows reconstruction.

**What did not**: a message is read only when its reader polls (CLD's urgent row, a 64-minute silence), two sources of truth (the status table and the files), times without a zone, long rows, stand-in files that cause rework, hypotheses sent as instructions, and rules added while operators worked.

**What changed and what to keep**: the status block with `eta` and `ack`; `sync_check.py` (the status given to the owner is the one it prints); inboxes for new work (max 5 lines); log rows up to 600 characters; one delivery line; a sign-off by the supervisor; short heartbeats until the first work order is taken; a no-model heartbeat (`sync_watch.py`); a branch of its own for each sync (`sync/<name>`) so parallel syncs do not touch each other.

**To improve next time**: measure the cost of each operator (the cost ledger was never filled); check the size of the real content before writing the scripts (the level length); never assign a long recording without a short probe and a serial log; put the owner's signal (an urgent row) in the inbox, never at the end of a table.
