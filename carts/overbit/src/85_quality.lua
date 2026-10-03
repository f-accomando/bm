-- Quality: five levels, chosen by hand or by the governor, which watches the
-- frame time on the console (stat(1): _update + _draw) and keeps 60 fps:
-- one level down when the frames run long, one up when there is room.
--
--  level  models (detail near)  Gouraud  shadows        effects
--  4      3, far LOD late       yes      planar, 30 m   all
--  3      3                     yes      planar, 30 m   all
--  2      3 near, 2             yes      none           3/4
--  1      2                     no       none           1/2
--  0      1                     no       none           1/4
--
-- With the GPU (stat(9)) the pixels cost the ARM nothing: Gouraud from
-- level 1 and the planar shadows from level 2 (to 40 m), only their
-- vertices and triangles count.

Quality = { names = { "LOW", "MEDIUM", "HIGH", "ULTRA", "EXTREME" } }

local hist, nh = {}, 0
local since = 0             -- seconds since the last change
local BUDGET = 16.0         -- ms: above this the frame misses 60 fps
local ROOM = 11.5           -- ms: below this for a while, try a level up

function Quality.apply()
  local q = G.quality
  Fx.density = ({ 0.25, 0.5, 0.75, 1, 1 })[q + 1]
  shadow3d(0)
end

function Quality.set(q)
  G.quality = clamp(q, 0, 4)
  Quality.apply()
  hist, nh, since = {}, 0, 0
end

-- every frame, with the cost of the last one
function Quality.update()
  since = since + DT
  local ms = stat(1)
  nh = nh + 1
  hist[(nh - 1) % 30 + 1] = ms
  if not G.qauto or nh < 30 then return end
  local sum, worst = 0, 0
  for i = 1, 30 do
    sum = sum + hist[i]
    if hist[i] > worst then worst = hist[i] end
  end
  local avg = sum / 30
  if avg > BUDGET and since > 1.0 and G.quality > 0 then
    Quality.set(G.quality - 1)
    log(string.format("overbit quality down to %d (%.1f ms)", G.quality, avg))
  elseif avg < ROOM and worst < BUDGET and since > 4.0 and G.quality < 4 then
    Quality.set(G.quality + 1)
    log(string.format("overbit quality up to %d (%.1f ms)", G.quality, avg))
  end
end

function Quality.avg()
  local n = min(nh, 30)
  if n == 0 then return 0 end
  local s = 0
  for i = 1, n do s = s + hist[i] end
  return s / n
end
