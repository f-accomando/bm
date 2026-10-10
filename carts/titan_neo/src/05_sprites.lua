-- High quality sprite positions in sheet.png
-- idle at 0,0 120x180; dash 130,0; jump 260,0

SPR = {
  idle = {0, 0, 120, 180, -60, -180},
  dash = {130, 0, 120, 180, -60, -180},
  jump = {260, 0, 120, 180, -60, -180},
  -- fallbacks
  punch = {0, 0, 120, 180, -60, -180},
  kick = {130, 0, 120, 180, -60, -180},
}

function sprite(name, x, y, flip)
  local r = SPR[name] or SPR.idle
  if flip then
    sspr(r[1], r[2], r[3], r[4], x - r[5] - r[3], y + r[6], true)
  else
    sspr(r[1], r[2], r[3], r[4], x + r[5], y + r[6])
  end
end
