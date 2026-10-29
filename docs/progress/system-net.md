# System: Bluetooth, WiFi and network

Split from [system](system.md); open work and rules stay there.

### Bluetooth (`src/bt/`)
Classic (`bt.c`, `hci.c`, `btuart.c`): up to four HID pads (DualShock 4 the reference, light
set to the player colour) and a mouse; link keys in `bm/config.txt` (`bt_pad<n>`). LE
(`ble.c`, `smp_crypto.c`): one keyboard (passkey) and one mouse, keys `bt_kbd*`,
`bt_mouse*`.

### WiFi and network (`src/wifi/`, `src/net/`, `third_party/lwip`, `third_party/mbedtls`)
- BCM43430/CYW43436 over SDIO (`wifi.c`, `sdio.c`): firmware and NVRAM loaded from the SD;
  the chip's firmware does the WPA 4-way handshake (`sup_wpa`).
- Auto-join (`wifi_auto.c`, Pi and RGB30): two tries at boot, then from the menu in a fiber
  after 5 s, 15 s, 30 s, 1 min and every 2 min while down; `wifi_boot=0` turns it off.
  `wifi_leave()` before every reboot.
- lwIP with DHCP and SNTP (`net.c`), TLS with mbedTLS (`tls.c`), HTTP(S) (`http.c`): updates
  (`update.c`, `release.c`, signed releases), Market catalog (`catalog.c`), GitHub reports
  (`github.c`, `report.c`), 3D images (`img3d.c`), games' UDP and LAN (`lan.c`, `cartnet.c`).
- Web API for cartridges (`web_get`, `image_load`, `file_write`; [docs/api/system.md](../api/system.md)):
  `src/bm/webpolicy.c` has the rules (https only, 3 redirects of the same scheme, 1 s per host, body
  cap 4 MB `web_max_kib`, file names for `bm/web`, file cap 8 MB `file_write_max_kib`), `webget.c` runs
  one `http_request` in a fiber of its own (128 KB stack) that `web_poll` advances for 4 ms a frame
  (the TLS handshake still stalls one frame), `webimg.c` decodes JPEG, PNG and WebP into the sheet.
  `http.c` got `max_redirects`, `same_scheme`, a `before` hook and `info->location` for it. The
  folder is `bm/web` because the FAT writer makes 8.3 names only (`downloads` has 9 letters).
  `file_write` is synchronous by default; `file_write(name, data, true)` queues it (writer fiber, pieces of 32 KB, `file_state`),
  and the SD writer lock (`sdlock.c`) lets a game save pause it. `http.c` keeps one connection (keep-alive) and
  `web_get(url, {resume = "NAME.EXT"})` resumes with `Range` from `bm/web/NAME.PRT` (stages 3-5 in
  [multitasking](multitasking.md)). Tests: `make test-webapi`
  (PC, ucontext fibers, local server), QEMU `test_web_api`. Not tried on a Pi or an RGB30 with a
  real server (divergence D50). Not done: PNG/JPEG decoding in bands, progressive JPEG.
- A chat model for cartridges (spec 6; [docs/api/system.md](../api/system.md), "A chat model"; design in
  [ai-agent](../design/ai-agent.md)): `ai_ask` / `ai_reply` / `ai_info` in `src/bm/api_web.c`, the JSON in `src/bm/aichat.c`,
  the POST in `webget.c` (`web_start_post`: method, body and header lines copied, no redirect, the header copy wiped at the end).
  The settings are in `bm/ai.txt` (`src/net/aiconf.c`; protected; the old `ai_*` keys of `config.txt` move there).
  `ai_setup` saves them after `sys_ask`; `ai_pair` / `ai_pair_finish` fetch a relay's CA over plain http on the LAN only
  (`web_start_lan`, `web_lan_url_check`), check it against the pairing code (`src/net/aipair.c`) and keep it in
  `bm/ca-user.pem`, which `tls_load_roots` reads after `bm/ca.pem`. Tests: `make test-webapi` (the JSON, a fake OpenAI-compatible server,
  401, the refused redirect), `make test-https` (the relay's CA appended to `boot/ca.pem`, a POST to an IP address over TLS),
  QEMU `test_web_api` (`ai_info`, the checks of `ai_ask`, `ai_setup` and `ai_pair`, the key not in the log, `config.txt`'s
  `ai_*` keys moved to `bm/ai.txt` on the card); the pairing code from Python checked by the C code in `make test-https`. Not tried on a Pi or an RGB30 with a real
  provider (D50).
- PC link (`netcon.c` console, `netxfer.c` files) for `tools/bm_net.py`: a file is answered
  `QD` once received and checked, written from the menu in a fiber
  (`netxfer_write_tick`); one at a time, another gets `BY`. An open or suspended game is
  replaced when closed. A kernel goes the same way while the menu runs (`menu_here()`: a
  `netxfer_write_tick` in the last 250 ms; the answer after the write), else it is written
  at once with the network polled (`save_now`). Tests: `tests/net/test_netcon.c` (run
  plain, `menu`, `gone`), `check_bm_net_config.py`.
  Read ops (R-NET2, header with size 0 and crc 0): `T` takes a screenshot of the running game
  (`bm_shot_ask`; answer `OK` + len32 + name, `NG` after 3 s without a frame); `G` gets a file
  of `bm/shots`, or exactly `bm/BOOTPREV.TXT`, `bm/BOOTLOG.TXT`, `bm/LASTRUN.TXT` (R-NET4; bridge `logs NAME`) (answer `OK` + len32 + crc32 + data; `PA`, `NF`, `SZ`, `PW`). Tools:
  `bm_net.py --shot` (T then G), `--get`; `pi_test.py --shot`. Shared with the RGB30 but not
  yet proved there.
- Monitor line commands (`src/kernel/monitor.c`: `:` prefix from menu/serial, `bm_net.py IP --line "..."`):
  `gpu`, `b3d`, `play`, `set`, `save`, `render`, `room`, `stack`, `log`, `err`, `send`, `reboot`.
  The `err` command (R-NET1) prints the last cartridge error (`err: <text>` or `err: none`,
  via `bm_last_error()`) and any suspended cartridge (`running: <title>`), allowing remote
  tests to distinguish a running/clean state from an execution halted with error.
  Tests: `tests/qemu_test.py:test_monitor_line`.

