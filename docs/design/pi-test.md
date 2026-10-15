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
| `shot OUT.png` | a screenshot of the running game (op `T`, then `G`), saved as OUT.png | OUT is a `.png` in a folder of the repo or in `~/bmk*` |
| anything else (`--config`, secrets, WiFi) | **refused** | |

Stop it with Ctrl-C (or `pkill -INT -f pi_bridge.py`): the socket is removed and nothing can be sent any more. After a session change the Pi's password (it is shown in clear in a terminal or a chat easily).

**`tools/pi_test.py`** runs a list of steps (kernel, cart + wait, `err` check, `--shot OUT.png`, `gpu`, `b3d`, free lines, restore the normal kernel) and prints `ok`/`FAIL` for each (the `--cart` step ends with the `err` check: FAIL if the cartridge stopped with an error) and `RESULT: PASS|FAIL` (exit 0/1, log in `out/pi_test.txt`). It needs WSL (Unix sockets).

```bash
python3 tools/pi_bridge.py 192.168.1.108                  # shell 1: the owner types the password
python3 tools/pi_test.py --kernel ~/bmk2/build/kernel.img --cart build/carts/pong.bm --wait 15 --gpu --restore ~/bmk/build/kernel.img   # shell 2
```

## What a test could not see (found 2026-10-14, closed by R-NET1 and R-NET2)

| Need | How it is seen now | Where in the code |
|---|---|---|
| The error of a cartridge that stopped | monitor line `err` prints `err: <text\|none>` and `running: <title\|menu>`; `pi_test.py --cart` ends with an `err` check (PASS/FAIL) | `src/kernel/monitor.c` (`err`), `bm_last_error()` in `src/bm/runtime.c` |
| The picture of the screen | transfer op `T` (screenshot of the running game), then op `G` (fetch the PNG): `bm_net.py IP --shot OUT.png`, `pi_test.py --shot OUT.png`, bridge verb `shot OUT.png` | `src/net/netxfer.c`, `bm_shot_ask`/`bm_shot_done`/`bm_shot_cancel` in `src/bm/runtime.c` |
| A file from the SD card | op `G`, only files of `bm/shots`: `bm_net.py IP --get bm/shots/NAME.PNG OUT` | `src/net/netxfer.c` |

Reports (`gpu`, `b3d`) already leave the Pi alone: it uploads them to the branch `reports` of `f-accomando/bm` with its GitHub token (`src/net/github.c`, `put_file` takes binary data). That channel is an alternative for pictures, with the token on the console and the files public in the repo: **not recommended for screenshots of work in progress**; the direct way (ops `T` and `G`, R-NET2) is.

## Development requests (kernel, branch `claude/net-test-*` from `bm-core`)

**R-NET1 — the last error of the cartridge. DONE.** The monitor line `err` (`src/kernel/monitor.c`) prints `err: <text|none>` (from `bm_last_error()`) and `running: <title|menu>`. `pi_test.py --cart` ends with an `err` check and prints PASS/FAIL, so "started" is no longer enough (the failure of 2026-10-14 was `attempt to call a nil value (global 'map3d_build')`, seen only on the screen). Test: `tests/qemu_test.py:test_monitor_line`.

**R-NET2 — a screenshot over the network. DONE (not a monitor line, as first designed).** Two ops of the transfer port (`src/net/netxfer.c`), both with size 0 and crc 0 in the header: `T` (take) asks the running game for a screenshot (`bm_shot_ask` in `src/bm/runtime.c`, the PNG is written in `bm/shots`) and answers `OK` + len32 + name, or `NG` if no frame came in 3 s; `G` (get) sends a file of `bm/shots` only, one level, no `..`, answering `OK` + len32 + crc32 + data (or `PA` path not allowed, `NF` not found, `SZ` too big, `PW` wrong password). Tools: `bm_net.py IP --shot OUT.png` (`T` then `G`) and `--get PATH OUT`; bridge/`pi.py` verb `shot OUT.png`; `pi_test.py --shot OUT.png`. Plain text network, home network only (as the console). The kernel code is shared with the RGB30 (`src/net/netxfer.c`, `src/bm/runtime.c`) but `T`/`G` are not yet proved on that console (divergence in `docs/spec/divergences.toml`). Tests: `make test-net` (`tests/net/test_netcon.c` runs the ops; `tests/net/check_bm_net_config.py` runs `bm_net.py` against a fake console).

**R-NET3 — a limited token for tests (decision for the owner, after R-NET1/2).** Today the console password lets anyone do everything (kernel, config, WiFi). A second secret `net_test_token` in `bm/config.txt` (a `_token` key is hidden by `config_list`) that only allows `line` (monitor lines from a short list), `send`/`play` and `get /bm/shots`, no `kernel`, no `config`: then an assistant could run tests without the owner's password at all. Needs a decision on the protocol (a flag in the request header) and on which lines are safe. Not to start before R-NET1 and R-NET2.

## Rules of the tools

- The password is never written to a file, a log, a command line the assistant types or the chat; `pi_bridge.py` takes it with `getpass` or from `BM_PASSWORD` of its own process.
- The bridge only runs the five requests of the table and only on files of the repo or `~/bmk*`.
- A failing client counts as a failing step (a test never answers PASS when it could not talk to the Pi).
