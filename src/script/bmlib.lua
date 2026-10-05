-- bmlib: the library the games share (R10, 2026-10-04): local lib = require "bmlib"
--
-- What every game used to write again (docs/API.md, docs/API-IT.md,
-- "bmlib"): math and random numbers, collisions between rectangles and
-- circles and with the map (by the flags of its tiles, fget/fset), easing
-- and tweens, timers and scripts, particles, a camera that follows, states
-- (title, play, pause), text, menus and the pause menu, jingles, saves,
-- frames of animations, colours and a 3D mesh builder.
--
-- Units: times in seconds (lib.update() once per _update moves them on by
-- 1/60 s), positions in pixels, speeds in pixels per frame. Objects
-- (particles, camera, states, menu, pause, builder) are used with ":".
-- No global is set and no function of the console is shadowed.

local lib = { VERSION = 1 }

local floor, ceil, abs, sqrt, atan, cos, sin = math.floor, math.ceil, math.abs, math.sqrt, math.atan, math.cos,
                                              math.sin
local random, min, max, pi = math.random, math.min, math.max, math.pi
local DT = 1 / 60

lib.TAU = 2 * pi
lib.DT = DT

----------------------------------------------------------------- math

function lib.clamp(v, lo, hi)
  return v < lo and lo or v > hi and hi or v
end

function lib.lerp(a, b, t)
  return a + (b - a) * t
end

-- where v is between a and b (0 at a, 1 at b)
function lib.unlerp(a, b, v)
  return a == b and 0 or (v - a) / (b - a)
end

-- v from the range a0..a1 to the range b0..b1
function lib.remap(v, a0, a1, b0, b1)
  return b0 + (b1 - b0) * lib.unlerp(a0, a1, v)
end

-- v moved toward target by at most step
function lib.approach(v, target, step)
  if v < target then return min(v + step, target) end
  return max(v - step, target)
end

function lib.sign(v)
  return v > 0 and 1 or v < 0 and -1 or 0
end

-- to the nearest integer, or the nearest multiple of step
function lib.round(v, step)
  if step then return floor(v / step + 0.5) * step end
  return floor(v + 0.5)
end

-- v brought into lo..hi (hi excluded), going round: coordinates of a world
-- that wraps
function lib.wrap(v, lo, hi)
  return lo + (v - lo) % (hi - lo)
end

-- index i (1..n) moved by d, going round: the rows of a menu
function lib.cycle(i, d, n)
  return (i - 1 + d) % n + 1
end

function lib.dist(ax, ay, bx, by)
  local dx, dy = bx - ax, by - ay
  return sqrt(dx * dx + dy * dy)
end

-- the distance squared (to compare with r * r, without the root)
function lib.dist2(ax, ay, bx, by)
  local dx, dy = bx - ax, by - ay
  return dx * dx + dy * dy
end

function lib.len(x, y)
  return sqrt(x * x + y * y)
end

-- x, y made 1 long (and the length they had); 0, 0, 0 for 0, 0
function lib.norm(x, y)
  local l = sqrt(x * x + y * y)
  if l == 0 then return 0, 0, 0 end
  return x / l, y / l, l
end

-- the angle from a to b (radians, 0 = right, pi / 2 = down on the screen)
function lib.angle(ax, ay, bx, by)
  return atan(by - ay, bx - ax)
end

-- from angle a to angle b the shortest way: -pi..pi
function lib.angdiff(a, b)
  local d = (b - a) % lib.TAU
  return d > pi and d - lib.TAU or d
end

-- angle a turned toward target by at most step
function lib.turn(a, target, step)
  local d = lib.angdiff(a, target)
  return a + lib.clamp(d, -step, step)
end

-- 8 directions: 0 right, 1 down-right, 2 down, ... 7 up-right (the
-- screen's y goes down); nil for 0, 0
function lib.dir8(x, y)
  if x == 0 and y == 0 then return nil end
  return floor(atan(y, x) / (pi / 4) + 0.5) % 8
end

local D = sqrt(0.5)
lib.DIR8 = { { 1, 0 }, { D, D }, { 0, 1 }, { -D, D }, { -1, 0 }, { -D, -D }, { 0, -1 }, { D, -D } }

----------------------------------------------------------------- random

-- a number in a..b (b excluded); lib.rnd(n): 0..n; lib.rnd(): 0..1
function lib.rnd(a, b)
  if not a then return random() end
  if not b then a, b = 0, a end
  return a + (b - a) * random()
end

function lib.chance(p)
  return random() < p
end

-- an element of the list t, by chance (nil if it is empty)
function lib.choose(t)
  if #t == 0 then return nil end
  return t[random(#t)]
end

function lib.shuffle(t)
  for i = #t, 2, -1 do
    local j = random(i)
    t[i], t[j] = t[j], t[i]
  end
  return t
end

-- a generator of its own, always the same numbers from the same seed (a
-- world made from a seed, games over the network): xorshift32
local Rng = {}
Rng.__index = Rng

function lib.rng(seed)
  local r = setmetatable({}, Rng)
  r:seed(seed or 1)
  return r
end

function Rng:seed(s)
  s = floor(s) & 0xFFFFFFFF
  self.s = s ~= 0 and s or 0x9E3779B9
end

function Rng:next()
  local s = self.s
  s = s ~ (s << 13) & 0xFFFFFFFF
  s = s ~ (s >> 17)
  s = s ~ (s << 5) & 0xFFFFFFFF
  self.s = s
  return s / 4294967296
end

function Rng:range(a, b)
  if not b then a, b = 0, a end
  return a + (b - a) * self:next()
end

-- an integer in a..b (both included)
function Rng:int(a, b)
  if not b then a, b = 1, a end
  return a + floor(self:next() * (b - a + 1))
end

function Rng:pick(t)
  if #t == 0 then return nil end
  return t[self:int(1, #t)]
end

function Rng:chance(p)
  return self:next() < p
end

----------------------------------------------------------------- collisions

-- two rectangles (x, y, w, h) touch
function lib.overlap(ax, ay, aw, ah, bx, by, bw, bh)
  return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah
end

-- two tables with x, y, w, h (w and h 8 if missing) touch
function lib.hit(a, b)
  return lib.overlap(a.x, a.y, a.w or 8, a.h or 8, b.x, b.y, b.w or 8, b.h or 8)
end

-- the point is in the rectangle
function lib.inside(px, py, x, y, w, h)
  return px >= x and py >= y and px < x + w and py < y + h
end

-- two circles touch
function lib.circles(ax, ay, ar, bx, by, br)
  local dx, dy, r = bx - ax, by - ay, ar + br
  return dx * dx + dy * dy < r * r
end

-- a circle and a rectangle touch
function lib.circrect(cx, cy, r, x, y, w, h)
  local dx = cx - lib.clamp(cx, x, x + w)
  local dy = cy - lib.clamp(cy, y, y + h)
  return dx * dx + dy * dy < r * r
end

----------------------------------------------------------------- the map

-- The flags of the tiles (fget/fset) as bmlib reads them: a convention,
-- the game can choose others with lib.tiles{}
lib.SOLID, lib.PLATFORM, lib.LADDER, lib.WATER, lib.HURT = 1, 2, 4, 8, 16
lib.GRAVITY, lib.MAXFALL = 0.25, 6

local T = { layer = 1, solid = 1, platform = 2, edge = false }

-- how the map stops the bodies: layer (number or name), solid and platform
-- (the flags, as masks: 0 = none), edge (true: out of the map is solid)
function lib.tiles(opt)
  for k, v in pairs(opt or {}) do T[k] = v end
  return T
end

local function blocked(x, y, w, h, mask)
  if T.edge then
    local mw, mh = msize()
    mw, mh = mw * 8, mh * 8
    if x < 0 or y < 0 or x + w > mw or y + h > mh or (w == 0 and x >= mw) or (h == 0 and y >= mh) then
      return true
    end
  end
  return mask ~= 0 and mflags(x, y, w, h, T.layer) & mask ~= 0
end

-- a rectangle (or a point, without w and h) touches something solid
function lib.solid(x, y, w, h)
  return blocked(x, y, w or 0, h or 0, T.solid)
end

-- moves a body {x, y, w, h} (w and h 8 if missing) by dx, dy, stopping it
-- against the solid tiles and sliding along them; it falls on a platform
-- only from above (and not with b.drop set: down through it). Returns hx,
-- hy: -1 / 1 where it hit (left / right, up / down), else 0.
function lib.move(b, dx, dy)
  local w, h = b.w or 8, b.h or 8
  local hx, hy = 0, 0
  local n = ceil(abs(dx) / 7)              -- steps under a tile: none is jumped over
  for _ = 1, n do
    local s = dx / n
    local nx = b.x + s
    if blocked(nx, b.y, w, h, T.solid) then
      if s > 0 then
        b.x = max(b.x, (ceil((nx + w) / 8) - 1) * 8 - w)
      else
        b.x = min(b.x, (floor(nx / 8) + 1) * 8)
      end
      hx = s > 0 and 1 or -1
      break
    end
    b.x = nx
  end
  n = ceil(abs(dy) / 7)
  for _ = 1, n do
    local s = dy / n
    local ny = b.y + s
    local stop = blocked(b.x, ny, w, h, T.solid)
    if not stop and s > 0 and T.platform ~= 0 and not b.drop then
      local r = ceil((ny + h) / 8) - 1     -- the row of the feet
      stop = b.y + h <= r * 8 and mflags(b.x, r * 8, w, 0, T.layer) & T.platform ~= 0
    end
    if stop then
      if s > 0 then
        b.y = max(b.y, (ceil((ny + h) / 8) - 1) * 8 - h)
      else
        b.y = min(b.y, (floor(ny / 8) + 1) * 8)
      end
      hy = s > 0 and 1 or -1
      break
    end
    b.y = ny
  end
  return hx, hy
end

-- one frame of a body with gravity (a platform game): b.vx, b.vy (pixels
-- per frame), b.gravity (lib.GRAVITY) and b.maxfall (lib.MAXFALL); then
-- b.ground is true on the ground, and the speed that hit is 0. Returns
-- what lib.move returns.
function lib.step(b)
  b.vx = b.vx or 0
  b.vy = min((b.vy or 0) + (b.gravity or lib.GRAVITY), b.maxfall or lib.MAXFALL)
  local hx, hy = lib.move(b, b.vx, b.vy)
  if hx ~= 0 then b.vx = 0 end
  if hy ~= 0 then b.vy = 0 end
  b.ground = hy > 0
  return hx, hy
end

-- the first tile with a flag of mask (lib.tiles' solid if missing) on the
-- segment from x0, y0 to x1, y1: x, y where the segment enters it and its
-- cell mx, my; nil if the way is free (line of sight, bullets)
function lib.ray(x0, y0, x1, y1, mask)
  mask = mask or T.solid
  local dx, dy = x1 - x0, y1 - y0
  local cx, cy = floor(x0 / 8), floor(y0 / 8)
  local ex, ey = floor(x1 / 8), floor(y1 / 8)
  local sx, sy = dx > 0 and 1 or -1, dy > 0 and 1 or -1
  local tdx = dx ~= 0 and abs(8 / dx) or math.huge
  local tdy = dy ~= 0 and abs(8 / dy) or math.huge
  local tx = dx > 0 and ((cx + 1) * 8 - x0) / dx or dx < 0 and (cx * 8 - x0) / dx or math.huge
  local ty = dy > 0 and ((cy + 1) * 8 - y0) / dy or dy < 0 and (cy * 8 - y0) / dy or math.huge
  local t = 0
  for _ = 0, abs(ex - cx) + abs(ey - cy) + 1 do
    if blocked(cx * 8, cy * 8, 0, 0, mask) then
      return x0 + dx * t, y0 + dy * t, cx, cy
    end
    if cx == ex and cy == ey then return nil end
    if tx < ty then
      t, tx, cx = tx, tx + tdx, cx + sx
    else
      t, ty, cy = ty, ty + tdy, cy + sy
    end
    if t > 1 then return nil end
  end
  return nil
end

----------------------------------------------------------------- hitboxes and hurtboxes

-- two bodies {x, y, w, h} that overlap are pushed apart along the axis
-- where they overlap less, half each (a.fixed or b.fixed: only the other
-- moves; both fixed: neither): crowds, fighters that do not pass through
-- each other. Returns true if they touched.
function lib.separate(a, b)
  local aw, ah, bw, bh = a.w or 8, a.h or 8, b.w or 8, b.h or 8
  local ox = min(a.x + aw, b.x + bw) - max(a.x, b.x)
  local oy = min(a.y + ah, b.y + bh) - max(a.y, b.y)
  if ox <= 0 or oy <= 0 then return false end
  if a.fixed and b.fixed then return true end
  local ka = b.fixed and 1 or a.fixed and 0 or 0.5
  if ox < oy then
    local d = a.x + aw / 2 < b.x + bw / 2 and -ox or ox
    a.x, b.x = a.x + d * ka, b.x - d * (1 - ka)
  else
    local d = a.y + ah / 2 < b.y + bh / 2 and -oy or oy
    a.y, b.y = a.y + d * ka, b.y - d * (1 - ka)
  end
  return true
end

-- a box of a frame (b.x, b.y from the frame's top-left corner, b.w, b.h:
-- a box of zboxes() or of the game's own tables) in the world, for the
-- frame drawn at x, y; flip: the frame is mirrored (facing left), fw its
-- width. Returns x, y, w, h.
function lib.box(b, x, y, flip, fw)
  if flip then return x + (fw or 8) - b.x - b.w, y + b.y, b.w, b.h end
  return x + b.x, y + b.y, b.w, b.h
end

local Hits = {}
Hits.__index = Hits

-- the hitboxes (what hurts: a punch, a sword, a bullet) and hurtboxes (where
-- a body can be hurt) of a frame, and who hits whom. In each _update:
-- H:clear(), then every body gives its hurtboxes (H:hurt) and every attack
-- its hitboxes (H:hit), then H:check() gives the contacts.
function lib.hits()
  return setmetatable({ hurts = {}, hitl = {}, done = {}, seen = {} }, Hits)
end

-- a new frame: the boxes of the last one go; the attacks (id) not given
-- in the last frame are over (their targets can be hit again)
function Hits:clear()
  for id in pairs(self.done) do
    if not self.seen[id] then self.done[id] = nil end
  end
  self.seen, self.hurts, self.hitl = {}, {}, {}
end

local function opt_box(list, who, x, y, w, h, o)
  local b = { who = who, x = x, y = y, w = w, h = h, o = o or {} }
  list[#list + 1] = b
  return b
end

-- a hurtbox of who (any value: the body, its table): o.team (the same team
-- does not hurt itself), o.part (a name: "head"; give the parts that count
-- most first), o.z and o.depth (a third dimension: the lane of a beat 'em
-- up, the height in a top-down game)
function Hits:hurt(who, x, y, w, h, o)
  return opt_box(self.hurts, who, x, y, w, h, o)
end

-- a hitbox of the attacker by: o.team, o.id (an attack: it hits each body
-- once while the same id is given frame after frame; the boxes of one
-- attack share it; without id each hitbox hurts in every frame it touches),
-- o.clash (two hitboxes with clash that touch make
-- a contact "clash": blades that meet), o.z and o.depth, and everything the
-- game wants with it (damage, knock...)
function Hits:hit(by, x, y, w, h, o)
  o = o or {}
  if o.id ~= nil then self.seen[o.id] = true end
  return opt_box(self.hitl, by, x, y, w, h, o)
end

-- the boxes of a sprite zone's frame (zboxes: hurt and hit; body and the
-- game's own are left) for who, drawn with zspr(name, x, y, frame, flip):
-- the hurtboxes with o (team, part...), the hitboxes with o.attack (the
-- options of H:hit; its team: o.team if missing)
function Hits:zone(who, name, frame, x, y, flip, o)
  o = o or {}
  local _, _, fw = zone(name)
  local group = {}                      -- the frame's hitboxes: one attack
  for _, b in ipairs(zboxes(name, frame)) do
    local bx, by, bw, bh = lib.box(b, x, y, flip, fw)
    if b.kind == "hurt" then
      self:hurt(who, bx, by, bw, bh, o)
    elseif b.kind == "hit" then
      local a = o.attack or {}
      if a.team == nil and o.team ~= nil then
        local c = {}
        for k, v in pairs(a) do c[k] = v end
        c.team, a = o.team, c
      end
      self:hit(who, bx, by, bw, bh, a).group = group
    end
  end
end

local function touch(a, b)
  if not lib.overlap(a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h) then return false end
  local za, zb = a.o.z, b.o.z
  if za and zb and abs(za - zb) > ((a.o.depth or 0) + (b.o.depth or 0)) / 2 then return false end
  return true
end

local function foes(a, b)
  if a.who == b.who then return false end
  local ta, tb = a.o.team, b.o.team
  return ta == nil or tb == nil or ta ~= tb
end

local function contact(kind, h, t)
  local x0, y0 = max(h.x, t.x), max(h.y, t.y)
  local x1, y1 = min(h.x + h.w, t.x + t.w), min(h.y + h.h, t.y + t.h)
  return { kind = kind, by = h.who, to = t.who, hit = h.o, part = t.o.part, x = (x0 + x1) / 2, y = (y0 + y1) / 2 }
end

-- the contacts of this frame, in the order the hitboxes were given: a list
-- of {kind = "hit" or "clash", by (the attacker), to (the body hurt, or the
-- other attacker), hit (the o of H:hit), part (of the hurtbox), x, y (the
-- middle of where they touch)}. An attack hits a body once a frame (its
-- first box that touches) and, with an id, once until the id ends.
function Hits:check()
  local out, once = {}, {}
  local hl = self.hitl
  for i, h in ipairs(hl) do
    local key = h.o.id
    if key == nil then key = h.group or h end
    local done = h.o.id ~= nil and self.done[h.o.id]
    for _, t in ipairs(self.hurts) do
      if foes(h, t) and touch(h, t) then
        local k1 = once[key] or {}
        once[key] = k1
        if not k1[t.who] and not (done and done[t.who]) then
          k1[t.who] = true
          if h.o.id ~= nil then
            done = done or {}
            self.done[h.o.id] = done
            done[t.who] = true
          end
          out[#out + 1] = contact("hit", h, t)
        end
      end
    end
    if h.o.clash then
      for j = i + 1, #hl do
        local g = hl[j]
        if g.o.clash and foes(h, g) and touch(h, g) then out[#out + 1] = contact("clash", h, g) end
      end
    end
  end
  return out
end

-- the boxes of the frame, to see them while making the game: hurtboxes
-- blue, hitboxes red (or the colours given)
function Hits:draw(hurt_c, hit_c)
  for _, b in ipairs(self.hurts) do rect(b.x, b.y, b.w, b.h, hurt_c or 0x40A0FF) end
  for _, b in ipairs(self.hitl) do rect(b.x, b.y, b.w, b.h, hit_c or 0xFF4040) end
end

----------------------------------------------------------------- easing

-- functions of t in 0..1, 0 at 0 and 1 at 1
local E = {}
lib.ease = E
function E.linear(t) return t end
function E.inquad(t) return t * t end
function E.outquad(t) return 1 - (1 - t) * (1 - t) end
function E.inoutquad(t) return t < 0.5 and 2 * t * t or 1 - (-2 * t + 2) ^ 2 / 2 end
function E.incubic(t) return t * t * t end
function E.outcubic(t) return 1 - (1 - t) ^ 3 end
function E.inoutcubic(t) return t < 0.5 and 4 * t * t * t or 1 - (-2 * t + 2) ^ 3 / 2 end
function E.insine(t) return 1 - cos(t * pi / 2) end
function E.outsine(t) return sin(t * pi / 2) end
function E.inoutsine(t) return -(cos(pi * t) - 1) / 2 end
local C1, C3 = 1.70158, 2.70158
function E.inback(t) return C3 * t * t * t - C1 * t * t end
function E.outback(t) return 1 + C3 * (t - 1) ^ 3 + C1 * (t - 1) ^ 2 end
function E.inoutback(t)
  local c2 = C1 * 1.525
  if t < 0.5 then return (2 * t) ^ 2 * ((c2 + 1) * 2 * t - c2) / 2 end
  return ((2 * t - 2) ^ 2 * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2
end
function E.outelastic(t)
  if t <= 0 or t >= 1 then return t <= 0 and 0 or 1 end
  return 2 ^ (-10 * t) * sin((t * 10 - 0.75) * (2 * pi / 3)) + 1
end
function E.outbounce(t)
  local n, d = 7.5625, 2.75
  if t < 1 / d then return n * t * t end
  if t < 2 / d then t = t - 1.5 / d; return n * t * t + 0.75 end
  if t < 2.5 / d then t = t - 2.25 / d; return n * t * t + 0.9375 end
  t = t - 2.625 / d
  return n * t * t + 0.984375
end
function E.smooth(t)
  t = lib.clamp(t, 0, 1)
  return t * t * (3 - 2 * t)
end

----------------------------------------------------------------- tweens, timers, scripts

-- everything that lib.update() moves on
local jobs = {}
local tunes = {}
lib.time = 0

local Job = {}
Job.__index = Job
function Job:cancel() self.dead = true end

local function job(t)
  t.dead = false
  jobs[#jobs + 1] = setmetatable(t, Job)
  return t
end

-- the fields of obj go to the values of `to` in secs seconds (ease: a
-- function or the name of one of lib.ease; linear if missing), then
-- done(obj). Returns a handle: h:cancel().
function lib.tween(obj, to, secs, ease, done)
  local from = {}
  for k in pairs(to) do from[k] = obj[k] end
  if type(ease) == "string" then
    ease = E[ease] or error("lib.tween: no easing called " .. ease, 2)
  end
  return job { kind = "tween", obj = obj, from = from, to = to, secs = secs, t = 0, ease = ease or E.linear,
               done = done }
end

-- fn() in secs seconds
function lib.after(secs, fn)
  return job { kind = "timer", t = secs, fn = fn, times = 1 }
end

-- fn() every secs seconds (times times, for ever if missing); fn
-- returning false stops it
function lib.every(secs, fn, times)
  return job { kind = "timer", t = secs, every = secs, fn = fn, times = times or math.huge }
end

-- fn(...) as a script that can wait: lib.wait(secs), lib.waitfor(cond).
-- It runs now until its first wait, then lib.update() takes it on.
function lib.script(fn, ...)
  local h = job { kind = "script", co = coroutine.create(fn), wait = 0 }
  h.run = function(...)
    local ok, w = coroutine.resume(h.co, ...)
    if not ok then
      h.dead = true
      error("lib.script: " .. tostring(w), 0)
    end
    if coroutine.status(h.co) == "dead" then h.dead = true; return end
    if type(w) == "function" then h.cond, h.wait = w, 0 else h.cond, h.wait = nil, w or 0 end
  end
  h.run(...)
  return h
end

local function in_script(name)
  local _, main = coroutine.running()
  if main then error(name .. ": only inside lib.script", 3) end
end

-- inside a lib.script: waits secs seconds (one frame if missing)
function lib.wait(secs)
  in_script("lib.wait")
  coroutine.yield(secs or 0)
end

-- inside a lib.script: waits until cond() is true
function lib.waitfor(cond)
  in_script("lib.waitfor")
  if not cond() then coroutine.yield(cond) end
end

function lib.cancel(h)
  if h then h.dead = true end
end

-- the fields keys of t that are above 0 go down by d (1/60 if missing),
-- not under 0: the cool-downs of a hero, counted in seconds (or frames, d
-- = 1)
function lib.countdown(t, keys, d)
  d = d or DT
  for _, k in ipairs(keys) do
    local v = t[k]
    if v and v > 0 then t[k] = v > d and v - d or 0 end
  end
end

-- cancels every tween, timer, script and jingle (a new level, a new state)
function lib.clear()
  for _, j in ipairs(jobs) do j.dead = true end
  jobs = {}
  for v in pairs(tunes) do tunes[v] = nil end
end

local function play_tunes(dt)
  for v, j in pairs(tunes) do
    j.t = j.t - dt
    while tunes[v] == j and j.t <= 0 do
      local e = j.notes[j.i]
      if not e then tunes[v] = nil; break end
      local n = e[1]
      if n and n ~= 0 and n ~= "-" then
        note(v, n, max(1, floor(e[2] * 850)), e[3] or j.wave, e[4] or j.vol)
      end
      j.t, j.i = j.t + e[2], j.i + 1
    end
  end
end

-- once in each _update (dt: 1/60 if missing): tweens, timers, scripts and
-- jingles go on
function lib.update(dt)
  dt = dt or DT
  lib.time = lib.time + dt
  local list, n = jobs, #jobs
  for i = 1, n do
    local j = list[i]
    if not j.dead then
      if j.kind == "tween" then
        j.t = j.t + dt
        local k = j.secs > 0 and min(j.t / j.secs, 1) or 1
        local e = k >= 1 and 1 or j.ease(k)
        for key, to in pairs(j.to) do j.obj[key] = j.from[key] + (to - j.from[key]) * e end
        if k >= 1 then
          j.dead = true
          if j.done then j.done(j.obj) end
        end
      elseif j.kind == "timer" then
        j.t = j.t - dt
        if j.t <= 0 then
          j.times = j.times - 1
          local r = j.fn()
          if j.times <= 0 or r == false or not j.every then
            j.dead = true
          else
            j.t = j.t + j.every
          end
        end
      else
        if j.cond then
          if j.cond() then j.run() end
        else
          j.wait = j.wait - dt
          if j.wait <= 0 then j.run() end
        end
      end
    end
  end
  local keep = {}                            -- the dead ones go (jobs may have grown meanwhile)
  for _, j in ipairs(jobs) do
    if not j.dead then keep[#keep + 1] = j end
  end
  jobs = keep
  play_tunes(dt)
end

----------------------------------------------------------------- lists

-- calls fn(item, i) for each item of the list; those for which it returns
-- false go away (the others keep their order)
function lib.each(list, fn)
  local k = 0
  local n = #list
  for i = 1, n do
    local it = list[i]
    if fn(it, i) ~= false then
      k = k + 1
      list[k] = it
    end
  end
  for i = n, k + 1, -1 do list[i] = nil end
  return list
end

-- the items with .dead set go away from the list (in order)
function lib.sweep(list)
  return lib.each(list, function(it) return not it.dead end)
end

----------------------------------------------------------------- particles

local Parts = {}
Parts.__index = Parts

-- a pool of at most max particles (200): when it is full a new one takes
-- the place of the oldest
function lib.particles(maxn)
  return setmetatable({ list = {}, max = maxn or 200, over = 1 }, Parts)
end

-- one particle: speeds in pixels per frame, life in seconds; o (optional):
-- size (radius; 0 or 1 a pixel), gravity, drag (speed kept per frame),
-- colors (a list: the colour through its life), shrink, floor (y where it
-- bounces), bounce (0.3)
function Parts:add(x, y, vx, vy, life, color, o)
  o = o or {}
  local p = { x = x, y = y, vx = vx, vy = vy, life = life, full = life, color = color or 0xFFFFFF,
              colors = o.colors, size = o.size or 0, gravity = o.gravity or 0, drag = o.drag or 1,
              shrink = o.shrink, floor = o.floor, bounce = o.bounce or 0.3 }
  local l = self.list
  if #l < self.max then
    l[#l + 1] = p
  else
    l[self.over] = p
    self.over = self.over % self.max + 1
  end
  return p
end

-- n particles from x, y: o.speed (2; random up to it), o.angle and o.spread
-- (radians: all round if missing), o.life (0.5 s, each a bit more or less),
-- o.color or o.colors, and the options of :add
function Parts:burst(x, y, n, o)
  o = o or {}
  local speed, spread, a0 = o.speed or 2, o.spread or lib.TAU, o.angle or 0
  local life = o.life or 0.5
  for _ = 1, n do
    local a = a0 + (random() - 0.5) * spread
    local s = speed * (0.3 + 0.7 * random())
    self:add(x, y, cos(a) * s, sin(a) * s, life * (0.7 + 0.6 * random()), o.color, o)
  end
end

function Parts:update(dt)
  dt = dt or DT
  lib.each(self.list, function(p)
    p.life = p.life - dt
    if p.life <= 0 then return false end
    p.vx = p.vx * p.drag
    p.vy = p.vy * p.drag + p.gravity
    p.x, p.y = p.x + p.vx, p.y + p.vy
    if p.floor and p.y > p.floor then
      p.y, p.vy, p.vx = p.floor, -p.vy * p.bounce, p.vx * 0.8
    end
  end)
  self.over = 1
end

function Parts:draw()
  for _, p in ipairs(self.list) do
    local k = p.life / p.full
    local c = p.color
    if p.colors then c = p.colors[min(#p.colors, floor((1 - k) * #p.colors) + 1)] end
    local r = p.shrink and p.size * k or p.size
    if r <= 1 then pset(floor(p.x), floor(p.y), c) else circfill(floor(p.x), floor(p.y), floor(r + 0.5), c) end
  end
end

function Parts:count() return #self.list end
function Parts:clear() self.list = {} end

----------------------------------------------------------------- camera

local Cam = {}
Cam.__index = Cam

-- a 2D camera: o.smooth (0.15: the part of the way it goes each frame; 1
-- = it sticks to the target), o.dead {w, h} (a box in the middle where the
-- target moves without the camera), o.bounds {x0, y0, x1, y1} in pixels
-- or true (the map, msize()), o.offset {x, y} (the target's place from
-- the middle of the screen), o.w, o.h (the screen if missing)
function lib.camera(o)
  o = o or {}
  local c = setmetatable({ x = o.x or 0, y = o.y or 0, smooth = o.smooth or 0.15, dead = o.dead,
                           bounds = o.bounds, offset = o.offset, w = o.w, h = o.h,
                           shake_a = 0, shake_t = 0, shake_n = 0, sx = 0, sy = 0 }, Cam)
  return c
end

function Cam:size()
  return self.w or SCREEN_W, self.h or SCREEN_H
end

-- one frame toward the point x, y (in the middle of the screen); snap:
-- there at once
function Cam:follow(x, y, snap)
  local w, h = self:size()
  local gx, gy = x - w / 2, y - h / 2
  if self.offset then gx, gy = gx - self.offset[1], gy - self.offset[2] end
  if self.dead and not snap then
    local dw, dh = self.dead[1] / 2, self.dead[2] / 2
    gx = gx > self.x + dw and gx - dw or gx < self.x - dw and gx + dw or self.x
    gy = gy > self.y + dh and gy - dh or gy < self.y - dh and gy + dh or self.y
  end
  if snap or self.smooth >= 1 then
    self.x, self.y = gx, gy
  else
    self.x = self.x + (gx - self.x) * self.smooth
    self.y = self.y + (gy - self.y) * self.smooth
  end
  local b = self.bounds
  if b then
    if b == true then
      local mw, mh = msize()
      b = { 0, 0, mw * 8, mh * 8 }
    end
    self.x = b[3] - b[1] < w and b[1] + (b[3] - b[1] - w) / 2 or lib.clamp(self.x, b[1], b[3] - w)
    self.y = b[4] - b[2] < h and b[2] + (b[4] - b[2] - h) / 2 or lib.clamp(self.y, b[2], b[4] - h)
  end
end

-- the screen shakes up to amount pixels, less and less for secs seconds
-- (0.3); a smaller shake does not cut a bigger one
function Cam:shake(amount, secs)
  secs = secs or 0.3
  if amount * secs >= self.shake_a * self.shake_t then
    self.shake_a, self.shake_t, self.shake_n = amount, secs, secs
  end
end

-- camera() at the camera's place (and the shake): call it in _draw before
-- the world; returns the x, y used. With a view {x, y, w, h} (lib.split:
-- a player's part of the screen) drawing is clipped to it and the camera's
-- top left corner is the view's.
function Cam:apply(view)
  self.sx, self.sy = 0, 0
  if self.shake_t > 0 then
    local k = self.shake_a * self.shake_t / self.shake_n
    self.sx, self.sy = floor((random() * 2 - 1) * k + 0.5), floor((random() * 2 - 1) * k + 0.5)
    self.shake_t = self.shake_t - DT
  end
  local x, y = floor(self.x + 0.5) + self.sx, floor(self.y + 0.5) + self.sy
  if view then
    clip(view.x, view.y, view.w, view.h)
    camera(x - view.x, y - view.y)
  else
    camera(x, y)
  end
  return x, y
end

-- the map's cells that are on screen (one layer, mask as in map())
function Cam:map(layer, mask)
  local w, h = self:size()
  local x, y = floor(self.x + 0.5) + self.sx, floor(self.y + 0.5) + self.sy
  local mx, my = floor(x / 8), floor(y / 8)
  map(mx, my, mx * 8, my * 8, w // 8 + 2, h // 8 + 2, layer, mask)
end

-- the rectangle (a point without w, h) is on screen
function Cam:sees(x, y, w, h)
  local sw, sh = self:size()
  return lib.overlap(x, y, w or 1, h or 1, self.x, self.y, sw, sh)
end

-- a point of the world on the screen (in the view, if given)
function Cam:screen(x, y, view)
  local vx, vy = view and view.x or 0, view and view.y or 0
  return x - floor(self.x + 0.5) - self.sx + vx, y - floor(self.y + 0.5) - self.sy + vy
end

----------------------------------------------------------------- states

local SM = {}
SM.__index = SM

-- states of the game: defs = { title = {enter=, update=, draw=, exit=},
-- play = {...} }, each function called with its table (function
-- play:update() ... end); s.t is the time in the state (seconds). Starts
-- in `first` (with its arguments) if given.
function lib.states(defs, first, ...)
  local m = setmetatable({ defs = defs, stack = {} }, SM)
  if first then m:go(first, ...) end
  return m
end

local function enter(m, name, ...)
  local s = m.defs[name] or error("lib.states: no state called " .. tostring(name), 3)
  m.stack[#m.stack + 1] = s
  s.name, s.t, s.machine = name, 0, m
  m.name = name
  if s.enter then s:enter(...) end
end

local function leave(m)
  local s = table.remove(m.stack)
  if s and s.exit then s:exit() end
  local top = m.stack[#m.stack]
  m.name = top and top.name
end

-- to state name: the states open now leave (exit), name enters (enter)
function SM:go(name, ...)
  while #self.stack > 0 do leave(self) end
  enter(self, name, ...)
end

-- name over the one now (a pause, a dialogue): it updates alone, both draw
function SM:push(name, ...)
  enter(self, name, ...)
end

-- back to the state under it
function SM:pop()
  leave(self)
end

function SM:is(name) return self.name == name end
function SM:top() return self.stack[#self.stack] end

-- in _update: the state on top
function SM:update()
  local s = self.stack[#self.stack]
  if s then
    s.t = s.t + DT
    if s.update then s:update() end
  end
end

-- in _draw: every open state, from the bottom (the game under its pause)
function SM:draw()
  local st = self.stack
  for i = 1, #st do
    local s = st[i]
    if s.draw then s:draw() end
  end
end

----------------------------------------------------------------- text

local function font_size()
  local w, h = font()
  return w or 8, h or 16
end

-- the width of a text in pixels with the font of print() now (the widest
-- line), and its height
function lib.textw(s, scale)
  local cw, ch = font_size()
  scale = scale or 1
  local wmax, lines = 0, 0
  for line in (tostring(s) .. "\n"):gmatch("(.-)\n") do
    wmax, lines = max(wmax, #line), lines + 1
  end
  return wmax * cw * scale, lines * ch * scale
end

-- text in the middle of the screen (or of x..x + w); grid: on the font's
-- columns (text that stays still, as the menus). Returns its x.
function lib.printc(s, y, c, scale, x, w, grid)
  s = tostring(s)
  x, w = x or 0, w or SCREEN_W
  local tw = lib.textw(s, scale)
  local px = x + (w - tw) // 2
  if grid then
    local cw = font_size() * (scale or 1)
    px = px // cw * cw
  end
  print(s, px, y, c, scale)
  return px
end

-- text that ends at x
function lib.printr(s, x, y, c, scale)
  s = tostring(s)
  local px = x - lib.textw(s, scale)
  print(s, px, y, c, scale)
  return px
end

-- text with a shadow under it, right and down (black if missing)
function lib.prints(s, x, y, c, shadow, scale)
  local k = scale or 1
  print(s, x + k, y + k, shadow or 0, scale)
  return print(s, x, y, c, scale)
end

-- text with an outline all round (black if missing)
function lib.printo(s, x, y, c, outline, scale)
  local k = scale or 1
  for dy = -k, k, k do
    for dx = -k, k, k do
      if dx ~= 0 or dy ~= 0 then print(s, x + dx, y + dy, outline or 0, scale) end
    end
  end
  return print(s, x, y, c, scale)
end

-- a bar: v of vmax filled with c, the rest with back (missing: empty), a
-- border if given
function lib.bar(x, y, w, h, v, vmax, c, back, border)
  local k = vmax > 0 and lib.clamp(v / vmax, 0, 1) or 0
  if back then rectfill(x, y, w, h, back) end
  local fw = floor(w * k + 0.5)
  if fw > 0 then rectfill(x, y, fw, h, c or 0xFFFFFF) end
  if border then rect(x - 1, y - 1, w + 2, h + 2, border) end
end

-- true half of the time, changing every period seconds (0.5): "press A"
function lib.blink(period, t)
  return floor((t or time()) / (period or 0.5)) % 2 == 0
end

-- seconds as "m:ss" ("h:mm:ss" from an hour); tenths: "m:ss.d"
function lib.timestr(secs, tenths)
  secs = max(0, secs)
  local h, m, s = floor(secs / 3600), floor(secs / 60) % 60, floor(secs) % 60
  local out = h > 0 and string.format("%d:%02d:%02d", h, m, s) or string.format("%d:%02d", m, s)
  if tenths then out = out .. "." .. floor(secs * 10) % 10 end
  return out
end

----------------------------------------------------------------- input and menus

local held = {}

-- btnp() that repeats while the button is held (menus, cursors): the first
-- frame, then after delay frames (15) every rate frames (4). i as for
-- btn(): a number, a button's name or an action of keymap(). Call it in
-- every frame for the same button.
function lib.btnr(i, p, delay, rate)
  local key = tostring(i) .. ":" .. tostring(p)
  local f = stat(3)
  local r = held[key]
  if not r then r = { n = 0, f = -1 }; held[key] = r end
  if r.f ~= f then
    local down
    if p then down = btn(i, p) else down = btn(i) end
    r.n = down and (r.f == f - 1 and r.n + 1 or 1) or 0
    r.f = f
  end
  delay, rate = delay or 15, rate or 4
  return r.n == 1 or (r.n > delay and (r.n - delay) % rate == 0)
end

local Menu = {}
Menu.__index = Menu

-- a list to choose from: items are texts or tables {label=, value=
-- (text or function giving it), change = function(item, d) (left / right),
-- ok = function(item), off = true (not chosen)}; o.p the player (any if
-- missing), o.wrap (true: from the last row to the first)
function lib.menu(items, o)
  o = o or {}
  return setmetatable({ items = items, sel = o.sel or 1, p = o.p, wrap = o.wrap ~= false }, Menu)
end

local function label(it)
  return type(it) == "table" and (it.label or "") or tostring(it)
end

function Menu:item() return self.items[self.sel] end

-- in _update: up and down move (repeating), left and right change, ok
-- chooses (it.ok(it), and returns it, "ok"), back returns nil, "back"
function Menu:update()
  local n, p = #self.items, self.p
  if n == 0 then return nil end
  local function bp(b) if p then return btnp(b, p) end return btnp(b) end
  for _, d in ipairs({ { "up", -1 }, { "down", 1 } }) do
    if lib.btnr(d[1], p) then
      local s = self.sel
      for _ = 1, n do
        local ns = self.wrap and lib.cycle(s, d[2], n) or lib.clamp(s + d[2], 1, n)
        s = ns
        local it = self.items[s]
        if not (type(it) == "table" and it.off) then break end
      end
      self.sel = s
    end
  end
  local it = self.items[self.sel]
  if type(it) == "table" and it.change then
    if lib.btnr("left", p) then it.change(it, -1) end
    if lib.btnr("right", p) then it.change(it, 1) end
  end
  if bp("ok") then
    if type(it) == "table" and it.ok then it.ok(it) end
    return it, "ok"
  end
  if bp("back") then return nil, "back" end
  return nil
end

-- the rows from x, y: o.w (width, for the values on the right and the bar
-- of the row chosen), o.c, o.sel_c, o.bar (colour of the chosen row's bar),
-- o.dim (rows that cannot be chosen), o.scale, o.gap (pixels between rows)
function Menu:draw(x, y, o)
  o = o or {}
  local scale = o.scale or 1
  local _, ch = font_size()
  local step = o.gap or (ch + 4) * scale
  local w = o.w or 160 * scale
  for i, it in ipairs(self.items) do
    local ry = y + (i - 1) * step
    local sel = i == self.sel
    local off = type(it) == "table" and it.off
    if sel and o.bar ~= false then rectfill(x - 2 * scale, ry - 2 * scale, w + 4 * scale, ch * scale + 4 * scale,
                                            o.bar or 0x2A3442) end
    local c = off and (o.dim or 0x505868) or sel and (o.sel_c or 0xFFFFFF) or (o.c or 0x9098A8)
    print((sel and "> " or "  ") .. label(it), x, ry, c, scale)
    if type(it) == "table" and it.value then
      local v = type(it.value) == "function" and it.value(it) or it.value
      lib.printr(tostring(v), x + w, ry, c, scale)
    end
  end
end

-- the pause menu every game had: Start (Esc) opens it while o.when() is
-- true (always if missing); RESUME, VOLUME (the console's: left / right),
-- o.rows (more rows, as lib.menu's), QUIT (o.quit(), if given). In
-- _update: if P:update() then return end; in _draw, after the game: P:draw().
function lib.pause(o)
  o = o or {}
  local P = { open = false, o = o }
  local rows = { { label = "RESUME", ok = function() P.open = false end } }
  rows[#rows + 1] = {
    label = "VOLUME", value = function() return tostring(volume()) end,
    change = function(_, d)
      volume(lib.clamp(volume() + d, 0, 10))
      note(o.voice or 7, 660, 60, SQUARE, 110)      -- how loud it is now
    end,
    ok = function() volume(volume() >= 10 and 0 or volume() + 1); note(o.voice or 7, 660, 60, SQUARE, 110) end,
  }
  for _, r in ipairs(o.rows or {}) do rows[#rows + 1] = r end
  if o.quit then rows[#rows + 1] = { label = "QUIT", ok = function() P.open = false; o.quit() end } end
  P.menu = lib.menu(rows)
  function P:update()
    if not self.open then
      if (not o.when or o.when()) and btnp("start") then
        self.open, self.menu.sel = true, 1
        return true
      end
      return false
    end
    if btnp("start") then self.open = false; return true end
    local _, a = self.menu:update()
    if a == "back" then self.open = false end
    return true
  end
  function P:draw()
    if not self.open then return end
    local s = o.scale or (SCREEN_W >= 640 and 2 or 1)
    local cw, ch = font_size()
    local accent = o.color or 0xFFD050
    local n = #rows
    local bw = (o.w or 104) * s
    local bh = (ch + 4) * s * n + (ch + 12) * s
    local x, y = (SCREEN_W - bw) // 2, (SCREEN_H - bh) // 2
    rectfill(x - 2, y - 2, bw + 4, bh + 4, accent)
    rectfill(x, y, bw, bh, o.bg or 0x101418)
    lib.printc(o.title or "PAUSED", y + 4 * s, accent, s, x, bw)
    self.menu:draw(x + 4 * s, y + (ch + 10) * s, { w = bw - 8 * s - cw * s, scale = s })
  end
  return P
end

----------------------------------------------------------------- players on one console

-- the players' colours: those of the pads' light bars (1 blue, 2 red, 3
-- green, 4 pink), as controller(p).color
lib.PLAYER_COLORS = { 0x3070FF, 0xFF3C28, 0x28D848, 0xFF38A8 }

-- the players who have a controller now: their numbers, in order ({1, 3})
function lib.pads()
  local _, m = players()
  local out = {}
  for p = 1, 4 do
    if m >> (p - 1) & 1 == 1 then out[#out + 1] = p end
  end
  return out
end

-- the screen cut into views for n players (1-4): a list of {x, y, w, h}.
-- Two side by side (o.vertical: one above the other), three or four in
-- the corners (with three the bottom right is free: a map, the scores);
-- o.gap pixels between them (2), o.x, o.y, o.w, o.h the part of the screen
-- (all of it if missing). A camera per player: lib.camera{w = v.w, h =
-- v.h}, then in _draw C:apply(v) for each view, and clip() camera() after.
function lib.split(n, o)
  o = o or {}
  local x0, y0, w, h, g = o.x or 0, o.y or 0, o.w or SCREEN_W, o.h or SCREEN_H, o.gap or 2
  local hw, hh = (w - g) // 2, (h - g) // 2
  if n <= 1 then return { { x = x0, y = y0, w = w, h = h } } end
  if n == 2 then
    if o.vertical then
      return { { x = x0, y = y0, w = w, h = hh }, { x = x0, y = y0 + h - hh, w = w, h = hh } }
    end
    return { { x = x0, y = y0, w = hw, h = h }, { x = x0 + w - hw, y = y0, w = hw, h = h } }
  end
  local out = {}
  for i = 1, min(n, 4) do
    local c, r = (i - 1) % 2, (i - 1) // 2
    out[i] = { x = x0 + c * (w - hw), y = y0 + r * (h - hh), w = hw, h = hh }
  end
  return out
end

-- the screen where the players of one console join a game: each presses ok
-- on their controller to be in, back to go out; Start of a player who is
-- in begins, with at least o.min players (1). o.max (4). P:update() in
-- _update returns the players in (their numbers, in the order they came)
-- when the game begins, else nil; P:draw([x, y, w, h]) draws a card for
-- each place. P.list are the players in now.
function lib.party(o)
  o = o or {}
  local P = { list = {}, min = o.min or 1, max = o.max or 4 }
  function P:has(p)
    for i, q in ipairs(self.list) do
      if q == p then return i end
    end
    return nil
  end
  function P:update()
    for p = 1, 4 do
      local i = self:has(p)
      if not i and #self.list < self.max and btnp("ok", p) then
        self.list[#self.list + 1] = p
        if o.join then o.join(p) end
      elseif i and btnp("back", p) then
        table.remove(self.list, i)
        if o.leave then o.leave(p) end
      elseif i and btnp("start", p) and #self.list >= self.min then
        return self.list
      end
    end
    return nil
  end
  function P:draw(x, y, w, h)
    x, y, w, h = x or 0, y or 0, w or SCREEN_W, h or SCREEN_H
    local _, ch = font_size()
    local cw, chh = (w - 40) // 4, h - ch - 8          -- the cards, then the line to begin
    for p = 1, 4 do
      local cx = x + 8 + (p - 1) * (cw + 8)
      local i = self:has(p)
      local col = lib.PLAYER_COLORS[p]
      local c = controller(p)
      rectfill(cx, y, cw, chh, i and lib.shade(col, 0.3) or 0x161A22)
      rect(cx, y, cw, chh, i and col or 0x303846)
      lib.printc("P" .. p, y + ch, i and col or 0x606878, 2, cx, cw)
      local ty = y + ch * 4
      if i then
        lib.printc("IN", ty, 0xFFFFFF, 1, cx, cw)
        lib.printc(c.kind, ty + ch + 4, 0x9098A8, 1, cx, cw)
      elseif c.kind == "none" then
        lib.printc("no controller", ty, 0x505868, 1, cx, cw)
      elseif #self.list < self.max and lib.blink() then
        local pw = prompt("ok", false, 1, p)
        local tw = lib.textw(" join")
        local px = cx + (cw - pw - tw) // 2
        print(" join", prompt("ok", px, ty, false, 1, p), ty, 0xFFFFFF)
      end
    end
    if #self.list >= self.min then
      lib.printc("START: play", y + h - ch, lib.blink() and 0xFFD050 or 0x9098A8, 1, x, w)
    else
      lib.printc((self.min - #self.list) .. " more to play", y + h - ch, 0x9098A8, 1, x, w)
    end
  end
  return P
end

----------------------------------------------------------------- sound

-- a short tune on a voice (3 if missing): notes = { {note, secs, [wave,
-- vol]}, ... }, a note in Hz or by name ("C5"), 0 or "-" a rest; wave
-- (TRIANGLE) and vol (120) for every note without its own. lib.jingle(nil,
-- voice) stops it. lib.update() plays it on.
function lib.jingle(notes, voice, wave, vol)
  voice = voice or 3
  if not notes then tunes[voice] = nil; return end
  tunes[voice] = { notes = notes, i = 1, t = 0, wave = wave or TRIANGLE, vol = vol or 120 }
end

-- the voice still plays a jingle
function lib.jingling(voice)
  return tunes[voice or 3] ~= nil
end

----------------------------------------------------------------- saves

local store

-- a field of the cartridge's save (save() / saved()): lib.store(k) reads
-- it, lib.store(k, v) writes it (on the SD card only if it changed: not in
-- every frame), lib.store() the whole table
function lib.store(k, v)
  if store == nil then store = saved() or {} end
  if k == nil then return store end
  if v == nil then return store[k] end
  if store[k] ~= v then
    store[k] = v
    save(store)
  end
  return v
end

-- the record of k: v if it beats it (then saved), and true if it is new
function lib.best(k, v)
  local b = lib.store(k)
  if b == nil or v > b then
    lib.store(k, v)
    return v, true
  end
  return b, false
end

----------------------------------------------------------------- animation and colour

-- the element of frames that time t (time() if missing) shows at fps
-- frames a second, in a loop
function lib.frame(frames, fps, t)
  return frames[floor((t or time()) * fps) % #frames + 1]
end

local Anim = {}
Anim.__index = Anim

-- an animation of its own: a:update() moves it on by a frame and returns the
-- element now; a.done when one that does not loop (loop = false) is over
function lib.anim(frames, fps, loop)
  return setmetatable({ frames = frames, fps = fps, loop = loop ~= false, t = 0, done = false }, Anim)
end

function Anim:update(dt)
  self.t = self.t + (dt or DT)
  return self:get()
end

function Anim:get()
  local i = floor(self.t * self.fps)
  if self.loop then return self.frames[i % #self.frames + 1] end
  self.done = i >= #self.frames
  return self.frames[min(i, #self.frames - 1) + 1]
end

function Anim:reset()
  self.t, self.done = 0, false
end

-- between colours c1 and c2 (0xRRGGBB): t = 0 c1, 1 c2
function lib.mix(c1, c2, t)
  local function ch(s)
    local a, b = c1 >> s & 255, c2 >> s & 255
    return floor(a + (b - a) * t + 0.5) << s
  end
  return ch(16) | ch(8) | ch(0)
end

-- a colour k times as bright (0 black, 1 as it is, more: brighter)
function lib.shade(c, k)
  local function ch(s) return min(255, floor((c >> s & 255) * k + 0.5)) << s end
  return ch(16) | ch(8) | ch(0)
end

----------------------------------------------------------------- 3D

local Build = {}
Build.__index = Build

-- a mesh made of pieces (Astro Wing's builder): every face of a convex
-- piece is turned away from its middle, so the order of the corners
-- never matters. b:box(...), b:piece(...), then b:build() gives the mesh.
function lib.builder()
  return setmetatable({ v = {}, f = {} }, Build)
end

-- a convex piece: pts = { {x, y, z}, ... }, tris = { {i, j, k, [colour]}, ... }
function Build:piece(pts, tris, color)
  local v, f = self.v, self.f
  local base = #v // 3
  local cx, cy, cz = 0, 0, 0
  for _, p in ipairs(pts) do
    v[#v + 1], v[#v + 2], v[#v + 3] = p[1], p[2], p[3]
    cx, cy, cz = cx + p[1], cy + p[2], cz + p[3]
  end
  cx, cy, cz = cx / #pts, cy / #pts, cz / #pts
  for _, t in ipairs(tris) do
    local a, c, d = pts[t[1]], pts[t[2]], pts[t[3]]
    local ux, uy, uz = c[1] - a[1], c[2] - a[2], c[3] - a[3]
    local vx, vy, vz = d[1] - a[1], d[2] - a[2], d[3] - a[3]
    local nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    local mx, my, mz = (a[1] + c[1] + d[1]) / 3 - cx, (a[2] + c[2] + d[2]) / 3 - cy, (a[3] + c[3] + d[3]) / 3 - cz
    local i, j, k = base + t[1], base + t[2], base + t[3]
    if nx * mx + ny * my + nz * mz < 0 then j, k = k, j end
    f[#f + 1], f[#f + 2], f[#f + 3], f[#f + 4] = i, j, k, t[4] or color
  end
  return self
end

function Build:tetra(p1, p2, p3, p4, color)
  return self:piece({ p1, p2, p3, p4 }, { { 1, 2, 3 }, { 1, 2, 4 }, { 1, 3, 4 }, { 2, 3, 4 } }, color)
end

-- a box between two corners; top: the colour of its top (y1), if another
function Build:box(x0, y0, z0, x1, y1, z1, color, top)
  return self:piece({ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
                      { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } },
                    { { 1, 2, 3 }, { 1, 3, 4 }, { 5, 6, 7 }, { 5, 7, 8 }, { 1, 2, 6 }, { 1, 6, 5 },
                      { 4, 3, 7, top }, { 4, 7, 8, top }, { 1, 4, 8 }, { 1, 8, 5 }, { 2, 3, 7 }, { 2, 7, 6 } },
                    color)
end

-- a flat quad p1..p4 (round its edge) seen from the side nx, ny, nz
function Build:quad(p1, p2, p3, p4, color, nx, ny, nz)
  local v, f = self.v, self.f
  local base = #v // 3
  for _, p in ipairs({ p1, p2, p3, p4 }) do v[#v + 1], v[#v + 2], v[#v + 3] = p[1], p[2], p[3] end
  local ux, uy, uz = p2[1] - p1[1], p2[2] - p1[2], p2[3] - p1[3]
  local vx, vy, vz = p3[1] - p1[1], p3[2] - p1[2], p3[3] - p1[3]
  local cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
  local tris = cx * nx + cy * ny + cz * nz < 0 and { { 1, 3, 2 }, { 1, 4, 3 } } or { { 1, 2, 3 }, { 1, 3, 4 } }
  for _, t in ipairs(tris) do
    f[#f + 1], f[#f + 2], f[#f + 3], f[#f + 4] = base + t[1], base + t[2], base + t[3], color
  end
  return self
end

function Build:build()
  return mesh(self.v, self.f)
end

return lib
