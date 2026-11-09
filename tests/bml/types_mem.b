-- Stage 6 test program: i32 32-bit overflow, arrays (i32[], bool[], f32[]), structs, and f32 VFPv2.

struct Body
  x: i32
  y: i32
  vx: i32
  vy: i32
end

struct Vec2f
  x: f32
  y: f32
end

function wrap_add(a: i32, b: i32): i32
  return a + b
end

function wrap_mul(a: i32, b: i32): i32
  return a * b
end

function array_sum_scale(arr: i32[], n: i32, factor: i32): i32
  local s = 0
  for i = 1, n do
    local v = arr[i] * factor
    arr[i] = v
    s = s + v
  end
  return s
end

function count_true(flags: bool[], n: i32): i32
  local c = 0
  for i = 1, n do
    if flags[i] then
      c = c + 1
      flags[i] = false
    else
      flags[i] = true
    end
  end
  return c
end

function step_body(b: Body, steps: i32, limit: i32): i32
  for i = 1, steps do
    b.x = b.x + b.vx
    b.y = b.y + b.vy
    if b.x > limit then
      b.x = limit
      b.vx = -b.vx
    elseif b.x < -limit then
      b.x = -limit
      b.vx = -b.vx
    end
    if b.y > limit then
      b.y = limit
      b.vy = -b.vy
    elseif b.y < -limit then
      b.y = -limit
      b.vy = -b.vy
    end
  end
  return abs(b.x) + abs(b.y)
end

function f32_poly(x: f32, y: f32): i32
  local d = sqrt(x * x + y * y)
  local m = min(max(d - 1.5, -10.0), 100.0)
  local a = abs(-m) + f32(3)
  return floor(a * 4.0)
end

function f32_cmp(a: f32, b: f32): bool
  return (a < b) and (a <= b) and (b > a) and (b >= a) and (a ~= b) and (a == a)
end

function dot_scale_f32(xs: f32[], ys: f32[], n: i32): i32
  local acc: f32 = 0.0
  for i = 1, n do
    local p = xs[i] * ys[i]
    xs[i] = p + 0.5
    acc = acc + p
  end
  return floor(acc * 2.0)
end

function step_vec2f(v: Vec2f, dx: f32, dy: f32): i32
  v.x = v.x + dx
  v.y = v.y + dy
  return floor(sqrt(v.x * v.x + v.y * v.y) * 8.0)
end
