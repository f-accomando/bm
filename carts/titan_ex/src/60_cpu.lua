-- Titan Clash EX: Intelligent Fighting AI
-- Handles neutral spacing, high/low guarding, chain combo cancels,
-- launcher confirms, hyper supers, and tactical 2v2 tag switching.

local function clear(i)
  i.lp, i.hp, i.lk, i.hk = false, false, false, false
  i.up, i.special, i.uppercut, i.super, i.tag, i.cdash = false, false, false, false, false, 0
  i.c_super_jump = false
end

local function press_normal(i, d, air)
  local r = random()
  if air then
    if r < 0.45 then i.hp = true elseif r < 0.75 then i.hk = true else i.lp = true end
    return
  end
  if d < 75 then
    if r < 0.30 then i.lp = true elseif r < 0.55 then i.lk = true elseif r < 0.78 then i.hp = true else i.hk = true end
  else
    if r < 0.45 then i.hk = true elseif r < 0.75 then i.hp = true else i.lk = true end
  end
end

function Cpu.update(f, o, level_key)
  local L = Data.CPU[level_key or "normal"] or Data.CPU.normal
  local i = f.inp
  clear(i)

  local d = abs(o.x - f.x)
  f.cpu_t = (f.cpu_t or 0) - 1

  -- 1. Tactical Tag Switch (Save damaged fighter, heal red health)
  if f.team and f.team.partner and f.team.partner.life > 0 and f.tag_slots >= 1 then
    if (f.life < 280 or f.shoulder == "gone") and random() < (L.tag or 0.4) then
      i.tag = true
      return
    end
  end

  -- 2. Guarding incoming threats
  local threat = (o.state == "attack" and o.move and not o.move.shots and d < 220) or
                 (o.state == "attack" and o.move == Data.MOVE.fire)
  if threat and (f.state == "stand" or f.state == "walk" or f.state == "crouch" or f.state == "block" or f.state == "cblock") then
    if f.guarding == nil then f.guarding = random() < L.guard end
    if f.guarding then
      local low = o.move.level == "low" or (o.move.crouch and o.move.level ~= "over")
      i.dir = low and 1 or 4
      return
    end
  else
    f.guarding = nil
  end

  -- 3. Combo Cancelling on hit confirmed
  if f.state == "attack" and f.connected and random() < L.chain then
    local m = f.move
    -- Super cancel
    if f.super_meter >= 100 and m.rank < 9 and random() < L.super then
      i.super = true
      return
    end
    -- Special cancel
    if m.rank < 5 and random() < L.special then
      if f.cfg.weapon == "sword" and f.energy >= 28 then
        i.special = true
      elseif f.cfg.weapon ~= "sword" and f.overheat == 0 then
        i.special = true
      end
      return
    end
    -- Chain into higher normal
    if m.crouch then
      if m.rank < 4 then i.hp = true else i.hk = true end
      i.dir = 2
    elseif m.air then
      i.hk = true
    elseif m.rank < 2 then i.lk = true
    elseif m.rank < 3 then i.hp = true
    elseif m.rank < 4 then i.hk = true end
    return
  end

  -- 4. Air combat
  if f.state == "jump" then
    if f.vy < 2 and d < 140 and f.y > 25 and random() < 0.45 then
      press_normal(i, d, true)
    end
    return
  end

  if f.state ~= "stand" and f.state ~= "walk" and f.state ~= "crouch" then
    i.dir = f.plan_dir or 5
    return
  end

  -- 5. Anti-Air check (opponents jumps in)
  if o.y > 45 and d < 130 and o.vy < 0 and random() < L.guard then
    if random() < 0.5 then
      i.uppercut = true
    else
      i.dir = 2
      i.hp = true -- launcher anti-air!
    end
    return
  end

  -- 6. Neutral action plan
  if f.cpu_t <= 0 then
    f.cpu_t = L.think + random(0, L.think)
    local r = random()
    f.plan = nil

    if d > 280 then
      if f.cfg.weapon == "guns" and f.overheat == 0 and r < L.special + 0.25 then
        f.plan = "shoot"
      elseif r < 0.40 and f.energy > 40 then
        f.plan = "dash"
      else
        f.plan = "walk_in"
      end
    elseif d > 150 then
      if f.super_meter >= 100 and r < L.super * 0.7 then
        f.plan = "super"
      elseif f.cfg.weapon == "sword" and f.energy >= 28 and r < L.special * 0.8 then
        f.plan = "slash"
      elseif r < 0.25 then
        f.plan = "jump_in"
      elseif r < 0.55 then
        f.plan = "walk_in"
      else
        f.plan = "poke"
      end
    else
      -- Close range
      if r < 0.15 and FREE[o.state] then
        f.plan = "throw"
      elseif r < 0.45 then
        f.plan = "crouch_attack"
      elseif r < 0.75 then
        f.plan = "attack"
      else
        f.plan = "back_off"
      end
    end
  end

  -- Execute current neutral plan
  local pl = f.plan
  if pl == "shoot" then
    i.special = true
  elseif pl == "slash" then
    i.special = true
  elseif pl == "super" then
    i.super = true
  elseif pl == "throw" then
    i.dir = 6
    i.hp = true
  elseif pl == "dash" then
    i.cdash = 1
  elseif pl == "walk_in" then
    i.dir = 6
  elseif pl == "back_off" then
    i.dir = 4
  elseif pl == "jump_in" then
    i.dir = 9
    if random() < 0.4 then i.c_super_jump = true end
  elseif pl == "crouch_attack" then
    i.dir = 2
    if random() < 0.5 then i.lk = true else i.lp = true end
  elseif pl == "attack" then
    press_normal(i, d, false)
  elseif pl == "poke" then
    if random() < 0.5 then i.lp = true else i.lk = true end
  else
    i.dir = 5
  end
end
