# Game proposals

This file collects game ideas before they become projects. Each proposal has a status. A proposal that starts development gets its own `docs/progress/<game>.md`.

| # | Proposal | Status |
|---|---|---|
| 1 | [Home Compiler](#1-home-compiler-working-title): a programmer's desk, 1977 to the near future | idea, base agreed 2026-10-28 |

---

## 1. Home Compiler (working title)

### Pitch

The player is a student who programs from home. The view is always one room: a desk against a wall, and the computer. The day starts when the player turns the computer on. The day ends when the player turns it off.

The player takes jobs, writes the programs, and earns money. The money buys better computers. Time passes from 1977. The game starts calm and nostalgic. It grows stranger as the technology grows.

### Tone arc

The tone changes in steps. The loop never changes.

| Phase | Years | Feeling |
|---|---|---|
| 1. Home hobby | 1977 to 1985 | Calm, warm, nostalgic. A desk, a lamp, a cassette. |
| 2. Office at home | 1985 to 1995 | More jobs, more pressure, more money. |
| 3. Connected | 1995 to 2008 | Internet, email, orders by computer, a bank account on the PC. |
| 4. New markets | 2008 to 2025 | Stocks, crypto, a mini server on the shelf. |
| 5. Strange machines | 2025 and later | AI helpers, Frankenstein computers built from parts, a Serial Experiments Lain mood. |

Rule: each phase adds objects and tools to the same loop. No phase replaces the loop. The end year (about 2040) is not decided yet.

### Fixed view and graphics

- The screen shows the desk and the wall at all times. The camera does not move.
- Every option is an object in the room with its own 3D model, drawn live with a pixel shader (see "Live 3D room"). The menu is the room. Examples: the mailbox letters, the phone, the magazines, the calendar, the cassette or floppy box, the coffee mug, the window (time of day), the shelf.
- A selected object shows a highlight and a short animation. The text appears on a paper or on the monitor, never as a bare text list over a picture.
- The room changes with the player's money and the years: new lamp, new chair, posters, a second shelf, a window view.
- Pad control: the D-pad moves between objects. A opens, B goes back.

### Live 3D room

The objects are low-poly meshes, drawn live by the bm3D engine. No object is pre-rendered. The player can place and rotate each object to any angle.

The pixel look comes from the engine, with no new feature:

| Look | bm tool |
|---|---|
| Hard light steps | `cel3d(bands, ...)` with flag 256 on each `draw3d` (3 or 4 bands) |
| Dark outline | `cel3d` outline colour and flag 512 |
| Dithered shade | `dither3d`, and smooth faces (flag 4) |
| Chunky pixels | Run the cartridge at 320x180 (the console scales it by 2) |
| Flat colours per era | Mesh colours and light colours chosen per year |
| Lamp and window light | `lamp3d` and the sun; the desk lamp is a real lamp in the scene |
| Contact shadows | `draw3d` flag 8 and `shadow3d` |

What live 3D gives that a pre-render does not:

- Free rotation of every prop, not 8 or 16 angles.
- A camera that moves. The room view is the default. When the player works, the camera moves in until the monitor fills the view.
- The light follows the time of day and the lamp. Shadows move with them.
- One mesh per prop: small files, and the meshes come from `bm-forge` (box JSON).

The monitor screen is an exception. Keep the camera frontal to the monitor when the player works, and draw the screen text as a 2D layer on the monitor rectangle. The text stays sharp and keeps the exact grid of the platform. A rotating computer is not a goal.

Costs to measure on the Pi Zero W before the art starts:

- The triangle budget of the room (aim at 2000 to 3000 triangles; the plaza of the hero cart has 3000 and runs at 60 fps with many heroes).
- The cost of cel light plus outline (the outline draws the mesh twice).
- Whether the GPU or the ARM raster is faster for a scene of many small meshes (on the GPU a draw costs one record per triangle in some paths).
- Whether a 320x180 cartridge keeps the monitor text readable (an Apple II line is 280 pixels wide).

### The monitor shows the real platform

The text on the monitor looks like the text of that computer. It is not a modern font in a box.

| Computer | Year | Monitor look |
|---|---|---|
| Apple II | 1977 | 40x24 text, upper case, one phosphor colour, the Applesoft `]` prompt |
| Commodore PET 2001 | 1977 | 40x25 text, green screen, PETSCII graphics, `READY.` |
| Commodore 64 | 1982 | 40x25 text, 16 colours, blue screen, border |
| Later PCs | 1985 and later | 80x25 text, then VGA windows, then a desktop |

Licence rule: the bm sources keep only original code or permissive licences. Do not copy a ROM character set. Draw an original look-alike font for each computer in bm Pixel.

### The programming page

The program is a page of text on the monitor. The page scrolls. The player does not type. The player moves a cursor over the page and changes things in the page.

What the cursor can select:

| Level | Example | What A does |
|---|---|---|
| Line | `20 PRINT TOTAL` | Opens a list of alternative lines. |
| Parameter | `FOR I = 1 TO 10` | Opens a number spinner or a list of names. |
| Value | `PRICE = 5` | Changes the value with up and down. |
| Block | A loop, a subroutine | Moves, copies, or removes the whole block. |
| Free space | An empty line | Inserts a line or a block from a palette. |

Keys: up and down scroll the page. L1 and R1 jump a block. A edits. B undoes. Y runs the program. Start opens the desk menu.

The loop of a job:

1. The job gives a spec sheet. It says what the program must print or do.
2. The player edits the page until the program matches the spec.
3. The player runs it (`RUN`). The monitor shows the output, line by line, at the speed of the computer.
4. If the output is wrong, the player edits and runs again. A bug is a wrong line, a wrong value, or a block in the wrong place.
5. If the output is right, the job is done and the client pays.

Each computer has its own small language: Applesoft-like BASIC, then PET and C64 BASIC, then assembler mnemonics, then C-like code, then a visual IDE. The language is a small interpreter in Lua that runs tiny programs. The interpreter has a step budget, so the Pi Zero W is safe (see `docs/PERFORMANCE.md`).

The jobs come from templates with random parameters. Examples: a shop price list, a payroll table, a sort of names, a simple game for a magazine, a counter for a bakery. The checker compares the printed output with the expected output.

### The day

1. The player turns the computer on. A boot animation plays that is right for the computer.
2. The player looks at the job board: letters, magazine ads, the phone. After 1995, the inbox. After 2000, web orders.
3. The player picks a job and works on the page. The clock on the wall moves. Light in the window changes.
4. Energy and focus fall with time. Coffee gives a short rise. A break, a snack, or a nap on the bed restores them.
5. The player turns the computer off. The day report shows hours, jobs, money, and bills.

### Money and upgrades

- Pay comes from hours and from finished jobs. A fast, clean job pays a bonus. A late job pays less.
- The bank account is a program on the PC from the first computer. Early on it is a paper ledger on the desk. After 1995 it is a bank window on the PC with balance, history, and bills.
- Upgrades change what the player can do:

| Part | Effect |
|---|---|
| RAM | A longer page and bigger jobs. |
| CPU | `RUN` is faster, so the day has more time. |
| Storage | Cassette, floppy, hard disk. Load and save take less time. |
| Display | More colours and a better text mode. |
| Printer, modem | New kinds of jobs. |
| Chair, lamp, desk | More energy and focus for a long day. |

- Personal projects: the player writes a program for sale (a game, a tool). The result depends on the player's skill and the computer. It earns a small income over time.

### Progressive layers

Each layer is added in one step and plays with the same loop.

| Layer | What it adds |
|---|---|
| Internet and email | Orders arrive on the PC. The phone rings less. |
| Bank | The account window, loans, and bills. |
| Stocks | A small market window. The player invests the saved money. |
| Crypto | A mining program and a wallet. |
| Mini server | A box on the shelf. It runs jobs while the player sleeps. |
| AI | A helper that fills lines of the page. It is fast and sometimes wrong. |
| Frankenstein computer | The player builds a machine from parts. The room and the monitor turn strange. |

### Channels: how things reach the desk (decided 2026-10-28)

Everything arrives through an object of the room. The channel changes with the years. The loop stays the same.

| Era | Jobs | Shopping | Money |
|---|---|---|---|
| 1977 to 1994 | The newspaper (job ads), letters, the phone | The catalogue (paper, order form, delivery by post) | Job cheque, paper ledger |
| 1995 and later | Email, web pages | Online shops (like a big store site), delivery | Online bank account, loans, bills |
| 2008 and later | Freelance sites, the mini server | Parts and cloud services | Stocks, crypto |
| 2025 and later | Prompts to an AI helper | Open hardware, parts for a Frankenstein machine | Income from owned programs and bots |

The daily choices: read the newspaper or the inbox, pick jobs, fix bugs, write programs, earn money, invest, buy hardware and software from the catalogue. New technology arrives as a news item, then as a catalogue page.

### Personal projects (decided 2026-10-28)

The player starts personal projects next to the jobs. A project grows in complexity over the years and is always useful to the player.

| Family | Examples | Use |
|---|---|---|
| Bots | Stock bot, mail bot, price watcher | Work while the player is away. |
| Auto-email | Reply rules, mass mail | Fewer hours on the inbox. |
| Work simplifiers | Report maker, test script, build script | Jobs take fewer hours. |
| Programs to sell | Utilities, games, engines | Small income over time. |
| Systems | An operating system, a game engine | Long projects. They unlock new jobs and new parts. |

The drift to cyberpunk is a result of these projects, not a change of game. The player builds open hardware, adds monitors, and runs a mini server. The tools grow, the room becomes strange, and the Lain mood comes from the player's own machines. The projects use the same programming page as the jobs. The aim of the long projects (open source system, engine, bots, hacker tools) stays a choice of the player: the game does not rank them.

### Hidden story layer (decided 2026-10-28)

The game has a dark, noir, mystery story. The game never tells it. The player finds pieces and decides what they mean.

Rules:

1. The story is written first, in a separate document (the bible). The game does not contain it, and the bible is not part of a cartridge.
2. There is no case, no quest log, no clue marker, no journal, and no "solved" state. The game does not say which thing is a clue.
3. A clue is an ordinary thing of the game: a line of a job, a `REM` line in a listing, a mail, an advert in the newspaper, a line of the bank statement, a name in a header, a bug that is not a bug.
4. Not every element connects to the main plot. Some belong to small side stories. Some belong to nothing. The player decides.
5. No element confirms another. Two elements can agree, disagree, or say nothing.
6. The questions and the answers happen outside the game. The player keeps their own notes. The game has no hint system and no cork board.
7. A name, a number, or a sentence can come back in other years in other forms. The bible says what is true. A character in the game can lie.
8. Nothing blocks the work loop. A player who ignores the story loses nothing.
9. The story has no ending. It has open threads. A thread can stay open for ever.

Where the clues live:

| Carrier | Example |
|---|---|
| Job spec | A client asks for a routine that does nothing useful. |
| Listing | A comment in a type-in program. A line that the program never runs. |
| Bug hunt | The bug is a message. |
| Mail and letters | No sender. A sender that changes name. |
| Newspaper and adverts | A short advert with a number. A news item about a firm that closes. |
| Bank statement | A payment with no reason. The same sum in two different years. |
| Room | A new object on a shelf that the player did not buy. |

Seed of the bible (not shown to the player): a client who signs with one letter orders pieces of a program over the decades. The player does not know what the pieces make. The bible decides what they make and who the client is.

### Optimization

Not decided. The idea is a resource budget (RAM, steps) that the player respects when editing the page. It needs a clear rule before it enters the game. Decide it after the base loop works.

### Technical notes

- Screen: 360x360 RGB565 (the RGB30 panel). The art target is hand-made pixel art with more than 16 colours: about 100 colours, ramps of 7 tones per material, ordered dither between tones. The live 3D shader must reach this look. The project is a `.bme`.
- The room is live 3D (see "Live 3D room"). Props are low-poly meshes made with `bm-forge`. The monitor font and the platform fonts are bitmaps drawn in bm Pixel.
- The monitor text is drawn by the game as a 2D layer. This keeps the look of each platform and costs little on the Pi Zero W.
- Cost to check on the Pi: the interpreter steps for `RUN`, the page scroll, and the number of animated objects in the room.
- Save: the account, the computer, the year, the unlocked layers, and the room state.

### Build order

1. Prototype: Apple II desk, one job type, the programming page with all five cursor levels, `RUN`, and the check.
2. Day cycle, energy, money, the ledger, and the day report.
3. PET 2001, upgrades, and the second language.
4. C64 and the personal projects.
5. First PCs, assembler or C-like code, and the larger jobs.
6. Internet, email, and the bank window.
7. Stocks, crypto, mini server.
8. AI and Frankenstein computers.

### Open questions

- Optimization: how does it work with a pad?
- Do jobs have a story, or only a spec sheet and a client name? Decided: the story is the hidden layer (see above). Most jobs stay a spec sheet and a client name.
- Ending: decided 2026-10-28. The game runs without end (it is a management game). The hidden story has open threads and no ending.
- The story bible: not written. Write it in `docs/giochi/home-compiler-bible.md`, kept out of cartridges. First decide what the pieces of the program make and who the client is, then three or four side stories.
- How many computers get a full room redraw, and how many only a new monitor?
