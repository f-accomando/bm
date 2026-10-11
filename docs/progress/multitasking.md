# Multitasking (fibers today; multicore later)
Cooperative fibers, menu only. Multicore / multithread for kernel7 / kernel8 will connect here:
fibers probably off or used differently (job per core, real threads for download, SD, WiFi).

## State now
- `src/kernel/fiber.c/.h`, `src/arch/fiber.S` (+ `src/rgb30/fiber.S`): cooperative coroutines,
  one at a time, menu loop only, never from an IRQ; no preemption; switch ~28 words, no MMU/cache.
- Jobs (`fiber_job_t`, named, run time and worst overrun): Market, covers, reports, Lib, netxfer
  write, WiFi retry, update, publish.
- Shares: contexts `FIBER_GAME` / `FIBER_TOOL` / `FIBER_MENU`; a `fiber_share_t` table per menu,
  guaranteed % first, leftover in table order. Pi (`menu_jobs`, `carts.c`): update 15, publish 15,
  Market 30, Lib 15, covers 15, reports 5, WiFi 5; RGB30 (`src/rgb30/ui.c`): update 30, Market 60,
  WiFi 10. A file/kernel from the PC (netxfer write) is exclusive, first, outside the table. GAME
  and TOOL are 0, nothing calls them: games own the machine; lockstep (Overbit, bmnet) untouched.
- FAT writes give the CPU back between pieces (`fat_write_tick` = `fiber_slice_tick`); FAT
  updates batched; multi-sector writes on for the Pi too.
- `menu idle:` log line (every 300 frames when a job ran or SD counters moved): ms taken of
  ms offered, job time, `+x` worst overrun, `stop` time apart, min slack, sd retries/timeouts.
- `background_stop()` before an app / rescan / delete, except update and publish (apps and pages
  are refused while `update_busy()` / `publish_busy()`). `net_wait_step` returns -1 if cancelled.

## Measured on the Pi (Pi Zero W, SDHC 7.4 GiB; 2026-10-11)
- Market download goes on in the background with ~1 ms overruns. Before the FAT work a 6.5 MB
  install froze the menu 30 s. Reports cost ~100 ms per TLS handshake (3 per report).
- One-off +502 ms covers stall (probably sdhost 500 ms timeout + retry; counters added); first
  Market catalog write +338 ms. QEMU only: Market first work 3.6 -> 0.3 s (56 games), install +26 -> +0.2 s.
- To re-measure after the FAT work: Overbit install (new and update) and the market job's `+x ms`;
  the `sd:` boot line (cluster size); any "sectors in one write failed or came back wrong" (CMD25);
  sd retried/timed out; leaving the Market tab during a write still installs; a kernel from the PC
  (`bm_net.py --kernel`) must not report a freeze at the next boot; no "no job" line every 5 s.

## Open
- A download inside a tool or game (jobs stop before an app: `background_stop`, `market_set_active(0)`;
  a fetch starts at byte 0, no `Range`, one request per connection, wall-clock read timeouts): needs
  a pause keeping the bytes + `Range` resume (catalog SHA-256 checks the whole), a `FIBER_TOOL` call
  in `runtime.c` `present()` before its vsync wait, SD writers kept apart (a game save and a fiber
  write may already meet: `store()`), RAM for 128 KiB stacks. Games: opt-in only, never lockstep.
- Still holding the CPU: TLS handshake (one per HTTPS request), ECDSA verify of a catalog or
  release (needs mbedTLS restartable ECC), `release.c` / `update.c` file hashes, a decoded
  cover, a directory growing by one cluster (written sector by sector).
- TLS session reuse for the 3 requests of a report (proposal: 2-4 `mbedtls_ssl_session` cached
  by host:port in `tls.c`, `MBEDTLS_SSL_SESSION_TICKETS`, ~60+40 lines; or HTTP keep-alive).
- Synchronous menu SD writes (`config_save`) vs a paused netxfer write at card wrap-around: `netxfer.h` says pause first, Settings does not.
- Failing tests: `test_sd_cartridges` (also on the old kernel), RGB30 `test_update_from_sd` flaky,
  `make test-net` link failure in `test_config` (pre-existing).

## Considerations
- Quotas matter little on a 14-game card: the value is the measurement and removing the long stalls
  (FAT, handshake, SD timeout), not the share table. With real multicore the idle-slice model gives
  way to real concurrency; the cancellation/lock rules (`background_stop`, card writer) carry over.
