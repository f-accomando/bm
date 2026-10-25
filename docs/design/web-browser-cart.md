# Web browser cart (design study)

Status: the study of 22 Oct is kept below. Stages 1 to 5 are built (section 8 says what the
cart does and where it differs from the plan). All sizes and times are estimates. Mark each number
"not measured" until a Pi Zero W run exists; nothing is verified on hardware.

Goal: a `.bm` cart written in Lua. It shows HTML and HTML5 pages on bm. It has no JavaScript
engine. The first target site is <https://poly.pizza>: low poly 3D models, licences CC0 and CC-BY.
The Pi Zero W (one core, 512 MB RAM, RGB565) is the benchmark. The RGB30 must work too.

## 1. Facts about poly.pizza

Checked on 2026-10-22 with `curl` (public GET, 10 s between page requests, no cookies, no login,
about 12 requests in total, User-Agent `bm-research/0.1`). The files stayed in a temporary folder.

| Question | Result |
|---|---|
| Server-rendered or JS-only? | **Server-rendered.** It is a React app with server-side rendering. The first HTML already holds the content. `/search/Tree` returned 313 KB with 32 model links (`/m/<PublicID>`), 88 `<img>` tags, and the titles and author names as text. |
| What the HTML also has | Layout markup from Material UI (many `div` and `class` names), inline `<svg>`, a hidden `<img class="hidden">` per card (shown later by JavaScript), and `<script>` tags. A page works as a text and link list without JavaScript. |
| Embedded data | `window.__SERVER_APP_STATE__ = {...}` JSON in a `<script>`. The model page has `Title`, `ResourceID`, `PublicID`, `Tris`, `Downloads`, `Category`, `Tags`, `Licence` ("CC-BY 3.0"), `Creator.Username`. The home page has `popularSearches` with URLs. The parser can read this JSON; it is plain data, not code. |
| Page sizes | Home 91 KB, `/explore` 173 KB, `/search/Tree` 313 KB, model page 108 KB. These are large for a Pi. The browser must stream them (section 3). |
| List and search | `/explore`, `/explore/<Category>`, `/explore?sort=1`, `/search/<words>`. Cards are `<a href="/m/ID">` around a preview `<img>`, then title and `/u/<author>` link. The search box is a JavaScript input with no `<form>`: the cart builds `/search/<text>` itself. Paging: not found in the first HTML. Not verified. |
| Model page | `/m/<PublicID>`. The title is in `<title>` and in the JSON. The Download button is **not** an `<a>`. The page uses JavaScript for it. The cart builds the URL from `ResourceID` (below). |
| Preview images | Cards use `https://static.poly.pizza/<uuid>.webp` (lossy WebP, 709x418 on the model checked, 27 KB) or an asset preview `asset-previews/<n>.webp`. Model pages also name `<ResourceID>.jpg`. That file is **really a PNG**: 1220x720, RGBA, 286 KB, served as `binary/octet-stream`. The server sends no useful `Content-Type`. The cart must sniff the magic bytes. |
| Model download | `https://static.poly.pizza/<ResourceID>.glb` returned 200, 70 188 bytes, a GLB version 2 with 1 mesh, 2 materials, 1 node, no extensions, no texture image. `<ResourceID>.glb.br` is a Brotli copy (the page viewer uses it). The cart must use the plain `.glb`. `Cache-Control: max-age=604800`. |
| robots.txt | `User-agent: *`, `Crawl-delay: 10`, `Disallow: /api/`, `Disallow: /v1/`. Pages and `static.poly.pizza` are not disallowed. |
| Terms of service (`/docs/tos`) | "Limitations on Automated Use": no bots, no scraping, no "automated means, including spiders, robots, crawlers", unless a separate agreement or `robots.txt` allows it. It says "published interfaces" are allowed. The TL;DR says no scraping "to feed AI". Downloads follow the Creative Commons licence of each model (the CC-BY needs attribution). |
| Public API | The site links `/docs/api/v1.1`. That page is a JavaScript Swagger page, so its text is not in the HTML. `robots.txt` disallows `/api/` and `/v1/` for generic agents. **I did not call the API.** The base URL, the key rule and the limits are `not verified`. |
| Hosting and TLS | Cloudflare in front (`server: cloudflare`), HSTS. See section 1.1. |

Plain verdict: poly.pizza is **not** JS-only. A parser with HTML plus the embedded JSON can read the
lists, the search results and the model pages. The pictures are WebP. The kernel decodes them with `webp_decode565` ([webp-decoder.md](webp-decoder.md)).

### 1.1 Terms and policy risk (needs the user)

An interactive browser, driven by a person, is a user agent. A cart that crawls is a bot. The terms
bar bots and scraping. Two safe designs:

1. **Human pace.** The cart fetches only what the person opens, one page at a time, and waits at
   least 10 s between page requests to the same host (the `Crawl-delay`). It never prefetches
   links. It sends an honest `User-Agent` (`bm-browser/1.0 (+https://github.com/f-accomando/bm)`).
   It follows `robots.txt` (it reads it once per host and refuses disallowed paths).
2. **Ask the owner.** The owner of the site is a single developer (the footer links Discord and a
   contact address). Ask for written permission, or for an official API key, before the Market
   release. Without an answer, ship the cart as a general browser and do not name poly.pizza in the
   Market text.

Image previews (`static.poly.pizza`) load for the page being read, at human pace. A 10 s gap
between images makes a 32-card page unusable. So the first release shows **text cards with no
preview**, and loads one preview on demand (the card in focus). This is the honest minimum.

### 1.2 TLS check

On this PC the network is a corporate TLS proxy (Cato Networks). `openssl s_client` showed a
Cato chain, not the real Cloudflare chain. So the certificate chain and the issuer root are
**`not verified`** (the proxy hides them). What the handshake showed with the proxy:

- Protocol TLS 1.3 by default (`TLS_AES_256_GCM_SHA384`).
- With `-tls1_2` the server accepted `ECDHE-RSA-AES128-GCM-SHA256` and `ECDHE-RSA-AES256-GCM-SHA384`.

The bm side (`src/net/bm_mbedtls.h`, `src/net/tls.c`):

- TLS **1.2 only** (`MBEDTLS_SSL_PROTO_TLS1_2`). No TLS 1.3.
- ECDHE with RSA or ECDSA certificates, curves P-256, P-384, X25519, AES-GCM and ChaCha20-Poly1305,
  SHA-256/384/512, SNI, extended master secret. `MBEDTLS_SSL_VERIFY_REQUIRED`: the chain must end
  at a root in `bm/ca.pem` (on the SD card, built by `scripts/make-ca.sh`) and the name must match.
- A failed check gives the text `certificate not trusted: no root in bm/ca.pem for <issuer>`.

Verdict: **compatible, with one condition.** Cloudflare accepts TLS 1.2 with ECDHE and AES-GCM, so
the handshake should work. The condition is the root CA. Cloudflare sites use roots such as
Google Trust Services or DigiCert, depending on the certificate. The cart must find the real
issuer from a machine without the proxy, and `boot/ca.pem` must hold that root. Run
`openssl s_client -showcerts` outside the proxy, or run the cart once on the Pi and read the error
text. Both are `not verified`. TLS 1.2 is a risk only if Cloudflare drops it (not announced).

## 2. What bm gives a cart today

| Need | Today | Gap |
|---|---|---|
| HTTP/HTTPS from Lua | **No generic fetch.** `src/net/http.c` is a C client (GET/PUT/POST, redirects, chunked bodies, a body sink, https through `tls.c`). Lua reaches it only through `picture3d`, Market and update code. A cart has UDP (`udp_open`, `net_resolve`) and bmnet. | **Kernel work.** A new Lua API is the first stage that touches the kernel (stage 2a). |
| TLS | mbedTLS 1.2, see 1.2. Handshake blocks the cart (a loop with `net_wait_step`). | Cost of one handshake on the Pi: `not measured`. `multitasking-alternatives.md` item b lists it. |
| JPEG | `src/bm/jpeg.c`: baseline only, progressive refused. | Progressive JPEG on the web is common. |
| PNG | `src/bm/png.c`: `png_rgba`, inflate, RGBA output in one malloc. | A 1220x720 RGBA image needs 3.5 MB. Needs downscale on decode. |
| WebP | `src/bm/webp.c`: `webp_decode565`, lossy (VP8) and lossless (VP8L), no Lua call yet ([webp-decoder.md](webp-decoder.md)). | The site's cards are lossy WebP. Stage 0 is the Lua call `image_load`. |
| Pixels from Lua | `sset(x, y, c)` and the sprite sheet (up to 4096x4096), `sspr` with zoom. No "draw this RGB buffer" call. | Per pixel `sset` from Lua is slow. Add `image_load(bytes, maxw, maxh)` that decodes and writes into a sheet area. |
| Text | Three bitmap fonts: 8x16, 8x14, 6x12, 256 glyphs in code page 437. No Unicode, no proportional font. | The cart maps UTF-8 to CP437 and uses `?` for the rest. Layout is on a fixed grid. |
| Screen | `screen(w,h)`: 320x180 up to 960x540 (16:9). 640x360 with `font("6x12")` gives 106 columns by 30 rows. | Few lines of text per screen: use the small font. |
| Input | Pad buttons, stick, `keyp()` for typed text, `mouse()` with `mousep()` (a cart must call `mouse(true)`). Same API on Pi and RGB30. | None. The RGB30 has no keyboard: an on-screen keyboard is needed for the URL bar. |
| Lua memory | `stat(0)` reports Lua KB. The kernel's own state has `LUA_MEM_LIMIT` 64 MB (`docs/progress/lua.md`). The cart limit: `not verified`. | Measure on the Pi before stage 2. |
| Write to SD | `save(t, slot)` (a table, 32 KB, 8 slots). `cart_save`, `cart_write`, `cart_data` (project files). No "write bytes to a path". | A `.glb` needs a raw file write. New API `file_write(path, bytes)` limited to `bm/web/`. |
| 3D import | C: `src/bm/glb.c` `glb_to_model` (positions welded, winding fixed, PNG or JPEG base colour texture shrunk to 256x256, model framed, decimated to `faces` triangles). Used by `picture3d("take")`. Python: `scripts/meshy2mesh.py`, bm-forge `mesh` for untextured `.glb`. | Lua cannot call `glb_to_model` on a file from the SD card. Add `glb_import(path, opts)`. The poly.pizza GLB checked has no texture: colours come from material factors (`not verified` that `glb.c` reads them). |
| Multitasking | Fibers are cooperative. `fiber_slice()` yields when a slice ends. SD reads and FAT writes yield. `net_wait_step()` always yields. `timeslice(co, k)` limits Lua. A cart's `_update` runs at 60 Hz. | The work-budget steps (alternative A of the study on `origin/claude/multitasking-alternatives`) are not in `bm-core`. |

Note on PR 88 (`claude/web-mock-pr`): it adds a static React mock page under `web/` (a launcher
mock). It has **no HTTP server and no mock for bm**. The test server for the browser cart is the
Python `http.server` pattern of `tests/net/run_http_test.py` (a handler class on a free port). Reuse
that, not PR 88.

## 3. Design of the browser cart

Name: **`bmweb`**, title "bm Web". The name is general, because the cart is not tied to one site. Sources:
`carts/bmweb/` with `main.lua`, `bm.info` and `src/*.lua` (as `carts/kitchen`). The rule in
`CLAUDE.md` says "change only `.bme` projects". Existing cart sources in `carts/` are plain
`main.lua` folders. The user must confirm that `carts/bmweb/` is the right form (question Q1).
The pure parts (tokenizer, parser, layout) are plain Lua modules with no draw calls. They run
under host Lua in `tests/web/`.

### 3.1 Pipeline (streaming, low RAM)

```
fetch (kernel, chunks) -> tokenizer -> tree builder -> style -> block/inline layout -> line list
                                                         ^                              |
                                                         +---- link and image tables <--+
                                                                              draw only the visible lines
```

- **Tokenizer.** A state machine over chunks. It handles tags, attributes (quoted and bare),
  character references (`&amp; &lt; &gt; &quot; &#N; &#xN; &nbsp;` and about 30 named ones),
  comments, `<!doctype>`. Raw text in `<script>` and `<style>`: it skips `<script>`, it keeps
  `<style>` text for the CSS parser. Cap per token: 4 KB.
- **Tree builder.** A small stack with the implied end rules for `p`, `li`, `tr`, `td`, `option`.
  Unknown tags become inline `span`s. No full HTML5 error recovery. Depth cap 64. Node cap 20 000.
- **Elements supported.** `html head title body`, `h1`-`h6`, `p`, `div`, `span`, `br`, `hr`,
  `a`, `ul ol li`, `dl dt dd`, `pre`, `code`, `b strong i em u`, `blockquote`, `img`, `table tr th td`
  (basic grid), `form input button select textarea` (GET only), `nav header main footer section
  article` as blocks. Ignored: `script`, `noscript`, `iframe`, `video`, `audio`, `canvas`, `object`,
  `embed`, `svg` (drawn as a grey box with its `aria-label`), `link`, `meta` (the cart reads
  `<title>` and `<meta charset>` only).
- **Embedded JSON.** A `<script>` that starts with `window.__SERVER_APP_STATE__ =` is parsed as
  data by a small JSON reader (the cart does not run it). This is a site profile feature (stage 4).
- **CSS (minimal).** Inline `style=""` and `<style>`; selectors by tag, `.class`, `#id` and a
  descendant pair. Properties: `color`, `background-color`, `display` (`none`, `block`, `inline`,
  `inline-block` as inline, `flex` as block), `margin`, `padding` (one value or four), `font-weight`,
  `text-align`, `text-decoration`, `visibility`. The cart ignores `position`, `float`, `flex`
  rules, `@media`, `var()`, `calc()` and units other than `px`, `em`, `%`. Cap: 64 KB of CSS per page.
  Tailwind-style class names (this site uses them) are not interpreted: the cart uses its own
  default sheet and only honours `hidden`/`display:none`.
- **Layout.** Block and inline flow only. Fixed-width font: a text run is `width = chars * 6` px.
  Line breaking at spaces. One pass produces a **line list**: each line holds a y position, a
  height and a list of runs (text, colour, style, link id, image id). The tree is dropped as soon as
  a block closes (streaming): the cart keeps the line list, the link table and the image table.
  Tables: column widths are the largest cell text, cut to the page width, cells stack when too wide.
- **Draw.** Only the lines in `[scroll, scroll + screen]` are drawn. A page of 300 KB of HTML gives
  about 2 000 lines at 24 bytes each (estimate 100 KB of Lua tables with strings interned).
- **Scroll.** Up/down moves one line, L/R shoulder or PgUp/PgDn one screen, mouse wheel three lines.
- **Links.** The pad moves focus to the next or previous link in reading order (Tab and Shift-Tab on
  a keyboard). A confirms. The mouse clicks. The status bar shows the full URL of the focused link.
  Relative URLs resolve against the `<base>` or the page URL (RFC 3986 subset, tested).
- **Forms.** `<form method=get>` with text inputs, selects and a submit button builds
  `action?name=value&...`. POST forms show "not supported". Password inputs are refused.
- **URL bar.** Shows the URL and a lock icon for a verified https page. Edit with the keyboard or
  the on-screen keyboard (RGB30). Input with no scheme is `https://`.
- **History.** Back and forward lists of up to 32 URLs with their scroll offset. A page body is not
  cached in RAM; the cart reloads it. Optional later: cache on the SD card under `bm/web/cache/`.
- **Images.** The image table lists `<img>` with width and height from attributes. A placeholder
  box (grey, with the `alt` text) is laid out at once. A loader step fetches one image at a time,
  nearest first to the focus. It decodes (PNG, JPEG baseline and WebP), shrinks to at
  most 160x120 and writes to a free area of the sprite sheet. Cap: 16 images per page, 200 KB per
  image, 4 MB total decoded, GIF and SVG skipped. Cards with no usable format stay as a placeholder.
- **Downloads.** A link that ends in `.glb` or answers `model/gltf-binary` (or a body that starts
  with `glTF`) opens a "Download?" box with name, size and URL. After A: stream to
  `bm/web/<safe name>.glb` (name cleaned to `[A-Za-z0-9._-]`, 8.3-safe on FAT). Cap 8 MB.
  Then a second box offers "Open in viewer" (section 3.4). The page shows the licence from the
  JSON and the cart writes `<name>.glb.txt` next to the file with the URL, author and licence
  (the CC-BY attribution).

### 3.2 Safety

- Schemes: `https://` only by default. `http://` needs a setting (`bm/config.txt` key `web_http=1`)
  and shows "not secure" in red. `file:`, `data:`, `javascript:`, `ftp:` and the like: refused,
  no request, a visible message.
- Certificate errors are shown in full (the `tls.c` text) and stop the load. There is no "continue
  anyway" button in v1.
- Redirects: at most 5, `https` to `http` downgrade refused, a loop (same URL twice) stops with a
  message. The final URL is shown.
- Caps: HTML 1 MB (stop and mark "page cut"), images as above, one request at a time to the same
  host, a gap of 10 s between page requests to `poly.pizza` (the crawl delay), a 15 s read timeout.
- No cookies, no credentials, no `Authorization`, no `Referer`, no login forms, no password input.
  The cart sends `User-Agent`, `Accept`, `Accept-Language`, `Connection: close`.
- Content is data. The cart never runs a script from a page, never uses page text as Lua, never
  builds a file path from page text without cleaning it, and never writes outside `bm/web/`.
- Downloaded files stay in the download folder `bm/web`. The kernel parses them with C code (`glb.c`) that
  has size checks. A fuzz test of `glb_to_model` with truncated files is part of stage 5.
- Privacy: the URL bar shows what leaves the console. No history is sent anywhere.

### 3.3 Multitasking fit

A cart gets one `_update` and one `_draw` per frame. The work must be split into steps, as in
alternative A of `docs/design/multitasking-alternatives.md` (on `origin/claude/multitasking-alternatives`):

- The tokenizer, the layout and the image decode are Lua coroutines. The cart resumes one with
  `timeslice(co, k)` so that a slice costs at most about 4 ms. The page appears while it loads.
- The fetch is a **kernel job** that fills a ring buffer (a fiber that calls `http_request` with a
  sink). The Lua API polls it: `web_poll(id)` returns the bytes that arrived. The TLS handshake and
  ECDSA verify still block one slice (item b of the study). Until restartable ECC lands, the cart shows
  "connecting" and the screen may freeze for the handshake: `not measured`, expect 0.5-2 s on the Pi.
- Image decode (PNG inflate, JPEG IDCT) runs in the kernel in bands of rows, one band per call, as the
  study proposes for cover decode.

If the work-budget steps are not in `bm-core` when stage 2 starts, the cart uses `timeslice` for its
Lua and accepts a freeze for the handshake and the decode of one image (about 100-300 ms, estimate).

### 3.4 Hand-off to the 3D path

Two routes, both on the console:

1. **Import into a project.** `glb_import(path, {faces=, height=})` calls `glb_to_model` and returns
   `{record, flat, texture, nv, nf, textured}` like `picture3d("take")`. The cart then calls
   `cart_data(8, ...)` or offers "Open in bm Studio" through `cart_tool` with the file path.
2. **Quick view.** The cart shows the model with `mesh()` and `model()` in a small 3D scene (orbit with
   the stick). It is a 3D viewer built from `docs/api/light3d.md`.

Limits of `glb.c` for poly.pizza files: positions are welded, one mesh set per scene, faces are cut to
`max_faces`. Vertex colours, skins and animations: `not verified` (poly.pizza GLBs may carry them).
Test with five real files in stage 5 and write what is lost in `docs/progress/tools.md`.
On the PC, `bm-forge` (`mesh` command) is the reference for the same `.glb` files.

## 4. Staged plan (one PR per stage, each with a test)

| Stage | Content | Test | Lines (est.) | Lua RAM (est.) |
|---|---|---|---|---|
| 1 | `carts/bmweb/src/html.lua` (tokenizer, tree), `css.lua`, `layout.lua`, `url.lua` and a host-Lua runner. Draw to a text dump: lines with runs, links, image boxes. No network, no kernel. | `tests/web/run.py` with `tests/web/fixtures/*.html` (a simple page, tables, lists, bad nesting, entities, a trimmed copy of the poly.pizza search page, an oversize page) and expected dumps. Runs in the host Lua already used by tests. | 1 200 Lua + 300 test | 100-300 KB for a 300 KB page |
| 2a | Kernel: Lua `web_get(url, opts)` with `web_poll`, `web_cancel`, `web_info` (status, final URL, type, error). Wraps `http_request` in a fiber. Respects the rules in 3.2 (scheme, redirects, caps). | QEMU test with the `http.server` mock of `tests/net/run_http_test.py`: 200, 301 loop, 404, chunked, 2 MB body, timeout. | 350 C | 64 KB buffer ring |
| 2b | Cart: URL bar, fetch, render, scroll, link focus, history, error pages, cert error text. | QEMU: the cart loads `/index.html` from the mock, a scripted key sequence follows a link, goes back, and a screenshot (`shot`) matches a reference (`tests/shot/`). | 700 Lua | up to 3 MB with the page |
| 3 | Kernel `image_load(bytes, maxw, maxh)` (PNG, JPEG, banded decode, shrink) and WebP VP8 decoder (own PR if large). Cart image loader and cache. | Host test of the decoder on fixtures (PNG, baseline JPEG, lossy WebP from poly.pizza `t.webp`); QEMU page with 4 images. | 250 Lua + 150 C glue (+ 1 200 C for WebP) | 4 MB cap on decoded images |
| 4 | Site profile `poly.pizza`: read `__SERVER_APP_STATE__`, grid of cards (title, author, tris, licence), `/search/<text>` from the search bar, category list, model page with licence and Download button; crawl delay and `robots.txt`. | Host test on saved pages (fixtures of home, search, model pages); mock server serving them. Live site check is manual, by the user. | 500 Lua | 1 MB |
| 5 | `file_write` (to `bm/web/` only) and `glb_import`; download box; `.glb.txt` licence note; hand-off to the viewer and to Studio. | Mock serves a real small `.glb`; QEMU checks the file on the SD image and the model counts; fuzz test of truncated `.glb`; bm-forge `validate` on the result. | 300 C + 400 Lua | 8 MB download buffer streamed to the SD, not held |

Total estimate: about 3 100 Lua lines, 700 C lines (+1 200 C for WebP). Run only the tests of the
stage (CLAUDE.md "Selective tests"). Each stage ends with the closure steps of `CLAUDE.md` section 3
(`docs_audit.py score`, `consistency.py check`; Lua API changes go into `docs/api/` and
`docs/spec/specs.toml`).

Recommended **stage 1 scope**: only the pure Lua parser, CSS subset and layout, with the host runner
and 8 fixtures, including one trimmed poly.pizza search page. It needs no kernel change, no network,
and no decision about the site terms. It proves the layout memory and speed on the Pi (a bench cart is
stage 1b, optional).

## 5. Risks

| Risk | Effect | Answer |
|---|---|---|
| Site terms bar bots | Takedown or ban of the cart's traffic | Human pace, `robots.txt`, honest UA, ask the owner (1.1). |
| Root CA missing in `bm/ca.pem` | Every https page fails | Find the root off-proxy, add to `boot/ca.pem`, show the error text. |
| TLS 1.2 dropped by a site | Page fails | Document. A TLS 1.3 port is a separate large task. |
| WebP only previews | No pictures on cards | PNG on model pages works; WebP decoder in stage 3 or text cards. |
| Pi RAM and speed | Page of 300 KB too slow | Streaming layout, line cap 5 000, "page cut" notice. Measure in stage 1b. |
| Handshake freeze (0.5-2 s, estimate) | Cart seems hung | "Connecting" screen drawn before the call; restartable ECC later. |
| CP437 font only | Accents become `?` | UTF-8 to CP437 map for Latin-1; unknown glyphs `?`. Real fix: a bigger font (outside this study). |
| Page JavaScript needed | Empty pages on JS-only sites | Show a notice "this page needs JavaScript". Out of scope. |
| Lua API for fetch is new | Kernel stage before any cart test on QEMU | Stage 2a first. Host tests (stage 1) do not wait for it. |
| Hardware | All timings are estimates | `not verified on hardware` until the owner runs the cart on the Pi and RGB30. |

## 6. Decisions of the user (22 Oct 2026)

- Q1. Decided: the source is a `.bme` project (Dev tab). Stage 1 Lua modules stay in `carts/bmweb/` until the project exists; port them into the project at stage 2b.
- Q2. Decided: bmweb is a generic web browser. Do not name poly.pizza in the cart, the Market text or the code. Use an honest User-Agent, human pace (one request per user action), `robots.txt` respected, https only by default. poly.pizza stays a test target by hand.
- Q3. Decided: develop the kernel API: `web_get`, `image_load`, `file_write` (new Lua API, with docs and specs).
- Q4. Decided: develop the WebP decoder (about 1 200 lines of C, host-tested).
- Q5. Decided: one Pi Zero W run per stage 2 and stage 5. The assistant does not use the Pi password: the user runs these Pi checks and copies the numbers back.
- Q6. Decided: name `bmweb`; `.bm` edition first. With a single `.bm` (PR 101) no separate `.b16` is planned.

## 7. Evidence log

- `curl --ssl-no-revoke` (Windows schannel could not check revocation behind the proxy): `/`,
  `/robots.txt`, `/search/Tree`, `/docs/tos`, `/docs/api/v1.1`, `/explore`, `/m/6Yjt8nIwLsD`, then
  three `HEAD` and three GET on `static.poly.pizza/e1349d8c-4037-4245-bdc2-3b79c417828f.{jpg,webp,glb}`.
- Parsing with Python: link and image counts, the JSON state, WebP header (VP8 lossy 709x418), PNG
  header of the `.jpg` (1220x720 RGBA), GLB header (version 2, 70 188 bytes).
- `openssl s_client` (proxy chain, TLS 1.3 and forced 1.2).
- Code read: `src/net/bm_mbedtls.h`, `tls.c`, `tls.h`, `http.h`, `src/bm/jpeg.h`, `png.h`, `glb.h`,
  `docs/api/*.md`, `tests/net/run_http_test.py`, the multitasking study and PR 88 file list.
- Not done: any build, any QEMU or hardware run, any API call, any login.

## 8. What is built (stages 2b to 5)

The cart is the `.bme` project `carts/bmweb/` (`main.lua`, `src/*.lua`, `bm.info`). `carts/bmweb/build/build.py`
joins the modules into one script (a cartridge has one). `make build/carts/bmweb.bme` (and `.bm`) builds it.
It is a generic browser: no site name is in the code. The user types or picks an address.

| Module | Job |
|---|---|
| `loader.lua` | One request at a time. A gap of 3 s between two requests to one host (more if `robots.txt` has a longer `Crawl-delay`, at most 30 s). It reads `robots.txt` once per host before the first other request and refuses a disallowed path. No prefetch. |
| `robots.lua` | Parser: group `bm`, else `*`; longest rule wins, `*` and `$` in a path; `Crawl-delay`. |
| `browser.lua` | State: history (32), bookmarks (32, in `save` slot 1), focus on links and fields, GET forms, the body laid out in slices of 6 ms a frame, pictures on demand, the download prompt. |
| `view.lua` | Screen 640x360 with the 6x12 font; input by pad, keyboard and mouse; an on-screen keyboard for the address and for text fields. |
| `files.lua` | Response kind (HTML, text, picture, file) from the Content-Type or the first bytes; the 8.3 name of a saved file (`STEM6` + 2 hash characters + `.EXT`, never `.BM`, `.BME` or `.B16`); the note text. |
| `atlas.lua` | 16 picture slots of 160x128 on a 640x512 sheet (`cart_sheet`). A picture is cut to 160x120. |
| `cp437.lua` | UTF-8 to code page 437, one byte per code point. |

Differences from the plan:

- **Stage 4 has no site profile.** Decision Q2 bars a site name in the cart. Stage 4 is the generic part: `robots.txt`,
  the pace, bookmarks and GET forms (a search box works through its `<form>`). The `__SERVER_APP_STATE__` reader is not built.
- **Pictures load when the user asks** (X), one at a time, nearest the top of the view first, 200 KB each. An opened picture
  (a link to a PNG, JPEG or WebP) shows at once.
- **A file is fetched first, then asked about.** The kernel has no HEAD request, so the file is in the kernel result
  (not in Lua memory) when the box "Save this file?" opens. A writes it with `file_write(name, id)` and a note
  `NAME.TXT` (`NAME.NTE` for a `.TXT` file) with the address, the page it came from and the size. B drops it.
- **No `glb_import` and no 3D viewer** in the cart. The kernel API stays as it is. A saved `.glb` is opened with the Dev tools.
- **Only `https://`.** The kernel refuses `http://`; the key `web_http` of section 3.2 is not used.
- The redirect limit is the kernel's 3 (not 5). The page cap is 1 MB; a file is capped by `web_max_kib` (4 MB).

Tests: `make test-web` (PC, mock kernel, `tests/web/cart.lua`) and QEMU `test_bmweb` (the Pi kernel, no network).
Keys and the Pi run are in [progress/tools.md](../progress/tools.md).

## 9. Mouse, keyboard, CSS, HTML5 and JavaScript (2026-10-24)

The decision of section 1 ("no JavaScript engine") is replaced: the cart runs page scripts with its own engine.

| Part | What it does |
|---|---|
| Input (`view.lua`) | The mouse and the keyboard are the main input. A tool bar has back, forward, reload (stop while loading), home, pictures, scripts and bookmark buttons. The search bar is a box in the middle of the page, like Spotlight: it is open at the start, Ctrl+Space (or Y, or a click in the address box) opens it, Up and Down choose a bookmark or a visited page that matches, Enter opens, Esc closes. Ctrl+L is not used. The mouse hovers a link (its address shows in the status line), clicks, scrolls with the wheel and drags the scroll bar. A right click goes back. The pad keeps its buttons. The on-screen keyboard shows only if no keyboard or mouse was used. The hint line follows the last device. |
| CSS (`css.lua`) | Selectors `>`, `+`, `~`, `[attr]`, `:first-child`, `:nth-child`, `:not`, `:root`; `var()`, `calc()`, `hsl()`, `@media` for a 640 px screen, borders, `width`, `text-transform`, `white-space`. External style sheets (`<link rel=stylesheet>`) are fetched before the layout. |
| HTML5 (`html.lua`, `layout.lua`) | Semantic elements, `details`, `dialog`, `fieldset`, `video`, `audio`, `iframe` and `canvas` boxes (video, audio and iframe link to their source), `progress`, `meter`, `picture`, `srcset`, `data-src`, input types as text fields. |
| Engine (`js.lua`) | ES2015 subset in pure Lua, compiled to closures: classes, arrow functions, destructuring, template strings, regular expressions, `Map`, `Set`, `Promise`, `JSON`, `Date` (UTC). A step budget stops endless loops. |
| Page world (`jsdom.lua`) | `window`, `document`, elements (tree, attributes, `style`, `classList`, `innerHTML`), events with bubbling, timers, `localStorage` in memory, `location`, `history`, `IntersectionObserver` (reports at once). No network. |

A page with scripts is built as a whole tree (no streaming), the scripts run on it in document order, and the layout comes after. A change from a timer or an event lays the page out again (at most every 0.25 s). Page speed on the Pi Zero W is `not measured`.

See also: [bm-site.md](bm-site.md), the design note of the site that bm Web must be able to reach (limits and rules for its authors).
