-- bm Tape engine, phase 1. Integer only. Arrays start at 1.

-- Euclidean rhythm: k pulses spread over n steps, then rotated by r steps.
-- Step i (0-based) holds a pulse when (i * k) % n < k. Writes 1 or 0 into out[1..n].
-- Returns the number of pulses (k clamped to 0..n). n is clamped to 1..64.
function euclid(k: i32, n: i32, r: i32, out: i32[]): i32
  local steps = max(1, min(n, 64))
  local pulses = max(0, min(k, steps))
  for i = 0, steps - 1 do
    local hit = 0
    if (i * pulses) % steps < pulses then
      hit = 1
    end
    out[(i + r) % steps + 1] = hit
  end
  return pulses
end

-- Meter fall for n meters. Units: level and peak 0..1000, dt in milliseconds.
-- levels[i]: falls 4 per 5 ms (1000 in 1250 ms), never below 0.
-- peaks[i]: the peak mark. peaks[n + i]: its hold time left in ms.
-- A level at or above the peak sets the peak and a 600 ms hold. After the hold
-- the peak falls 3 per 10 ms (1000 in 3333 ms), never below the level.
function meter_fall(levels: i32[], peaks: i32[], n: i32, dt: i32)
  for i = 1, n do
    local lv = max(0, levels[i] - dt * 4 // 5)
    levels[i] = lv
    if lv >= peaks[i] then
      peaks[i] = lv
      peaks[n + i] = 600
    elseif peaks[n + i] > 0 then
      peaks[n + i] = max(0, peaks[n + i] - dt)
    else
      peaks[i] = max(lv, peaks[i] - dt * 3 // 10)
    end
  end
end
