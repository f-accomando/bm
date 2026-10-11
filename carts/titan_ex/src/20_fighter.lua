-- Titan Clash EX: Fighter Simulation Engine
-- Implements complete 2D Mecha fighting mechanics:
-- 4-button normals, special moves, hyper supers, mecha throws,
-- super jump, air dash, chain canceling, localized armor break,
-- overheat/energy resources, and 2v2 Tag Team battle system.

local GUARDING = { stand = true, walk = true, crouch = true, block = true, cblock = true }

function Fighter.new(team_num, slot_num, pad, cfg, x, face)
  local ch_id = cfg.chassis or "vanguard"
  local C = Data.CHASSIS[ch_id] or Data.CHASSIS.vanguard
  local A_cfg = Data.ARMOR[cfg.armor or "medium"] or Data.ARMOR.medium
  local W_cfg = Data.WEAPON[cfg.weapon or "sword"] or Data.WEAPON.sword

  local max_arm = floor(C.armor_base * A_cfg.armor_mod)
  local max_life = 1000

  local f = {
    team_num = team_num, slot_num = slot_num,
    pad = pad, cfg = cfg,
    C = C, A_cfg = A_cfg, W = W_cfg,
    cpu = pad == "cpu",
    x = x, y = 0, vx = 0, vy = 0, face = face, push = 0,
    state = "stand", anim = "idle", fi = 1, ft = 8, t = 0, st = 0,

    -- Health and Armor
    life = max_life, life_red = max_life, life_show = max_life,
    armor = max_arm, armor_max = max_arm,
    shoulder = "ok", -- "ok", "cracked", "gone" (broken off)

    -- Resources
    energy = 100, heat = 0, overheat = 0,
    super_meter = 0, -- 0 to 300 (Level 1 = 100, Level 2 = 200, Level 3 = 300)
    tag_slots = 2, tag_timer = 0,

    -- Combat stats
    walk_spd = C.walk * A_cfg.speed_mod,
    back_spd = C.back * A_cfg.speed_mod,
    jump_spd = C.jump,
    air_jumps_max = max(0, C.air_jumps + A_cfg.air_jump_mod),
    air_jumps = max(0, C.air_jumps + A_cfg.air_jump_mod),
    dash_spd = C.dash * A_cfg.speed_mod,
    dash_t_max = C.dash_t,
    absorb = clamp(C.absorb + A_cfg.absorb_mod, 0.45, 0.85),
    dmg_mult = C.dmg_mult,

    -- Combat state
    stun = 0, hitstop = 0, juggle = 0, combo = 0, combo_show = 0,
    super_freeze = 0,
    hist = {}, inp = { dir = 5 }, taps = { [4] = -99, [6] = -99 }, lastdir = 5,
    down_tap = -99,
  }
  return f
end

---------------------------------------------------------------- controls

local function numpad(f, l, r, u, d)
  local fwd, back = r, l
  if f.face < 0 then fwd, back = l, r end
  if fwd and back then fwd, back = false, false end
  if d then return back and 1 or (fwd and 3 or 2) end
  if u then return back and 7 or (fwd and 9 or 8) end
  return back and 4 or (fwd and 6 or 5)
end

function Fighter.read(f)
  local i = f.inp
  if not f.cpu then
    local p = f.pad
    i.dir = numpad(f, btn(BL, p), btn(BR, p), btn(BU, p), btn(BD, p))
    i.lp, i.hp = btnp(BX, p), btnp(BY, p)
    i.lk, i.hk = btnp(BA, p), btnp(BB, p)
    i.up = btnp(BU, p)
    -- shoulder macros
    i.tag = btnp("l1", p) or (btn(BX, p) and btnp(BA, p))
    i.assist = btnp("r1", p)
  end

  insert(f.hist, 1, i.dir)
  f.hist[30] = nil

  -- double tap for dash
  i.dash = 0
  local d = i.dir
  if (d == 6 or d == 4) and f.lastdir ~= d then
    if f.t - f.taps[d] <= 13 then i.dash = d == 6 and 1 or -1 end
    f.taps[d] = f.t
  end

  -- Super Jump input: tap down (1,2,3) then immediately up (7,8,9)
  i.super_jump = false
  if (d == 8 or d == 9 or d == 7) and f.lastdir ~= d then
    if f.t - f.down_tap <= 12 then i.super_jump = true end
  end
  if d == 2 or d == 1 or d == 3 then f.down_tap = f.t end
  f.lastdir = d

  if f.cpu then
    i.dash = i.cdash or 0
    i.super_jump = i.c_super_jump or false
  end

  if not f.cpu then
    -- Special motion: QCF (↓↘→ + Punch: 2, 3, 6)
    local qcf, want = false, 6
    for j = 1, 18 do
      local h = f.hist[j]
      if not h then break end
      if h == want then
        if want == 6 then want = 3 elseif want == 3 then want = 2 else qcf = true break end
      end
    end

    -- Dragon punch motion (→↓↘ + Punch: 6, 2, 3)
    local dp, dp_want = false, 3
    for j = 1, 18 do
      local h = f.hist[j]
      if not h then break end
      if h == dp_want then
        if dp_want == 3 then dp_want = 2 elseif dp_want == 2 then dp_want = 6 else dp = true break end
      end
    end

    -- Special move: QCF + Punch, or Y+B shortcut
    i.special = (qcf and (i.lp or i.hp)) or (btn(BY, f.pad) and i.hk) or (btn(BB, f.pad) and i.hp)
    i.uppercut = dp and (i.lp or i.hp)

    -- Hyper Super: QCF + (LP+HP) or (HP+HK when Super >= 100)
    i.super = (qcf and ((i.lp and i.hp) or (btn(BX, f.pad) and i.hp))) or
              ((i.hp and i.hk) and f.super_meter >= 100)
  end
end

---------------------------------------------------------------- animation

local LOOP_TICKS = { idle = 9, walk = 6, dash = 4, win = 18 }

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
  local anim = ANIM[f.anim] or ANIM.idle
  local fi = clamp(f.fi, 1, #anim)
  return FR[anim[fi]] or FR[1]
end

function Fighter.art(f)
  local anim = ANIM[f.anim] or ANIM.idle
  local fi = clamp(f.fi, 1, #anim)
  local list = (f.team_num == 2 and FR2 or FR)
  return list[anim[fi]] or list[1]
end

function Fighter.celebrate(f)
  if FREE[f.state] and f.y == 0 then set_state(f, "win", "win", 1) end
end

function Fighter.hands_off(f)
  local i = f.inp
  i.dir, i.lp, i.hp, i.lk, i.hk, i.up, i.special, i.uppercut, i.super, i.dash, i.cdash = 5, false, false, false, false, false, false, false, false, 0, 0
end

---------------------------------------------------------------- boxes

local function wbox(f, b)
  local x0, x1 = b[1], b[3]
  if f.face < 0 then x0, x1 = -b[3], -b[1] end
  return f.x + x0, f.y - b[4], f.x + x1, f.y - b[2]
end
Fighter.wbox = wbox

local function overlap(a0, a1, a2, a3, b0, b1, b2, b3)
  return a0 < b2 and b0 < a2 and a1 < b3 and b1 < a3
end

function Fighter.hits(f, l, b, r, t)
  if f.state == "down" or f.state == "getup" or f.state == "ko" or f.state == "tag_out" then return nil end
  local fr = Fighter.frame(f)
  for _, hb in ipairs(fr.hurt) do
    local l2, b2, r2, t2 = wbox(f, hb)
    if overlap(l, b, r, t, l2, b2, r2, t2) then
      return (max(l, l2) + min(r, r2)) / 2, (max(b, b2) + min(t, t2)) / 2
    end
  end
  return nil
end

---------------------------------------------------------------- combat actions

local function start_move(f, name)
  local m = Data.MOVE[name]
  if not m then return end
  f.move, f.mi, f.hit_done, f.connected, f.shots = m, 1, false, false, 0
  set_state(f, "attack")
  f.anim, f.fi, f.ft = m.anim, 1, m.ticks[1]
  if not m.air then f.vx = 0 end
  Snd.swing(m.rank)
end

local function start_throw(f, o)
  f.move, f.mi, f.hit_done, f.connected = Data.MOVE.throw, 1, false, false
  set_state(f, "attack", "hp", 1)
  f.ft = 8
  f.vx = 0
  
  -- snap opponent into grab hold
  o.vx, o.vy = 0, 0
  o.face = -f.face
  o.x = f.x + f.face * 38
  set_state(o, "hit", "hit", 2)
  o.stun = 35
  
  Snd.clank()
  Fx.spark(o.x, o.y + 70, "bigspark")
  Fx.text_alert("GRAPPLE!", f.x, f.y + 110, 0xFFD040)
end

local function can_special(f)
  local w = f.cfg.weapon
  if w == "sword" then return f.energy >= Data.WEAPON.sword.energy end
  if w == "cannon" then return f.energy >= Data.WEAPON.cannon.energy and f.overheat == 0 end
  return f.overheat == 0
end

local function start_special(f)
  local w = f.cfg.weapon
  if w == "sword" then
    f.energy = max(0, f.energy - Data.WEAPON.sword.energy)
    start_move(f, "slash")
    Snd.draw_sword()
  elseif w == "cannon" then
    f.energy = max(0, f.energy - Data.WEAPON.cannon.energy)
    f.heat = min(100, f.heat + Data.WEAPON.cannon.heat)
    start_move(f, "cannon")
    Fx.cannon_fire(f)
    Snd.boom()
  else
    start_move(f, "fire")
  end
end

local function start_super(f)
  f.super_meter = max(0, f.super_meter - 100)
  G.freeze = 28
  Fx.super_flash(f)
  Snd.super_charge()

  local s_name = "super_vanguard"
  if f.C.id == "valkyrie" then s_name = "super_valkyrie"
  elseif f.C.id == "behemoth" then s_name = "super_behemoth" end

  start_move(f, s_name)
  Fx.text_alert(f.C.super_name, W // 2, 140, 0x58B4FF, 2)
end

local function start_jump(f, d, super)
  local mult = super and 1.42 or 1.0
  f.vy = f.jump_spd * mult
  f.vx = (d == 9 and 1 or (d == 7 and -1 or 0)) * f.face * f.walk_spd * 1.35
  f.air_jumps = f.air_jumps_max
  set_state(f, "jump", "jump", 1)
  if super then
    Fx.super_jump_blast(f)
    Snd.boost()
  else
    Snd.jump()
  end
end

local function start_dash(f, dir, air)
  local cost = air and 20 or f.A_cfg.cost
  if f.energy < cost then
    Fx.text_alert("NOT ENOUGH POWER", f.x, f.y + 40, 0xFF4A28)
    return
  end
  f.energy = f.energy - cost
  f.dash_dir, f.dash_t = dir, air and 12 or f.dash_t_max
  f.vx = dir * f.face * f.dash_spd
  if air then f.vy = 0 end
  set_state(f, air and "airdash" or "dash", "dash")
  Snd.boost()
end

local function normal_for(f, air)
  local i = f.inp
  if air then
    if i.hp then return "jhp" end
    if i.lp then return "jlp" end
    if i.hk then return "jhk" end
    if i.lk then return "jlk" end
    return nil
  end
  local c = i.dir <= 3
  if i.hk then return c and "chk" or "hk" end
  if i.hp then return c and "chp" or "hp" end
  if i.lk then return c and "clk" or "lk" end
  if i.lp then return c and "clp" or "lp" end
  return nil
end

local function try_tag_switch(f)
  if not f.team or not f.team.partner or f.team.partner.life <= 0 then return false end
  if f.tag_slots < 1 then return false end
  
  f.tag_slots = f.tag_slots - 1
  set_state(f, "tag_out")
  f.vx = -f.face * 8
  f.vy = 6
  
  local p = f.team.partner
  p.x = G.camx + (f.face > 0 and 20 or W - 20)
  p.y = 180
  p.face = f.face
  p.vx = f.face * 7
  p.vy = -6
  set_state(p, "tag_in", "jump", 3)
  
  G.fighters[f.team_num] = p
  Fx.text_alert("TAG SWITCH!", p.x, p.y + 50, 0x40E8A0)
  Snd.boost()
  return true
end

local function neutral(f, o)
  local i = f.inp
  -- Tag switch check
  if i.tag and try_tag_switch(f) then return end

  -- Hyper Super check
  if i.super and f.super_meter >= 100 then return start_super(f) end

  -- Throw check: Close range + Fwd/Back + Heavy Attack
  if (i.hp or i.hk) and (i.dir == 4 or i.dir == 6) and abs(o.x - f.x) < 46 and abs(o.y - f.y) < 22 and FREE[o.state] then
    return start_throw(f, o)
  end

  -- Rising uppercut special
  if i.uppercut then return start_move(f, "uppercut") end

  -- Weapon special
  if i.special and can_special(f) then return start_special(f) end

  -- Normal attacks
  local n = normal_for(f)
  if n then return start_move(f, n) end

  -- Dashing
  if i.dash ~= 0 then return start_dash(f, i.dash) end

  -- Jumping
  local d = i.dir
  if d >= 7 then return start_jump(f, d, i.super_jump) end

  -- Movement
  if d <= 3 then
    f.vx = 0
    set_state(f, "crouch", "crouch")
  elseif d == 6 then
    f.vx = f.face * f.walk_spd
    set_state(f, "walk", "walk")
    loop_anim(f)
  elseif d == 4 then
    f.vx = -f.face * f.back_spd
    set_state(f, "walk", "walk")
    loop_anim(f)
  else
    f.vx = 0
    set_state(f, "stand", "idle")
    loop_anim(f)
  end
end

---------------------------------------------------------------- hits and damage

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
    Fx.text_alert("ARMOR BROKEN!", f.x, f.y + 90, 0xFF4A28, 2)
    Snd.armor_break()
  end
end

local function damage(f, dmg, armor_k)
  local real_dmg = dmg
  if f.armor > 0 then
    local to_armor = dmg * f.absorb * (armor_k or 1.0)
    local over = to_armor - f.armor
    f.armor = max(0, f.armor - to_armor)
    real_dmg = (dmg - to_armor) + max(0, over)
  end

  f.life = max(0, f.life - real_dmg)
  -- red health (recoverable on tag bench)
  f.life_red = max(f.life, f.life_red - real_dmg * 0.45)
  shoulder_check(f)
end

function Fighter.take_hit(f, a, m, hx, hy)
  local push_dir = a.x < f.x and 1 or -1

  -- Gain super on hitting and taking hits
  a.super_meter = min(300, a.super_meter + 12)
  f.super_meter = min(300, f.super_meter + 8)

  if guards(f, m.level) and not m.unblockable then
    local low = f.inp.dir == 1
    set_state(f, low and "cblock" or "block", low and "cblock" or "block")
    f.stun = m.bstun
    f.push = push_dir * m.push * 0.85
    f.armor = max(0, f.armor - m.dmg * 0.10)
    shoulder_check(f)
    f.hitstop, a.hitstop = 5, 5
    Fx.spark(hx, hy, "bspark")
    Snd.block()
    return "block"
  end

  -- combo scaling
  local scale = max(0.35, 1 - 0.11 * f.combo)
  damage(f, m.dmg * a.dmg_mult * scale, m.armor_k)

  f.combo = f.combo + 1
  a.combo_show, a.combo_t = f.combo, 90

  local heavy = (m.rank or 1) >= 3 or m.special or m.super
  f.hitstop, a.hitstop = heavy and 10 or 6, heavy and 10 or 6
  if heavy then G.shake = max(G.shake, 6) end

  Fx.spark(hx, hy, heavy and "bigspark" or "spark")
  Snd.hit(heavy)

  if f.y > 0 or m.kd or m.launch or f.life <= 0 or f.state == "air" then
    f.juggle = f.juggle + 1
    f.vy = m.launch and 12.5 or (f.state == "air" and 6.5 or 7.5)
    f.vx = push_dir * (m.launch and 1.2 or 3.0)
    set_state(f, "air", "down", 1)
  else
    local low = f.state == "crouch" or f.inp.dir <= 3
    set_state(f, low and "chit" or "hit", low and "chit" or "hit", 1)
    f.stun = m.stun
    f.push = push_dir * m.push
  end
  return "hit"
end

local function check_hit(a, f)
  local m = a.move
  if a.hit_done or not m.dmg then return end
  local fr = Fighter.frame(a)
  local hb = fr.hit
  if not hb then return end
  local l, b, r, t = wbox(a, hb)
  local hx, hy = Fighter.hits(f, l, b, r, t)
  if not hx then return end
  if f.state == "air" and f.juggle >= 5 then return end
  a.hit_done = true
  a.connected = Fighter.take_hit(f, a, m, hx, hy)
end

---------------------------------------------------------------- projectiles

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
    f.heat, f.overheat = 100, 160
    Fx.text_alert("OVER HEAT!", f.x, f.y + 60, 0xFF4A28)
    Snd.overheat()
  end
  Snd.shot()
end

---------------------------------------------------------------- fighter update

local function attack(f, o)
  local m = f.move
  if f.connected then
    local i = f.inp
    -- Hyper Super cancel
    if i.super and f.super_meter >= 100 and m.rank < 9 then return start_super(f) end
    -- Special cancel
    if i.special and can_special(f) and m.rank < 5 then return start_special(f) end
    -- Chain cancel into higher rank normal
    local n = normal_for(f, m.air)
    if n and Data.MOVE[n].rank > m.rank then return start_move(f, n) end
  end

  if m.shots and f.ft == m.ticks[f.mi] and f.shots < m.shots then fire(f) end
  check_hit(f, o)

  f.ft = f.ft - 1
  if f.ft > 0 then return end
  f.mi = f.mi + 1
  if f.mi > #m.ticks or (m.loop and f.shots >= m.shots and f.mi > 3) then
    if m.air then f.mi, f.ft = #m.ticks, 1 return end
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
  if f.C.id == "behemoth" then G.shake = max(G.shake, 4) end
  Snd.land(f.C.id == "behemoth")
end

function Fighter.update(f, o)
  f.t = f.t + 1
  f.st = f.st + 1

  -- energy and heat recovery
  if f.state ~= "dash" and f.state ~= "airdash" then
    f.energy = min(100, f.energy + f.C.energy_regen)
  end

  if f.overheat > 0 then
    f.overheat = f.overheat - 1
    if f.overheat == 0 then f.heat = 30 end
    if f.t % 5 == 0 then Fx.smoke(f.x + f.face * 26, f.y + 115) end
  else
    f.heat = max(0, f.heat - 0.35)
  end

  -- tag slots recharge
  f.tag_timer = f.tag_timer + 1
  if f.tag_timer >= 600 then
    f.tag_timer = 0
    f.tag_slots = min(2, f.tag_slots + 1)
  end

  -- bench partner red health recovery
  if f.team and f.team.partner and f.team.partner ~= f then
    local p = f.team.partner
    if p.life > 0 and p.life < p.life_red then
      p.life = min(p.life_red, p.life + 0.35)
    end
  end

  f.life_show = approach(f.life_show, f.life, 3.5)
  if f.combo_t then f.combo_t = f.combo_t - 1; if f.combo_t <= 0 then f.combo_t = nil end end
  if f.hitstop > 0 then f.hitstop = f.hitstop - 1 return end

  local s = f.state

  -- pushback
  if f.push ~= 0 then
    f.x = f.x + f.push
    f.push = approach(f.push, 0, 0.65)
  end

  if FREE[s] then
    f.face = o.x >= f.x and 1 or -1
    f.combo = 0
    f.juggle = 0
    neutral(f, o)
  elseif s == "jump" then
    local i = f.inp
    if i.up and f.air_jumps > 0 and f.vy < 5 then
      f.air_jumps = f.air_jumps - 1
      f.vy = f.jump_spd * 0.90
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
    if f.stun <= 0 then
      set_state(f, (s == "cblock" or s == "chit") and "crouch" or "stand",
                    (s == "cblock" or s == "chit") and "crouch" or "idle")
    end
  elseif s == "air" then
    set_anim(f, "down", 1)
  elseif s == "down" then
    if f.st > 35 and f.life > 0 then set_state(f, "getup", "down", 3) end
  elseif s == "getup" then
    if f.st > 16 then set_state(f, "stand", "idle") end
  elseif s == "tag_out" then
    f.x = f.x + f.vx
    f.y = f.y + f.vy
    if f.x < G.camx - 80 or f.x > G.camx + W + 80 then
      -- fully out
      f.vx, f.vy = 0, 0
    end
  elseif s == "tag_in" then
    -- incoming partner diving in
    check_hit(f, o)
    if f.y <= 0 then
      land(f)
      set_state(f, "stand", "idle")
    end
  elseif s == "win" then
    loop_anim(f)
  end

  -- physics & falling
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

function Fighter.separate(a, b)
  local d = b.x - a.x
  local gap = 84
  if abs(d) < gap and abs(a.y - b.y) < 110 then
    local k = (gap - abs(d)) / 2
    local s = d >= 0 and 1 or -1
    a.x, b.x = a.x - s * k, b.x + s * k
  end
  for _, f in ipairs({ a, b }) do
    f.x = clamp(f.x, max(36, G.camx + 36), min(ARENA_W - 36, G.camx + W - 36))
  end
end

---------------------------------------------------------------- drawing

function Fighter.draw(f)
  local fr = Fighter.art(f)
  local x, y = floor(f.x - G.camx), floor(GROUND - f.y + G.sy)
  local flip = f.face < 0
  local heavy = f.C.id == "behemoth" or f.cfg.armor == "heavy"
  local weapon = f.cfg.weapon

  -- Floor shadow
  local sw = heavy and 48 or 40
  rectfill(x - sw, GROUND + G.sy - 2, sw * 2, 4, 0x10141C)

  -- Mecha layers rendering
  pieces(fr.b, x, y, flip)
  local front = fr.swf
  if weapon == "sword" and fr.sw and not front then pieces(fr.sw, x, y, flip) end
  if heavy and fr.hv then pieces(fr.hv, x, y, flip) end
  if weapon == "guns" and fr.g then pieces(fr.g, x, y, flip) end
  if f.shoulder ~= "gone" then
    local key = heavy and (f.shoulder == "ok" and "sh" or "shd") or (f.shoulder == "ok" and "s" or "sd")
    if fr[key] then pieces(fr[key], x, y, flip) end
  end
  if weapon == "sword" and fr.sw and front then pieces(fr.sw, x, y, flip) end
end

function Fighter.draw_tag(f, label, col)
  local fr = Fighter.frame(f)
  local top = 0
  for _, r in ipairs(fr.b) do top = min(top, r[6]) end
  local x = floor(f.x - G.camx)
  local y = floor(GROUND - f.y + top + G.sy) - 14
  tri(x - 5, y, x + 5, y, x, y + 6, col)
  text_c(label, x, y - 14, col)
end
