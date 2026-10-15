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
- PC link (`netcon.c` console, `netxfer.c` files) for `tools/bm_net.py`: a file is answered
  `QD` once received and checked, written from the menu in a fiber
  (`netxfer_write_tick`); one at a time, another gets `BY`. An open or suspended game is
  replaced when closed. A kernel goes the same way while the menu runs (`menu_here()`: a
  `netxfer_write_tick` in the last 250 ms; the answer after the write), else it is written
  at once with the network polled (`save_now`). Tests: `tests/net/test_netcon.c` (run
  plain, `menu`, `gone`), `check_bm_net_config.py`.
  Read ops (R-NET2, header with size 0 and crc 0): `T` takes a screenshot of the running game
  (`bm_shot_ask`; answer `OK` + len32 + name, `NG` after 3 s without a frame); `G` gets a file
  of `bm/shots` (answer `OK` + len32 + crc32 + data; `PA`, `NF`, `SZ`, `PW`). Tools:
  `bm_net.py --shot` (T then G), `--get`; `pi_test.py --shot`. Shared with the RGB30 but not
  yet proved there.
- Monitor line commands (`src/kernel/monitor.c`: `:` prefix from menu/serial, `bm_net.py IP --line "..."`):
  `gpu`, `b3d`, `play`, `set`, `save`, `render`, `room`, `log`, `err`, `send`, `reboot`.
  The `err` command (R-NET1) prints the last cartridge error (`err: <text>` or `err: none`,
  via `bm_last_error()`) and any suspended cartridge (`running: <title>`), allowing remote
  tests to distinguish a running/clean state from an execution halted with error.
  Tests: `tests/qemu_test.py:test_monitor_line`.

