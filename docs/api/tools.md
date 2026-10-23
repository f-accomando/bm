# API: Keyboard, files and assistant (for the development tools)

Part of the game API. Index and basics: [API.md](../API.md).

### Keyboard and files (for tools such as the editors)

**Games and projects** (2026-10-06): a `.bm` (and a `.b16`) is a **game**: bm's tools read it
and take its code and its assets, but never change it. What they change is a **project**, a
`.bme`: the same container, with bit 0 of the u16 at offset 18 of the header set
(`src/bm/project.h`). A tool that saves a game asks first (*Save an editable copy: name.bme?*,
like the permissions) and writes the copy in `/bme` (`/bme/name.bme`, or `name1.bme`… if
that is taken; the folder is made when missing); a new `.bm` becomes a project (`"/carts/new.bm"` → `/bme/new.bme`). A
`.bme` found next to its game (an old card) is read and saved where it is. So
`cart_save`, `cart_write` and `cart_put_audio` return `true` **and the file written** when it is
not the one asked: the tool goes on with that one. A project makes its game with
`cart_build`. In the menu: *Make an editable copy* in a game's options (the project is not listed
in the tabs: open it with the SDK's *Open*, Ctrl+O); *Build the game (.bm)* is in the SDK (Ctrl+B). These rules are for bm's tools; the other cartridges write only `.bm` and `.bme` files
in `/carts` that they made in the same run.

| Function | Description |
|---|---|
| `keyheld(name)` | `true` while key `name` of a keyboard is held: `"f1"`…`"f12"`, `"tab"`, `"space"`, `"enter"`, `"esc"` (bm Pixel: space held to draw) |
| `keyp()` | the next key typed: a character (`"a"`, `"\n"` Enter, `"\b"` Backspace, `"\t"`), a name (`"up"`, `"down"`, `"left"`, `"right"`, `"home"`, `"end"`, `"pgup"`, `"pgdn"`, `"del"`, `"esc"`, `"f1"`…`"f10"`), `"^s"` for Ctrl+S or `"^S"` for Ctrl+Shift+S (Ctrl+I and Ctrl+M too: `"^i"`, `"^m"`, not Tab and Enter); `nil` if none. F11 and F12 are the system's and never come. From the first call the keyboard types and no longer works as a gamepad for `btn()`, and Esc is a key like the others (Ctrl+Esc, Start+Select and PS close) |
| `keyhelp(list, [title])` | the cartridge's keys, shown under the system's while **F12** is held (2026-10-04): `list` is `{ {"keys", "what they do"}, "subtitle", … }`; keys as in `prompt()` (lower case the keyboard, upper case the pad), separated by spaces: `"ctrl s"`, `"shift w a s d"`, `"a / d"` (alternatives), `"1 - 5"` (a range), `"Y LEFTRIGHT"`. Returns how many entries name a key the system keeps for itself and the cartridge never gets (F11, F12, Ctrl+Esc, Ctrl+Shift+Esc: in red and in the log, they must go); the other system keys (Esc, Ctrl+S…) are listed when saying what they do there; `keyhelp(nil)` takes it away. Call it again when the page changes |
| `ls([folder])` | the files of the SD card: `{ {name=, size=, dir=}, … }` |
| `cart_load(path)` | opens a `.bm`: its sprite sheet (with the tiles' flags, the zones and their boxes), its map (with its layers) and its 3D models (with the skeletons) take the place of those of the calling cartridge; returns `{title, author, res, lua, sheet_w, sheet_h, map_w, map_h, layers, [palette]}`; `layers` the names of the map's layers; `palette` the colours (0xRRGGBB) of the SHEET8 section's palette, in their order, if the sheet is saved that way |
| `cart_sheet([w, h])` | the width and height of the project's sprite sheet; with `w` and `h` (multiples of 8, from 8 to 4096) it gets that size: the pixels that fit stay where they are, the new ones are transparent (bm Pixel); the flags stay with their tile |
| `cart_new()` | an empty sprite sheet and map (256×256, one layer), no flags, zones, boxes or models |
| `cart_save(path, {title, author, res, lua})` | writes a `.bm` with the code given and the current sprite sheet, tile flags, zones with their boxes, map with its layers, cover, sound bank, models and skeletons (the other sections of the file opened stay as they were; a **SHEET8** sheet nothing was drawn on since `cart_load` goes back as the file had it, byte for byte, else the sheet is written whole as SHEET); an 8.3 name, e.g. `"/bme/game.bme"`. `true` (and the file written when it is not `path`: a game's editable copy, see above), or `false` and a message |
| `cart_build(project)` | bm's tools: builds the **game** of a project (`.bme`): the same bytes, marked as a game that keeps the project's name (offset 104), in `.BM` in the games folder (`/carts` on the Pi, `/bm` on the RGB30) when the project is in `/bme`, next to it for an old project; the game it built before is replaced, a game of the same name it did not build stays and the new one takes a digit (`pong1.bm`). `true` and the game's path, or `false` and a message. The SDK: Ctrl+B, *Build the game .bm* |
| `cart_read(path)` | the code and the header of a `.bm`: `{title, author, res, lua, size}`, **without** touching the caller's sheet and map (unlike `cart_load`): for editors with several files open |
| `cart_write(path, {[lua, title, author, res, from, sections]})` | changes **only** the code (and the data fields) of a `.bm`: sprite sheet, map, cover, sound bank and the sections the kernel does not know stay as they were; a file with a long name keeps it. Without `lua` the code stays as it is. A file that is not there becomes a cartridge with only the code (an 8.3 name). `from`: the other sections come from another file ("save as"); `from = false`: a new cartridge, whatever the file holds (bm Studio's new project). `sections`: `{[8] = MESH bytes, [9] = ANIM bytes, [11] = SPRITES bytes}` (`false` takes them away), checked first (`false, "broken MESH section"`): so bm Mesh writes the models and bm Pixel's Stack page writes the zones. `sheet = true`: the project's sprite sheet (`cart_load`, `sset`, `cart_sheet`) takes the place of the file's, as **SHEET8** when it has at most 256 colours (otherwise SHEET), with the colours of `palette` (`{0xRRGGBB, …}`) first in its palette, as they are and in that order (the transparent entry of the palette of before stays in its place); a pixel that still has the RGB565 it had in the file keeps its 24 bits of before (the console keeps 16 bits per pixel): only the pixels drawn again change. So bm Pixel saves the sheet. If `fset` changed some flags meanwhile, those go into the file too. Returns like `cart_save` |
| `cart_meshes(path)` | the meshes the **code** of a `.bm` builds with `mesh()`, `mesh_sphere()` and `mesh_cube()` (also through bmlib's builder): `{ {name=, kind=, verts={x,y,z,…}, faces={a,b,c,colour,…}, [uv={…}]}, … }` (the arguments of `mesh()`, indices from 1, colour `-1` = texture) and `nil` or the code's first error; `nil` and a message if the file cannot be read. The code runs **apart** (a Lua state of its own, `src/bm/meshcap.c`): the file's body, then `_init`, `_update` and `_draw` once, with an instruction limit; bm's other functions do nothing (no files, screen or sound). The name is that of the variable holding the mesh (`M.ship` → `"ship"`; in an array `chef[2].body` → `"chef2_body"`). For bm Mesh |
| `mesh_reduce(record, triangles, [bones, [max_err]])` | **fewer triangles** for a model of the MESH section (`src/bm/decimate.c`: edge collapse with Garland-Heckbert quadrics, no AI): `record` is the model's part of the section (as `encode_mesh` of `bm3d.lua` writes it), `bones` the bone of each vertex (a byte each, from the ANIM section), `max_err` stops before a costlier collapse (0: no limit). Returns the record with at most `triangles` triangles (more only if it cannot go further without flipping faces), the bones of its vertices (`nil` without `bones`) and the number of triangles; `nil` and a message if the record is broken. Borders, colour lines and texture seams stay in place; the vertices stay the model's. bm Studio's models page (**-**); on the PC `tools/bmreduce.py` |
| `picture3d(action, ...)` | a **picture becomes a 3D model** through an image-to-3D service (`src/net/img3d.c`; the first is Meshy, key `meshy_key=...` in `bm/config.txt` on the SD card). `picture3d("providers")` the services; `picture3d("ready", service)` `true`, or `false` and why (the key is missing); `picture3d("start", picture, {provider=, polycount=})` starts the job on a `.png`/`.jpg` of the SD card or an https URL and gives the job's id (or `nil` and a message); `picture3d("status", id, service)` → `"running", progress` or `"done", url` of the `.glb` (or `nil` and a message); `picture3d("take", url, {name=, faces=, height=})` downloads the `.glb` and converts it (`src/bm/glb.c`: positions merged, faces clockwise, PNG or JPEG texture reduced to 256×256, the model framed `height` high, reduced to `faces` triangles) → `{record=, flat=, texture=, nv=, nf=, textured=}`: `record` is the model for the MESH section (with the texture, if any), `flat` the same with the colours taken from the texture, `texture` 256×256 RGBA. The calls block while the network works. bm Studio's models page (**m**) |
| `cutout3d(picture, {name=, lathe=, height=, depth=, segments=, faces=})` | a model from the **picture's outline**, made on the console without network or AI (`src/bm/cutout.c`): the transparent background (or the corners' colour) goes, the outline becomes a simplified polygon and the polygon a solid: a **cutout** `depth` thick (a fraction of the height, 0.2: the picture in front, mirrored behind, the border's colours on the sides) or, with `lathe = true`, the half outline **turned** round the vertical axis in `segments` steps (vases, towers, rockets; the picture projected in front). `picture`: a `.png` or `.jpg` of the SD card; `height` in blocks (2); `faces` triangles at most (1200). Gives the same table as `picture3d("take")`, or `nil` and a message. bm Studio's models page (**m**, the first two entries) |
| `cart_audio([path])` | the sound bank of a `.bm` as a string (`false` if it has none) and its title; without a path, the bank of the running cartridge |
| `cart_put_audio(path, bank, [title, lua])` | puts the bank (a string; `nil` takes it away) into a `.bm`, the rest of the file as before; if the file is not there it creates it with that title and that code. `true` (and the file written, as `cart_save`), or `false` and a message |
| `audio_bank(bank)` | from now on this bank plays (for the editors: music and effects playing go on); `nil`: none |
| `audio_pattern(p, bpm, swing)` / `audio_play(v, sound, note, [vol], [fx], [ms])` | a pattern in a loop, a sound of the bank on a voice (the editors' previews) |
| `cart_arg()` | the table a tool was opened with, or `nil`: `{path=` the file to work on, `error=` the error of the game just tried, `back=true` when it comes back from a game, `from=` the name of the tool that called it, `run=` the dev kit's numbers of that game`}` (`cart_run` and `cart_tool` set it) |
| `cart_run(path, [options])` | leaves, plays that file and then opens again the cartridge that asked, with `cart_arg()` = `{path=, error=, back=true, run=}` (from the menu, "Open in the SDK", "... Sound editor", "... bm Studio", "... bm Animator", "... bm Mesh" or "... bm Pixel": `back=false`). `run` (2026-10-04, the dev kit) are the numbers of the run tried: `{frames, secs, fps, ms, ms_max, slow, lua_kb, lua_peak_kb, data_kb, instr_max, tokens, tris, gpu}` (average and top ms of `_update` + `_draw`, `slow` the frames over 16.7 ms, Lua's memory at the end and at its top, the data's, the instructions of the busiest frame, the tokens, the triangles of the last frame, whether the GPU did the 3D); the SDK shows them in its dev kit |
| `cart_run(path, {breaks = {lines}, stop = true})` | the game tried is a session of the **debugger** (R13, bm Code: F8 and F5): it stops at the lines `breaks` of its `main.lua`, at `breakpoint()` and, with `stop`, at its first line. Stopped, the debugger covers the screen: the code round the line, the function's variables (the locals, then the upvalues in light blue), the stack; F10 or A the next line (over the calls), F8 or X into the call, Shift+F8 or Y out of the function, F5 or Start goes on, Esc or Select stops the game (which ends with `main.lua:N: stopped in the debugger`: the tool comes back on the line); the arrows scroll the variables and choose the function of the stack. Every game tried from a tool with `cart_run` is a session: without lines it costs nothing until it reaches `breakpoint()` |
| `breakpoint([why])` | in a game tried from a tool (`cart_run`) the debugger stops here, with the reason in its title; in the other games it does nothing |
| `cart_tool(name, [path])` | leaves and opens another tool of the console on the same file: `"studio"`, `"animator"`, `"mesh"`, `"pixel"`, `"code"`, `"sdk"`, `"sound"` (bm Studio → *Open in bm Animator*, and back); the tool finds it in `cart_arg()` as from the menu, with `from` = the name of the tool that opened it (`"sdk"`: the suite's menus offer *Back to bm SDK*) |
| `cart_config(key, [value])` | the text of a key of `bm/config.txt`, or `nil`: the **flags** a game takes from the monitor line (`set overbit_bench=auto ; play overbit`) or from `easy_install.sh` (`bench`, `config`). Only the keys that start with the first word of the game's title in lower case and `_` (Overbit: `overbit_`); anything else is an error. With a value the key is changed and saved (`""` clears it: a flag read once). Values up to 127 characters, without `;` or `,` (they split the monitor line and the host's config): Overbit's flags are `key:value` apart by `/`, lists by `+` |
| `cart_data(kind, [bytes])` | the project's **MESH** (`kind` 8), **ANIM** (9), **SPRITES** (11) and **BOXES** (14) sections, as strings in the format of `src/bm/bm.h`: without `bytes` it returns them (`nil` if there are none), with `bytes` it replaces them (`nil` or `""` takes them away) → `true`, or `false` and the reason. The kernel checks them first; `model()`, `animate()`, `zone()` and `zboxes()` use the new ones at once and `cart_save` writes them. So bm Studio, bm Animator and bm Pixel change models, skeletons, zones and hitboxes (with `spack` / `sunpack`) |

### Assistant (M30, for the development tools)

A small AI that runs on the console: it understands a question (Italian or English, also
with typos) and answers with the entries of its knowledge base (every function of the API,
code examples for games, Lua errors, tips) or draws the base of a sprite. It is not a
chatbot: a tiny INT8 network chooses among the entries it knows, in less than a
millisecond. It does nothing until you call it (no background process; knowledge base and
network are in the kernel). Try it from **Dev > Assistant** (or `I` from the monitor).

| Function | Description |
|---|---|
| `ai.ask(question, [{n=5, ctx=word, kinds="api,howto"}])` | the best entries, the first the most likely: `{ {id=, title=, kind=, score=}, … }`, and as the second value the microseconds taken. Titles and texts are in the language of the answers (`ai.lang()`), which follows the question's. `ctx`: the word under the cursor (if it is a function of the API, its entry goes to the top). `kinds`: `api`, `howto`, `error`, `tip`, `sprite` |
| `ai.entry(id)` | an entry: `{id, kind, title, name, text, code, gen, see = {id, …}}` |
| `ai.list([kinds])` | all the entries `{id, title, kind}` (to browse them with the pad) |
| `ai.lang([language])` | the language of the answers (R18): `"it"` or `"en"`, and whether it follows the questions (`true`); `"it"`/`"en"` fix it, `"auto"` makes it follow the language of each question again. It starts from `assist_lang` in `bm/config.txt` (`it`, `en`, `auto`: the default, from the language used last). In the panel Ctrl+E changes it; the language is on the title bar |
| `ai.near(word)` | the name of the API closest to a misspelt word (`"sprr"` → `"spr"`, 1), or `nil` |
| `ai.sprite(request, [{gen=, size=16, seed=1, outline=true, palette={…}}])` | the base of a sprite: `{w, h, gen, name, seed, px = {0xRRGGBB or -1 (transparent), …}}` row by row. The recipe comes from the words (`"slime"`, `"spaceship"`, `"coin"`, `"grass"`…) or from `gen`; the colours (`"red"`, `"blu"`…) and the size (`"8x8"`, `"32x32"`, `"small"`, `"big"`) from the words; another `seed` is a variant; with `palette` each pixel becomes the closest colour of the palette |
| `ai.recipes()` | the sprites' recipes `{id, name}`; `ai.recipes("mesh")` the 3D ones `{id, name, rigged}` |
| `ai.script(text)` | a model written in the **parts language** (`src/ai/mesh_script.c`: `mat`, `box`, `bx`, `tube`, `cyl`, `ell`, `prism`, `wedge`, `tf`, `bone`, `use`, `side`, `mirror`, `clip`, `key`, `turn`, `shift`, one per line): the same table as `ai.mesh`, or `nil` and the error (`"line 3: ..."`). It is the format `tools/img2mesh.py` gets from the vision model for a picture |
| `ai.music(request, [{gen=, seed=1, key=, minor=, bpm=, bars=, inst=, context=}])` | **music** for a sound bank (`src/ai/music.c`): rhythms, backing tracks (drums, bass and chords), bass lines, arpeggios, melodies and classic sound effects, 87 recipes. The words choose the recipe (`"rock beat"`, `"lofi backing"`, `"sad melody"`, `"coin sound"`), the key (`"in A minor"`, `"la minore"`), the tempo (`"120 bpm"`, `"fast"`), the length (`"8 bars"`) and the instrument (`"with the piano"`, `"8 bit"`); another `seed` is a variant. The melodies are written by a small network trained on traditional tunes and tunes written for bm. `context = {notes = {…}, bars = {…}}`: the notes already there (MIDI) and the bar of each: melodies, arpeggios and bass lines take their key and chords. Returns `{gen, name, kind, bpm, swing, key, minor, meter, bars, echo, room, seed, chords = {"Am", …}, instruments = {"kick", …}, patterns = { {len, tracks = {[0..7] = {step, …}}} }, sfx = {ms, loop = {a, b}, steps = {step, …}}}`; a step is `note \| instrument << 8 \| vol << 16 \| fx << 24` (instrument: 0-based index into `instruments`). On the RGB30 too, where the words choose the recipe without the network. `ai.music_recipes()` lists them |
| `ai.mesh(request, [{gen=, seed=1, scale=1, rig=true}])` | the base of a **3D model** for bm Studio and bm Animator: `{gen, name, seed, faces = { {p = {{x,y,z}, …}, c = 0xRRGGBB, b = {bone, …}}, … }, bones = { {name, parent, head, tail}, … } or nil, clips = { {name, loop, length, mode, keys = { {t, pose = { {q, t}, … }}, … }}, … }}`, the same tables as `bm3d.lua` (one unit = one block of bm Studio, the model looks toward −z and stands on y = 0). The recipe (53: shapes, objects, people, animals, machines) comes from the words (`"house"`, `"tree"`, `"mech"`…) or from `gen`; the colours (`"red"`, `"blue"`), the size (`"small"`, `"big"`, `"huge"`), the proportions (`"tall"`, `"short"`, `"wide"`, `"thin"`) and `"no skeleton"` from the words; another `seed` is a variant. People, animals and machines have a skeleton (every corner on a bone) and animations (`idle`, `walk`, `fly`, `attack`…) |
| `ai.checksum(question)` | the CRC-32 of the network's outputs for a question: for the tests (equal to that of the Python reference) |

**Small networks for the games** (M38.4: Overbit's bots). A network of dense layers with
INT8 weights and activations (the same maths as the assistant, with the ARMv6's SIMD
instructions), from a string a training script writes (`scripts/nnetlib.py`: it quantises a
numpy network, packs it and gives the console's exact numbers for the tests):

| Function | What it does |
|---|---|
| `nnet(blob)` | the network of a "BMNN" string (format in `src/ai/net.h`; an error if it is broken), up to 8 layers of 256 |
| `net:run(inputs, [outputs])` | the outputs (numbers) for a table of inputs; `outputs`: a table to fill instead of a new one |
| `k, v = net:pick(inputs, [mask])` | the index (from 1) of the biggest output and its value; `mask`: a table of booleans, `false` = that output is not chosen |
| `n_in, n_out = net:size()` | how many inputs and outputs |

**The panel** (`require "assist"`): what the tools open with a key (F6 in the Assistant).
It answers while you type; Enter (A) passes the code or the sprite to the tool, Esc (B)
closes, Tab (X) changes mode; without a question you browse everything with the pad. While
a word of the question is typed the rest of the most likely one appears in blue-grey and Tab
writes it (the completion, below).

```lua
local assist = require "assist"

function _update()
  if assist.update() then return end          -- open: the keys are its own
  local k = keyp()
  if k == "f6" then
    assist.open{ mode = "code", ctx = word_under_cursor,
                 on_insert = function(code) insert_lines(code) end }
  end
end

function _draw()
  draw_tool()
  assist.draw()                                -- on top, if open
end
```

`assist.open{...}`: `mode` = `"code"` (API, examples, errors), `"sprite"`, `"mesh"` (the 3D
recipes: the model turns in the panel, `on_mesh(m)` gets it; it is bm Studio's and bm
Animator's mode with F6), `"error"` or `"any"`; `query` (a question already written), `ctx`,
`error` (an error message: the panel shows the line, the misspelt name and what it means),
`size` and `palette` for the sprites, `on_insert(code)`, `on_sprite(sprite)`, `on_close()`,
`x, y, w, h` (default: almost the whole screen). Then `assist.update()` and `assist.draw()`
in every frame, `assist.is_open()`, `assist.close()`.

**Actions on the code** (`assist.act(request, lines, n)`): what a line `#entry: request #`
at line `n` of `lines` (a table of strings) asks. The network chooses among the actions
(ternary operator, comment, log, remove the logs, nil check, make local, indentation,
rename, comment/uncomment, optimise, explain), an example to insert or a sprite written as
code; it works on the function round the line or just below it. Returns `{lines, ok,
message, cursor, explain}` (the new lines, without the `#entry:` line), and changes nothing
if it is not sure enough. bm Code uses it with Enter on those lines.

The knowledge base is in `src/ai/kb/` (format and how to train again:
`src/ai/kb/README.md`).

**Word completion** (`require "predict"`, guide in [PREDICT.md](../PREDICT.md)):
`predict.complete(text_before_the_cursor, {lang = "lua"})` → `nil` or
`{prefix, word, rest, ending, list}`: the most likely word that starts as the one written
(`rest` is what is missing, to show in `predict.C_GHOST`; Tab replaces `prefix` with
`word`). `lang`: `"it"`, `"en"`, `"lua"`, `"ask"` (the questions to the assistant), a mix
with weights (`{it = 1, ask = 2}`) or `"none"`; `words` the code's names
(`predict.count_words(lines)`), weighted by `words_weight`. The dictionaries are read at the
first word, or one piece per frame with `predict.preload({"lua", "it"})`.

**Typing with the pad** (`require "padtype"`, guide in [PADTYPE.md](../PADTYPE.md)): quick
composing (the cross writes consonants, the prediction finishes the syllable, □ and △ turn
it, a press waits for its double, L2 / R2 / L2+R2 more levels, R2 + ✕ □ △ the words) and
the on-screen keyboard; Share goes from one to the other. `pt.update(host)` every frame
reads `pad()` and changes the text through the host (`before`, `insert`, `erase`,
`newline`, `move`, `lang`; `pt.text_host(lang)` makes one on a string), `pt.draw(x, y)`
draws the controller's overlay (`pt.size()`), `pt.coach(host, text)` gives the next button
to write a text. bm Code and the Pad Typing cartridge use it.
