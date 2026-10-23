# API: bmlib

Part of the game API. Index and basics: [API.md](../API.md).

## bmlib: the games' shared library

`local lib = require "bmlib"` (R10, 2026-10-04): what every game wrote again by itself
(found in 5–10 cartridges of the repository) in a single library, in Lua, in the kernel
(`src/script/bmlib.lua`). It does not touch the global variables and hides no function of
the console. Units: **times in seconds**, positions in pixels, speeds in **pixels per
frame**. Objects (particles, camera, states, menu, pause, 3D builder) are used with a
colon: `cam:follow(x, y)`.

Tweens, timers, scripts and jingles go on with **`lib.update()`**, to call once in
`_update` (it adds 1/60 s; `lib.update(dt)` another step). `lib.time` is the time counted
that way.

```lua
local lib = require "bmlib"

function _update()
  lib.update()          -- tweens, timers, scripts and jingles
  -- ... the game
end
```

### Numbers

| Function | Description |
|---|---|
| `lib.clamp(v, lo, hi)` | `v` between `lo` and `hi` |
| `lib.lerp(a, b, t)` / `lib.unlerp(a, b, v)` | from `a` (t = 0) to `b` (t = 1); the reverse: where `v` is between `a` and `b` |
| `lib.remap(v, a0, a1, b0, b1)` | `v` from the range `a0..a1` to the range `b0..b1` |
| `lib.approach(v, target, step)` | `v` toward `target` by at most `step` (bars that go down slowly, speeds that brake) |
| `lib.sign(v)` | −1, 0 or 1 (0 for 0) |
| `lib.round(v, [step])` | to the nearest integer, or to the nearest multiple of `step` |
| `lib.wrap(v, lo, hi)` | `v` brought back into `lo..hi` (`hi` excluded) going round: worlds that wrap |
| `lib.cycle(i, d, n)` | index `i` (1..n) moved by `d` going round: the rows of a menu |
| `lib.dist(ax, ay, bx, by)` / `lib.dist2(...)` | the distance / its square (to compare it with `r * r` without the root) |
| `lib.len(x, y)` / `lib.norm(x, y)` | the length of a vector / the vector 1 long (and the length it had); `0, 0, 0` for `0, 0` |
| `lib.angle(ax, ay, bx, by)` | the angle from `a` to `b` in radians (0 to the right, π/2 down on the screen) |
| `lib.angdiff(a, b)` / `lib.turn(a, target, step)` | from `a` to `b` the shortest way (−π..π) / angle `a` turned toward `target` by at most `step` |
| `lib.dir8(x, y)` | one of 8 directions: 0 right, 1 down-right, 2 down… 7 up-right (`nil` for `0, 0`); `lib.DIR8[d + 1]` is the vector 1 long |
| `lib.TAU` | 2π |

### Random numbers

| Function | Description |
|---|---|
| `lib.rnd([a, b])` | a number between `a` and `b` (`b` excluded); `lib.rnd(n)`: between 0 and `n`; `lib.rnd()`: between 0 and 1 |
| `lib.chance(p)` | `true` with probability `p` |
| `lib.choose(t)` / `lib.shuffle(t)` | a random element of the list (`nil` if it is empty) / the list shuffled (in place) |
| `lib.rng(seed)` | a generator of its own, which always gives the same numbers from the same seed (worlds made from a seed, lockstep network matches): `r:next()` between 0 and 1, `r:range(a, b)`, `r:int(a, b)` (integers, both included), `r:pick(t)`, `r:chance(p)`, `r:seed(s)` |

### Collisions

| Function | Description |
|---|---|
| `lib.overlap(ax, ay, aw, ah, bx, by, bw, bh)` | two rectangles (x, y, width, height) touch |
| `lib.hit(a, b)` | the same for two tables with `x, y, w, h` (`w` and `h` 8 if missing) |
| `lib.inside(px, py, x, y, w, h)` | the point is in the rectangle |
| `lib.circles(ax, ay, ar, bx, by, br)` / `lib.circrect(cx, cy, r, x, y, w, h)` | two circles / a circle and a rectangle touch |

### The map: walls, platforms, gravity

The functions look at the **tiles' flags** ([above](draw.md#sprites-and-map)): `lib.SOLID` (flag
0, 1), `lib.PLATFORM` (flag 1, 2), `lib.LADDER` (4), `lib.WATER` (8), `lib.HURT` (16).

| Function | Description |
|---|---|
| `lib.tiles(opt)` | how the map stops the bodies: `layer` (the layer: number or name, 1), `solid` and `platform` (the flags' masks: 1 and 2; 0 = none), `edge` (`true`: outside the map is solid). Returns the configuration |
| `lib.solid(x, y, [w, h])` | the rectangle (or the point) touches something solid |
| `lib.move(b, dx, dy)` | moves body `b = {x, y, w, h}` (`w`, `h` 8 if missing) by `dx, dy`, stopping it against the solid tiles and sliding it along the walls; it falls on a platform only from above (and not with `b.drop`: down through it). In steps shorter than a tile: it never jumps one. Returns `hx, hy`: −1 / 1 where it hit (left / right, up / down), otherwise 0 |
| `lib.step(b)` | one frame of a body with gravity (a platformer): `b.vx, b.vy` in pixels per frame, `b.gravity` (`lib.GRAVITY`, 0.25) and `b.maxfall` (`lib.MAXFALL`, 6); then `b.ground` is `true` on the ground and the speed that hit is 0. Returns what `lib.move` gives |
| `lib.ray(x0, y0, x1, y1, [mask])` | the first tile with a flag of `mask` (`lib.tiles`' solid one if missing) on the segment: `x, y` where the segment enters it and its cell `mx, my`; `nil` if the way is free (line of sight, bullets) |

```lua
local hero = { x = 40, y = 40, w = 8, h = 8, vx = 0, vy = 0 }
lib.tiles({ edge = true })                         -- the map's borders are walls
function _update()
  hero.vx = (btn("right") and 2 or 0) - (btn("left") and 2 or 0)
  if hero.ground and btnp("a") then hero.vy = -5 end
  hero.drop = btn("down")                          -- down from a platform
  lib.step(hero)
end
```

### Hitboxes and hurtboxes

**Hurtboxes** are where a body can be hit, **hitboxes** where an attack hurts (a punch, a
sword, a bullet). A world of hits gathers them in every frame and says who hits whom:
teams, one hit per attack, the parts of the body, the lanes of a beat 'em up, blades that
clash. The boxes are given in the code or come from the sheet (`zboxes`,
[above](draw.md#sprites-and-map)).

| Function | Description |
|---|---|
| `lib.hits()` | a world of hits `H` (tables `H.hurts`, `H.hitl`: the frame's boxes) |
| `H:clear()` | a new frame, to call in `_update` before the boxes: those of before go; the attacks (`id`) that did not come back in the last frame are over |
| `H:hurt(who, x, y, w, h, [opt])` | a hurtbox of `who` (any value: the body's table). `team` (the same team does not hit itself), `part` (a name: `"head"`; the parts that count most first), `z` and `depth` (a third dimension: the lane, the height) |
| `H:hit(who, x, y, w, h, [opt])` | a hitbox of the attacker `who`: `team`, `id` (an attack: it hits each body **once** while the same `id` comes back frame after frame; the boxes of one attack share it; without `id` it hurts in every frame it touches), `clash` (two hitboxes with `clash` that touch make a `"clash"` contact), `z`, `depth` and whatever the game needs (`damage`, `knock`...) |
| `H:zone(who, name, frame, x, y, [flip, opt])` | the boxes of a zone's frame (`zboxes`: `hurt` and `hit`) for the sprite drawn with `zspr(name, x, y, frame, flip)`: the hurtboxes with `opt` (`team`, `part`), the hitboxes with `opt.attack` (the options of `H:hit`; the team of `opt` if missing) |
| `H:check()` | the contacts of the frame, in the order of the hitboxes: `{kind = "hit" or "clash", by (the attacker), to (the body hit, or the other attacker), hit (the options of H:hit), part, x, y (the middle of where they touch)}`. An attack hits a body once a frame (its first box that touches) |
| `H:draw([hurt_c, hit_c])` | the boxes, to see them while making the game (blue and red) |
| `lib.box(b, x, y, [flip, w])` | a box of a frame (`b.x`, `b.y` from the corner, `b.w`, `b.h`) in the world, for the frame drawn at `x, y`; with `flip` mirrored over the frame's width `w`: `x, y, w, h` |
| `lib.separate(a, b)` | two bodies `{x, y, w, h}` that overlap move apart along the axis where they overlap less, half each (`a.fixed` or `b.fixed`: only the other); `true` if they touched. For fighters that do not pass through each other, crowds |

```lua
local H = lib.hits()

function _update()
  H:clear()
  for _, f in ipairs(fighters) do
    H:zone(f, f.anim, f.frame, f.x, f.y, f.face < 0,
           { team = f.team, attack = { id = f.swing, damage = 5 } })
  end
  for _, c in ipairs(H:check()) do
    c.to.life = c.to.life - c.hit.damage    -- c.by hit c.to
  end
end
```

### Easing, tweens, timers and scripts

`lib.ease` has the curves from 0 to 1: `linear`, `inquad`, `outquad`, `inoutquad`,
`incubic`, `outcubic`, `inoutcubic`, `insine`, `outsine`, `inoutsine`, `inback`, `outback`,
`inoutback`, `outelastic`, `outbounce`, `smooth` (smoothstep).

| Function | Description |
|---|---|
| `lib.tween(obj, to, secs, [ease, done])` | the fields of `obj` go to the values of table `to` in `secs` seconds (`ease`: a function or the name of one of `lib.ease`, linear if missing), then `done(obj)`. Returns a handle: `h:cancel()` |
| `lib.after(secs, f)` | `f()` in `secs` seconds |
| `lib.every(secs, f, [times])` | `f()` every `secs` seconds (`times` times, for ever if missing); `f` returning `false` stops it |
| `lib.script(f, ...)` | `f(...)` as a **script that can wait**: `lib.wait(secs)` (a frame if missing) and `lib.waitfor(cond)` (until `cond()` is true). It runs at once up to its first wait, then `lib.update()` takes it on: dialogues, cutscenes, waves of enemies written in a row |
| `lib.cancel(h)` | stops a tween, a timer or a script (the same as `h:cancel()`) |
| `lib.countdown(t, keys, [d])` | the fields `keys` of `t` above 0 go down by `d` (1/60 if missing), not under 0: a hero's cool-downs (in frames with `d = 1`) |
| `lib.clear()` | stops every tween, timer, script and jingle (a new level) |

```lua
lib.tween(title, { y = 80 }, 0.6, "outback")
lib.after(2, function() door.open = true end)
lib.script(function()
  say("Who goes there?")
  lib.wait(1.5)
  lib.waitfor(function() return btnp("a") end)
  say("You may pass.")
end)
```

### Lists and particles

| Function | Description |
|---|---|
| `lib.each(list, f)` | `f(item, i)` for each item; those for which it returns `false` leave the list (the others keep their order) |
| `lib.sweep(list)` | takes away the items with `.dead` (in order) |
| `lib.particles([max])` | a pool of at most `max` particles (200); when it is full a new one takes the place of the oldest |
| `P:add(x, y, vx, vy, life, colour, [opt])` | one particle (speeds in pixels per frame, life in seconds); `opt`: `size` (radius; 0 or 1 a pixel), `gravity`, `drag` (the part of the speed kept every frame), `colors` (a list: the colour through its life), `shrink`, `floor` (the y where it bounces), `bounce` (0.3) |
| `P:burst(x, y, n, [opt])` | `n` particles from `x, y`: `speed` (2), `angle` and `spread` (in radians; all round if missing), `life` (0.5 s), `color` or `colors`, and the options of `:add` |
| `P:update()` / `P:draw()` | in `_update` / in `_draw` |
| `P:count()` / `P:clear()` | how many there are / all away |
| `lib.particles3d([max])` | 3D sibling of `lib.particles` that wraps the 3D particle functions; pool of at most `max` particles (default 1024) |
| `P3:add(x, y, z, vx, vy, vz, life, color)` | adds one 3D particle |
| `P3:burst(x, y, z, n, o)` | `n` particles from `x, y, z`: `o.speed` (2), `o.life` (30 updates), `o.color` (0xFFFFFF), `o.up` (0) |
| `P3:update([gravity])` | steps the 3D particles with `gravity` (0.02) |
| `P3:draw([radius])` | draws the 3D particles with `radius` (0.1) |
| `P3:count()` | how many live 3D particles |

`lib.particles3d` is the 3D sibling of `lib.particles` and wraps the 3D particle functions (`particles`, `particles_add`, `particles_step`, `particles_draw`, `particles_count`).

```lua
local fx = lib.particles(300)
fx:burst(x, y, 20, { speed = 3, colors = { 0xFFFFFF, 0xFFD050, 0xFF6020 }, gravity = 0.1 })

local p3 = lib.particles3d(500)
p3:burst(x, y, z, 20, { speed = 2, life = 30, color = 0xFFA020 })
p3:update()
p3:draw()
```

### Entities

`lib.world(kinds)`: the list of the things in a game (the player, foes, bullets, pickups) with
the loop that every game wrote again. `kinds = { name = { ... } }` says what each kind is. An
entity reads the fields it does not hold from its kind, so a spawn makes one table and copies
nothing. The fields of a kind: `w`, `h` (the box, `x`, `y` the top left), `hp`, `team`, `dmg`
(damage on contact), `pierce`, `ttl` (frames), `layer` (draw order, 0 first), and the functions
`update(e, world)`, `draw(e, world)`, `hurt(e, by, n)` and `die(e, world)`. Every entity has
`kind`, `age` (frames), `x`, `y`, `vx`, `vy` (pixels per frame); `e.dead` marks the end.

| Function | Description |
|---|---|
| `W:spawn(name, [props])` | a new entity of kind `name` with the fields in `props`; it updates from the next frame; returns it. An unknown kind is an error |
| `W:load(rows)` | spawns from data: rows `{kind, x, y, [props]}` or `{kind = , x = , y = , ...}` (a wave, a level) |
| `W:update()` | in `_update`: each entity ages, moves by `vx`, `vy`, loses `ttl` and `invuln`, runs its `update`; then the dead leave, `die` runs once for each (a spawn made in `die` starts next frame) |
| `W:fight()` | in `_update`, after `W:update()`: each entity with `dmg` hurts each entity of another `team` with `hp` and a box that touches it, once a frame; the attacker dies unless `pierce`. Returns the number of hits. It makes no table |
| `W:hurt(e, n, [by])` | `n` damage: runs `e:hurt(by, n)`, and at `hp` 0 the entity dies. Nothing happens while `e.invuln` (frames) is above 0. Returns `true` if it hurt |
| `W:kill(e)` / `W:clear()` | `e` leaves at the end of the update / all leave (no `die`) |
| `W:find(name)` / `W:count(name)` | the first live entity of kind `name` / how many |
| `W:draw()` | in `_draw`: the `draw` of each entity, the lower `layer` first |

### Sound events

`lib.sounds(events)`: the sounds of a game by name. The rules say `snd:play("hit")`; the voices,
notes and tunes stay in one table. Each event has one of these fields:

| Field | Plays |
|---|---|
| `note = { voice, hz, ms, wave, vol }` | a `note()` |
| `sfx = n`, `semis`, `vol` | effect `n` of the sound bank (`sfx`) |
| `play = sound`, `pnote`, `ms`, `vol` | an instrument or a sound of the bank (`play`) |
| `tune = { {hz, frames}, ... }`, `voice` (3), `wave` (`TRIANGLE`), `vol` (120), `ms` (14 per frame) | a short tune, one note after the other; `hz` 0 is a rest |

Any event can have `gap` (frames before it can sound again) and, for a tune, `prio` (0 by
default).

| Function | Description |
|---|---|
| `snd:play(name)` | plays the event. Returns `false` if the name is unknown, the `gap` is not over, or a tune of a higher `prio` is running. A new tune replaces a tune of the same or a lower `prio`; its first note sounds in the next `snd:update()` |
| `snd:update()` | once a frame, first in `_update`: counts the frames and plays the next note of the tune |
| `snd:stop()` | stops the tune |

`lib.sounds(events, api)` takes `api = { note =, sfx =, play = }` to replace the sound functions
(tests). `snd:play` and `snd:update` make no table.

### Save slots

Slot 1 of the cartridge is for `lib.store` (settings and records). `lib.slots()` gives the
slots 2 to 8 as the game's slots 1 to 7 (games in progress).

| Function | Description |
|---|---|
| `S:get(i)` | the table saved in slot `i`, or `nil` |
| `S:put(i, t)` | saves table `t` in slot `i` (at the end of a match, not in every frame): `true`, or `false` and the reason |
| `S:used(i)` | `true` if slot `i` holds a save |
| `S:del(i)` | empties slot `i` |
| `S:list()` | the slots in use as `{ {slot = i, bytes = n}, ... }` for a "load game" page; it makes a table, so call it when the page opens |

A slot out of 1 to 7 is an error. `lib.slots(api)` takes `api = { save =, saved =, saves =, delsave = }` (tests).

`W.list` is the plain list (read it in your loops; do not remove from it). For fighters with
frame data (startup, active, recovery) use `lib.hits` (above): `W:fight()` is for contact damage.
`tests/gamekit/cart.lua` is a whole small game on the kit (states, input, tween, timer, record).

### Elastic layout

`lib.layout` (2026-10-23) cuts the screen into regions on the text grid (the cell of `font()`:
8 x 16 pixels). Each call reads `SCREEN_W` and `SCREEN_H`, so an app that calls it again lays
itself out again when `screen()` changes the size. A region is a table `{x, y, w, h}`. The
SDK uses it (1280x720 where the GPU draws, 640x360 otherwise).

| Function | Description |
|---|---|
| `lib.layout.rect(x, y, w, h)` / `lib.layout.screen()` | a region / the whole screen now |
| `lib.layout.split(r, "v", sizes)` | `r` cut top to bottom (`"h"`: left to right). A size is pixels (`16`), rows or columns of text (`"3r"`, `"20c"`) or a share of what is left (`"*"`, `"2*"`). Shares fall on whole cells, the last one takes the rest. More than fits is an error |
| `lib.layout.side_by_side(r, [cols])` | `r` cut into a panel and the work region. The panel has `cols` columns (16 on a screen narrower than 480 pixels, 30 otherwise) |
| `lib.layout.cols(r)`, `lib.layout.rows(r)`, `lib.layout.row(r, i)` | whole columns and rows of text in `r`; `x, y` of the text row `i` |
| `lib.layout.snap(x)`, `lib.layout.fit(s, cols)`, `lib.layout.wrap(s, cols, [n])` | `x` on the next column; `s` cut to `cols` characters; `s` in lines of at most `cols` characters, cut between words |
| `lib.layout.inside(a, b)`, `lib.layout.overlap(a, b)` | with regions (`lib.inside` and `lib.overlap` take numbers) |
| `lib.layout.narrow([w])` | the screen is too narrow for panels side by side |
| `lib.layout.on_resize(fn, [now])` / `lib.layout.poll()` | `fn(w, h)` runs when the size changed. Call `lib.layout.poll()` first in `_update` and `_draw`: it runs the functions when `screen()` took effect (the next frame). With `now` the function runs once at once |
| `lib.layout.big_ok()` / `lib.layout.want(w, h)` | whether the GPU draws; `screen(w, h)`, but a size over 640x360 only where the GPU draws. `true` if the screen is that size now or from the next frame |

```lua
local L = require("bmlib").layout
local W, H, body
local function relayout(w, h)
  W, H = w, h
  local bars
  body, bars = table.unpack(L.split(L.rect(0, 0, w, h), "v", { "*", "3r" }))
end
L.on_resize(relayout, true)
L.want(1280, 720)               -- 720p with the GPU, else it stays
function _update() L.poll() end -- relayout runs at the frame the size changes
```

Variables that hold `W`, `H` or a size cut from them (`local W = SCREEN_W` at the top of a
file) stay at the old size. Keep them in `relayout`. `bm3d.resize(w, h)` does it for the
suite's frame (`HINT_Y`, `STATUS_Y`, `FOCAL`).

### Camera

| Function | Description |
|---|---|
| `lib.camera([opt])` | a 2D camera: `smooth` (0.15: the part of the way it goes every frame; 1 = it sticks to the target), `dead` `{w, h}` (a box in the middle where the target moves without the camera), `bounds` `{x0, y0, x1, y1}` in pixels or `true` (the map, `msize()`), `offset` `{x, y}`, `w`, `h` (the screen if missing). Fields `x`, `y` |
| `C:follow(x, y, [snap])` | one step toward point `x, y` (in the middle of the screen); `snap`: there at once |
| `C:shake(amount, [secs])` | the screen shakes up to `amount` pixels, less and less, for `secs` seconds (0.3) |
| `C:apply([view])` | `camera()` at the camera's place (with the shake): in `_draw` before the world, then `camera()` for the HUD. With a view `{x, y, w, h}` (`lib.split`) the drawing stays in the view (`clip`) and the camera's corner is the view's: after the views, `clip()` and `camera()` |
| `C:map([layer, mask])` | the cells of the map that show |
| `C:sees(x, y, [w, h])` / `C:screen(x, y, [view])` | the rectangle shows / where a point of the world is on the screen (in the view) |

### Game states

`lib.states(defs, [first, ...])`: title, match, pause, game over as tables
`{enter, update, draw, exit}`; each function gets its table (`function play:update()`),
which has `t` (the seconds spent in the state) and `name`.

| Function | Description |
|---|---|
| `S:go(name, ...)` | to state `name`: the open ones leave (`exit`), `name` enters (`enter(...)`) |
| `S:push(name, ...)` / `S:pop()` | `name` over the one now (a pause, a dialogue: only it updates, both draw) / back to the one under it |
| `S:update()` / `S:draw()` | in `_update` / in `_draw` (from the bottom: the game under its pause) |
| `S:is(name)`, `S.name`, `S:top()` | the state on top |

```lua
local S = lib.states({
  title = { update = function(s) if btnp("ok") then S:go("play") end end,
            draw = function() cls(0); lib.printc("PRESS A", 160, 0xFFFFFF) end },
  play = { enter = function(s) s.score = 0 end,
           update = function(s) ... end, draw = function(s) ... end },
}, "title")
function _update() lib.update(); S:update() end
function _draw() S:draw() end
```

### Text, bars and menus

| Function | Description |
|---|---|
| `lib.input([over])` | the games' input standard ([games-input-standard](../design/games-input-standard.md)): puts `confirm`, `cancel`, `pause` and the actions of the game's scheme on the keymap and returns the table. `over.scheme` picks the scheme: `"scroller"` (default: `act1` jump = B, `act2` attack = Y, `act3` = the A/B button that is not ok, `act4` = X, shoulders L1/R1), `"topdown"` (`act1` = B, `act2` = the other A/B button, `act3` = Y, `act4` = X) or `"stick"` (`act1` R2, `act2` L2, `act3` L1, `act4` R1, `act5`..`act8` = B, A, Y, X); `lib.input_scheme` tells which runs. The other keys of `over` (`{ act3 = "x" }`) change single actions and are listed in `lib.input_exceptions` (declare them in the game's `docs/progress/<game>.md`). `lib.INPUT_STD` is the common part (`confirm`, `cancel`, `pause`) |
| `lib.textw(text, [scale])` | width and height in pixels with the font of `print` now (the longest line) |
| `lib.printc(text, y, [c, scale, x, w, grid])` | in the middle of the screen (or of `x..x + w`); `grid`: on the font's columns (text that stays still, as the menus). Returns the x |
| `lib.printr(text, x, y, [c, scale])` | ending at `x` (numbers aligned to the right) |
| `lib.prints(text, x, y, c, [shadow, scale])` / `lib.printo(...)` | with a shadow under it to the right / with an outline all round (black if the colour is missing) |
| `lib.bar(x, y, w, h, v, max, [c, back, border])` | a bar filled for `v` of `max` (life, stamina, loading) |
| `lib.blink([period, t])` | `true` half of the time, changing every `period` seconds (0.5): "press A" |
| `lib.timestr(secs, [tenths])` | `"m:ss"` (`"h:mm:ss"` from an hour), with tenths `"m:ss.d"` |
| `lib.btnr(i, [p, delay, rate])` | `btnp` that **repeats** while the button is held: the first frame, then after `delay` frames (15) every `rate` (4). `i` as for `btn()`. Call it in every frame |
| `lib.menu(items, [opt])` | a list to choose from: the items are texts or tables `{label=, value=, change=function(item, d), ok=function(item), off=true}`; `opt.p` the player, `opt.wrap` (`true`) |
| `M:update()` | up and down (repeating) move, skipping the `off` items, left and right call `change`, ok chooses (`ok(item)`; it returns the item and `"ok"`), back returns `nil, "back"` |
| `M:draw(x, y, [opt])` | the rows from `x, y`: `w` (width: the values on the right), `c`, `sel_c`, `bar`, `dim`, `scale`, `gap` |
| `lib.pause([opt])` | the **pause menu** every game had: Start (Esc) opens it when `opt.when()` is true (always if missing); RESUME, VOLUME (the console's: left and right), the rows of `opt.rows`, QUIT (`opt.quit()`, if given). `opt.color`, `opt.title`, `opt.scale` |
| `P:update()` / `P:draw()` | in `_update`: `if pause:update() then return end` (while it is open the game stands still); in `_draw`, after the game |

```lua
local pause = lib.pause({ when = function() return S:is("play") end,
                          quit = function() S:go("title") end })
function _update()
  if pause:update() then return end
  lib.update(); S:update()
end
function _draw() S:draw(); pause:draw() end
```

### Players on one console

`btn(i, p)`, `stick(p)` and `controller(p)` read player `p` ([Input](input.md#input)); bmlib adds
the screen where they join, the split screen and the colours. The SDK's *Versus 2D*
template is a whole example.

| Function | Description |
|---|---|
| `lib.PLAYER_COLORS` | the players' colours, those of the pads' lights: 1 blue, 2 red, 3 green, 4 pink (as `controller(p).color`) |
| `lib.pads()` | the numbers of the players who have a controller now, in order (`{1, 3}`) |
| `lib.party([opt])` | the screen where the players of one console join: each presses ok on their controller to be in, back to go out; Start of a player who is in begins, with at least `min` players (1); `max` (4); `join(p)`, `leave(p)`: functions called when one comes or goes. `P:update()` in `_update` returns the players in (their numbers, in the order they came) when the game begins, else `nil`; `P:draw([x, y, w, h])` a card for each place, in the player's colour, with their controller and the key to join; `P.list` who is in now |
| `lib.split(n, [opt])` | the screen cut into views `{x, y, w, h}` for `n` players (1–4): two side by side (`vertical`: one above the other), three or four in the corners (with three the bottom right stays free: a map, the scores); `gap` pixels between them (2); `x, y, w, h` the part of the screen (all of it if missing) |

```lua
local party = lib.party({ min = 2 })
local views, cams

function _update()
  if not views then
    local who = party:update()               -- e.g. {1, 2}
    if who then
      views, cams = lib.split(#who), {}
      for i, v in ipairs(views) do cams[i] = lib.camera({ w = v.w, h = v.h }) end
    end
    return
  end
  -- ... cams[i]:follow(hero[i].x, hero[i].y)
end

function _draw()
  cls(0)
  if not views then party:draw(16, 80, SCREEN_W - 32, 200) return end
  for i, v in ipairs(views) do
    cams[i]:apply(v)
    cams[i]:map()
    draw_world()
  end
  clip()
  camera()
end
```

### Sound, saves, animations, colours

| Function | Description |
|---|---|
| `lib.jingle(notes, [voice, wave, vol])` | a short tune on the voice (3): `notes = { {note, secs, [wave, vol]}, ... }`, the note in Hz or by name (`"C5"`), 0 or `"-"` a rest; `lib.jingle(nil, voice)` stops it, `lib.jingling([voice])` says whether it still plays |
| `lib.store([k, [v]])` | a field of the cartridge's save (`save`/`saved`): `lib.store(k)` reads it, `lib.store(k, v)` writes it (on the SD card only if it changed: not in every frame), `lib.store()` the whole table |
| `lib.best(k, v)` | record `k`: `v` if it beats it (and saves it), and `true` if it is new |
| `lib.frame(frames, fps, [t])` | the element of `frames` that time `t` (`time()` if missing) shows at `fps` a second, looping |
| `lib.anim(frames, fps, [loop])` | an animation of its own: `a:update()` moves on by a frame and returns the element; `a.done` when one that does not loop (`false`) is over; `a:reset()` |
| `lib.mix(c1, c2, t)` / `lib.shade(c, k)` | between two `0xRRGGBB` colours / a colour `k` times brighter (0 black) |

### 3D: Astro Wing's builder

`lib.builder()`: a mesh made of convex pieces; each face of a piece is turned outwards by
itself, so the order of the corners never matters.

| Function | Description |
|---|---|
| `B:piece(points, triangles, [colour])` | a convex piece: `points = { {x, y, z}, ... }`, `triangles = { {i, j, k, [colour]}, ... }` |
| `B:box(x0, y0, z0, x1, y1, z1, colour, [top])` | a box between two corners; `top`: the colour of its top face |
| `B:tetra(p1, p2, p3, p4, colour)` / `B:quad(p1, p2, p3, p4, colour, nx, ny, nz)` | a tetrahedron / a flat quad seen from the side `nx, ny, nz` |
| `B:build()` | the mesh (`mesh()`); the methods return `B`, so they chain |

```lua
local house = lib.builder()
  :box(-1, 0, -1, 1, 1.5, 1, 0xC0A080)
  :piece({ { -1.1, 1.5, -1.1 }, { 1.1, 1.5, -1.1 }, { 1.1, 1.5, 1.1 }, { -1.1, 1.5, 1.1 }, { 0, 2.4, 0 } },
         { { 1, 2, 5 }, { 2, 3, 5 }, { 3, 4, 5 }, { 4, 1, 5 }, { 1, 2, 3 }, { 1, 3, 4 } }, 0xA03020)
  :build()
```

Tests of it all: `make test-gameapi` (bmhost, `tests/gameapi/cart.lua`: every function
with its cases, the buttons too with a script) and `test_game_api` in QEMU.
