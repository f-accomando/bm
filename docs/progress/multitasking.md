# Multitasking (fibers today; multicore later)
Cooperative fibers, menu only. Multicore / multithread for kernel7 / kernel8 will connect here: fibers
probably off or used differently (job per core, real threads for download, SD, WiFi).

## State now
- `src/kernel/fiber.c/.h`, `src/arch/fiber.S` (+ `src/rgb30/fiber.S`): cooperative coroutines, one at a time,
  menu loop only, never from an IRQ; no preemption; switch ~28 words, no MMU/cache. Jobs (`fiber_job_t`,
  named, run time, worst overrun): Market, covers, reports, Lib, netxfer write, WiFi retry, update, publish.
- Shares: contexts `FIBER_GAME` / `FIBER_TOOL` / `FIBER_MENU`; a `fiber_share_t` table per menu, guaranteed
  % first, leftover in table order. Pi (`menu_jobs`, `carts.c`): update 15, publish 15, Market 30, Lib 15,
  covers 15, reports 5, WiFi 5; RGB30 (`src/rgb30/ui.c`): update 30, Market 60, WiFi 10. A file/kernel from
  the PC (netxfer write) is exclusive, first, outside the table. GAME and TOOL are 0, nothing calls them:
  games own the machine; lockstep (Overbit, bmnet) untouched.
- FAT writes give the CPU back between pieces (`fat_write_tick` = `fiber_slice_tick`), FAT updates batched,
  multi-sector writes on the Pi too. `menu idle:` line (every 300 frames when a job ran or SD counters
  moved): ms taken of offered, job time, `+x` worst overrun, `stop` apart, min slack, sd retries/timeouts.
- `background_stop()` before an app / rescan / delete, except update and publish (apps and pages are refused
  while `update_busy()` / `publish_busy()`). `net_wait_step` returns -1 if cancelled.

## Measured on the Pi (Pi Zero W, SDHC 7.4 GiB, 32 KiB clusters; 2026-10-11)
- Market download in the background, ~1 ms overruns; ~100 ms per TLS handshake (3 per report); first catalog
  write +338 ms. To re-measure after the FAT work: Overbit install (new, update), the market's `+x ms`; any
  "sectors in one write failed or came back wrong" (CMD25); leaving the Market tab during a write still
  installs; a kernel from the PC (`bm_net.py --kernel`): no freeze report next boot; no "no job" line every 5 s.

## SD read timeouts (`sdhost.c`)
- Seen (v0.2.5-115): 1-3 per covers pass after a rescan, very likely 4-7 per Overbit load (4.6 / 3.1 s =
  1.08 s + 0.5 s each), none in ~200 Market read-backs; 500 ms each, the retry ok; which wait: not known.
- Now: first read try 0.1 s (SDTOUT 100 ms, waits 150 ms), then two of 0.5 s; a failed try recovers clean
  (FSM kick, CMD12 also after a failed command, FIFO drained); a log line per failed transfer (`sd: read N
  at LBA: <wait> ...`), `ok but slow` (> 50 ms for data), `at its end` (last block's CRC flag, logged only).
- To re-measure: those lines (card "data", no words: its latency or a lost start bit, so a gap after CMD12
  as Linux's `delay_after_stop`, or CMD23 reads; "read not completed": CMD12 right after the data as Linux
  and Circle do); covers `+x`, Overbit's load time; "at its end" never seen: make it a failure.

## Open
- A download inside a tool or game (jobs stop before an app: `background_stop`, `market_set_active(0)`; fetches
  start at byte 0, no `Range`, one request per connection, wall-clock timeouts) needs a pause keeping the bytes and
  a `Range` resume (SHA-256 checks the whole), a `FIBER_TOOL` call in `runtime.c` `present()` before vsync, SD
  writers kept apart (`store()`: a game save and a fiber write may meet), 128 KiB stacks; games opt-in, never lockstep.
- Still holding the CPU: TLS handshake (one per HTTPS request; reuse for a report's 3: 2-4 `mbedtls_ssl_session`
  by host:port in `tls.c`, or keep-alive), ECDSA verify of a catalog or release (needs mbedTLS restartable
  ECC), file hashes in `release.c` / `update.c`, a decoded cover, a directory growing by one cluster.
- `config_save` (synchronous) vs a paused netxfer write at card wrap-around: `netxfer.h` says pause first, Settings not.
- Failing tests: `test_sd_cartridges` flaky (its screen check; also on c880a33), RGB30 `test_update_from_sd`
  flaky, `make test-net` link failure in `test_config`.

## Considerations
- Quotas matter little on a 14-game card: the value is measuring and removing the long stalls (FAT, handshake,
  SD timeout). Real multicore replaces the idle slices; the rules (`background_stop`, card writer) carry over.
