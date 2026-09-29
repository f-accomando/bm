-- The computer's robot: it fills the same controls a player would press
-- (f.inp: directions as on a numpad seen from the robot, 6 = towards the
-- other one), so it obeys the same rules. It thinks every few frames (slower
-- when easy), keeps a plan for a while (walk in, wait, back off, jump in),
-- guards some attacks it sees coming and chains its hits when they land.

Data.CPU = {
  easy = { name = "EASY", think = 22, guard = 0.2, attack = 0.25, chain = 0.2, special = 0.15 },
  normal = { name = "NORMAL", think = 12, guard = 0.5, attack = 0.45, chain = 0.6, special = 0.35 },
  hard = { name = "HARD", think = 6, guard = 0.8, attack = 0.6, chain = 0.9, special = 0.55 },
}
Data.CPUS = { "easy", "normal", "hard" }

local function clear(i)
  i.lp, i.hp, i.lk, i.hk, i.up, i.special, i.cdash = false, false, false, false, false, false, 0
end

local function press_normal(i, d, air)
  local r = random()
  if air then
    if r < 0.5 then i.hp = true else i.hk = true end
    return
  end
  if d < 95 then
    if r < 0.35 then i.lp = true elseif r < 0.6 then i.lk = true elseif r < 0.8 then i.hp = true else i.hk = true end
  else
    if r < 0.45 then i.hk = true elseif r < 0.8 then i.hp = true else i.lk = true end
  end
end

function Cpu.update(f, o, level)
  local L = Data.CPU[level or "normal"]
  local i = f.inp
  clear(i)
  local d = abs(o.x - f.x)
  f.cpu_t = (f.cpu_t or 0) - 1
  -- guarding what is coming: a move of the other robot in reach
  local threat = (o.state == "attack" and o.move and not o.move.shots and d < 230) or
                 (o.state == "attack" and o.move == Data.MOVE.fire)
  if threat and (f.state == "stand" or f.state == "walk" or f.state == "crouch" or f.state == "block" or
                 f.state == "cblock") then
    if f.guarding == nil then f.guarding = random() < L.guard end
    if f.guarding then
      local low = o.move.level == "low" or (o.move.crouch and o.move.level ~= "over")
      i.dir = low and 1 or 4
      return
    end
  else
    f.guarding = nil
  end
  -- a hit landed: follow it with something stronger
  if f.state == "attack" and f.connected and random() < L.chain then
    local m = f.move
    if m.rank < 9 and random() < L.special and (f.cfg.weapon == "sword" and f.energy >= 30 or f.overheat == 0) then
      i.special = true
    elseif m.air then
      i.hk = true
    elseif m.crouch then
      if m.rank < 3 then i.hp = true else i.hk = true end
      i.dir = 2
    elseif m.rank < 2 then i.lk = true
    elseif m.rank < 3 then i.hp = true
    elseif m.rank < 4 then i.hk = true end
    return
  end
  -- in the air: kick when coming down near the other
  if f.state == "jump" then
    if f.vy < 2 and d < 150 and f.y > 30 and random() < 0.3 then press_normal(i, d, true) end
    return
  end
  if f.state ~= "stand" and f.state ~= "walk" and f.state ~= "crouch" then
    i.dir = f.plan_dir or 5
    return
  end
  -- the other one jumps in: hit it on the way down
  if o.y > 40 and d < 140 and o.vy < 0 and random() < L.guard then
    i.dir = 2
    i.hp = true
    return
  end
  -- a new plan now and then
  if f.cpu_t <= 0 then
    f.cpu_t = L.think + random(0, L.think)
    local guns = f.cfg.weapon == "guns"
    local r = random()
    f.plan = nil
    if d > 300 then
      if guns and f.overheat == 0 and r < L.special + 0.2 then
        f.plan = "shoot"
      elseif r < 0.35 and f.energy > 40 then
        f.plan = "dash"
      else
        f.plan = "walk"
      end
    elseif d > 170 then
      if guns and f.overheat == 0 and r < L.special then
        f.plan = "shoot"
      elseif not guns and f.energy >= 30 and d < 210 and r < L.special * 0.6 then
        f.plan = "slash"
      elseif r < 0.2 then
        f.plan = "jump"
      elseif r < 0.3 and f.energy > 40 then
        f.plan = "dash"
      else
        f.plan = "walk"
      end
    else
      if r < L.attack then
        f.plan = "attack"
      elseif r < L.attack + 0.12 then
        f.plan = "back"
      elseif r < L.attack + 0.2 then
        f.plan = "sweep"
      elseif not guns and f.energy >= 30 and r < L.attack + 0.2 + L.special * 0.3 then
        f.plan = "slash"
      else
        f.plan = "wait"
      end
    end
  end
  local p = f.plan
  f.plan_dir = 5
  if p == "walk" then
    i.dir = 6
  elseif p == "back" then
    i.dir = 4
  elseif p == "dash" then
    i.cdash = 1
    f.plan = "walk"
  elseif p == "jump" then
    i.dir = 9
    f.plan = nil
  elseif p == "shoot" or p == "slash" then
    i.special = true
    f.plan = "wait"
  elseif p == "sweep" then
    i.dir = 3
    i.hk = d < 150
    if d >= 150 then i.dir = 6 end
  elseif p == "attack" then
    if d > 120 then
      i.dir = 6
    else
      press_normal(i, d)
      f.plan = "wait"
    end
  else
    i.dir = 5
  end
end
