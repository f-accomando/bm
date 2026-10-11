-- The fighters: reading the controls, the state machine, moving, attacking,
-- guarding, taking hits. Positions are in pixels: x along the arena, y the
-- height above the floor (up is +). A fighter faces +1 (right) or -1.
--
-- States: stand, walk, crouch (free to act), jump, airdash, dash, attack,
-- block/cblock (guard stun), hit/chit (hit stun), air (knocked into the
-- air, can be juggled), down, getup, win, ko.

local FREE = { stand = true, walk = true, crouch = true }
local GUARDING = { stand = true, walk = true, crouch = true, block = true, cblock = true }

function Fighter.new(p, pad, cfg, x, face)
  local A = Data.ARMOR[cfg.armor]
  return {
    p = p, pad = pad, cfg = cfg, A = A, W = Data.WEAPON[cfg.weapon], cpu = pad == "cpu",
    x = x, y = 0, vx = 0, vy = 0, face = face, push = 0,
    state = "stand", anim = "idle", fi = 1, ft = 8, t = 0, st = 0,
    life = 1000, life_show = 1000, armor = A.armor, armor_max = A.armor,
    energy = 100, heat = 0, overheat = 0, shoulder = "ok",
    stun = 0, hitstop = 0, air_jumps = 0, juggle = 0, combo = 0, combo_show = 0,
    hist = {}, inp = { dir = 5 }, taps = { [4] = -99, [6] = -99 }, lastdir = 5,
  }
end

---------------------------------------------------------------- controls

-- the direction as on a numpad, seen from the fighter (6 = forwards)
local function numpad(f, l, r, u, d)
  -- not `f.face > 0 and r or l`: with r false that gives l
  local fwd, back = r, l
  if f.face < 0 then fwd, back = l, r end
  if fwd and back then fwd, back = false, false end
  if d then return back and 1 or (fwd and 3 or 2) end
  if u then return back and 7 or (fwd and 9 or 8) end
  return back and 4 or (fwd and 6 or 5)
end

-- this frame's controls of a player: direction, buttons just pressed, and
-- what the recent past says (a quarter circle forwards, a double tap)
function Fighter.read(f)
  local i = f.inp
  if not f.cpu then
    local p = f.pad
    i.dir = numpad(f, btn(BL, p), btn(BR, p), btn(BU, p), btn(BD, p))
    i.lp, i.hp, i.lk, i.hk = btnp(BX, p), btnp(BY, p), btnp(BA, p), btnp(BB, p)
    i.up = btnp(BU, p)
  end
  -- the history, newest first
  insert(f.hist, 1, i.dir)
  f.hist[25] = nil
  -- double tap forwards or backwards: a dash
  i.dash = 0
  local d = i.dir
  if (d == 6 or d == 4) and f.lastdir ~= d then
    if f.t - f.taps[d] <= 12 then i.dash = d == 6 and 1 or -1 end
    f.taps[d] = f.t
  end
  f.lastdir = d
  if f.cpu then i.dash = i.cdash or 0 end
  if not f.cpu then
    -- the weapon: down, down-forwards, forwards + a punch; or both heavy buttons
    local qcf, want = false, 6
    for j = 1, 16 do
      local h = f.hist[j]
      if not h then break end
      if h == want then
        if want == 6 then want = 3 elseif want == 3 then want = 2 else qcf = true break end
      end
    end
    i.special = (qcf and (i.lp or i.hp)) or (i.hp and i.hk) or (btn(BY, f.pad) and i.hk) or (btn(BB, f.pad) and i.hp)
  end
end

---------------------------------------------------------------- animation

local LOOP_TICKS = { idle = 9, walk = 6, dash = 4, win = 20 }

local function set_anim(f, anim, fi)
  if f.anim ~= anim or (fi and f.fi ~= fi) then
    f.anim, f.fi = anim, fi or 1
    f.ft = LOOP_TICKS[anim] or 6
  end
end

local function set_state(f, s, anim, fi)
  if f.state ~= s then f.state, f.st = s, 0 end
  if anim then set_anim(f, anim, fi) end
end

local function loop_anim(f)
  f.ft = f.ft - 1
  if f.ft <= 0 then
    f.fi = f.fi % #ANIM[f.anim] + 1
    f.ft = LOOP_TICKS[f.anim] or 6
  end
end

function Fighter.frame(f)
  return FR[ANIM[f.anim][f.fi]]
end

-- the pictures of the frame: P2 has its own colours
function Fighter.art(f)
  return (f.p == 2 and FR2 or FR)[ANIM[f.anim][f.fi]]
end

-- for the screens: the round is over, the robot celebrates when it can
function Fighter.celebrate(f)
  if FREE[f.state] and f.y == 0 then set_state(f, "win", "win", 1) end
end

-- no controls (before FIGHT!, after K.O.)
function Fighter.hands_off(f)
  local i = f.inp
  i.dir, i.lp, i.hp, i.lk, i.hk, i.up, i.special, i.dash, i.cdash = 5, false, false, false, false, false, false, 0, 0
end

---------------------------------------------------------------- boxes

-- a box of the frame {x0, y0, x1, y1} (x forwards, y down from the feet)
-- in the world: left, bottom, right, top
local function wbox(f, b)
  local x0, x1 = b[1], b[3]
  if f.face < 0 then x0, x1 = -b[3], -b[1] end
  return f.x + x0, f.y - b[4], f.x + x1, f.y - b[2]
end
Fighter.wbox = wbox

local function overlap(a0, a1, a2, a3, b0, b1, b2, b3)
  return a0 < b2 and b0 < a2 and a1 < b3 and b1 < a3
end

-- the point where a box hits the fighter, or nil
function Fighter.hits(f, l, b, r, t)
  if f.state == "down" or f.state == "getup" or f.state == "ko" then return nil end
  local fr = Fighter.frame(f)
  for _, hb in ipairs(fr.hurt) do
    local l2, b2, r2, t2 = wbox(f, hb)
    if overlap(l, b, r, t, l2, b2, r2, t2) then
      return (max(l, l2) + min(r, r2)) / 2, (max(b, b2) + min(t, t2)) / 2
    end
  end
  return nil
end

---------------------------------------------------------------- acting

local function start_move(f, name)
  local m = Data.MOVE[name]
  f.move, f.mi, f.hit_done, f.connected, f.shots = m, 1, false, false, 0
  set_state(f, "attack")
  f.anim, f.fi, f.ft = m.anim, 1, m.ticks[1]
  if not m.air then f.vx = 0 end
  Snd.swing(m.rank)
end

local function can_special(f)
  local w = f.cfg.weapon
  if w == "sword" then return f.energy >= Data.WEAPON.sword.energy end
  return f.overheat == 0
end

local function start_special(f)
  if f.cfg.weapon == "sword" then
    f.energy = f.energy - Data.WEAPON.sword.energy
    start_move(f, "slash")
    Snd.draw_sword()
  else
    start_move(f, "fire")
  end
end

local function start_jump(f, d)
  f.vy = f.A.jump
  f.vx = (d == 9 and 1 or (d == 7 and -1 or 0)) * f.face * f.A.walk * 1.35
  f.air_jumps = f.A.air_jumps
  set_state(f, "jump", "jump", 1)
  Snd.jump()
end

local function start_dash(f, dir, air)
  f.energy = f.energy - (air and 20 or f.A.dash_cost)
  f.dash_dir, f.dash_t = dir, air and 11 or f.A.dash_t
  f.vx = dir * f.face * f.A.dash
  if air then f.vy = 0 end
  set_state(f, air and "airdash" or "dash", "dash")
  Snd.boost()
end

-- a normal from the buttons pressed now (nil if none)
local function normal_for(f, air)
  local i = f.inp
  if air then
    if i.hp or i.lp then return "jhp" end
    if i.hk or i.lk then return "jhk" end
    return nil
  end
  local c = i.dir <= 3
  if i.hk then return c and "chk" or "hk" end
  if i.hp then return c and "chp" or "hp" end
  if i.lk then return c and "clk" or "lk" end
  if i.lp then return c and "clp" or "lp" end
  return nil
end

local function neutral(f)
  local i = f.inp
  if i.special and can_special(f) then return start_special(f) end
  local n = normal_for(f)
  if n then return start_move(f, n) end
  if i.dash ~= 0 and f.energy >= f.A.dash_cost then return start_dash(f, i.dash) end
  local d = i.dir
  if d >= 7 then return start_jump(f, d) end
  if d <= 3 then
    f.vx = 0
    set_state(f, "crouch", "crouch")
  elseif d == 6 then
    f.vx = f.face * f.A.walk
    set_state(f, "walk", "walk")
    loop_anim(f)
  elseif d == 4 then
    f.vx = -f.face * f.A.back
    set_state(f, "walk", "walk")
    loop_anim(f)
  else
    f.vx = 0
    set_state(f, "stand", "idle")
    loop_anim(f)
  end
end

---------------------------------------------------------------- hits

-- the fighter's guard against a hit of this level, from its controls now
local function guards(f, level)
  if not GUARDING[f.state] then return false end
  local d = f.inp.dir
  if d ~= 4 and d ~= 1 then return false end
  local low = d == 1
  if level == "low" and not low then return false end
  if level == "over" and low then return false end
  return true
end

local function shoulder_check(f)
  local k = f.armor / f.armor_max
  if f.shoulder == "ok" and k < 0.5 then
    f.shoulder = "cracked"
    Fx.burst(f, 0x80E0FF)
    Snd.crack()
  elseif f.shoulder ~= "gone" and f.armor <= 0 then
    f.shoulder = "gone"
    Fx.shoulder_off(f)
    Snd.armor_break()
  end
end

-- damage into the armour first (it absorbs most of it while it lasts)
local function damage(f, dmg, armor_k)
  if f.armor > 0 then
    local to_armor = dmg * f.A.absorb * (armor_k or 1)
    local over = to_armor - f.armor
    f.armor = max(0, f.armor - to_armor)
    f.life = f.life - (dmg - to_armor) - max(0, over)
  else
    f.life = f.life - dmg
  end
  f.life = max(0, f.life)
  shoulder_check(f)
end

-- f is hit by `m` (a move or a bullet) from attacker a, at world (hx, hy).
-- Returns "block" or "hit".
function Fighter.take_hit(f, a, m, hx, hy)
  local push_dir = a.x < f.x and 1 or -1
  if guards(f, m.level) then
    local low = f.inp.dir == 1
    set_state(f, low and "cblock" or "block", low and "cblock" or "block")
    f.stun = m.bstun
    f.push = push_dir * m.push * 0.9
    f.armor = max(0, f.armor - m.dmg * 0.12)
    shoulder_check(f)
    f.hitstop, a.hitstop = 5, 5
    Fx.spark(hx, hy, "bspark")
    Snd.block()
    return "block"
  end
  -- combos: each hit in a row counts less
  local scale = max(0.4, 1 - 0.12 * f.combo)
  damage(f, m.dmg * a.A.dmg * scale, m.armor_k)
  f.combo = f.combo + 1
  a.combo_show, a.combo_t = f.combo, 90
  local heavy = (m.rank or 1) >= 3
  f.hitstop, a.hitstop = heavy and 10 or 6, heavy and 10 or 6
  if heavy then G.shake = max(G.shake, 5) end
  Fx.spark(hx, hy, heavy and "bigspark" or "spark")
  Snd.hit(heavy)
  if f.y > 0 or m.kd or m.launch or f.life <= 0 or f.state == "air" then
    -- into the air (a juggle, or knocked down)
    f.juggle = f.juggle + 1
    f.vy = m.launch and 11.5 or (f.state == "air" and 6 or 7)
    f.vx = push_dir * (m.launch and 1.2 or 2.8)
    set_state(f, "air", "down", 1)
  else
    local low = f.state == "crouch" or f.inp.dir <= 3
    set_state(f, low and "chit" or "hit", low and "chit" or "hit", 1)
    f.stun = m.stun
    f.push = push_dir * m.push
  end
  return "hit"
end

-- the active frames of a's move against f
local function check_hit(a, f)
  local m = a.move
  if a.hit_done or not m.dmg then return end
  local fr = Fighter.frame(a)
  local hb = fr.hit
  if not hb then return end
  local l, b, r, t = wbox(a, hb)
  local hx, hy = Fighter.hits(f, l, b, r, t)
  if not hx then return end
  if f.state == "air" and f.juggle >= 4 then return end
  a.hit_done = true
  a.connected = Fighter.take_hit(f, a, m, hx, hy)
end

---------------------------------------------------------------- the guns

-- where the barrels end in this frame (the front of the gun pieces)
local function muzzles(f)
  local fr = Fighter.frame(f)
  local out = {}
  if not fr.g then return out end
  for _, r in ipairs(fr.g) do
    local mx, my = r[5] + r[3] - 3, r[6] + floor(r[4] * 0.3)
    out[#out + 1] = { f.x + f.face * mx, f.y - my }
  end
  return out
end

local function fire(f)
  if f.overheat > 0 then return end
  local ms = muzzles(f)
  if #ms == 0 then return end
  local m = ms[(f.shots % #ms) + 1]
  f.shots = f.shots + 1
  Fx.bullet(f, m[1], m[2])
  Fx.muzzle(m[1], m[2], f.face)
  f.heat = f.heat + Data.WEAPON.guns.heat
  if f.heat >= 100 then
    f.heat, f.overheat = 100, 150
    Snd.overheat()
  end
  Snd.shot()
end

---------------------------------------------------------------- update

local function attack(f, o)
  local m = f.move
  -- a hit or a guard lets a stronger move follow at once
  if f.connected then
    local i = f.inp
    if i.special and can_special(f) and m.rank < 9 then return start_special(f) end
    local n = normal_for(f, m.air)
    if n and Data.MOVE[n].rank > m.rank then return start_move(f, n) end
  end
  if m.shots and f.ft == m.ticks[f.mi] and f.shots < m.shots then fire(f) end
  check_hit(f, o)
  f.ft = f.ft - 1
  if f.ft > 0 then return end
  f.mi = f.mi + 1
  if f.mi > #m.ticks or (m.loop and f.shots >= m.shots and f.mi > 3) then
    if m.air then
      f.mi = #m.ticks
      f.ft = 1
      return
    end
    set_state(f, m.crouch and "crouch" or "stand", m.crouch and "crouch" or "idle")
    return
  end
  f.fi = m.loop and ((f.mi - 1) % #ANIM[m.anim] + 1) or f.mi
  f.ft = m.ticks[f.mi]
  if m == Data.MOVE.slash and (f.mi == 3 or f.mi == 4) then Fx.trail(f, f.mi - 3) end
end

local function land(f)
  f.y, f.vy = 0, 0
  f.vx = 0
  Fx.dust(f.x)
  if f.cfg.armor == "heavy" then G.shake = max(G.shake, 3) end
  Snd.land(f.cfg.armor == "heavy")
end

function Fighter.update(f, o)
  f.t = f.t + 1
  f.st = f.st + 1
  -- resources
  if f.state ~= "dash" and f.state ~= "airdash" then f.energy = min(100, f.energy + f.A.regen) end
  if f.overheat > 0 then
    f.overheat = f.overheat - 1
    if f.overheat == 0 then f.heat = 40 end
    if f.t % 6 == 0 then Fx.smoke(f.x + f.face * 30, f.y + 110) end
  else
    f.heat = max(0, f.heat - 0.3)
  end
  f.life_show = approach(f.life_show, f.life, 3)
  if f.combo_t then f.combo_t = f.combo_t - 1; if f.combo_t <= 0 then f.combo_t = nil end end
  if f.hitstop > 0 then
    f.hitstop = f.hitstop - 1
    return
  end
  local s = f.state
  -- the pushback of hits and guards
  if f.push ~= 0 then
    f.x = f.x + f.push
    f.push = approach(f.push, 0, 0.6)
  end
  if FREE[s] then
    f.face = o.x >= f.x and 1 or -1
    f.combo = 0
    f.juggle = 0
    neutral(f)
  elseif s == "jump" then
    local i = f.inp
    if i.up and f.air_jumps > 0 and f.vy < 6 then
      f.air_jumps = f.air_jumps - 1
      f.vy = f.A.jump * 0.85
      Fx.flames(f)
      Snd.jump()
    elseif i.dash ~= 0 and f.energy >= 20 then
      return start_dash(f, i.dash, true)
    else
      local n = normal_for(f, true)
      if n then
        local vx, vy = f.vx, f.vy
        start_move(f, n)
        f.vx, f.vy = vx, vy
      end
    end
    set_anim(f, "jump", f.vy > 3 and 1 or (f.vy > -3 and 2 or 3))
  elseif s == "dash" or s == "airdash" then
    f.dash_t = f.dash_t - 1
    if f.t % 2 == 0 then Fx.flames(f) end
    loop_anim(f)
    if f.dash_t <= 0 then
      if s == "airdash" then
        f.vx = f.vx * 0.4
        set_state(f, "jump", "jump", 3)
      else
        f.vx = 0
        set_state(f, "stand", "idle")
      end
    end
  elseif s == "attack" then
    attack(f, o)
  elseif s == "block" or s == "cblock" or s == "hit" or s == "chit" then
    f.stun = f.stun - 1
    if s == "hit" and f.stun < 8 then set_anim(f, "hit", 2) end
    if f.stun <= 0 then set_state(f, (s == "cblock" or s == "chit") and "crouch" or "stand",
                                  (s == "cblock" or s == "chit") and "crouch" or "idle") end
  elseif s == "air" then
    set_anim(f, "down", 1)
  elseif s == "down" then
    if f.st > 40 and f.life > 0 then set_state(f, "getup", "down", 3) end
  elseif s == "getup" then
    if f.st > 18 then set_state(f, "stand", "idle") end
  elseif s == "win" then
    loop_anim(f)
  end
  -- moving and falling
  f.x = f.x + f.vx
  if f.y > 0 or f.vy ~= 0 then
    local airborne = f.state == "jump" or f.state == "air" or (f.state == "attack" and f.move.air)
    if f.state ~= "airdash" then f.vy = f.vy - GRAV end
    f.y = f.y + f.vy
    if f.y <= 0 then
      if f.state == "air" then
        f.y, f.vy, f.vx = 0, 0, 0
        G.shake = max(G.shake, 4)
        Fx.dust(f.x)
        Snd.land(true)
        set_state(f, f.life <= 0 and "ko" or "down", "down", 2)
      elseif airborne or f.state == "airdash" then
        land(f)
        set_state(f, "stand", "idle")
      else
        f.y, f.vy = 0, 0
      end
    end
  end
end

-- the two fighters never stand inside each other; both stay in the arena
-- and on screen
function Fighter.separate(a, b)
  local d = b.x - a.x
  local gap = 88
  if abs(d) < gap and abs(a.y - b.y) < 120 then
    local k = (gap - abs(d)) / 2
    local s = d >= 0 and 1 or -1
    a.x, b.x = a.x - s * k, b.x + s * k
  end
  for _, f in ipairs({ a, b }) do
    f.x = clamp(f.x, max(40, G.camx + 40), min(ARENA_W - 40, G.camx + W - 40))
  end
end

---------------------------------------------------------------- drawing

function Fighter.draw(f)
  local fr = Fighter.art(f)
  local x, y = floor(f.x - G.camx), floor(GROUND - f.y + G.sy)
  local flip = f.face < 0
  local heavy, weapon = f.cfg.armor == "heavy", f.cfg.weapon
  -- the shadow on the floor
  local sw = heavy and 46 or 40
  rectfill(x - sw, GROUND + G.sy - 2, sw * 2, 4, 0x14141A)
  pieces(fr.b, x, y, flip)
  local front = FR[ANIM[f.anim][f.fi]].swf
  if weapon == "sword" and fr.sw and not front then pieces(fr.sw, x, y, flip) end
  if heavy and fr.hv then pieces(fr.hv, x, y, flip) end
  if weapon == "guns" and fr.g then pieces(fr.g, x, y, flip) end
  if f.shoulder ~= "gone" then
    local key = heavy and (f.shoulder == "ok" and "sh" or "shd") or (f.shoulder == "ok" and "s" or "sd")
    if fr[key] then pieces(fr[key], x, y, flip) end
  end
  if weapon == "sword" and fr.sw and front then pieces(fr.sw, x, y, flip) end
end

-- the player's tag over the head
function Fighter.draw_tag(f, label, col)
  local fr = Fighter.frame(f)
  local top = 0
  for _, r in ipairs(fr.b) do top = min(top, r[6]) end
  local x = floor(f.x - G.camx)
  local y = floor(GROUND - f.y + top + G.sy) - 14
  tri(x - 5, y, x + 5, y, x, y + 6, col)
  text_c(label, x, y - 14, col)
end
