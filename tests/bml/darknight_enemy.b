-- Stage 8: Dark Night enemy movement, wall bounce, and pairwise separation scan in the language of bm (.b).
-- Moves N enemies inside a 5.0 x 5.0 room and finds all live enemy pairs closer than 0.8 units.

function bounce_vel(p: f32, v: f32): f32
  if p < 0.0 or p > 5.0 then
    return -v
  end
  return v
end

function cam_blocked(x: f32, z: f32, n: i32, ox: f32[], oz: f32[], r: f32[]): bool
  for i = 1, n do
    local rr = r[i] + 0.3
    local dx = x - ox[i]
    local dz = z - oz[i]
    if dx * dx + dz * dz < rr * rr then
      return true
    end
  end
  return false
end

function move_and_separate(
  n: i32,
  x: f32[],
  z: f32[],
  vx: f32[],
  vz: f32[],
  alive: bool[],
  out: i32[],
  counts: i32[]
): i32
  for i = 1, n do
    if alive[i] then
      local nvx = bounce_vel(x[i], vx[i])
      local nvz = bounce_vel(z[i], vz[i])
      vx[i] = nvx
      vz[i] = nvz
      x[i] = x[i] + nvx
      z[i] = z[i] + nvz
    end
  end

  local total = 0
  for self = 1, n do
    local count = 0
    counts[self] = 0
    if alive[self] then
      local ex = x[self]
      local ez = z[self]
      for i = 1, n do
        if i ~= self and alive[i] then
          local dx = ex - x[i]
          if dx < 0.8 and dx > -0.8 then
            local dz = ez - z[i]
            local d2 = dx * dx + dz * dz
            if d2 < 0.64 and d2 > 0.00001 then
              count = count + 1
              out[(self - 1) * n + count] = i
            end
          end
        end
      end
      counts[self] = count
      total = total + count
    end
  end
  return total
end
