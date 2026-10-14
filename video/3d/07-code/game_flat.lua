local world, pico, goal, apple_m, crate_m, crate_h, lamps
local apples, crates = {}, {}
local score, lives, hurt, cam_y, state = 0, 3, 0, 0, "title"
local S = { APPLE = 0, CRATE = 1, JUMP = 2, STEP = 3, SPIN = 4, HIT = 5 }
local ICON = 0
local SC = 0.4
local function near(o, r)
local dx, dy, dz = o.x - pico.x, o.y - pico.y - 0.5, o.z - pico.z
return dx * dx + dy * dy + dz * dz < r * r
end
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
crate_h = select(5, bounds3d(crate_m)) * SC
local x0, _, z0, x1, _, z1 = bounds3d(pico.m)
pico.cx, pico.cz = (x0 + x1) / 2, (z0 + z1) / 2
cam_y = p.y
music(0)
end
local function hit()
if hurt > 0 then return end
hurt, lives = 40, lives - 1
sfx(S.HIT)
end
function _update()
if state == "title" then
if btnp("jump") then state = "play" end
return
end
if state == "clear" then return end
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
for i = #apples, 1, -1 do
if near(apples[i], 0.9) then
table.remove(apples, i)
score = score + 1
if score % 100 == 0 then lives = lives + 1 end
sfx(S.APPLE)
end
end
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
if pico.z >= goal.z then state = "clear" end
if lives <= 0 then lives = 3 end
end
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
function _draw()
cls(0x9CD0E8)
zclear()
lights()
cam_y = cam_y + (pico.y - cam_y) * 0.1
local px, py, pz, a = pico.x, cam_y, pico.z, 0
local d, h = 6, 3
local cx = px - math.sin(a) * d
local cz = pz - math.cos(a) * d
camera3d(cx, py + h, cz, a, -0.35, 60)
map3d_draw(world)
animate(pico.m, pico.clip, pico.at)
local c, s = math.cos(pico.face), math.sin(pico.face)
local ox = pico.x - (pico.cx * c + pico.cz * s) * SC
local oz = pico.z - (pico.cz * c - pico.cx * s) * SC
draw3d(pico.m, ox, map3d_height(world, pico.x, pico.z) or pico.y, oz, 0, pico.face, 0, SC, 8)
for _, c in ipairs(crates) do draw3d(crate_m, c.x, c.y, c.z, 0, 0, 0, SC) end
for _, o in ipairs(apples) do
draw3d(apple_m, o.x, o.y + math.sin(time() * 3 + o.z) * 0.1, o.z, 0, time() * 2, 0, SC)
end
draw3d(pico.m, ox, pico.y, oz, 0, pico.face, 0, SC)
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
