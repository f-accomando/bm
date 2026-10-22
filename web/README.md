# bm Web Player Mock

This directory contains a web mock interface for bm.

## How to Open

Run a local HTTP server in the `web/` folder:

```sh
cd web
python3 -m http.server 8080
```

Open `http://localhost:8080` in your web browser.

## Launcher Contract

The page calls `window.bmLauncher.start(cartUrl)` when the user clicks "Start bm".
The function accepts one string argument `cartUrl` pointing to the cartridge to run.
The future web player must initialize the WebAssembly bm runtime and attach its canvas to `canvas#bm-canvas`.
If `window.bmLauncher` is missing, the interface displays: "The bm player is not connected yet."

Mock only. Assets need the licence policy before real content.

## External loads

The page loads React 18.3.1 and ReactDOM 18.3.1 (MIT) from `cdnjs.cloudflare.com`. The page has no `fetch`, no storage, and sends no data. The only link is "Buy me a coffee" (a plain `<a>`).

## Cartridge reader (stage 1)

`bmcart.js` reads a `.bm` or `.b16` file in the page (header, CRC-32, sections, INFO lines, cover). The Start tab has an "Open a .bm file" input that shows the result. The file stays in the browser. Test: `node tests/webplayer/test_bmcart.js`. The plan is in `docs/design/bm-web-player.md`.
