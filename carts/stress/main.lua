-- Rendering stress test through the cartridge API (Lua 5.4 + C drawing).
-- Same method as the C part (src/bm/stress.c): raise the load step by
-- step, measure update+draw time per frame, interpolate the loads that
-- fit in 16.7 ms (60 fps) and 33.3 ms (30 fps). Results go to log().

local W, H = SCREEN_W, SCREEN_H
local FRAMES, LIMIT = 20, 40.0
local ball

local tests = {
  { name = "sprites 16x16 (Lua)", unit = "spr", start = 16, max = 30000,
    draw = function(n, f)
      cls(0x0A0A1E)
      for i = 1, n do
        local x = (i * 97 + f * (1 + i % 5)) % (W + 16) - 16
        local y = (i * 61 + f * (1 + i % 3)) % (H + 16) - 16
        spr(0, x, y, 2, 2, i % 2 == 1)
      end
    end },
  { name = "3D spheres 96 (Lua)", unit = "obj", start = 1, max = 3000,
    draw = function(n, f)
      cls(0x0A0A1E)
      zclear()
      camera3d(0, 0, -8, 0, 0, 60)
      local side = math.ceil(math.sqrt(n))
      local step = 9.0 / math.max(side, 1)
      for i = 0, n - 1 do
        local x = -4.5 + step * (i % side + 0.5)
        local y = 2.6 - step * 0.56 * (i // side + 0.5)
        draw3d(ball, x, y, i % 3, f * 0.03 + i, f * 0.05, 0, step * 0.45)
      end
    end },
}

local ti, frame, acc, tris = 1, 0, 0, 0
local n = tests[1].start
local samples, results = {}, {}

local function threshold(s, limit)
  local t = tests[ti]
  if #s == 0 or s[1].ms > limit then return "<" .. t.start end
  for i = 2, #s do
    if s[i].ms > limit then
      local a, b = s[i - 1], s[i]
      local k = (limit - a.ms) / (b.ms - a.ms)
      local nn = math.floor(a.n + k * (b.n - a.n))
      if t.unit == "obj" then
        return string.format("%d (%d tri)", nn, math.floor(a.tris + k * (b.tris - a.tris)))
      end
      return tostring(nn)
    end
  end
  return ">" .. t.max
end

local function finish_test()
  local t = tests[ti]
  local a, b = samples[1], samples[#samples]
  local per = #samples > 1 and (b.ms - a.ms) * 1000 / (b.n - a.n) or 0
  results[#results + 1] = string.format("%-24s%16s%16s%10.2f %s", t.name,
    threshold(samples, 16.667), threshold(samples, 33.333), per, t.unit)
end

function _init()
  for y = 0, 15 do
    for x = 0, 15 do
      if (x - 7.5) ^ 2 + (y - 7.5) ^ 2 < 60 then sset(x, y, rgb(x * 16, y * 16, 200)) end
    end
  end
  ball = mesh_sphere(6, 8, 0x4080FF, 0xFFC040)
end

function _update()
  if frame > 0 then acc = acc + stat(1) end    -- cost of the previous frame
  if frame == FRAMES then tris = stat(4) end   -- 3D triangles of the last frame
  frame = frame + 1
  if frame <= FRAMES then return end

  local ms = acc / FRAMES
  samples[#samples + 1] = { n = n, ms = ms, tris = tris }
  frame, acc = 0, 0
  n = math.max(n + 1, n * 3 // 2)
  if ms > LIMIT or n > tests[ti].max then
    finish_test()
    ti, samples = ti + 1, {}
    n = tests[ti] and tests[ti].start or 0
    if ti > #tests then
      for _, line in ipairs(results) do log(line) end
      quit()
    end
  end
end

function _draw()
  if ti > #tests then return end
  tests[ti].draw(n, frame)
  rectfill(0, 0, W, 16, 0)
  print(string.format("stress: %s  n=%d  %.2f ms", tests[ti].name, n, stat(1)), 0, 0, 0xFFFFFF)
end
