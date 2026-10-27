# The 500 ms SD read timeout (sdhost): history and open fault

Status: open fault, cause unknown. Written 27 Oct 2026 after a Pi Zero W lost its WiFi because of this fault. Read this file before you change a timeout in `src/drivers/sdhost.c` or `src/drivers/sd.c`.

Short rule: do not change the timeout value again to hide the stall. Two attempts of that kind failed. One of them is the suspected cause of the 27 Oct WiFi loss.

## 1. The symptom

- On the Pi Zero W, one SDHOST read now and then runs out its wait. The wait was 500 ms.
- The retry of the same read works at once (about 1 ms).
- The cost is the wait only. The card is not damaged and the data is right.
- The user sees it as one frame of 500 ms: a cover that loads late, a menu that stops, a game that loads 0.5 s slower per event.
- It is a fault, not a scheduling problem. A fiber cannot help: the 500 ms is inside one call (`read_blocks`) and the CPU spins there.

## 2. What is measured

Source: Pi reports of 11 Oct (`v0.2.5-115`) and 23-24 Oct 2026. The numbers are from [multitasking](../progress/multitasking.md) and [sd-stall-plan](sd-stall-plan.md).

| Fact | Value |
|---|---|
| Timeouts per covers pass after a rescan | 1 to 3 |
| Timeouts per Overbit load | 4 to 7 (load 4.6 s and 3.1 s, each 1.08 s over the rest after 0.5 s per timeout is removed) |
| Timeouts in about 200 Market read-backs | 0 |
| Retry after a timeout | works every time (`sd 17 retried, 17 timed out`) |
| Site of the timeouts (S0, 24 Oct) | `rdend`: `wait_transfer_complete` waits for the state machine to go back to idle (`sd sites: rdend 14`) |
| Not covered by that run | the other sites: command response, first data word, the controller's own `REW_TIMEOUT` |
| Trigger | not the WiFi reports alone: covers-only windows show timeouts too |

Not known: why the controller is slow to return to idle after the last block. Not known: whether the card, the controller state, the previous command (`CMD12`), or a read that follows a write causes it.

## 3. What was tried

| Date | Commit | Change | Result |
|---|---|---|---|
| 11 Oct | `1bbd7ab8` | Counters for retries and timeouts, cluster size at boot | Kept. This gave the numbers of section 2. |
| 11 Oct | `e701579f` | A log line for each failed transfer: which wait, which state | Reverted (`1a54d696`) with the rest of the series. |
| 11 Oct | `1abd2c65` | A failed try leaves the controller clean: state machine out of READWAIT or WRITESTART1 (`FORCE_DATA`), card status, `CMD12` and the wait for "tran", drop the words left in the FIFO. A failed command also sends `CMD12`. | Reverted (`08635c11`). |
| 11 Oct | `0b9947ad` | The first read try gives up after 0.1 s (SDTOUT 100 ms, our waits 150 ms, the SD spec value for SDHC). Then the two tries of 0.5 s of before. | Reverted (`b936db7e`). With the clean recovery above the Pi became unusable. The progress note is `bbcd93d0`. |
| 23 Oct | `2fc0ef96` (S0) | Measurement only: counters per site, first-word latency histogram, game load profile | Kept. It found the site `rdend`. |
| 24 Oct | `af7ec66f` (S1) | `wait_transfer_complete` waits 25 ms for a read and 250 ms for a write. | Kept. In the cold-boot cover scan the worst frame fell from 504.3 ms to 29.5 ms and the scan from 4129 ms to 407 ms. It hides the stall. It does not remove it. See section 4. |
| 26 Oct | (C6) | The covers job starts a new cover only after the selection rested 250 ms | Kept. It avoids reads while the user moves. It does not touch the fault. |

Why the 11 Oct attempt failed is not recorded in detail. The note says "the Pi became unusable". The change had three parts (shorter first try, clean recovery, log lines) in one test. The rule since then is one change per Pi test.

Ideas written in the notes and not tried:
- A gap after `CMD12`, as Linux does (`delay_after_stop`).
- Reads with `CMD23` (set block count) instead of `CMD12` to end them.
- `CMD12` right after the data, as Linux and Circle do, for "read not completed".
- Check `EDM_THRESHOLDS` in `sdhost_init`.
- A command that the card missed after the `stop()` of the previous read.
- A read that follows a write while the card still programs.

## 4. The side effect of S1: the WiFi can be lost

`sd_read` in `src/drivers/sd.c` moves the card to the Arasan (EMMC) controller for good when `sdhost_read` fails after its own retry. The WiFi chip uses the Arasan controller. After the move there is no controller for WiFi.

The plan in [sd-stall-plan](sd-stall-plan.md) section 2 has this rule: a soft timeout must never reach `fall_back`; only the third attempt uses the full spec timeout. S1 did not keep it. `wait_transfer_complete` uses 25 ms for the first try and for the retry. Two timeouts in a row now cost 50 ms and then call `fall_back`. Before S1 the same case needed two waits of 500 ms.

Event of 27 Oct 2026 (Pi Zero W, kernel `v0.2.5-1816-g988772b3`, RAM log of the run before, 673 s after boot):
1. Boot: `sd: SDHC card (sdhost)`. The WiFi joined the network and asked for an address.
2. In the menu: `config: cannot save (SD read error)` two times.
3. Then: `wifi: the SD card is on the Arasan controller (SDHOST failed): no controller left for WiFi`.
4. The Pi had no IP address, the covers were missing, the menu was slow. The log could not be saved to the SD card for the same read error.

Not proved: that the 25 ms retry caused the move. The log has no `sd_error()` text and no site counters at that time. The kernel before this one had the same driver code, and it connected to the WiFi in an earlier run. So the failure is intermittent. The 25 ms retry is the main suspect because it removed the margin that the 500 ms retry gave.

Open question for the owner: the S1 pass criterion "no more fallbacks than before" was written. Nobody counted the fallbacks on the Pi. Count them first.

Change on branch `claude/sd-reinit-before-arasan` (27 Oct 2026, not yet run on the Pi): `sd_read` and a single-block `sd_write` call `restart_sdhost` in `src/drivers/sd.c` after SDHOST fails twice. It writes the error text and the sites that moved to the RAM log, runs `sdhost_init` once, and repeats the transfer. The card moves to the Arasan controller only if that fails too. Look for these lines in `logs bootprev` after the next fault: `sd: sdhost read failed (...)`, `sd: sdhost started again`.

## 5. Rules for the next attempt

- Measure first. Get the text of `sd_error()` and the site counters at the moment of the fallback into the RAM log. The RAM log survives a reboot from the bridge (`line reboot`). The SD file does not, because the SD fails at the same time.
- Change one thing per Pi test.
- A shorter wait must not shorten the retry. The retry keeps the long wait. Only the first try may be short.
- A failed try must not move the card to the Arasan controller while the WiFi is on. Try to start SDHOST again first (`sdhost_init`) and read again. Move to Arasan only if that fails too.
- Do not reset the controller as part of the timeout change (the 11 Oct attempt).
- Check the result with the `menu idle:` line (`covers +x`, `sd retried`, `timed out`, fallbacks) and with 10 minutes of browsing that has no "SD read error".
- Test on the Pi. QEMU never times out.

## 6. Where to look for an outside solution

These leads are not checked. Mark each result here when you read it.
- Linux `drivers/mmc/host/bcm2835.c` (the SDHOST driver of the Raspberry Pi kernel): `bcm2835_wait_transfer_complete`, `delay_after_stop`, the data timeout `SDTOUT`, the use of `CMD23`, the reset after a failed command.
- Circle (bare metal, `lib/sd`): how it ends a multi-block read.
- Raspberry Pi forum and GitHub issues of `raspberrypi/linux`: search "sdhost", "timeout", "Pi Zero W", "READWAIT".
- The BCM2835 SDHOST register sheet: states of the `SDEDM` state machine, the meaning of `FSM_READWAIT`.
- Card side: the same card model on a second Pi. A second card on this Pi. If the timeouts follow the card, the cause is the card.

## 7. Code and notes

- Driver: `src/drivers/sdhost.c` (`wait_transfer_complete`, `read_blocks`, `sdhost_read`), `src/drivers/sd.c` (`fall_back`), `src/wifi/wifi.c` (the message).
- Plan with the stages S0 to S5: [sd-stall-plan](sd-stall-plan.md).
- History of the fibers and the first notes: [multitasking](../progress/multitasking.md).
- Wait budget remark: [multitasking-alternatives](multitasking-alternatives.md) item 11, [multitasking-report](multitasking-report.md).
