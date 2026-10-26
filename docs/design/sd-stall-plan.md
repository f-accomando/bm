# SD stall plan (single core)

Status: design, 23 Oct 2026. S0 is in the code (measurement only, QEMU-checked, not yet run on the Pi). The other stages are not started. Every number marked "log" comes from the Pi report `reports/unknown/20261023-174601_log_pi-zero-w_dev.txt` (branch `reports`). Every other number is not measured.

Scope: one core (Pi Zero W, and the RGB30 until SYS-10 gives it a worker core). The method is the one built in stages 1-5 of [multitasking](../progress/multitasking.md): fibers, shares, the SD writer lock, keep-alive, Range, bands (`menu_bg_step`).

Goal: no menu frame, no game start and no game close loses more than one frame (17 ms) to the SD card, for three workloads:
1. covers and SD reads while the menu moves;
2. downloads in the background (Market, a file from the PC, a kernel);
3. lag at open, start, close and the return to the menu.

## 0. Result of S0 and S1 (Pi Zero W, 24 Oct, kernel `af7ec66f`)

- The timeouts sat at site `rdend` (`sd sites: rdend 14`): `wait_transfer_complete` in `sdhost.c` spun 500 ms when the controller was slow to return to idle.
- S1 bounds that read wait to 25 ms (`timeout_us = write ? 250000u : 25000u`). A retry reads in about 1 ms. The worst frame lag in the cold-boot cover scan fell from 504.3 ms to 29.5 ms; the scan fell from 4129 ms to 407 ms.
- `rescan()` in `src/kernel/carts.c` keeps the loaded covers (`c->cover`, `c->cover_read = 1`) across scans. Going back to the menu after a game does no SD cover read.
- Left: report jobs still yield up to 100 ms in a TLS handshake (stages S3 and S5).

## 1. Why the fiber did not fix the 500 ms

The fiber yields only between calls. The 500 ms is inside one call: `read_blocks` in `src/drivers/sdhost.c` spins on `timer_ticks() - t0 > 500000`. The hardware timeout (`SDTOUT`, `set_clock`) is also half a second. A fiber cannot give the CPU back from inside the spin. A smaller share does not help.

What the log says:
- `sd 17 retried, 17 timed out` and `covers 8753 ms (+503.9)`: every first attempt fails and every retry works. The abort path (`HSTS_CLEAR`, `stop()`, second `read_blocks`) is sound. The fault is a transient: the first data never arrives. The only cost is the wait.
- Timeouts come in covers-only windows too, so the WiFi reports are not the only trigger.
- The site is not known. Four waits count as a timeout: the command response, the first data word, the controller's own `REW_TIMEOUT`, the end of the transfer (`wait_transfer_complete`).

Conclusion: do not wait for the root cause. Make a stall cheap (layer A), then make it invisible (layer B), then do not start work that cannot finish in time (layer C). Measure the cause in parallel (S0).

## 2. Layer A: bound the wait in the driver

A1. Count by site. Add per-site timeout counters and a histogram of the time from the command to the first data word (under 1 ms, 3 ms, 10 ms, 25 ms, over). Print them in the `menu idle:` line. No behavior change. About 30 lines.

A2. Timeouts from the SD specification. The read access time is 100 ms at most, the write busy time 250 ms at most. Set `SDTOUT` from the direction of the next transfer, and use the same value in the software check. The command response has no reason to wait 500 ms: use 20 ms after init. Do NOT reset the controller (the reset made the Pi unusable on 11 Oct; the abort path already used by the retry stays as it is).

A3. Soft first attempt. When the caller is a fiber, the first attempt gives up after 25 ms through the same abort path. A second soft attempt is allowed. Only the third attempt uses the full spec timeout. A timeout of a soft attempt is counted apart ("soft") and is not a failure.

Rule for A3 (do not break): `sd_read` moves the card to the Arasan controller for good after one failed retry (`fall_back`, `src/drivers/sd.c`), and the WiFi chip uses that controller. A soft timeout must never reach `fall_back`.

Expected effect: a timeout costs 25 ms instead of 500 ms. Check with `menu idle:`: `covers +x` under 30 ms, `sd soft` counting, `sd retried` and fallback unchanged, no "SD read error" in 10 minutes of browsing.

## 3. Layer B: park the fiber, do not spin

When the card is slow, a fiber that spins takes the CPU. A fiber that parks lets the menu draw.

B1. A wait hook. `sdhost.c` calls `sd_wait_hook()` in each wait loop once the wait passed a threshold. In a fiber the hook calls `fiber_yield()`. Outside a fiber it does nothing.

B2. Where it is safe.
- Card busy after a write (`wait_card_ready`, the FIFO drain): DAT0 is low, no data is in flight. Parking is always safe. This is the 603 ms worst write of the report.
- Waiting for the first word of a read: nothing is in flight until the data starts. If it starts while parked, the 16-word FIFO fills in about 5 us and overruns (`HSTS_FIFO_ERR`). The existing abort and retry handles it. Set the park threshold above the normal first-data latency: take it from the A1 histogram (the 99.9th percentile). Do not guess it.
- In the middle of a block stream: never park.

B3. An owner flag. A parked command owns the controller.
- `sdhost_read` and `sdhost_write` set `sd_owner` (the job) at entry and clear it at exit.
- A second fiber that enters while the flag is set yields until it clears (and honors `fiber_cancelled`).
- The main loop that enters while the flag is set runs the owner with `fiber_job_run(owner, now + 1 ms)` until it clears. The owner is inside a bounded wait (at most the spec timeout), so this ends. Check that `fiber_job_run` accepts a finished fiber.
- `background_stop()` already cancels the owner. The wait loop tests `fiber_cancelled()` after each park and aborts the command with the existing path.
- The SD writer lock (`sd_writer_begin`) stays. It orders writers. The owner flag protects one command.
- `src/kernel/sdlog.c` and `config_save` write from the main loop: they follow the main-loop rule above. Check each direct `sd_read`/`sd_write` caller once (grep) before S2.

B4. The RGB30 drivers (`rk_sd.c`, `sd_emmc.c`) call the same hook from `src/drivers/sd.c`. Same rules.

Expected effect: with A and B together a stall is invisible to the menu for writes, and at most one soft attempt (25 ms) for reads that stall before data.

## 4. Layer C: do not start what cannot finish

C1. Quiet gate for the reports. Send the reports only when the menu has been still for 5 s and the covers job is idle. A TLS handshake (270-300 ms in the log) then falls in idle time. If the user moves during a handshake, the loss is one stall, once.

C2. Poll USB in `sd_tick` (about 5 lines, `src/kernel/carts.c`). The pointer motion then arrives in small parts and the pointer does not jump. Details: [multitasking-report](multitasking-report.md) section 7.

C3. Cancel at the selection. When the user presses A on a game (or holds the cursor on it), raise the cancel flag of all jobs at once. The fibers end while the splash draws. `background_stop()` at the real start then finds nothing to wait for. Today the start waits for the longest job step.

C4. Idle-aware shares. `fiber_share_run` takes one table per context. Add a second table per context for "user idle for 2 s". The active table keeps the small shares of today. The idle table gives the large share to the job the user waits for.

C6. Cover gate (done 26 Oct 2026, not measured on a device). The covers job and the Market cover job start a new cover only when the selection and the tab have rested 250 ms (`menu_ui_still`, `COVER_QUIET_US`). The covers job ends between two covers when the user moves again.

C5. Back-off for a bad cover. A cover whose read failed twice goes to the end of the queue and waits 2 s. The job takes the next cover.

## 5. The three workloads

### 5.1 Covers and menu reads

| Cost | Fix |
|---|---|
| SD timeout 500 ms | A2, A3, B |
| Pointer jump during a read | C2 |
| Handshake of the reports (100-300 ms) in the same time as covers | C1 |
| Same cover read again after a failure | C5 |
| Decode of a cover | done 26 Oct 2026: `menu_load_cover` gives the CPU back every 4 rows (`cover_band`) and stops on cancel; C6 holds new covers while the user moves. Measure `covers +x` on the device. |
| Many small reads | one cover cache file, only if S0 shows reads are the cost |

### 5.2 Background downloads

- Market install: already a fiber with 1 ms overruns (log of 11 Oct). What is left: the write waits (B), the handshake once per host (keep-alive is in), the ECDSA verify of the catalog (time not measured, `menu stalls:` has it). Range resume in the Market job lets a download survive a game start (stage 5 part file exists, the job does not use it yet).
- File or kernel from the PC (bridge, `easy_install`): `netxfer_write_tick` is exclusive and first, so the menu gets what is left. Use C4: while the user is active it gets a share (for example 60 percent of the free time) with pieces of 8 KB sized from `fiber_budget_us()`. While the user is idle it keeps all the free time. The queue rule (4 jobs, own copy of the data) and the pause hook stay. A kernel is written the same way. Its restart countdown starts only after the write ended and the size check passed (existing behavior, keep).
- The write waits are the larger part of the 603 ms worst write: B is the main gain here. Measure the split first (A1 counts the wait in `wait_card_ready`).
- If the stall after A, B and C is still a TLS handshake or a verify, then restartable ECC (alternative A in [multitasking-alternatives](multitasking-alternatives.md)). Not before the numbers.

### 5.3 Open, start, close, return to the menu

Known: `background_stop()` waits for the longest step of a job (an SD wait, a handshake). A cartridge is read whole and parsed. The suspended game is freed first. The menu rescans and the jobs restart.

| Cost | Fix |
|---|---|
| Stop waits for a job step | C3, and A, B shorten the SD part |
| Read of a large cartridge (about 2.4 s for 12 MB at 5 MB/s, derived) | dwell prefetch (below) |
| Parse, assets, state, first frame | not measured: S0 load profile |
| Close of a game (`bm_close_suspended`, Lua state, assets) | not measured: S0; if over 100 ms, give the game its own heap arena and free it in one step (design first) |
| Freeze back in the menu: rescan, covers and reports all restart in the same frames | staggered restart: draw the menu from the cache, then start covers, reports one by one, 1 s apart |

Dwell prefetch (proposal): when the cursor stays on a game for 700 ms and the menu is otherwise idle, the covers job reads that cartridge into RAM in parked 32 KB pieces. The start then skips the read. The buffer is freed when the selection changes or the free heap is low. Needs a check of the load path (`bm_run`) to take a buffer. Not designed further until S0 shows the read is the larger part.

## 6. Stages (one change per Pi test)

| Stage | Change | Pass when |
|---|---|---|
| S0 | A1 counters and histogram, load profile line, stall attribution line (which counters moved in a frame over 2x budget). No behavior change. | One Pi run gives the site of the timeouts, the first-data histogram and the load profile. |
| S1 | A2 + A3 | `covers +x` under 30 ms; no more fallbacks than before; 10 min of browsing with no "SD read error". |
| S2 | B for writes, then B for reads | The worst frame during a 4 MB queued write under 20 ms; Market install `+x` under 5 ms; no FAT damage (QEMU and `chkdsk`-style host check). |
| S3 | C1, C2, C5 | The pointer follows the hand while covers load; no report starts within 5 s of input. |
| S4 | C3, C4, staggered restart | Time from A to first frame and from exit to a smooth menu drop (numbers from S0). |
| S5 | Only what S0-S4 numbers still demand: band decode, cover cache, restartable ECC, dwell prefetch, arena. | Each by its own number. |

S0 output on the Pi console: `menu idle: sd sites: rd1 3, rew 1; first word <1ms 120 <3ms 4 >25ms 3 (max 503.2 ms)` (the site of each timeout and the time to the first word of a read, since the last line) and, at each game start, `load: TITLE N ms (close, parse, assets+state, mode, audio, lua)`. Sites: busy, cmd, rd1 (no first word), rdN (stall in the stream), rew (the controller's own timeout), rdend, wr, wrend, card (busy after a write). To revert S0: `git revert` of the commit "S0".

Host test for S1 and S2: write the wait ladder as a pure function (state, time, status in; action out) and test it on the host with injected stalls, because QEMU never times out. Test the number of yields, not time. Add a host test that a fiber parked in a write and `config_save` from the main loop leave the file intact.

## 7. What this does not do

- It does not find why the first data word never comes. S0 gives the site. Suspects to check with the site: a command that the card missed after `stop()` of the previous read, a read that follows a write with the card still programming, the `EDM_THRESHOLDS` setting in `sdhost_init`.
- It does not preempt. Code that holds the CPU without a wait (a decode, a verify) stays a stall until S5.
- A game that does its own SD access keeps the old behavior until it goes through the same hook (`save()`, `file_write`).
