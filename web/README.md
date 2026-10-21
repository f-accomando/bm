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
