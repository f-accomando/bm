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
  Market 25, Lib 15, covers 10, reports 5, WiFi 5, SD map 10; RGB30 (`src/rgb30/ui.c`): update 25,
  Market 55, WiFi 10, SD map 10. A file/kernel from the PC (netxfer write) is exclusive, first, outside the table. GAME
  and TOOL are 0, nothing calls them: games own the machine; lockstep (Overbit, bmnet) untouched.
- FAT writes give the CPU back between pieces (`fat_write_tick` = `fiber_slice_tick`); FAT
  updates batched; multi-sector writes on for the Pi too.
- `menu idle:` log line (every 300 frames when a job ran or SD counters moved): ms taken of
  ms offered, job time, `+x` worst overrun, `stop` time apart, min slack, sd retries/timeouts.
- The menu background (blurred selected cover, `menu_bg.c`) is made `BG_ROWS` (24) rows per frame while the old one stays on screen; the whole picture in one frame (230,000 pixels of float math) was a long frame at each new selection and the mouse pointer jumped over the covers (23 Oct; cause by code reading and the host timing, not yet measured on the Pi). `make test-menu-bg`.
- `background_stop()` before an app / rescan / delete, except update and publish (apps and pages
  are refused while `update_busy()` / `publish_busy()`). `net_wait_step` returns -1 if cancelled.

## Measured on the Pi (Pi Zero W, SDHC 7.4 GiB; 2026-10-11)
- Market download goes on in the background with ~1 ms overruns. Before the FAT work a 6.5 MB
  install froze the menu 30 s. Reports cost ~100 ms per TLS handshake (3 per report).
- Covers +500 ms = one sdhost read timeout + retry (counters; cause unknown). A 0.1 s read timeout +
  controller reset in `sdhost.c` was tried and reverted: the Pi became unusable; one change per test.
  First catalog write +338 ms. QEMU only: Market first work 3.6 -> 0.3 s (56 games), install +26 -> +0.2 s.
- To re-measure after the FAT work: Overbit install (new and update) and the market job's `+x ms`;
  the `sd:` boot line (cluster size); any "sectors in one write failed or came back wrong" (CMD25);
  sd retried/timed out; leaving the Market tab during a write still installs; a kernel from the PC
  (`bm_net.py --kernel`) must not report a freeze at the next boot; no "no job" line every 5 s.

## Stages 1-5 of the plan
Measurement build (1), hash steps (2), SD writer lock (3), keep-alive (4) and Range resume (5) are done. The detail, the tests and the Pi numbers still to collect are in [multitasking-stages](multitasking-stages.md). The rules they add are in `## Rules (do not break)` below.

## Open
- A download inside a tool or game (jobs stop before an app: `background_stop`, `market_set_active(0)`;
  the Market fetch starts at byte 0 and has no `Range` yet; wall-clock read timeouts): needs the part file of
  stage 5 in `market.c` (catalog SHA-256 checks the whole) and a `FIBER_TOOL` call
  in `runtime.c` `present()` before its vsync wait, RAM for 128 KiB stacks. Games: opt-in only, never lockstep.
- Still holding the CPU: TLS handshake (one per HTTPS request), ECDSA verify of a catalog or
  release (needs mbedTLS restartable ECC), a decoded cover, a directory growing by one
  cluster (written sector by sector).
- TLS session reuse for a new connection (proposal: 2-4 `mbedtls_ssl_session` cached
  by host:port in `tls.c`, `MBEDTLS_SSL_SESSION_TICKETS`, ~60+40 lines). Keep-alive is in; do it only if the handshake time
  per new connection on the Pi justifies it.
- Pi numbers for stages 3-5: handshakes per report (before 3, now 1), the longest frame during a queued `file_write`
  of 4 MiB (`menu stalls:` is menu only; use a cart that logs `timer_ticks`), `Accept-Ranges` and 206 of the catalog host
  and the release CDN, the real cost of a part file save of 4 MiB.
## Rules (do not break)
- Every SD write from a cartridge, the settings or a fiber that gives the CPU back goes through `sd_writer_begin` /
  `sd_writer_end` (or `sd_write_file`). A writer that gives the CPU back registers a pause hook and is safe to stop at
  any tick; it starts again from its first byte.
- `sd_writer_begin` never pauses another writer from inside a fiber: the hook runs fibers.
- A queued write keeps its own copy of the data (the web slot may be freed meanwhile); at most 4 jobs.
- Keep-alive is opt-in per request (`keep_alive`). The kept connection is closed at `web_reset`; a request to another host closes it.
  A request that fails on a reused connection before any answer byte is sent again once on a new one.
- A part file is written only when the server sent `Accept-Ranges` (or 206) and a size, and is checked by the address hash and the total.
  The whole file is checked by the caller (SHA-256 or size).
- The limits of the web API stay: https only, 3 redirects, 1 s per host, 4 MiB `web_get`, 8 MiB `file_write`, 128 files (a part file counts), no `.bm`/`.bme`/`.b16`.
- Work in the menu frame (drawing, a new selection) stays below one frame. The pointer position is read once per frame, so a long frame is one jump of the pointer. Make long work in bands (`menu_bg_step`) or in a job.


## Considerations
- Triage of the failing tests (cause, evidence, owner) and the roadmap cleanup list for open PRs 80-100: [`docs/design/triage-and-roadmap-cleanup.md`](../design/triage-and-roadmap-cleanup.md).
- Best approach per workload (SD writes, downloads, covers, game freezes, app load and close), ranked, with what to measure first and what only multicore can do (ROADMAP SYS-10): [`docs/design/multitasking-report.md`](../design/multitasking-report.md).
- Alternatives to R49 for every Open item, ranked, with a staged plan and Pi numbers to collect: [`docs/design/multitasking-alternatives.md`](../design/multitasking-alternatives.md).
- Quotas matter little on a 14-game card: the value is the measurement and removing the long stalls
  (FAT, handshake, SD timeout), not the share table. With real multicore the idle-slice model gives
  way to real concurrency; the cancellation/lock rules (`background_stop`, card writer) carry over.

### Multitasking su Single-Core: Confronto Unix vs BM & Strategie Differenziate
Confronto architetturale tra l'approccio kernel monolitico/preemptive (Unix-like) e Bare-Metal cooperativo:

1. **Cosa fa meglio Unix rispetto a BM su single-core:**
   - **Preemption hardware garantita:** Tramite timer interrupt e time-slicing forzato, i task bloccanti (`while(1);` o calcolo CPU-bound) non congelano il sistema.
   - **Isolamento della memoria (MMU):** Spazi di indirizzamento virtuale separati; il crash/fault di un task termina il processo senza corrompere la memoria globale o far collassare il kernel.
   - **Scheduler a priorità dinamiche ed euristiche I/O:** Favorisce i task interattivi abbattendo la latenza e previene l'inversione di priorità.

2. **Limiti delle sole Fiber cooperative:**
   - *Fragilità cooperativa:* Basta un delay busy-waiting, una chiamata I/O sincrona non spezzata (es. handshake TLS o timeout SD) o la dimenticanza di un `yield()` per mandare in starvation gli altri task.
   - *Assenza di Hard Real-Time:* Impossibile garantire latenze sub-millisecondo per eventi critici (audio, stream input/network).
   - *Stack non protetto:* Rischio di overflow silenzioso tra gli stack allocati delle fiber in assenza di MPU.

3. **Approccio Differenziato / Ibrido per Categoria di Task:**
   - **Livello 0 - Interrupt Hardware (ISR ad alta priorità):** Streaming audio buffer (DMA/I2S), cattura frame di clock seriale/pad.
   - **Livello 1 - Event-driven / Non-bloccante nel loop principale:** Logica 60 fps di giochi e strumenti (`present()`, rasterizer C).
   - **Livello 2 - Fiber cooperative a time-slice:** Task lenti di background (download Market, hashing SHA-256 cartucce, scrittura FAT a blocchi, scraping cataloghi).
   - **Livello 3 - Preemption controllata / Watchdog / MPU (Evoluzione R49):** Controllo overflow stack con guardie o MPU e timer di slice per mitigare stalli lunghi.

## Spunti di Sviluppo (Beta R1 / R2)

- **R49 — Multitasking Ibrido & Scheduler Differenziato (Beta Rev 1 & Rev 2)**:
  - *Revisione 1 (Beta R1 - Consolidamento cooperativo I/O):*
    - Estensione del modello `FIBER_TOOL` e `FIBER_MENU` con yield obbligatorio nei loop lenti di crittografia/hash (mbedTLS ECC restartable, hash SHA-256 incrementale) e download con ripresa (`Range: bytes=`).
    - Isolamento stretto tra scritture SD concorrenti (salvataggio gioco `store()` vs download fiber in background).
    - Canarini di stack software all'estremità degli stack delle fiber per intercettare gli overflow al context switch.
  - *Revisione 2 (Beta R2 - Approccio ibrido con Preemption & MPU):*
    - Scheduler gerarchico differenziato: ISR per audio e periferiche veloci, time-slice preemptivo leggero o interruzione tramite timer SysTick/timer counter per impedire a una fiber di monopolizzare la CPU oltre la quota assegnata.
    - Protezione opzionale tramite MPU (sulle piattaforme che ne dispongono) per isolare le aree di stack e segnalare immediatamente gli overflow.

