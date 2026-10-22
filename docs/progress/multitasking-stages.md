# Multitasking: stages of the plan (1-5)
Detail of the stages of [multitasking-alternatives](../design/multitasking-alternatives.md). State, open work and rules: [multitasking](multitasking.md).

## Measurement build (stage 1 of docs/design/multitasking-alternatives.md; no behavior change, 2026-10-22)
- Stack high-water per job: the stack is painted at its first allocation (`fiber_job_start`);
  `fiber_jobs_report` scans it and prints `stk 3.4/16K` after `(+x.y` in the `menu idle:` line.
- `fiber_over` flag: `fiber_tick()` (in the 1 kHz tick hook, Pi and RGB30) sets it when the running
  fiber is past its slice. `fiber_job_run` logs `fiber: <job> did not yield: x.y ms past its slice`
  for a slice over `FIBER_STALL_US` (5 ms), once for each new record of a job.
- `menu stalls:` line (after `menu idle:`, same window): TLS handshake (`tls.c`), `release_verify`
  and `release_check_file` (`update.c`): count, last and worst ms, KiB hashed (`src/kernel/stalls.h`).
- Boot diagnostics (monitor B) run `fiber_selftest()`; QEMU test `test_fiber_measure`.
- Pi numbers not collected. To collect with this build (section 6 of the design doc): 1 handshake ms
  (`menu stalls:`), 2 verify ms (`menu stalls:`), 3 hash speed (`menu stalls:` file hash KiB / ms),
  5 worst `+x` per job (`menu idle:`), 6 stack `stk` per job after a full Market session and a report.
  Items 4 and 7-12 need other measurements.

## Stage 2: hash steps (stage 2 of the design doc, 2026-10-22)
- `release_check_file` (`src/net/release.c`) hashes in 16 KiB pieces and calls `release_hash_tick`
  between them; `update_start` sets it to `fiber_slice` (like `catalog_hash_tick` in `market.c`).
  Same digest and errors; with no hook (host, main loop) it runs through.
- `fiber_budget_us()` (`fiber.h`): microseconds left in the slice, 0 when over, `UINT32_MAX`
  outside a fiber (no budget). One `timer_ticks` read. No caller sizes pieces with it yet.
- Hash while receiving: not done. `fetch` in `update.c` also reads from the SD card, and a piece
  hash in the HTTP sink would run inside the receive path. Only the chunked hash is in.
- Tests: `make test-release` (6 MB file: 383 hook calls, same digest as one-shot, changed byte and
  short file refused); `test_fiber_measure` checks `fiber_budget_us` (in a slice, over a slice,
  outside a fiber).
- Pi timing not measured. To collect: worst `+x` of the `update` job during a full install
  (`menu idle:`), and hash speed from the `hash` line of `menu stalls:` (it times the whole check, yields included).

## Stages 3-5: SD writer lock, keep-alive, Range resume (2026-10-22)
- **Lock** (`src/fs/sdlock.[ch]`): `sd_writer_begin(who, pause)` / `sd_writer_end(who)`. A holder with a pause hook
  is stopped by the next writer that begins (its job starts again later from byte 0); a holder with no hook makes
  the next one fail with -1. `sd_write_file(who, dir, name, ...)` wraps `fat_mkdirs` + `fat_write_file` for the
  short writes: game `save()`, `doc_write()`, `config_save()`. The netxfer write job holds it with `netxfer_write_pause`
  as hook. Market, update and covers do not hold it (the FAT store keeps each write whole at every tick, as before).
  `begin` called inside a fiber never pauses another writer (the hook runs fibers): it returns -1.
- **Queued `file_write`** (`src/bm/webfile.[ch]`): `file_write(name, data, true)` copies the data into one of 4 jobs and
  returns at once. A writer fiber (64 KiB stack) puts it on the card; `fat_write_tick` yields after each piece (a run of
  clusters, up to 32 KiB). `web_poll` and `file_state` pump it (3 ms). `web_reset` (cartridge end) writes what is left
  (`webfile_reset`). The longest stall of a frame is one piece (read-back included); not measured.
  The sync `file_write` takes the lock and, if a queued write is under way, pauses it.
- **Keep-alive** (`http.c`, `http_req_t.keep_alive`): one connection kept after a complete answer (length or chunks, no
  `Connection: close`) for the next request to the same host, port and scheme; a request that finds it dead before any
  answer byte is sent again on a new connection. Used by `web_get`, the Market download (`market.c`) and the GitHub calls
  of the reports (`github.c`: 3 requests of a report, 1 handshake). `http_idle_close()` at `web_reset`. A 204/304/1xx answer
  has no body whatever the headers say (before: the client waited for the server to close).
- **Range resume** (`webget.c`, `http_req_t.range_from`, `if_range`, `on_headers`): `web_get(url, {resume = "NAME.EXT"})` keeps
  `bm/web/NAME.PRT` (1280-byte header: magic, total size, ETag or Last-Modified, hash of the address; then the bytes)
  when a request fails or is cancelled, and about every 1 MiB; only if the server sent `Accept-Ranges: bytes` (or 206) and a size.
  The next request for the same address sends `Range: bytes=N-` and `If-Range`; 206 with the right start appends; 200
  (changed file, or Range ignored) drops the start; 416 or a wrong 206 starts again from byte 0. Done with a 2xx: the part file is
  deleted. The caller checks the whole file. Same stem with another extension shares the part file (`A.PNG`, `A.JPG`).
- Tests: `make test-webapi` (PC: 3 requests on one connection, `Connection: close`, a connection the server closed, a queued
  write in pieces, a save pausing it, the lock rules, queue limits, resume with a cut at 80000 bytes for a server that honors Range,
  one that lies, one that answers 416, one whose file changed). Pi timing not measured.
