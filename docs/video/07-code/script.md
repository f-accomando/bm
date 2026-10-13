# Tropic Trail · Episode 7: Code the game (bm Code + assistant) — copione

Durata ~4:04 (gancio 0:00, titolo 0:08, corpo 0:11). Ritmo calmo: ~480 parole di narrazione in 4:04 (~118 parole/minuto).
Prosa di servizio in italiano, tutto ciò che compare nel video in inglese. Contratti: [`PIANO.md`](../PIANO.md) e `SYNC.md` §2.

**Dipendenza (blocco)**: il codice usa `map3d_build/draw/height/blocked/objects` (SYNC §2). Sono *definite* ma **non ancora
implementate** (AGY-01 `PENDING`): finché `src/bm/map3d.c` non è in `bm-core` il video non si può registrare (come il 4).
Non si bara davanti allo spettatore con una mappa fatta a script.

**Lingua dell'assistente**: la base di conoscenza ha la versione **inglese** (`title_en`/`text_en` e domande `ask:` in inglese,
`src/ai/kb/README.md`); si chiede quindi in **inglese** e il pannello risponde in inglese (`ai_lang_of`). Nel video 2D 05-assistant
si chiedeva in italiano perché allora la KB inglese non c'era.

## Gancio e chiusura

- **Gancio (0:00–0:08)**: 8 s del gioco finito a schermo pieno (480×270 ×4): Pico corre, salta una buca, fa lo spin su una cassa,
  il contatore delle mele sale. Narrazione sopra. Il clip è preso dalla partita registrata alla fine (parte M).
- **Chiusura (3:44–4:04)**: il gioco appena costruito, provato sulla console: frecce, A (salto), B (spin). Frase finale che rimanda
  al video 8 (la partita completa).

## Tabella

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Hook: Pico in the finished game, 4× full screen | Pico runs, jumps and spins through the jungle. Today we write the code that makes him move. |
| 0:08 | Title card "Episode 7 · Code the game" (silent) | — |
| 0:11 | Dev ▸ bm Code: `Ctrl+O`, list of cartridges, `Enter` on Tropic Trail | Episode seven: the game logic, in bm Code. Ctrl+O lists the cartridges on the card; we open Tropic Trail, with the models, the map and the sounds we made in the last episodes. |
| 0:26 | `Ctrl+K` cuts the old preview code line by line | The preview only turned the models on a stand. Ctrl+K cuts a line: we clear it and start again, with a game. |
| 0:36 | Chapter 1: blocks 1a, 1b typed (state, `near`) | First the state: the models, the score, the lives, and the numbers of the six sounds in the bank. Then near, a helper that tells if something is close to Pico. |
| 0:50 | Block 1c typed (`_init`) | In init, keymap names the buttons: jump is A, spin is B. map3d build makes the world from the map, and its objects give Pico's start, the goal, the apples and the crates. Then the jungle song. |
| 1:08 | Chapter 2: blocks 2a, 2b, 2c typed (hit, `_update`, movement) | Update. The stick gives a direction, and Pico moves a little each frame. If map3d blocked says a wall is in the way, he bumps it and takes a hit. Otherwise he steps, and map3d height tells how high the ground is there. |
| 1:30 | Block 2d typed (jump, gravity, hole) | Gravity pulls him down every frame. Press jump on the ground and he goes up, with the jump sound; let go early and the jump is shorter. Where the ground is missing, in a hole, he falls, and that is a hit too: back to the last safe spot. |
| 1:54 | Chapter 3: blocks 3a, 3b, 3c typed (spin, clip, steps) | B starts a spin, twenty-four frames long, with its sound. Then we choose the clip: idle, run, jump, spin or hit, and animate puts Pico in that pose. While he runs, a step sound every fourteen frames. |
| 2:14 | Blocks 3d, 3e, 3f typed (apples, crates, goal) | An apple closer than nine tenths of a unit is collected: one point, and the apple sound. A hundred apples give a life. A crate breaks if Pico spins near it, or lands on top. It leaves an apple, and the jump bounces him. The goal ends the course. |
| 2:38 | Chapter 4: block 4a, then 4b typed (lights, `_draw` start) | Draw. The sun, the sky, the fog and the nearest four lanterns go in one function, as we set them in episode six. Then the camera, which has to follow Pico from behind: a good question for the assistant. |
| 2:56 | `F6`, question typed, answer, `Enter` inserts block 4c | F6 opens the assistant. We ask it in English: third person camera. It finds the guide and shows its code: a camera behind the player, a little higher, looking down. The names match ours, so Enter inserts it just as it is. |
| 3:18 | Blocks 4d, 4e typed (models, shadow, HUD, title and clear screens) | Then the drawing: terrain, Pico's shadow, crates, spinning apples, and Pico himself. The score and the lives come last, over the 3D, and the title and clear screens. |
| 3:33 | `Ctrl+S` in bm Code, then bm SDK: `Ctrl+O`, `Ctrl+B` | Ctrl+S saves the code: bm Code never touches a game file, so it makes an editable copy, tropic dot B M E. In the SDK, Ctrl+B builds the game. |
| 3:44 | The built game, 4× full screen: title, `A`, run, apples, hole, crate | And here it is, on the console: arrows to run, Space to jump, X to spin. Apples, a crate, a hole. Next time, the whole trail, played from the title to the goal. |

## Capitoli (YouTube)

0:00 Pico in action
0:11 Open the project in bm Code
0:36 The world and Pico
1:08 Move, fall and jump
1:54 Spin, apples and crates
2:38 Camera, HUD and the AI assistant
3:33 Build and play

## Il codice (testo, scritto senza indentazione: bm Code rientra da solo)

Regola: solo funzioni verificate in `docs/api/*` (`keymap`, `stick`, `btn`, `btnp`, `model`, `animate`, `draw3d`, `camera3d`,
`light3d`, `sky3d`, `lamp3d`, `fog3d`, `shadow3d`, `bounds3d`, `zclear`, `sfx`, `music`, `time`, `spr`, `print`, `cls`) più
`map3d_*` (SYNC §2, non ancora implementate). La battitura va in time-lapse (1 frame per carattere, `Enter` 2 frame); il codice
completo sta nel repo (`[link code]` nella descrizione YouTube). Costanti di prova (velocità 0.07, salto 0.22, gravità 0.012) tarate
per buche larghe 2 celle: da confermare con la mappa vera.

### Capitolo 1 — il mondo e Pico (0:36–1:08)

Blocco 1a (stato)

```lua
local world, pico, goal, apple_m, crate_m, crate_h, lamps
local apples, crates = {}, {}
local score, lives, hurt, cam_y, state = 0, 3, 0, 0, "title"
local S = { APPLE = 0, CRATE = 1, JUMP = 2, STEP = 3, SPIN = 4, HIT = 5 }
local ICON = 0
```

Blocco 1b (`near`)

```lua
local function near(o, r)
local dx, dy, dz = o.x - pico.x, o.y - pico.y - 0.5, o.z - pico.z
return dx * dx + dy * dy + dz * dz < r * r
end
```

Blocco 1c (`_init`)

```lua
function _init()
keymap({ jump = "a", spin = "b" })
world = map3d_build()
local p = map3d_objects(world, "PLAYER_START")[1]
pico = { m = model("PICO"), x = p.x, y = p.y, z = p.z, vy = 0, face = 0, clip = "idle", at = 0, spin = 0, walk = 0, sx = p.x, sy = p.y, sz = p.z }
goal = map3d_objects(world, "GOAL")[1]
lamps = map3d_objects(world, "LAMP")
for _, o in ipairs(map3d_objects(world, "APPLE")) do apples[#apples + 1] = { x = o.x, y = o.y + 0.7, z = o.z } end
for _, o in ipairs(map3d_objects(world, "CRATE")) do crates[#crates + 1] = { x = o.x, y = o.y, z = o.z } end
apple_m, crate_m = model("APPLE"), model("CRATE")
crate_h = select(5, bounds3d(crate_m))
cam_y = p.y
music(0)
end
```

### Capitolo 2 — muovere, cadere, saltare (1:08–1:54)

Blocco 2a (`hit`)

```lua
local function hit()
if hurt > 0 then return end
hurt, lives = 40, lives - 1
sfx(S.HIT)
end
```

Blocco 2b (inizio di `_update`: titolo e fine)

```lua
function _update()
if state == "title" then
if btnp("jump") then state = "play" end
return
end
if state == "clear" then return end
```

Blocco 2c (movimento, muro)

```lua
local sx, sy = stick()
if hurt > 0 then hurt = hurt - 1; sx, sy = 0, 0 end
local moving = sx ~= 0 or sy ~= 0
local ox, oz = pico.x, pico.z
local nx, nz = ox + sx * 0.07, oz - sy * 0.07
local g = map3d_height(world, nx, nz)
if map3d_blocked(world, nx, pico.y + 0.5, nz, 0.3) then
if moving then hit(); pico.z = oz - 0.6 end
elseif g == nil or g <= pico.y + 0.5 then
pico.x, pico.z = nx, nz
end
if moving then pico.face = math.atan(sx, -sy) end
```

Blocco 2d (salto, gravità, buca)

```lua
if btnp("jump") and pico.ground then
pico.vy = 0.22
sfx(S.JUMP)
end
pico.vy = pico.vy - 0.012
if pico.vy > 0.08 and not btn("jump") then pico.vy = 0.08 end
pico.y = pico.y + pico.vy
g = map3d_height(world, pico.x, pico.z)
pico.ground = false
if g and pico.y <= g and pico.vy <= 0 then
pico.y, pico.vy, pico.ground = g, 0, true
pico.sx, pico.sy, pico.sz = pico.x, g, pico.z
end
if pico.y < -6 then
hit()
pico.x, pico.y, pico.z, pico.vy = pico.sx, pico.sy + 0.5, pico.sz - 1.5, 0
end
```

### Capitolo 3 — spin, mele, casse (1:54–2:38)

Blocchi 3a (spin), 3b (clip), 3c (passi)

```lua
if btnp("spin") and pico.spin == 0 and hurt == 0 then
pico.spin = 24
sfx(S.SPIN)
end
if pico.spin > 0 then pico.spin = pico.spin - 1 end
local want = moving and "run" or "idle"
if not pico.ground then want = "jump" end
if pico.spin > 0 then want = "spin" end
if hurt > 0 then want = "hit" end
if want ~= pico.clip then pico.clip, pico.at = want, 0 end
pico.at = pico.at + 1 / 60
pico.walk = (pico.ground and moving) and pico.walk + 1 or 0
if pico.walk == 14 then pico.walk = 0; sfx(S.STEP) end
```

Blocco 3d (mele)

```lua
for i = #apples, 1, -1 do
if near(apples[i], 0.9) then
table.remove(apples, i)
score = score + 1
if score % 100 == 0 then lives = lives + 1 end
sfx(S.APPLE)
end
end
```

Blocco 3e (casse)

```lua
for i = #crates, 1, -1 do
local c = crates[i]
local dx, dz = c.x - pico.x, c.z - pico.z
local d2 = dx * dx + dz * dz
local spun = pico.spin > 0 and d2 < 2.25
local stomp = d2 < 1 and pico.vy < 0 and pico.y > c.y + crate_h * 0.5 and pico.y < c.y + crate_h + 0.3
if spun or stomp then
table.remove(crates, i)
apples[#apples + 1] = { x = c.x, y = c.y + 0.7, z = c.z }
if stomp then pico.vy = 0.15 end
sfx(S.CRATE)
elseif d2 < 1 and pico.y < c.y + crate_h then
pico.x, pico.z = ox, oz
end
end
```

Blocco 3f (traguardo, vite; chiude `_update`)

```lua
if pico.z >= goal.z then state = "clear" end
if lives <= 0 then lives = 3 end
end
```

### Capitolo 4 — luci, camera con l'assistente, HUD (2:38–3:33)

Blocco 4a (luci)

```lua
local function lights()
light3d(-0.4, 0.8, -0.3, 0.35)
sky3d(0xFFF0D0, 0xA0D0FF, 0x406030)
fog3d(0x9CD0E8, 14, 40)
shadow3d(1)
local n = 0
for _, o in ipairs(lamps) do
if n < 4 and math.abs(o.z - pico.z) < 12 then
n = n + 1
lamp3d(n, o.x, o.y + 1.5, o.z, 4, 1, 0xFFC888)
end
end
for i = n + 1, 4 do lamp3d(i) end
end
```

Blocco 4b (inizio di `_draw`; i nomi `px, py, pz, a` sono scelti per coincidere con la risposta dell'assistente)

```lua
function _draw()
cls(0x9CD0E8)
zclear()
lights()
cam_y = cam_y + (pico.y - cam_y) * 0.1
local px, py, pz, a = pico.x, cam_y, pico.z, 0
```

Blocco 4c: **inserito dall'assistente** (F6, domanda `third person camera`, `Enter`). Risposta attesa: la guida
`guide.3d_camera` di `src/ai/kb/guide_sdk.txt` (`text_en`: 1. the player has a direction a, the camera stays behind at distance d
and a bit higher; 2. `camera3d(x, y, z, yaw, pitch)`: yaw = a, negative pitch to look down; 3. for a smooth camera move closer
every frame), con questo codice. Adattamento: nessuno (le variabili sono già quelle giuste); la morbidezza del punto 3 è `cam_y`.

```lua
local d, h = 6, 3
local cx = px - math.sin(a) * d
local cz = pz - math.cos(a) * d
camera3d(cx, py + h, cz, a, -0.35, 60)
```

Blocco 4d (modelli)

```lua
map3d_draw(world)
animate(pico.m, pico.clip, pico.at)
draw3d(pico.m, pico.x, map3d_height(world, pico.x, pico.z) or pico.y, pico.z, 0, pico.face, 0, 1, 8)
for _, c in ipairs(crates) do draw3d(crate_m, c.x, c.y, c.z) end
for _, o in ipairs(apples) do
draw3d(apple_m, o.x, o.y + math.sin(time() * 3 + o.z) * 0.1, o.z, 0, time() * 2, 0, 1)
end
draw3d(pico.m, pico.x, pico.y, pico.z, 0, pico.face, 0, 1)
```

Blocco 4e (HUD, titolo, fine; chiude `_draw`)

```lua
spr(ICON, 6, 6)
print("x " .. score, 28, 8, 0xFFFFFF)
print("LIVES " .. lives, SCREEN_W - 80, 8, 0xFFFFFF)
if state == "title" then
print("TROPIC TRAIL", (SCREEN_W - 288) // 2, 90, 0xFFE070, 3)
print("PRESS A", (SCREEN_W - 56) // 2, 170, 0xFFFFFF)
elseif state == "clear" then
print("COURSE CLEAR", (SCREEN_W - 192) // 2, 100, 0xFFE070, 2)
end
end
```

## Scelte di design (mie, da confermare)

- Il **`hit`** nasce in due modi: urtare il muro basso mentre si cammina, o cadere in una buca (`y < -6`, ritorno all'ultimo punto
  sicuro). Il bible non dice cosa causa il colpo; senza nemici questa è la scelta più semplice. Il video 8 lo mostra con un urto
  voluto contro il muro.
- Il muro basso è saltabile (il salto sale ~2 unità); la cassa non regge in piedi: si rompe sempre (spin o salto sopra).
- Titolo e schermata finale sono nella cartuccia del 07 perché il video 8 le mostra.

## [DA VERIFICARE]

- Numero di righe del codice di anteprima di `06-sdk.bm` (il `×N` di `Ctrl+K`).
- Valori di luce/cielo/nebbia usati nel video 6 (qui sono valori di prova) e se le lanterne sono già gestite lì.
- Indice di ogni suono nel banco (qui APPLE 0 … HIT 5) e `music(0)` = `JUNGLE`.
- Numero della cella della sheet con l'icona mela (`ICON`, video 2).
- Valori `kind` di `map3d_objects` per 1024/1030/1032 (SYNC §2 mostra solo `"APPLE"`); qui `"PLAYER_START"`, `"GOAL"`, `"LAMP"`.
- Segno della rotazione `ry` di `draw3d` rispetto a `math.atan(sx, -sy)` (Pico potrebbe guardare dalla parte opposta).
- Che `animate` con `t` oltre la durata di una clip non ciclica resti sull'ultima posa.
- Che il pannello F6 in modo codice proponga la guida `guide.3d_camera` per `third person camera` (altrimenti la voce api `camera3d`).
- Se alla prima `Ctrl+S` su un `.bm` bm Code chiede conferma (manuale: «writes an editable copy»; l'SDK chiede *Save an editable copy*).
