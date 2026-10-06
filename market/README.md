# bm Market

Free games for **bm**, the bare metal console for the Raspberry Pi Zero
([f-accomando/bm](https://github.com/f-accomando/bm)).

- On the console: the **Market** tab, first in the menu. A downloads a game,
  checks it and installs it in `/carts`; A again plays it.
- From a PC: the catalog is on <https://f-accomando.github.io/bm-market/>;
  download a game and send it with `tools/bm_net.py IP --send game.bm`.

Everything is free: no accounts, no payments.

## Publishing a game

Open a pull request that adds one folder to `games/`:

```
games/my-game/my-game.bm     the cartridge (exactly one .bm)
games/my-game/info.txt       version, license, about
```

- **The folder's name is the game's id**: `a-z`, `0-9` and `-`, at most 23
  characters. It never changes: the console recognises updates by it.
- **info.txt**:

  ```
  version: 1.0
  license: MIT
  about: One line about the game, at most 120 characters.
  ```

  The **license is required** (MIT, CC-BY-4.0, CC0-1.0, ...): it says what
  others may do with your game. Only publish what you made or may
  redistribute; for content of others, say where it comes from in `about`
  or in a `CREDITS` line of info.txt.
- **Title and author** come from the cartridge's header (what bm Studio,
  the SDK or `scripts/mkbm.py --title --author` write): they are what the
  console shows and what names the save file, so keep them the same across
  versions, and two games cannot share both.
- **Updating a game**: the same folder, the new `.bm`, a new `version`.
- A `.bm` has no size limit of its own; GitHub refuses files over 100 MiB.

The check of the pull request (`mkmarket.py --check`) says what is wrong.
Once the pull request is merged the catalog is built again, signed and
published: the consoles see the game the next time they open the Market.

Locally, from a clone of bm next to this one:

```sh
python3 ../bm/scripts/mkmarket.py games --check
python3 ../bm/scripts/mkmarket.py games --add my-game.bm --version 1.0 --license MIT --about "..."
```

## How it works

- `games/` is the catalog. On every push to `main` the workflow
  (`.github/workflows/market.yml`) runs `scripts/mkmarket.py` of bm: it
  checks every game, writes `index.txt` (ids, titles, sizes, SHA-256 of
  every file), the covers (PNG) and `index.html`, signs `index.txt` with
  the secret `BM_MARKET_KEY` (ECDSA P-256) and publishes everything with
  GitHub Pages.
- The console checks the signature with the Market key built into its
  kernel (`keys/market-pub.pem` in bm), then each file with its SHA-256,
  before it writes anything to the SD card. Every game runs in bm's sandbox:
  Lua only; it writes only its own save and new `.bm` files in `/carts`,
  changed again only while it runs (never one that was there, another
  game); it cannot read the console's
  settings, so never its keys or passwords, nor use the services that spend
  them. The network (UDP, for online games), a report to the console's
  repository and the player's documents in `/docs` (an app like bm Write)
  only after the player says yes: the console asks the first time.
- The repository is public on purpose: the consoles download without an
  account, and authors propose games from their forks.

## Setting it up (once)

In bm, `scripts/market.sh` (or `./easy_install.sh market` from WSL) does
all of this, asking before each step, and then puts bm's games here. By
hand:

1. In bm: `scripts/market-key.sh` makes the key pair; commit
   `keys/market-pub.pem` to `bm-core` and build the kernel.
2. Here: Settings > Secrets and variables > Actions > New repository
   secret `BM_MARKET_KEY`, the whole private key file (or
   `gh secret set BM_MARKET_KEY -R f-accomando/bm-market < ~/.bm/market-key.pem`).
3. Settings > Pages > Source: **GitHub Actions**.
4. The workflow takes `scripts/mkmarket.py` and the public key from bm's
   main branch, `bm-core`; the variable `BM_REF` (Settings > Secrets and
   variables > Actions > Variables) can name another branch.

The games of the bm project come from bm itself: `scripts/market.sh` there
(or `make market-seed MARKET=../bm-market`, then commit and push here)
builds them and updates their folders.
