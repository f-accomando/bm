# Multitasking report: the best approach for each workload

Status: investigation only. No kernel code changed. Written 23 Oct 2026.
The Pi Zero W (one ARM1176 core at 700 MHz, RGB565) is the benchmark.
Every number is from a doc named next to it. A number marked "derived" is a division of two measured numbers. A number marked "not measured" has no run behind it.
Nothing here is verified on hardware beyond what the cited docs say.

Related files:
- State and rules: [multitasking](../progress/multitasking.md), [multitasking-stages](../progress/multitasking-stages.md).
- Earlier study of the CPU holders: [multitasking-alternatives](multitasking-alternatives.md). This report does not repeat its options A to J. It uses their letters.
- 3D numbers: [BENCH3D](../BENCH3D.md). SD numbers: [video](../progress/video.md).

## 1. What bm has today

Mechanisms, with the file that holds each one:
- **Cooperative fibers.** `src/kernel/fiber.c`. One fiber runs at a time, inside `fiber_resume`. Only the menu loop runs them. A `fiber_share_t` table gives each job a percent of the menu's free time (`menu_jobs` in `src/kernel/carts.c`). The contexts `FIBER_GAME` and `FIBER_TOOL` exist, but their shares are 0 and nothing calls them.
- **The SD writer lock.** `src/fs/sdlock.c`. One writer at a time. A pausable writer (netxfer, queued `file_write`) stops when another writer begins.
- **Pause hooks.** `fat_load_tick` and `fat_write_tick` call `fiber_slice` between pieces. `background_stop()` stops the fibers before an app, a rescan or a delete.
- **Keep-alive and Range resume.** `http.c`, `webget.c`. One kept connection per host. A part file in `bm/web/NAME.PRT` lets a download restart at byte N.
- **The GPU job queue.** `gpu3d_queue`, `gpu3d_set_queue(2)`. The ARM works while the V3D draws.
- **QPU programs.** `gpu3d_qpu=1`. Particles, fade, block audio. Opt-in, default off.
- **IRQ parts.** A 1 kHz timer tick (it sets `fiber_over`), and the HDMI audio DMA with its own IRQ (`src/audio/hdmi_audio.c`).
- **Lockstep netplay.** Overbit and bmnet. They own the machine. No background work may run.

Facts that shape every workload:
- The SD card is read and written by **polled PIO** on both Pi drivers (`src/drivers/sdhost.c`: a 16-word FIFO; `sd_emmc.c`). Nothing uses DMA for the card. The CPU spins during a card busy wait.
- The USB mouse and keys are **polled** from the input loops (`usb_poll` in `src/kernel/input.c`). The network is polled too (`net_poll`). The WiFi chip is on SDIO and is polled.
- A fiber yields only at a tick it owns. A TLS handshake, an ECDSA verify, a cover decode and an SD timeout do not yield.
- `fiber_job_stop` is **synchronous**: it cancels the fiber and runs it until it yields (`fiber.c:189`). A job inside a stall holds the caller for that whole stall.
- Games get `web_get`, `web_poll` and a queued `file_write` (`docs/api/system.md`). One `web_poll` costs about 4 ms. A game gets no Market download and no background job.

Measured on the Pi Zero W (all from the docs):
- SD read 5.14 MB/s. SD write 2.50 MB/s mean, worst single write 603 ms (`video.md`, 22 Oct).
- One sdhost read timeout adds 500 ms. Cause unknown. A 0.1 s timeout with a controller reset made the Pi unusable and was reverted (`multitasking.md`).
- First catalog write: +338 ms. One TLS handshake: about 100 ms (reports, 3 per report before keep-alive).
- Market download in the background: about 1 ms overruns. A 6.5 MB install froze the menu 30 s before the FAT work.
- 2D fade of the full screen: 20.4 ms on the ARM, 9.3 ms on the QPU (`BENCH3D.md`, one run).
- A 60 Hz frame is 16.7 ms.

## 2. Workload 1: SD writes (saves, install, netxfer)

### Problem today
- A 6.5 MB install writes at 2.50 MB/s. That is 2.6 s of card time (derived).
- One piece of a queued `file_write` is up to 32 KB. At the mean speed that is 13 ms (derived). That is more than one frame. The worst frame during a 4 MB queued write is **not measured**.
- The worst single write is 603 ms. Most of it is probably card-internal busy time, with the CPU spinning in `wait_card_ready`. This is a hypothesis. It needs the split in section 8, item 1.
- A directory that grows by one cluster is written sector by sector (`multitasking.md`, Open).
- The lock, the pause hooks and the whole-write FAT store are done. Saves and netxfer cannot corrupt each other.

### Options
- **Fibers with smaller pieces (A).** Size each piece from `fiber_budget_us`. Cost: about 30 lines. Risk: low. Gain: the piece drops from 13 ms to about 3 ms for 8 KB (derived). The total time grows a little.
- **Yield during the busy wait.** `wait_card_ready` spins. A fiber could test the card state once per tick and yield in between. Cost: about 40 lines in `sdhost.c`. Risk: medium. The sdhost timeout change already failed once, so change one thing at a time. Gain: if the 603 ms is busy time, this removes most of it.
- **Write-behind for `save()`.** `save()` would copy the data, return at once, and write in the free time of the vsync wait. Cost: about 60 lines. Risk: medium. A power cut loses the last save. Say so in the API doc. Gain: a game never waits for the card.
- **DMA or IRQ state machine for the card.** The BCM2835 SDHOST has a DMA request line. The data phase would run without the CPU. Cost: large (a new driver path). Risk: high. Gain: the CPU copy time goes away. The card busy time does not.
- **A dedicated core (RGB30 only).** One core owns the SD driver and the FAT. Gain: none of the writes can stall the game. Cost: large (the FAT, the heap and the lock become thread safe). The Pi cannot use it.
- **Hardware timer slicing with a watchdog (C).** Not useful. A preempted write corrupts the FAT state.

### Ranking
1. Measure the split between copy time and busy time (section 8, item 1).
2. Budgeted pieces from `fiber_budget_us`, then the busy-wait yield if the split shows busy time.
3. Write-behind `save()`, with the loss rule written in the API doc.
4. Hybrid for RGB30 later: the same fiber code, with the SD driver on a worker core.
5. DMA state machine: only if the copy time is the larger part. Reject for now.

## 3. Workload 2: downloads (Market, update, web API)

### Problem today
- Each new HTTPS connection costs one handshake, about 100 ms, with no yield. Keep-alive removes the repeats for the Market, the reports and `web_get`.
- The ECDSA verify of a catalog or release and the hash of a 6 MB file are chunked (hash) or one block (verify). The verify time is **not measured**. The `menu stalls:` line prints it.
- The Market fetch starts at byte 0 in the menu. `web_get` has the `resume` option for games. The Market job itself has no Range resume yet (`multitasking.md`, Open).
- A download stops before every app (`background_stop`, `market_set_active(0)`). No download runs inside a game or a tool.
- The WiFi chip is polled. A long stall lets the TCP window fill. The effect on the throughput is **not measured**.
- The card write of the file is the slower side: 2.50 MB/s (section 2).

### Options
- **Fibers with work-budget steps (A).** Done for hashes. Restartable ECC for the verify and the handshake is open. Cost: 60 to 100 lines plus an mbedTLS config change. Risk: medium.
- **TLS session reuse (J1).** Cuts the handshake to a symmetric one for a second connection. About 60 to 100 lines. Do it only if the handshake time justifies it.
- **Pause and resume with Range (E).** A download stops when a game starts and resumes after it. The cost is one handshake (about 100 ms) at the resume. This is cheap and needs no concurrency. It needs the Market job to use the part file, and the server to send `Accept-Ranges` (**not verified**).
- **Download in the vsync slice (G).** `present()` calls `fiber_share_run` with the free time of the frame. The lwIP timers stay alive, so the connection survives. It needs A first, because a step longer than the free time drops a frame. RAM: the job stack stays allocated (128 KB for a Market download).
- **Run-to-completion state machines (F).** Saves stack RAM. Needs a rewrite of Market, update, publish and reports. Reject.
- **A worker core (RGB30 only).** TLS crypto and hashes run on core 1. See section 9.
- **QPU or GPU DMA offload.** SHA-256 on a QPU is possible but poor value: games use the QPUs, and the hash is not the bottleneck on the Pi (chunked already).
- **Hardware timer slicing (C-lite).** Only if the verify cannot be split. Last resort.

### Ranking
1. Collect the handshake, verify and hash numbers with the build that exists (section 8, items 2 to 4). No code.
2. Range resume in the Market job (E). A download then survives a game start by pause and resume.
3. Restartable ECC (A part 2), only if the verify stall is over 20 ms.
4. Download in the vsync slice (G) for tools first. Games opt in later. Lockstep games never.
5. Hybrid on RGB30: the same job code, with the crypto on a worker core.
6. Session reuse (J1) only if item 2 of section 8 shows more than one new connection per session.

## 4. Workload 3: loading covers in the menu

### Problem today
- `covers_main` reads the first 48 KB of each `.bm` with `fat_load_part` and decodes the cover (`read_cover`, `carts.c:210`). One read is about 9 ms at 5.14 MB/s (derived).
- The covers job has a 10 percent share of the free time of a menu frame. A read does not stop at the end of the share. It ends when the read ends. `sd_tick` calls `fiber_slice`, which yields only when the slice time is over.
- One sdhost read timeout adds **500 ms** to a cover (`multitasking.md`). Cause unknown. It is a fault, not a scheduling problem. It blocks any slice budget under 500 ms.
- A decoded cover (`menu_load_cover`) holds the CPU. Its time is **not measured**.
- The order is the tab's covers first, nearest to the selection (`next_cover`, `carts.c:246`). The selection moves when the pointer moves, so the order changes while the user moves the mouse.
- No `usb_poll` runs inside a cover read (`sd_tick`, `carts.c:294`). The mouse waits for the read.

### Options
- **Fibers (today).** Works when the card is healthy.
- **Smaller work per slice.** Read 16 KB first (the cover section header says if the cover is there), decode in bands (A).
- **A cover cache file.** One packed file on the card with the small covers. The menu reads it in one sequential read. The file is rebuilt when a cartridge is added or removed. Cost: about 150 lines. Risk: medium (cache invalidation). Gain: one read, not one per game. This is a proposal. It is not in any doc yet.
- **Poll USB inside the cover tick.** One `usb_poll` call in `sd_tick` keeps the mouse alive during a read. About 5 lines. It is in menu code, so another runner must decide. See section 7.
- **DMA read.** Same as workload 1. Poor value for 48 KB.
- **A worker core (RGB30).** The cover job runs on core 1 and posts the decoded cover. Gain: no menu stall. Cost: large. The decode is a pure function, so it is the easiest thing to move.
- **QPU decode.** No. The cover is small.

### Ranking
1. Find the cause of the 500 ms read timeout. Count timeouts per 100 cover reads (section 8, item 5).
2. Poll USB inside the cover tick, or clamp the pointer delta (section 7).
3. The cover cache file, if the count of reads shows many small reads are the cost.
4. Decode in bands, if the decode time is over 5 ms.
5. RGB30 worker core for the decode, later.

## 5. Workload 4: freezes of a game that is running

### Problem today
Sources of a long frame in a game:
- **SD timeout.** 500 ms, seen on cover reads. A game that saves or reads can meet the same fault. Not measured inside a game. Cause unknown.
- **A save.** One write of a few KB. A directory growth costs sector writes. The worst write was 603 ms in a bench with large files.
- **TLS handshake.** About 100 ms, inside `web_get` of a new connection. The doc says "show a connecting message" (`docs/api/system.md`). Not measured inside a cartridge.
- **GC.** The runtime uses the generational collector (`runtime.c:5584`). The pause length is **not measured** in any doc.
- **A queued `file_write` piece.** Up to 32 KB, about 13 ms (derived). Not measured.
- **A long frame by the game itself.** `frameskip(n)` and the session report (the 12 longest frames) show these.

What a game **can** do while a background job runs: nothing runs behind it, because `background_stop()` ends all the fibers. The game's own `web_get`, `web_poll` and queued `file_write` run in the game's frames. The GPU queue and the QPU run beside the game's code on their own hardware.
What a game **cannot** do: start a Market download, share a TLS handshake, or write the card without the lock. A lockstep game cannot take any slice.

### Options
- **Stall attribution (new, small).** When a frame is over twice its budget, print which counters moved: SD retries and timeouts, handshakes, GC step, queued write pieces. About 40 lines. Risk: very low. Gain: the cause of every freeze is named, not guessed.
- **Vsync slice (G).** `present()` gives the free time to `FIBER_GAME` jobs. Opt-in. Needs A first.
- **GC in the free time.** Run `lua_gc(LUA_GCSTEP)` in the vsync wait when the free time is large. Cost: about 20 lines. Gain: shorter GC pauses. Needs the GC pause numbers first.
- **Write-behind `save()`.** From workload 1.
- **Hardware timer slicing with a watchdog (C).** The watchdog part alone is useful as a report: `fiber_over` already prints "did not yield". A preempting switch is not safe (malloc, lwIP, FAT, mbedTLS are not reentrant).
- **A worker core (RGB30).** The only way to take a handshake or an SD timeout out of a game's frame. See section 9.
- **QPU or GPU DMA offload.** Good for the game's own work (fade, particles). It does not help the system jobs.

### Ranking
1. Stall attribution plus the GC pause number (section 8, items 6 and 7).
2. Write-behind `save()`.
3. GC steps in the vsync wait, if the GC pause is over 5 ms.
4. Vsync slice (G) for tools, then for games that opt in.
5. RGB30 worker core for crypto and SD.
6. Hybrid: the vsync slice on the Pi, the worker core on RGB30, one job API for both.

## 6. Workload 5: loading and closing games and apps

### Problem today
- `background_stop()` runs before every app. It stops the covers, the reports, WiFi retry and the SD map. It also pauses a netxfer write (`carts.c:380`).
- The stop is synchronous. If a job is inside a handshake (100 ms) or an SD timeout (500 ms), the start of the game waits for it. The `menu idle:` line prints the worst `stop` time. The value is **not in any doc**.
- `bm_run` frees the suspended game first (`bm_close_suspended`, `runtime.c:9985`). Only one cartridge is in memory. The close frees Lua state, assets and sheets. Its time is **not measured**.
- A start needs: the SD read of the whole file, the parse, `load_assets`, `new_cart_state`, a mode change and the audio bank. `loading_tick` redraws the splash between steps. Time to first frame is **not measured** in any doc.
- The update and the publish jobs are not stopped. An app cannot start while they run (`update_busy`).
- A suspended game stays in RAM and comes back at once. This is the fast path.
- The menu fibers keep their stacks (up to 128 KB each) until the job ends. The RAM free inside a game with a stack allocated is **not measured**.

### Options
- **Fibers (today).** Works. The cost is the stop wait.
- **Cancel flag checked inside the long calls.** The SD read already checks `fat_load_tick`. A handshake does not. Restartable ECC (A) gives the check point.
- **Stop in two steps.** Raise the cancel flag when the user selects a game, not when the start begins. The fibers then end while the splash draws. About 10 lines. It is menu code.
- **Free the job stacks at the stop.** `fiber_job_stop` can release the stack of a stopped job. Gain: RAM for the game.
- **Load in slices behind the splash.** The start is the splash, `bm_parse`, the assets, the state. Each step can call `loading_tick`. It does. A worker core (RGB30) could decode assets while core 0 draws the splash.
- **DMA copy of assets.** `src/drivers/dma.c` exists. A memcpy of a large sheet could use it. The gain is **not measured**. Poor value until the load profile exists.
- **Hardware timer slicing.** No gain here.

### Ranking
1. Measure the load profile: stop time, read, parse, assets, state, first frame (section 8, item 8).
2. Free the job stacks at the stop.
3. Raise the cancel flag at the selection, not at the start (menu code, another runner).
4. RGB30 worker core for the asset decode, only if the profile shows the decode is the larger part.

## 7. The pointer jump over the covers

Another runner is fixing this bug. This report does not touch the menu code. These are the findings from the multitasking side. They are **not verified on the Pi**.

Chain of cause, with the lines:
1. `pointer_update` (`src/kernel/pointer.c:90`) takes the mouse motion that piled up since the last call (`hid_mouse_take`, `src/usb/hid.c:1093`). It takes it **once per menu frame**.
2. `accel()` (`pointer.c:82`) turns the size of that sum into a gain from 0.4 to 1.2. The gain depends on the **size of the sum**, not on the speed.
3. The mouse is polled by `usb_poll`, called from the input loops. A cover read, a decode, a handshake and an SD timeout do not call it (`sd_tick`, `carts.c:294`).
4. A stall of one read (about 9 ms, derived), of a decode, or of a **500 ms sdhost timeout** makes the next frame take the motion of the whole stall in one call. The sum is large, so the gain is high. The pointer jumps far.
5. In the menu, `pt->moved` over a cover sets the selection (`carts.c:1545`). A jump lands on a far cover. The selection jumps too, and `cover_sel` changes (`carts.c:1254`). `next_cover` then reorders the reads, so the next read is another cover.

Why this matches the symptom:
- The jump shows only while covers load. The same stalls (card read, decode) are the ones in workload 3.
- The pointer is the only input that depends on a per-frame sum. The keys move one step per press.

How to check it (benchmark only, no live game):
- In the menu idle line, compare `+x` (worst overrun of the covers job) with the frames where the pointer jumped. A jump with `sd retries/timeouts` moving says the 500 ms timeout is the cause.
- Log the largest `m.dx, m.dy` per frame next to the frame time. A sum that grows with the frame time says the cause is the stall, not the gain curve.

Fixes that the other runner may use (this report does not choose):
- Call `usb_poll` inside the fiber tick, so the motion arrives in small parts.
- Scale or clamp the sum by the frame time, so a long frame does not make a long jump.
- Apply `accel()` to each report, not to the sum.
- Remove the 500 ms timeout. This fixes the cause (workload 3, step 1).

## 8. What to measure first on the Pi Zero W

Benchmarks and log lines only. Never a live game. For a frame number, use a cart that logs `timer_ticks` (the bench carts), or the 3D Bench. Run one change per test.

1. **SD write split.** For a 4 MB write in 32 KB pieces: per piece, the time to fill the FIFO and the time in `wait_card_ready`. Output: the share of busy time in the 603 ms worst case.
2. **Handshake.** Time of one full handshake to the catalog host and to the release host. 10 runs. Source: `menu stalls:` (`tls.c`).
3. **Verify and hash.** Time of one ECDSA P-256 verify (`release_verify`) and the SHA-256 speed in MB/s. Source: `menu stalls:`.
4. **Range.** `Accept-Ranges` and `206` from the catalog host and the release CDN. Open item in `multitasking.md`.
5. **Cover reads.** The count of sdhost read timeouts per 100 cover reads. The time of one `menu_load_cover`. The worst `stop` time of `menu idle:`.
6. **Longest frame during a queued write.** A bench cart queues a 4 MB `file_write` and logs the largest `timer_ticks` gap between frames. Open item in `multitasking.md`.
   - Result of 25 Oct (Pi Zero W, `pi_bench.lua`, 2048 KB): sync call 656 ms; queued: done in 1240 ms, worst frame 682.0 ms. The 682 ms was a bench error: the frame clock was not restarted after the sync call (656 + 26 ms). Host check (`make test-fat-queue`): no step of the queued write writes more than 74 sectors (4 KB clusters) or 68 (32 KB clusters). The bench is fixed. The corrected worst frame is **not measured**: the user runs the bench again.
7. **GC pause.** The longest `lua_gc` step in a bench cart that allocates like a game. Use the session report's longest frames.
8. **Load profile.** Stop time, SD read of the file, `bm_parse`, `load_assets`, `new_cart_state`, `enter_mode`, time to the first `present()`. For a 1 MB, a 6 MB and a 12 MB cart.
9. **Free time per frame.** Milliseconds left at `pace()` for three bench loads (light, medium, heavy). This is the budget for G.
10. **RAM.** Free heap at the Market tab and in a bench cart with a job stack allocated.
11. **Mouse reports.** Count of `usb_poll` calls per second in the menu with covers loading, and the largest `dx, dy` per frame.

## 9. What only new multicore hardware can do

The RGB30 has four Cortex-A55 cores. `CPU_ON` works (checked on the console, 26 Oct). With `cores=2` to `4` in `config.txt` the kernel starts the other cores as pure workers. Without the key it uses one. The state is in `docs/progress/multicore.md`.
The Pi Zero W has one core. Nothing here helps it.

Only another core can:
- Run a TLS handshake, an ECDSA verify, a SHA-256 or an image decode **without taking a cycle from the game's frame**.
- Hide an SD busy wait or an SD timeout from the game, if the card driver lives on that core.
- Run a download and its card write while a game is at 60 fps, with no vsync slice at all.
- Keep the menu pointer polled while a cover decodes.

What it costs:
- The heap (`malloc`) is not thread safe. lwIP, the FAT, mbedTLS and the SD driver are not reentrant.
- The cores need the MMU on with the same page tables, and the caches coherent. The current map is flat (`src/rgb30/mmu.c`).
- A bug here is a race. QEMU does not show races reliably.

Safe model (a hybrid):
- Core 0 owns the heap, lwIP, the FAT and the Lua state.
- Core 1 runs **pure functions** only: hash, verify, decode, handshake crypto. The input buffer is copied in. The output buffer is copied out. A small lock-free ring carries the request and the answer.
- The job API (`fiber_job_t`) stays the same. On the Pi a job runs in a fiber. On the RGB30 a job may post its pure part to core 1 and yield until the answer.
- The cancel rule (`background_stop`) and the card writer lock carry over unchanged.
- Move a subsystem (SD, network) to core 1 later, after the pure worker works and the numbers justify it.

Do not build this before the Pi numbers of section 8 exist. The code written for the Pi (budget steps, Range resume, the lock) is the same code the worker model needs.

## 10. Ranked plan across the workloads

Independent phases. Each one works alone. Stop after any phase if the numbers are good.

- **Phase 1: measure.** Items 1 to 11 of section 8, plus the stall attribution line (workload 4). No behavior change.
- **Phase 2: the SD card.** Find the 500 ms timeout. Budgeted pieces. The busy-wait yield if the split shows busy time. Write-behind `save()`.
- **Phase 3: downloads.** Range resume in the Market job. Restartable ECC if the verify stall is over 20 ms. The vsync slice (G) for tools.
- **Phase 4: covers and the start of an app.** The cover cache file, the stack release at the stop, the cancel flag at the selection, USB poll inside the cover tick.
- **Phase 5: games.** GC steps in the vsync wait, the vsync slice for opt-in games.
- **Phase 6: RGB30 core.** Check why `CPU_ON` is absent. Then the pure-function worker.

Top recommendation per workload, one line each:
1. SD writes: measure copy time against busy time, then budgeted pieces and a busy-wait yield.
2. Downloads: Range resume in the Market job, so a game start pauses a download for one handshake.
3. Covers: find the cause of the 500 ms read timeout before any other change.
4. Game freezes: name every freeze with a stall attribution line, then write-behind `save()` and GC steps.
5. Loading and closing: measure the load profile, then free the job stacks at the stop.
