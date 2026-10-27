# The end of an SDHOST read: what other systems do

Status: 27 Oct 2026. Change R1 is on branch `claude/sharp-brown-qn7l6i`. It builds and passes the QEMU SD tests. It is not yet run on the Pi. QEMU never times out, so only the Pi can show the effect.

Scope: the 500 ms (now 25 ms) stall at site `rdend` on the Pi Zero W ([sd-stall-plan](sd-stall-plan.md) section 0). This file compares the end of a read in `src/drivers/sdhost.c` with six other drivers for the same controller. It gives one change to test and the next changes in order.

## 1. The difference

`read_blocks` in `src/drivers/sdhost.c` did these steps after the last data word:
1. `wait_transfer_complete(0)`: wait until the state machine in `SDEDM` is IDENT or DATA. Force DATA from READWAIT.
2. Only then `CMD12` (`stop()`) for a read of more than one block.

The S0 counters put every timeout at step 1 (`sd sites: rdend`).

A read of more than one block is `CMD18` without `CMD23`. That read is open-ended: the card sends blocks until it gets `CMD12`. So step 1 waits for an idle state before the command that ends the transfer.

| Driver | Multi-block read ends with | Waits for the idle state before `CMD12` |
|---|---|---|
| Linux mainline, `drivers/mmc/host/bcm2835.c` (`bcm2835_transfer_complete`) | `CMD23` before `CMD18` if the card supports it; otherwise `CMD12` at once | No. It waits for idle only when no `CMD12` is sent. |
| Linux Raspberry Pi, `bcm2835-sdhost.c` (`ALLOW_CMD23_READ 1`) | the same | No |
| Circle, `addon/SDCard/sdhost.cpp` (`ALLOW_CMD23_READ 0`) | `CMD12` at once | No |
| U-Boot, `drivers/mmc/bcm2835_sdhost.c` | idle wait after each block; READWAIT, WRITESTART1 **and READDATA** count as idle and are forced to DATA (commit `79fd08f`, May 2018) | It forces READDATA instead of waiting |
| FreeBSD, `bcm2835_sdhost.c` | `CMD23` before `CMD18`, and `CMD12` at once after the last word | No |
| NetBSD, `bcm2835_sdhost.c` | DMA; the generic layer sends `CMD12` | No |
| bm before R1 | idle wait, then `CMD12` | **Yes** |

No other driver waits for the idle state before the `CMD12` of an open-ended read.

## 2. Why this matches the measured facts

- The old software wait was 500 ms. The controller's own data timeout (`SDTOUT` in `set_clock`) is also half a second. A state machine that stays out of idle until a timeout gives a stall of exactly that length.
- The retry works at once. The retry path (`fail:`) sends `CMD12` first. That is the order of the other drivers.
- The fault follows long reads: covers (32 KB clusters) and game loads. Market read-backs gave none.
- The data were complete when the wait started. Each timeout threw away a correct read.

This is a hypothesis. Only the Pi run of R1 can confirm it.

## 3. Change R1 (on the branch, one change for one Pi test)

In `read_blocks`, send `CMD12` before the idle wait. The order is then the order of Linux, Circle, FreeBSD and NetBSD. The rest stays the same: the 25 ms wait, the retry, the site counters, the fall-back rule, writes. A read of one block (`CMD17`) does not change.

Check on the Pi:
- `menu idle: sd sites:` shows no `rdend` after 10 minutes of browsing and one Overbit load.
- `sd N retried, N timed out` stays at 0 or near 0.
- No `config: cannot save (SD read error)` and no move to the Arasan controller.
- Covers load with no data errors (a wrong cover shows a fault in the data).

Revert: `git revert` of the commit "SD read: CMD12 before the idle wait".

## 4. Next changes, in order (only if R1 leaves `rdend`)

R2. Force READDATA, as U-Boot does. In `wait_transfer_complete`, a read also forces DATA from READDATA (`0x2`). Every word is already out of the FIFO at that point. About 3 lines.

R3. `CMD23` before `CMD18`, as Linux mainline does for reads. The card then ends the read by itself and no `CMD12` is sent. Read the SCR at init (`ACMD51`, 8 bytes, one data read) and use `CMD23` only if bit 33 (`CMD_SUPPORT`, SET_BLOCK_COUNT) is set. Keep `CMD12` after an error. About 40 lines. Linux mainline uses `CMD23` for reads only. Do the same.

R4. A timeout at `rdend` is not a failed read. When all words are in and `SDHSTS` has no `CRC16` or `FIFO_ERR`, the data are good. Send `CMD12`, count the site, and return the data. Then a stall at the end can never reach `fall_back` in `src/drivers/sd.c`.

## 5. The structural answer of the other systems: DMA

The 500 ms cannot be cut short because bm polls the FIFO on the only core. Linux and NetBSD move the data with a DMA channel paced by the SDHOST DREQ and sleep until the DMA interrupt (NetBSD `sdhost_dma_wait`: `cv_timedwait`). Their timeout costs no CPU.

For bm this removes the blocker of layer B in [sd-stall-plan](sd-stall-plan.md) section 3 (B2): a parked fiber cannot overrun the 16-word FIFO when the DMA engine empties it. With DMA a read can park for the whole wait, at any point of the transfer, and the menu keeps drawing.

Cost and risk: a DMA channel that the WiFi, the audio and the screen do not use; control blocks in uncached memory; a cache clean before and an invalidate after each transfer on the ARM1176. Design this only if R1 to R4 leave a stall. It is the general fix for card latency, not for the `rdend` fault.

## 6. Relation to branch `claude/sd-reinit-before-arasan`

That branch changes `src/drivers/sd.c`: SDHOST starts again (`sdhost_init`) before the card moves to the Arasan controller. It also adds `docs/design/sd-read-timeout.md`. R1 changes only `read_blocks` in `src/drivers/sdhost.c`. The two changes do not touch the same lines. Test them one at a time on the Pi. The restart is a safety net. R1 aims at the cause.

## 7. Sources

- Linux mainline: `drivers/mmc/host/bcm2835.c`, functions `bcm2835_wait_transfer_complete`, `bcm2835_transfer_complete`, `bcm2835_request` (`use_sbc`).
- Linux Raspberry Pi: `raspberrypi/linux`, branch `rpi-4.19.y`, `drivers/mmc/host/bcm2835-sdhost.c` (`ALLOW_CMD23_READ`, `delay_after_stop`).
- U-Boot: `drivers/mmc/bcm2835_sdhost.c`; commit `79fd08f` "mmc: Unirqify bcm2835_sdhost and fix writes" (READDATA); commit `b112580` (the wait in ms).
- Circle: `rsta2/circle`, `addon/SDCard/sdhost.cpp`.
- FreeBSD: `sys/arm/broadcom/bcm2835/bcm2835_sdhost.c` (`MMC_SET_BLOCK_COUNT`, then `MMC_STOP_TRANSMISSION` after the last word).
- NetBSD: `sys/arch/arm/broadcom/bcm2835_sdhost.c` (`sdhost_dma_transfer`, `sdhost_dma_wait`).
- These are references only. No code was copied (licenses: Linux GPL, U-Boot GPL, Circle GPL, BSD).
