Fighter = {}

function Fighter.new(p, pad, cfg, x, face)
  local A = Data.ARMOR[cfg.armor or "light"]
  return {
    p = p, pad = pad, cfg = cfg, A = A,
    x = x, y = 0, vx = 0, vy = 0, face = face,
    state = "stand", anim = "idle", life = 1000, armor = A.armor,
    energy = 100, heat = 0, overheat = 0,
    stun = 0, hitstop = 0
  }
end

function Fighter.update(f, o)
  -- simplified but solid controls and states
  local p = f.pad
  local l, r, u, d = btn(0, p), btn(1, p), btn(2, p), btn(3, p)
  local punch, kick = btnp(6, p), btnp(4, p)
  local special = btnp(5, p) or btnp(7, p)

  if f.stun > 0 then
    f.stun = f.stun - 1
    return
  end

  if f.state == "stand" or f.state == "walk" then
    if l or r then
      f.vx = (r and 1 or -1) * f.A.walk * f.face
      f.anim = "dash"
      f.state = "walk"
    else
      f.vx = 0
      f.anim = "idle"
      f.state = "stand"
    end
    if u then
      f.vy = f.A.jump
      f.state = "jump"
      f.anim = "jump"
    end
    if punch or kick then
      f.state = "attack"
      f.anim = punch and "punch" or "kick"
      f.stun = 8
    end
    if special and f.energy > 20 then
      f.energy = f.energy - 20
      f.state = "special"
      f.anim = "dash"
      f.stun = 12
    end
  elseif f.state == "jump" then
    f.vy = f.vy - 0.45
    f.y = f.y + f.vy
    if f.y <= 0 then
      f.y, f.vy = 0, 0
      f.state = "stand"
      f.anim = "idle"
    end
  end

  f.x = f.x + f.vx
  f.x = clamp(f.x, 60, ARENA_W - 60)
  f.energy = min(100, f.energy + 0.3)
end

function Fighter.draw(f)
  local x = floor(f.x - G.camx)
  local y = floor(GROUND - f.y + G.sy)
  local flip = f.face < 0
  -- shadow
  rectfill(x - 40, y - 2, 80, 4, 0x101018)
  sprite(f.anim, x, y, flip)
  -- simple damage indicator
  if f.armor < 150 then
    if random(6) == 1 then circfill(x + random(-20,20), y - 80 + random(-20,20), 2, 0xFF6020) end
  end
end
