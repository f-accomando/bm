# The bm site: play .bm games and download assets (design note)

Status: design only. No site exists. Stage 1 is not started. This note lists the facts that the site must respect, the
rules for its authors, the open decisions and a staged plan. It changes no code. Each fact has a source. A fact
with no source is marked "not verified" or "not decided".

User brief (25 Oct): keep a document for the web site. In the future, people play `.bm` games in the browser and
download `.bm` files and assets for the editors (`.bmm` and others). Respect the limits of bm for web pages, so that
bm Web can reach these resources.

See also: [bm-web-player.md](bm-web-player.md) (the browser player), [web-browser-cart.md](web-browser-cart.md) (bm Web),
[single-bm-and-adaptive-screen.md](single-bm-and-adaptive-screen.md) (the end of `.b16`), [../RISORSE.md](../RISORSE.md)
(resource files), [../../web/README.md](../../web/README.md) (the mock page).

## 1. Goal and users

| User | Wants | Path |
|---|---|---|
| (a) A visitor with a PC browser | Play a `.bm` with no install | The WebAssembly player: section 5 |
| (b) A visitor with a PC browser | Download `.bm` games and apps, and assets for the editors | Plain links: sections 2 and 4 |
| (c) A bm device (Pi Zero W, RGB30) | Reach the same resources and save them on the SD card | bm Web (`carts/bmweb`) and the Market |

Two ways reach a device. They differ:

- **The Market** (`src/kernel/market.c`) lists games and apps only. It installs a `.bm` or a `.b16` and a PNG cover. It reads
  the catalog `index.txt` and `index.sig` under the base address (`market.c:23`, key `market_url` in
  `docs/spec/config-keys.toml`; today a GitHub Pages address). The catalog is signed with ECDSA P-256 over SHA-256. The
  public key is built into the kernel (`keys/market-pub.pem`, `src/net/catalog.c:1-4`); `bm/market.pem` on the SD card adds
  a second key (`catalog_add_key`, `catalog.c:41`). A record has `id`, `title`, `author`, `version`, `license`, `about`, `kind`,
  `targets`, `file` (path, size, SHA-256) and `cover`. The kernel checks the size and SHA-256 of each file (`catalog.h`:
  `catalog_check_file`). Limits: 256 games, 256 KB of `index.txt`, 100 MB per file (`catalog.h:19-25`). The Market
  does not list `.bmm` or other resource files. Source: `src/net/catalog.c`, `src/net/catalog.h`.
- **bm Web** opens any `https://` page and saves a file in `bm/web` after the user presses A (section 3). It checks nothing
  beyond the rules of the kernel.

The Market has two channels today: `.bm` (the Pi) and `.b16` (the RGB30). The same `id` may exist once per kind
(`catalog.c`: `finish()`). The plan is to end `.b16`: one `.bm` format with a `targets` header (`docs/NEXT.md`, Queue,
"Remove the `.b16` system"; `single-bm-and-adaptive-screen.md`, section 6 step 6 and section 7). After that, the site
has one channel. Until then, the site must offer both editions of a game that has both (Overbit).

## 2. Asset types and where they go

Sources: `docs/BM-FORMAT.md` (resource files, `BMRES`), `docs/RISORSE.md` section 3,
`docs/PROJECT-LAYOUT.md` ("Where projects live on the SD card"), `src/kernel/sdmap_core.c:138-155`,
`src/bm/runtime.c:3486-3540` (`res_write`), `src/bm/webpolicy.c:86-98`, `scripts/bmres.py`.

| Type | Ext | Card folder | Who validates | Editors | Size limit |
|---|---|---|---|---|---|
| Game or app | `.bm` | `bm/` (RGB30) or `/carts` (Pi) (`market.c:24-28`); the Pi menu scans `/`, `/carts`, `/bm`, `/bme` | Kernel loader (header, CRC-32); Market: size and SHA-256 | none (a game is run) | None in the kernel; 100 MB in the Market |
| Game, RGB30 edition | `.b16` | `bm/` | as `.bm`; ends with SYS-8 | none | 8 MB when built (`single-bm...md` 2.1) |
| Project | `.bme` | `/bme` | The tool that opens it | The editors open the projects (`PROJECT-LAYOUT.md`) | not verified |
| Model | `.bmm` | `/bmm` | `res_write`: MESH needed, each section parsed as the loader does; `BMRES` | bm Mesh (`bm3d.lua:1523-1531`); bm Studio and bm Animator (plan in `RISORSE.md` 3) | up to 256 models per file; Lib reads files up to 8 MB (`lib.c:13`) |
| Image, sprites | `.bmi` | `/bm/lib` (no tab reads it now) | `BMRES` parse in `format.c` | bm Pixel, bm Studio (plan in `RISORSE.md` 3) | as above |
| Sound | `.bms` | `/bm/lib` | `BMRES`; AUDIO section | bm Sound (plan) | PCM at most 8388608 values (`BM-FORMAT.md`) |
| Tile map | `.bmt` | `/bm/lib` | `BMRES` | bm editor (plan) | as above |
| Palette | `.bmc` | `/bm/lib` | `BMRES`; SHEET8 of N x 1 pixels, N up to 256 | bm Pixel, bm Studio (plan) | small |
| Kit | `.bmk` | `/bm/lib` | `BMRES`; any mix of sections | all (plan) | as above |
| glTF binary | `.glb` | `/glb` is read by the tools (`bm3d.lua:1531`) | `glb.c` reads it; bm Studio and bm Mesh read it, never change it | bm Studio, bm Animator, bm Mesh | bm Web: see below |

Rules for all of them:

- A resource file has the magic `BMRES`, never a LUA section (`BM-FORMAT.md`). A device can receive it from anyone.
- Name: 1 to 8 characters of `a-z 0-9 _ -`, lower case, for `.bmm` in `/bmm` (`runtime.c:3495-3515`). The real title
  is inside the file (`RISORSE.md` section 2, rule 6). The site must publish a file under such a name.
- Every file has an INFO section with title, author, `license`, version and tags (`RISORSE.md`, rule 5).
- The Lib tab scanned `/bm/lib`, `/carts`, `/bm` and `/` for resource files and `.bm` files. It is removed (25 Oct 2026):
  no kernel code scans the card for resource files. The editors list their own files.
- `res_write` is for bm's tools only (`tool_mode`). It writes `.bmm` in `/bmm` and nothing else (`runtime.c:3513-3531`).

What bm Web can save today (`webpolicy.c:86-98`, `system.md` `file_write`, `carts/bmweb/src/files.lua`):

- Only into `bm/web`, by `file_write`, as an 8.3 name. The cart builds the name: up to 6 characters of the file name, 2
  characters of a hash and a 3-letter extension (`files.lua:name`). A `.bmm` keeps the extension `bmm`.
- Never `.bm`, `.bme` or `.b16`. A `.bm` must come by the Market. The site cannot give a `.bm` to bm Web.
- At most 8 MB per file (`file_write_max_kib`, up to 32 MB) and 128 files in the folder.
- bm Web opens a link as a page with a cap of 1 MB (`browser.lua:18` and `:293`). A larger file ends in an error. This is a real
  limit for downloads in bm Web. Keep each asset under 1 MB, or the cap must change (not decided).
- bm Web writes a note file beside each download (`files.lua:note`).
- No tool reads `bm/web` (`grep` of `src` finds only the write side). The user moves a file by hand.
- Done 25 Oct (host tests only, not tried on a device): `file_write` with a name ending in `.bmm` checks the bytes as a BMRES model (kind 1, a MESH section, the mesh check, the size cap), never replaces a file, and writes `/bmm/NAME.bmm`. bm Web recognises a model by its magic bytes and shows "saved in /bmm/NAME.bmm". No new permission prompt (`docs/api/system.md`).

## 3. Limits of bm Web and the kernel

All limits below are read from the code of this branch. "Cart" means `carts/bmweb/src`.

**Kernel `web_get`** (`docs/api/system.md`, section "The web"; `src/net/http.c`):

| Rule | Value |
|---|---|
| Scheme | `https://` only; address up to 1024 characters; no user name, no `#fragment` |
| Certificate | Chain to a root in `boot/ca.pem` (`tls.c:94`; the file is read from the config folder) |
| TLS | 1.2 only (`bm_mbedtls.h:65-68`, `OS-CAPABILITIES.md:25`). IPv4 only (`OS-CAPABILITIES.md:25`) |
| Redirects | At most 3; a redirect to `http://` is refused |
| Pause | 1 s between two requests to one host |
| User agent | Fixed: `bm/<version>`. No other header (`http.c:225-226`: `Accept: */*`, `Connection`) |
| Body cap | 4 MB by default; `web_max_kib` up to 32768; a body over the cap is an error |
| Concurrency | One request at a time; keep-alive to the same host |
| Protocol | HTTP/1.1; chunked bodies are read (`http.c:284`). No `Accept-Encoding` is sent and no `Content-Encoding` is decoded: the server must send the body as is. Cookies: none (`grep` finds no cookie code in `src/net`, `carts/bmweb/src`) |
| Resume | `Range` with `resume = "NAME.EXT"`; needs `Accept-Ranges: bytes` and a size. bm Web does not use `resume` (`grep` finds none in `carts/bmweb`) |
| Pictures | `image_load` reads baseline JPEG, PNG and WebP; not progressive JPEG; at most 4 megapixels. GIF is not a picture in bm Web: it is a download |

**The roots in `boot/ca.pem`** (21 on this branch; `grep '^# ' boot/ca.pem`): USERTrust (2), Sectigo (2), DigiCert (3),
ISRG X1 and X2, GTS R1 to R4, GlobalSign (2), Amazon (4), SSL.com (2). To check a host, run
`openssl s_client -connect HOST:443 -showcerts` and compare the last issuer with these names. A missing root gives
"certificate not trusted: no root in bm/ca.pem for ..." (`tls.c:221`).

**The cart** (`carts/bmweb/src`):

| Rule | Value | Source |
|---|---|---|
| HTML | 1 MB read, 20000 nodes, depth 64, 4096 bytes per tag | `html.lua:7-12`, `browser.lua:18` |
| Lines | 5000 per page | `layout.lua:12` |
| CSS | 64 KB per page, 4000 rules; at most 4 sheets of 128 KB | `css.lua:14-17`, `browser.lua:25,27` |
| Scripts | At most 6, 256 KB each; `type=module` is not run; tracker hosts are skipped | `browser.lua:26,28`, `:617` |
| JavaScript | ES2015 subset; no `async`/`await`, no generators, no modules; no `fetch`, `XMLHttpRequest`, `WebSocket` | `js.lua:1593`; `docs/progress/tools.md` (Web browser cart) |
| Forms | No POST (the cart only follows links; a form with `method=post` is not sent: not verified in detail) | `grep` of `browser.lua`, `page.lua` finds no POST |
| Pictures | 16 per page; each cut to 160x120 in a cell of 160x128; at most 200 KB per picture; loaded only when the user asks | `layout.lua:13`, `atlas.lua:4-9`, `browser.lua:19` |
| Types | `text/html` and `application/xhtml+xml` are pages; `text/plain` is shown; PNG, JPEG, WebP are pictures; an empty or `application/octet-stream` type is sniffed (PNG, JPEG, WebP, `glTF`, GIF, HTML); everything else is a download | `files.lua:kind`, `:sniff` |
| Pace | `GAP` 3 s between two requests to one host, or the `Crawl-delay` if longer (up to 30 s); `robots.txt` is read once per host and obeyed (group `bm`, else `*`) | `loader.lua:15,102`, `robots.lua:9` |

A gap between this note and the rest: `docs/progress/tools.md` says the sheets and scripts of a page wait 1 s. The loader
does not read `opts.gap` on this branch (`grep 'opts.gap' carts/bmweb/src/loader.lua` finds nothing). The `Crawl-delay`
and the 3 s gap apply to every request, sub-resources too. `docs/NEXT.md` (bm Web and poly.pizza) lists the fix (a).

No site name may be in the code. `docs/progress/tools.md`, Rules: "bm Web (`carts/bmweb`): no site name in the code, the
texts or the Market text." This note says "a third-party site" where a name would be needed.

Not verified on hardware: `web_get` against a real server on the Pi and the RGB30 (D50), the load time of a page, the
step budget of scripts (`docs/progress/tools.md`).

## 4. Rules for the authors of the site (checklist)

Numbers come from section 3.

1. Serve content and downloads as server-rendered HTML. A page must work with scripts off (F9 in bm Web).
2. Make each download a plain `<a href>` to a file with a clear extension. Send the right `Content-Type`:
   `model/gltf-binary` for `.glb`; `application/octet-stream` for `.bm`, `.bmm` and the other `bm` types (bm Web then
   sniffs and finds nothing, so it offers a download).
3. Keep each page under 1 MB and 20000 nodes (aim for 100 KB). Use at most 1 style sheet and 0 scripts for content.
4. Show at most 16 cards on an index page, one preview each (the picture cap). Split a long list into pages with
   plain "next" links. A page with 16 previews costs 16 requests; at 3 s each a device needs about 48 s (estimate, not measured).
5. Make a preview a PNG, JPEG (baseline) or WebP of 160x120 pixels, under 200 KB (aim for 20 KB). The mock uses 24
   PNG files of 128x128 and 10767 bytes in total (`web/assets`); the cart would cut 8 rows of each.
6. Give an index page for each type: games, apps, models, images, sounds, maps, palettes, kits, projects.
7. Give one stable URL per asset. Do not redirect more than 3 times. Never redirect to `http://`.
8. Use a short `robots.txt` with no `Crawl-delay`, or a small one (1 to 3). The loader waits the delay before each request.
9. Use a certificate that chains to a root in `boot/ca.pem` (section 3). Serve TLS 1.2. Serve an IPv4 address.
10. Serve files as they are: no `Content-Encoding`. Send `Content-Length` and `Accept-Ranges: bytes` for each large file.
11. Keep an asset to 1 MB for bm Web; larger assets (a `.bm` of several MB) go by the Market (100 MB).
12. Publish a SHA-256 for each file in its page. For the Market, use the signed catalog: the kernel checks the signature
    and each file (`catalog.c`). A signature for assets is not decided.
13. Name each file `1 to 8 of a-z 0-9 _ -` plus the extension. If the display name is longer, put it in the page as
    a separate "name" field.
14. Give a licence for each asset (SPDX id) on its page and in the INFO of the file. The mock says: "Mock only. Assets need the licence policy before real content"
    (`web/README.md:23`). `bm-web-player.md` section 6: "The site must not host a cartridge without a licence."
    The policy is not decided (section 7).
15. Add a JSON or plain-text catalog endpoint for tools. Not decided. If built, reuse the Market line format (`catalog.c`) to share code.

## 5. The player side (play in the browser)

Sources: `docs/design/bm-web-player.md` (stages, section 8), `web/README.md`, `docs/progress/tools.md` (Web player).

- **Contract.** The page calls `window.bmLauncher.start(cartUrl)`. Other calls: `startBytes(bytes, name)`, `stop()`. The canvas
  is `canvas#bm-canvas`. Without `bmweb.js` the launcher says the runtime is not built (`web/README.md`).
- **Size.** The limit is 16 MB for a page with its cartridges (`ROADMAP.md` WEB-1). The release in `build/web/player/` is 5 MB
  with 3 cartridges; the wasm module is 1.5 MB (0.57 MB gzip) (`bm-web-player.md` section 5, stages 2 and 6).
- **What runs.** Chaos Kitchen, Hero Stress (it replaced Stages 3D, removed 25 Oct) and a local `.bm` from a file input. Checks: `make test-wasm` (frames equal to the PC
  build), `make test-wasm-3d`, `make test-webplayer`. All other cartridges: not verified.
- **Rendering.** 60 Hz pacing (`BmPacer`), integer scaling on a canvas. 3D uses the ARM rasterizer `r3d.c`. Its speed in wasm is not measured.
- **Controls.** Keyboard: arrows, Space or J = A, K or X = B, U = X, I = Y, Enter = Start, Backspace or right Shift = Select, Q and
  E = L1 and R1, 1 and 3 = L2 and R2, Esc = leave. Gamepad API with the standard mapping (`bm-web-player.md` section 8).
- **Saves.** IndexedDB (`/sd/bm/save`, IDBFS). Written every 3 s and when the cartridge ends. They stay in the browser.
- **Audio.** AudioWorklet at 48 kHz, started by the click on "Start bm". The sound itself was not checked by ear; the 60 Hz rate
  in a visible tab, a real gamepad and the new worklet in a browser: not verified.
- **Not supported.** Network, recorder and assistant are stubs (`bm-web-player.md` section 2). A cartridge that waits for a
  network answer fails in the browser. Fibers need Asyncify.
- **Hosting of the cartridge.** `start(url)` fetches the `.bm` by the page's own origin or with CORS. The site must send
  CORS headers if the `.bm` is on another host (not verified; not tested).
- **Screens and `.b16`.** A `.b16` is 360x360. The player draws any listed size. The adaptive screen is decided
  (`single-bm-and-adaptive-screen.md` section 8, question 10): a game for 360x360 such as Yharnam runs at 640x360 with black bars of 140
  pixels on each side. A web page must show these bars too, until the screen modes (720x405, `safe_area()`, `aspect()`) exist.

## 6. The test mock today

- `web/`: a React mock page (React 18.3.1 from cdnjs with SRI), 24 placeholder PNG previews in `web/assets` (3 groups of 8:
  `2d_*`, `3d_*`, `snd_*`, 128x128), and the launcher contract (`web/README.md`). Previews and text are placeholders.
- Test assets for bm Web: `web/models/` holds an index page, four model pages (house, tree, crate, walker) with a `.bmm` and a 256x256 PNG preview each, and a `robots.txt` without a delay; `build.py` makes them (`--check` proves they are deterministic). They are the test fixture of this design: they let a Pi open an index page, load previews and save a `.bmm` to `/bmm`. Check them against section 4: the previews are 256x256 (the cart cuts a picture to 160x120), so make 160x120 previews for the site.
- Test of the player: section 5. Test of bm Web: `make test-web`.

## 7. Open decisions

For the user. Nothing here is decided.

1. **Hosting.** GitHub Pages (the Market already uses it: `market.c:23`; the player is "ready for GitHub Pages") or another host. The
   host must give a root from `boot/ca.pem`, Range requests, and control of `robots.txt` and `Content-Type`.
2. **Licence policy.** Which licences the site accepts, and who checks them (`FORMATS-AUDIT.md:152-159`: "Who owns the table?").
3. **Accounts and uploads.** None for now. The site is static.
4. **Signing of assets.** Only SHA-256 on the page, or a signed manifest like the Market (key, tool, who holds it).
5. **Catalog format.** The Market lines (`catalog.c`) with new kinds (`model`, `image`...), or a separate JSON file.
6. **Name and domain.** Which site name may be in the code. The rule says: none in bm Web, the texts or the Market text.
   Keep "a third-party site" in docs. Decide the site's own name apart from `carts/bmweb`.
7. **One catalog or two.** The Market and the site share one signed catalog, or the site keeps its own.
8. **The 1 MB cap of bm Web.** Raise `MAX_HTML` for a link to a file, or keep assets under 1 MB.
9. **Where bm Web saves.** `bm/web` today; `/bmm` for a `.bmm` is done (host tests); other types: not decided.

## 8. Staged plan

| Stage | Work | Done when |
|---|---|---|
| 1. Static site with test assets | Static pages with the test `.bmm` and PNG previews (`web/models/`), served over HTTPS with a root from `boot/ca.pem`, no `Crawl-delay`, section 4 rules 1-10 | A Pi opens the index in bm Web, loads at least 4 previews and saves a `.bmm` in `bm/web`. Not started; needs the Pi (D50) |
| 2. Real assets with licences | Decisions 1, 2, 4 and 6 taken; assets with SPDX licence and SHA-256; index per type; short names | Every asset has a licence, a stable URL and a hash; `python3 scripts/bmres.py list FILE` reads each resource file without error |
| 3. Player page with the full catalog | The player page lists the catalog and starts any `.bm` of it; downloads for `.bm` link to the Market flow; one channel after `.b16` ends | A listed `.bm` plays in a browser from a click, and the same `.bm` installs from the Market |

The poly.pizza findings (`docs/NEXT.md`, "bm Web and poly.pizza") apply to stage 1: a slow `Crawl-delay`, a missing root,
a refused user agent, more than 16 images and a client-rendered page are the causes of a failed load. The site avoids each one by section 4.

## 9. What was not verified

- Any load of a real site on the Pi or the RGB30 (D50). Page load times and the 48 s estimate.
- CORS for the player when the cartridge is on another host.
- That each editor opens each type (`RISORSE.md` is a plan; `FORMATS-AUDIT.md:119-120` says some openings are not built).
- A POST form in bm Web (no code found; not tested).
- The `.bmm` save in `/bmm` and the files in `web/models/` on a device (host tests only).
