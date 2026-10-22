# Multitasking alternatives (design study)

Status: study only. No kernel code changes. All cycle and size numbers are estimates until the
Pi measurements in section 6 exist. Mark each number "not measured" until then.

This study answers the "Open" list of [`docs/progress/multitasking.md`](../progress/multitasking.md)
and compares the R49 ideas (Beta R1: cooperative I/O consolidation; Beta R2: hybrid preemption with
MPU; the Unix comparison) with other options. The Pi Zero W (ARM1176, one core, 700 MHz) is the
benchmark. The RGB30 (RK3566, AArch64) has one usable core (no PSCI `CPU_ON`).

## 1. Facts from the code

- Fibers are cooperative. `fiber_switch` saves r4-r12, lr, d8-d15, FPSCR (`src/arch/fiber.S`).
  A fiber runs only inside `fiber_resume`. Nothing calls it from an IRQ.
- `fiber_slice()` yields when `timer_ticks()` passed `f->until`. The SD reads (`fat_load_tick`) and
  the FAT writes (`fat_write_tick`) call it. `net_wait_step()` always yields.
- Stacks have 16 guard words at the low end (`fiber_prepare`). `fiber_resume` checks them only
  after the fiber yields. An overflow that corrupts memory first is found late, or never.
- The Pi MMU is on, with a flat map of 1 MiB sections (`src/arch/mmu.c`: `SECTION`, one `ttb[4096]`,
  no second-level tables). It gives cache attributes only. It gives no protection between tasks.
  The RGB30 uses 2 MiB blocks (`src/rgb30/mmu.c`: `l1`, `l2[4]`), also flat.
- Correction to R49 Beta R2: the ARM1176 and the Cortex-A55 of the RK3566 have an MMU, not an MPU.
  A stack guard uses a 4 KiB page that is "no access". See alternative H.
- A data abort handler exists (`src/kernel/exceptions.c`: prints DFAR and DFSR). A guard page would
  give a clean report with no new handler.
- A 1 kHz timer IRQ exists (`irq_init`, `irq_register`). The IRQ runs on the stack that is current.
- Jobs: eight `fiber_job_start` users. Stack sizes: covers 16 KiB, Lib 32 KiB, sdmap 16 KiB, reports
  128 KiB, others use `STACK_SIZE`, `JOB_STACK`, `TASK_STACK`, `WRITE_STACK`. Nobody measured the
  real use of these stacks.
- TLS is mbedTLS 1.2 only (`MBEDTLS_SSL_PROTO_TLS1_2`). Session tickets and restartable ECC are not
  enabled. `tls.c` loops on `mbedtls_ssl_handshake` and the BIO callbacks wait with `net_wait_step`.
- `release_verify` hashes the whole manifest, then calls `mbedtls_pk_verify` once (`src/net/release.c`).
  The file check hashes the whole buffer in one call (`release.c`, `mbedtls_sha256(data, len, ...)`).
- `http.c` and `http_kernel.c` have no `Range` support (grep finds none).
- `netxfer_write_pause()` exists. `config_save()` does not call it.
- `runtime.c` `present()` ends with `pace(deadline, ...)`, the vsync wait. This wait is the natural
  place for a game or tool slice.

## 2. Open items (a to e)

| Item | Text |
|---|---|
| a | Download inside a tool or game: pause and `Range` resume |
| b | CPU holders: TLS handshake, ECDSA verify, file hashes, cover decode, FAT cluster growth |
| c | TLS session reuse or keep-alive (3 requests per report) |
| d | Menu SD writes (`config_save`) against a paused netxfer write |
| e | Failing tests: `test_sd_cartridges`, RGB30 `test_update_from_sd`, `make test-net` link |

Item e is not a multitasking problem. `test_sd_cartridges` also fails on the old kernel. The
`test_config` link failure is older than the fiber work. Do item e as stage 0 (section 5) with the
`bm-ci-triage` skill. No alternative below fixes it. Do not guess a cause before the triage.

## 3. Alternatives

Each entry gives: mechanism, items solved, cost on the Pi Zero W, risk, QEMU test, hardware first.

### A. Work-budget steps (resumable loops)

Mechanism: change each long loop into steps of a bounded size. The step ends with `fiber_slice()`.
- SHA-256: feed the data in chunks of 4-16 KiB (`mbedtls_sha256_update`). Hash while the download
  arrives, so the final check is one `finish`. Hash a file on SD by reading it in chunks.
- Add one API: `fiber_budget_us(n)`. It returns how many microseconds are left in the slice. A loop
  asks and sizes its next chunk. The same code runs straight through outside fibers.
- ECDSA verify and the TLS handshake: enable `MBEDTLS_ECP_RESTARTABLE`. Call
  `mbedtls_ecp_set_max_ops(n)` and `mbedtls_pk_verify_restartable`. The ECP code returns
  `MBEDTLS_ERR_ECP_IN_PROGRESS`. The caller yields and calls again. For the handshake, mbedTLS 3.x
  returns `MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS` for ECDSA/ECDHE client steps. Check that the
  imported mbedTLS version has it. RSA certificate chains do not restart: they stay one block.
- Cover decode: decode a band of rows per call, then yield.
- FAT cluster growth: write the new cluster in pieces. `fat_write_tick` already yields between
  sector writes; the gap is the directory extension path. Add the tick there.

Solves: b fully for hashes, cover, FAT; b for ECDSA and handshake only if the mbedTLS check passes.
Cost: RAM about +2 KiB (restart contexts). Code about +3-6 KiB. About 150-250 lines (hash 60, cover
50, FAT 20, ECC and handshake 60-100, budget API 15). Cycles: small overhead per step. Restartable
ECC is slower in total (estimate 5-15 percent, not measured). The user feels it less: no frame stalls.
Risk: low for hashes. Medium for restartable ECC (config, tests, patch size).
QEMU test: add a test that runs `release_verify` and a 6 MB hash inside a fiber and checks that
`fiber_jobs_report` worst overrun is under the budget. QEMU timing is not the Pi timing: test the
number of yields, not the time.
Hardware first: time of one ECDSA verify, one P-256 ECDHE, one full handshake, SHA-256 MB/s (section 6).

### B. Cooperative with budget: the watchdog flag

Mechanism: the 1 kHz timer IRQ sets a volatile flag `fiber_over` when the slice time ended. Long
loops (those we own) test the flag in one load instead of reading the system timer. The IRQ does
not switch. The loop yields at a safe point.
Solves: nothing new alone. It makes A cheaper in tight loops (a flag load, not an MMIO read). It also
gives a second signal: if the flag stays set for more than N ms, log "fiber did not yield" with the
fiber name. This is a diagnostic, and it makes stalls visible without the report line.
Cost: 20 lines, no RAM. Risk: very low.
QEMU test: a fiber with a `for(;;)` that checks the flag; check that it yields and the log appears.
Hardware first: cost of `timer_ticks()` read on the Pi (device memory read) against a flag load.

### C. Preemption of fibers by the timer IRQ

Mechanism: the IRQ handler saves the fiber state and calls `fiber_switch` to the resumer.
Solves: b completely, also for code we cannot edit (mbedTLS inner loops).
Cost: IRQ frame handling per platform (about 80 lines ASM and C for each of ARM and AArch64), more
stack in each fiber (a full register frame). Cycles: 1 kHz tick plus a switch only when over budget.
Risk: high. lwIP, mbedTLS `calloc`, `malloc`, FAT and the SD driver are not reentrant. The menu
runs on the main stack and uses the same state. A preempted fiber that holds the heap or lwIP lock
corrupts it. Rules needed: a `fiber_nopreempt` counter inside `malloc`, `free`, lwIP, FAT, SD; or
preempt only inside declared pure-compute regions (`fiber_compute_begin/end` around
`mbedtls_pk_verify` and `mbedtls_sha256`) and when the heap counter is zero. This is C-lite.
C-lite cost: about 150 lines, one global counter, a `malloc` wrapper. It solves the same as A for
ECDSA, with no mbedTLS patch, but it still needs the heap rule.
QEMU test: stress test with a hash loop in a fiber and `malloc`/`free` in the main loop, 10 minutes,
check heap integrity. Hard to cover all races: QEMU is deterministic in a way the Pi is not.
Hardware first: nothing to measure; first prove A is not enough.

### D. SD card lock with pause hooks, and an SD owner fiber

D1 (lock): one `sd_writer_begin(who)` / `sd_writer_end()` pair. `begin` calls the registered pause
hooks (`netxfer_write_pause`, and later the download pause) and fails or waits if a non-pausable
writer holds the card. `config_save`, `store()`, `project.c store`, and `fat` direct callers use it.
Solves: d. Also the "game save and fiber write may meet" part of a. Cost: about 40 lines, 0 RAM.
Risk: low. A pausable writer must be safe to stop at any tick (netxfer already is).
QEMU test: a netxfer write in progress, call `config_save`, check the file is intact and the write
restarts. Run with the card wrap-around case (a file larger than one pass of the FAT).

D2 (owner fiber with request queue): one fiber owns the card. Others post `{op, path, buffer}` and
wait or poll. Solves d and a, and it removes the lock. Cost: about 250 lines and a copy of each
buffer (RAM: up to a cart size; the write of a 6.5 MB cart would need streaming requests). The menu
and the games call `store()` and `config_save()` synchronously and expect a result at return. A
queue changes all these call sites. Risk: medium-high for little gain over D1 on one core.

### E. Range resume with a persisted partial file

Mechanism: add `Range: bytes=N-` to `http.c`. A download writes to `<name>.part` on SD (or keeps
bytes in RAM while paused). A small `.part.meta` file holds the URL, total size, the byte count
and the SHA-256 midstate (`mbedtls_sha256_context` is a plain struct, about 108 bytes; or hash the
part again at resume, one pass). On resume, send `Range`, check `206` and `Content-Range`, append.
When the server returns `200`, restart at byte 0.
Solves: a (pause across an app start, power loss, WiFi loss), and the old "start at byte 0" limit.
Cost: RAM 0 extra if the `.part` is on SD. Code about 120-180 lines (http 40, market 60, meta 40).
Risk: medium. Needs a server check: the catalog host and the release CDN must send `Accept-Ranges`.
Not verified. The final SHA-256 of the whole file stays the safety net. The SD write of the part
must use D1.
Note: a connection that nobody polls dies. A pause across a game or tool start always means a new
TLS connection and a new handshake. Range is therefore needed for this case, with or without
alternative G.
QEMU test: the host test server (`bm_net.py` or a local `http.server` with Range) serves a cart;
cut the connection at 40 percent; resume; compare the SHA-256. Also test a server that ignores Range.
Hardware first: time of a new handshake (about 100 ms from the reports, to confirm), server Range support.

### F. Run-to-completion job queue instead of fibers for I/O

Mechanism: each job is a state machine with `step(budget)`. No own stack. mbedTLS is non-blocking
(`WANT_READ`), so a handshake can be a state in the step.
Solves: saves stack RAM (128 KiB for reports, others smaller). It does not solve b: a step that
runs ECDSA still holds the CPU. Item a only with the same Range work as E.
Cost: a rewrite of Market, update, publish, reports. Thousands of lines. Risk: high, because the
current code works and is tested. A new state machine bug looks like a stuck job.
Use it only for new small jobs. Do not migrate the existing ones. Rank last.

### G. Use the vsync wait as the slice (FIBER_TOOL and FIBER_GAME)

Mechanism: in `present()`, before `pace()`, compute `free = deadline - now - margin`. If a tool or
a game opted in, call `fiber_share_run(table, n, ctx, now + free)`. `net_poll` keeps the lwIP
timers alive, so a download goes on in a game without a new connection.
Solves: a (no pause needed in the game case, `Range` only for a restart). Cost: about 30 lines in
`runtime.c`, one opt-in call in the API (`docs/api`), a table for FIBER_TOOL and FIBER_GAME. RAM:
the job stacks stay allocated while a game runs (128 KiB for a Market download, less for a lighter
job). Cycles: a 60 Hz game at 16.6 ms with 5 ms free gives a small background share.
Risk: a frame that misses vsync. A hash or handshake step longer than `free` (item b) breaks the
frame. So G needs A (steps of 1-2 ms). Lockstep games (Overbit, bmnet) never opt in.
QEMU test: a game with a fixed load plus a background job; check the dropped-frame counter stays 0
for 60 s, and the job progressed.
Hardware first: free time per frame of a real game; `net_poll` cost per call; slice jitter.

### H. Stack canaries, high-water marks and guard pages

H1 (software, now): check the 16 guard words at every `fiber_yield`, not only in `fiber_resume`
(they are the same point on one core, so only the tail of a stack that overflows between two
yields is missed). Paint each stack with a pattern at `fiber_prepare`. In `fiber_jobs_report` print
the high-water mark per job. Use the numbers to right-size the stacks (RAM saved, safe margins).
Cost: 30 lines, 0 RAM, a one-time paint of 128 KiB (about 0.1 ms estimate, not measured).
H2 (MMU guard page): put a 4 KiB "no access" page below each stack.
- ARM1176: replace one 1 MiB section entry of the stack area with a coarse second-level table
  (1 KiB, 256 small-page entries). Set AP = no access for the guard pages. Allocate stacks
  from a page-aligned pool (a 128 KiB stack costs +4 KiB guard, +4 KiB alignment waste).
- AArch64: add an L3 table (4 KiB) under one 2 MiB block. Same idea.
Solves: the "unprotected stack" point of the Unix comparison, with a precise abort (DFAR in the
log). Cost: about 60 lines per platform, TLB: more small-page entries for the stack area.
Risk: medium. Wrong table attributes break the cache setting of the region (the ARM1176 note in
`mmu.c`: the S bit and TEX must stay the same as `MEM_CACHED`). Only the stacks move to the pool.
QEMU test: a fiber that recurses until overflow. Expect an abort report that names the fiber, not
a hang. QEMU does enforce page tables.
Hardware first: nothing before H1 gives real stack numbers.
Not worth it: a full MPU or per-task address space. Fibers share the kernel, the data is shared by
design, and there is no process model to protect.

### I. IRQ-time-sliced hashing

Mechanism: the timer IRQ hashes a block per tick.
Verdict: reject. The IRQ blocks the main loop for the same time. It adds latency to the audio and
input. Use A instead: it gets the same smoothness in thread context.
Better for speed: the RGB30 A55 has the ARMv8 SHA-2 instructions. mbedTLS can use them
(`MBEDTLS_SHA256_USE_A64_CRYPTO_IF_PRESENT` in 3.x; check the imported version). This is a free
win on the RGB30 only. The Pi has no accelerator.

### J. TLS session reuse or keep-alive (item c)

J1 (session resume): cache `mbedtls_ssl_session` per host:port in `tls.c`. A repeat handshake does
no certificate check and no ECDHE (an abbreviated handshake, symmetric only). Needs the server to
accept session IDs, or `MBEDTLS_SSL_SESSION_TICKETS` + `MBEDTLS_SSL_CLI_C` with tickets. About
60-100 lines, RAM 2-4 sessions of about 0.3 KiB each plus the peer certificate if kept.
J2 (HTTP keep-alive): reuse one connection for the 3 requests of a report. About 60-90 lines in
`http.c`. Needs `Content-Length` or chunked parsing on every reply, and idle handling when the
server closes. Gives the biggest win (no handshake at all) for reports. J1 helps every case with a
new connection (including the Range resume of E).
Verdict: J2 for reports first (one host, three requests in a row). J1 after, if the handshake time
on the Pi justifies it. Risk: low-medium. Server support not verified.
QEMU test: local TLS test server (`openssl s_server`) counts handshakes; assert 1 for 3 requests.
Hardware first: handshake time per host (reports go to one host).

## 4. Ranking

Score = how many of a-d it solves, against cost and risk. The R49 ideas are placed too.

| Rank | Option | Solves | Cost | Risk |
|---|---|---|---|---|
| 1 | A: work-budget steps (with B flag, H1 canary and high-water) | b, enables a/G | ~250 lines | low-medium |
| 2 | D1 + E: SD lock with pause hooks, Range resume with a part file | d, a | ~220 lines | low / medium |
| 3 | G: vsync slice for tools and games (needs A) | a (in-game) | ~30 lines + API | medium |
| 4 | J2 then J1: keep-alive, session resume | c | ~70 + ~80 lines | low-medium |
| 5 | H2: MMU guard pages | stack safety | ~60 lines per platform | medium |
| 6 | C-lite: preempt in compute regions | b without mbedTLS patch | ~150 lines | high |
| 7 | F: run-to-completion queue, D2: owner fiber | stack RAM | very large | high |
| - | I: IRQ hashing | none | - | reject |

R49 mapping:
- Beta R1 (cooperative I/O consolidation) is rank 1 + 2 + part of 3. Keep it. Add the budget API,
  the part file and the lock as concrete forms.
- Beta R2 (timer preemption and MPU): replace "MPU" with MMU guard pages (rank 5). Replace
  "timer preemption" with the flag (B) first. Do C-lite only if hardware numbers show that A
  cannot reach a 2 ms step.
- Unix comparison, level 3: the same plan; drop per-task address spaces.

Option 1 (recommended): stages 1 to 6 below, in order, A first. Every stage works alone.
Option 2: C-lite first. It fixes b faster with less editing, but it adds a bug class (races) that
QEMU cannot show reliably. Choose it only if section 6 shows the ECDSA verify cannot be split.
Recommendation: option 1.

## 5. Staged plan

Each stage is one PR and has a QEMU test. Stop after any stage if the numbers are good.

0. Triage item e with `bm-ci-triage`. Output: a cause per failing test, or "not a regression".
1. Measurement build (no behavior change): stack high-water per job in `fiber_jobs_report` (H1),
   the `fiber_over` flag and "did not yield" log (B), timing lines for handshake, verify and hash.
   QEMU: the report line prints; a forced long loop logs. Pi: collect section 6.
2. Hash steps (A, part 1): chunked `release.c` and `update.c` hashes, hash while receiving. Add
   `fiber_budget_us`. QEMU: yield count of a 6 MB hash in a fiber.
3. SD lock with pause hooks (D1): `config_save`, `store()`, project store. QEMU: netxfer write plus
   `config_save` test; wrap-around case. Closes item d.
4. Keep-alive (J2) for reports. QEMU: handshake count equals 1.
5. Range resume with `.part` file (E), using the lock of stage 3. QEMU: cut at 40 percent, resume,
   SHA-256 equal; a server that ignores Range. Closes the base of item a.
6. Restartable ECC for `release_verify` and the handshake (A, part 2), cover bands, FAT directory
   tick. Only if stage 1 numbers show a stall over 20 ms. QEMU: yield count in `release_verify`.
7. Vsync slice (G) with the opt-in API, only for tools first, then games. QEMU: dropped frames 0.
8. Optional: MMU guard pages (H2) and C-lite, after the stack numbers and the stall numbers.

Update `docs/progress/multitasking.md` ("Open work") at the end of each stage. Run
`python scripts/consistency.py check` when stage 7 adds an API.

## 6. Numbers to collect on the Pi Zero W

Run on the Pi (no serial: use the `menu idle:` line and the reports branch). One change per test.
1. Time of one full TLS handshake to the catalog host and to the report host, in ms. Repeat 10 times.
2. Time of one ECDSA P-256 verify (`release_verify`), one ECDHE P-256 key exchange, one RSA
   verify if a chain uses RSA. Show the longest single call.
3. SHA-256 speed in MB/s with the current build, and the time for a 6.5 MB file.
4. Cover decode time per cover (worst), and the time of one FAT directory cluster growth.
5. The longest `fiber_job_run` overrun per job, from `menu idle:` ("+x ms"), over a full Market
   session and a report.
6. Stack high-water mark per job (stage 1), to set the sizes. Today: 128 KiB for reports.
7. Free time per frame in `present()` for three real games (ms from `pace()`).
8. Cost of `timer_ticks()` read against a flag load; cost of one `net_poll` call.
9. `Accept-Ranges` and `206` support of the catalog host and the release asset host.
10. RAM free at the Market tab and inside a game with a download job allocated.
11. The 500 ms SD read timeout (covers): count per 100 reads. It is a separate fault; it blocks
    any slice budget below 500 ms until the cause is known.
12. The effect of keep-alive: handshakes per report before and after.

Not verified on hardware: all numbers in this document. QEMU passing does not prove Pi timing.

## 7. Rules for the work (proposed, for `## Rules` after the first stage)

- A loop that can run more than 2 ms inside a fiber calls `fiber_slice()` or checks the budget.
- Only `sd_writer_begin` / `sd_writer_end` write to the card from the menu, a tool or a fiber.
- A lockstep game never opts in to a background slice.
- A new stack is sized from the measured high-water mark, plus a margin of 25 percent.
