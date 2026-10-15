# Testing on a real Pi from the PC, without typing every command

> Tools: [`tools/pi_bridge.py`](../../tools/pi_bridge.py), [`tools/pi.py`](../../tools/pi.py), [`tools/pi_test.py`](../../tools/pi_test.py), on top of [`tools/bm_net.py`](../../tools/bm_net.py) (network console, port 3333, transfers 3334).
> Written 2026-10-14 after a first test of a 3D map cartridge on a Pi Zero W (kernel `v0.2.5-358`, then a test kernel).

## What exists

**The bridge.** The owner types the console password once in `python3 tools/pi_bridge.py <ip>` (WSL, `getpass`; or `BM_PASSWORD` in the environment of that process only). The bridge keeps it in memory and listens on a Unix socket `~/.pi_bridge.sock` (mode 0600). A client, such as Claude Code, sends one request per connection; the bridge runs `bm_net.py` with the password and returns the answer with the password replaced by `***`. The client never sees the password.

| Request | Does | Limits |
|---|---|---|
| `line TEXT` | a monitor line (`gpu`, `b3d`, `send`, `play`...) | at most 200 characters from `A-Z a-z 0-9 space _ - . =` |
| `send FILE` / `play FILE` | the cartridge to the SD card / played at once | FILE is a file inside the repo or in `~/bmk*` |
| `kernel FILE` | a kernel written as `kernel.img`, then the Pi reboots (waited for) | same |
| `keys NAME` | the keys of a fixed table pressed on the Pi, one by one (`bm_net.py --keys`): `monitor` = `q`, `crash-dabt` = `Xa`, `crash-undef` = `Xu`, `crash-prefetch` = `Xb` | the name must be in `KEYS` of `pi_bridge.py`; no free text, no freeze; refused when a game or page has the keys. `crash-*` makes the Pi fall: a Pi Zero W does not restart by itself, unplug the power |
| anything else (`--config`, secrets, WiFi) | **refused** | |

Stop it with Ctrl-C (or `pkill -INT -f pi_bridge.py`): the socket is removed and nothing can be sent any more. After a session change the Pi's password (it is shown in clear in a terminal or a chat easily).

**`tools/pi_test.py`** runs a list of steps (kernel, cart + wait, `gpu`, `b3d`, free lines, restore the normal kernel) and prints `ok`/`FAIL` for each and `RESULT: PASS|FAIL` (exit 0/1, log in `out/pi_test.txt`). It needs WSL (Unix sockets).

```bash
python3 tools/pi_bridge.py 192.168.1.108                  # shell 1: the owner types the password
python3 tools/pi_test.py --kernel ~/bmk2/build/kernel.img --cart build/carts/pong.bm --wait 15 --gpu --restore ~/bmk/build/kernel.img   # shell 2
```

## What a test can NOT see today (found 2026-10-14)

| Need | Why not | Where in the code |
|---|---|---|
| The picture of the screen | the net console is text only; there is no screenshot request | `src/net/netcon.c`, `src/net/netxfer.c` (ops `S` send, `P` play, `K` kernel, `C` config: nothing to read back) |
| A file from the SD card | the transfer port only writes (no `get`) | `src/net/netxfer.c` |
| The error of a cartridge that stopped | `--play` answers once the cart is received and started; the error is drawn on the Pi's own screen | `bm_last_error()` is in `src/bm/runtime.c:5477` but no monitor line prints it |
| A screenshot taken on the Pi | exists, but only by key (`shot_req` -> `shot_grab()` writes a PNG in `/bm/shots`, `src/bm/runtime.c:7183`) | no way to ask it from the net, nor to fetch the file |

Reports (`gpu`, `b3d`) already leave the Pi alone: it uploads them to the branch `reports` of `f-accomando/bm` with its GitHub token (`src/net/github.c`, `put_file` takes binary data). That channel is an alternative for pictures, with the token on the console and the files public in the repo: **not recommended for screenshots of work in progress**; the direct way below is.

## Development requests (to plan with `bm-planner`; kernel, so branch `claude/net-test-*` from `bm-core`)

**R-NET1 — the last error of the cartridge.** A monitor line `err` that prints `bm_last_error()` (empty = no error) and what the Pi is running (`menu`, the cartridge's title). Files: `src/kernel/devtool.c` (the monitor lines), `src/bm/runtime.c` (`bm_last_error`). Verification: a cartridge that calls a nil function, then `bm_net.py IP --line err` shows the error text; QEMU test of the line. Value: `pi_test.py` turns "started" into PASS/FAIL (the failure of 2026-10-14 was `attempt to call a nil value (global 'map3d_build')`, seen only on the screen).

**R-NET2 — a screenshot over the network.** (a) a monitor line `shot` that sets the screenshot request the keys set (`shot_req`) so the running cartridge or the menu writes `/bm/shots/NAME.PNG` and prints `NAME.PNG`; (b) a transfer op `G` in `src/net/netxfer.c` ("get a file": the console sends the file of a path in `/bm/shots` only, same password) and `bm_net.py IP --get /bm/shots/NAME.PNG out.png`; (c) `tools/pi_test.py --shot out.png`. Files: `src/net/netxfer.c`, `src/net/netcon.c`, `src/kernel/devtool.c`, `src/bm/runtime.c` (`shot_req`), `tools/bm_net.py`, `docs/SCREENSHOTS.md`, `docs/spec/` (a new request tag: `docs/spec/specs.toml` + `consistency.py check`). Rules to keep: only files in `/bm/shots`; size limit as `MAX_SIZE`; plain text network, home network only (as the console). Verification: `make test-qemu -k net` for the new op, and on a Pi `pi_test.py --cart X --shot out.png` gives a PNG. Open decision: the RGB30 (`src/rgb30/ui.c`) must answer the same op.

**R-NET3 — a limited token for tests (decision for the owner, after R-NET1/2).** Today the console password lets anyone do everything (kernel, config, WiFi). A second secret `net_test_token` in `bm/config.txt` (a `_token` key is hidden by `config_list`) that only allows `line` (monitor lines from a short list), `send`/`play` and `get /bm/shots`, no `kernel`, no `config`: then an assistant could run tests without the owner's password at all. Needs a decision on the protocol (a flag in the request header) and on which lines are safe. Not to start before R-NET1 and R-NET2.

## Rules of the tools

- The password is never written to a file, a log, a command line the assistant types or the chat; `pi_bridge.py` takes it with `getpass` or from `BM_PASSWORD` of its own process.
- The bridge only runs the requests of the table and only on files of the repo or `~/bmk*`.
- A failing client counts as a failing step (a test never answers PASS when it could not talk to the Pi).
