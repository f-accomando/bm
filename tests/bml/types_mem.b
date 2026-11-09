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

const SCALE_EXTRA: f32 = 0.5
const ENEMY_MAX: i32 = 42
const FLAG_DEBUG: bool = true
const TEST_LUT: i32[] = [10, 25, 40, 55, 70]
const TEST_FLUT: f32[] = [1.5, 2.5, 3.5, 4.5]

enum Direction
  NORTH = 1,
  EAST = 2,
  SOUTH = 5,
  WEST = 6
end

function Vec2f:len_sq(): f32
  return self.x * self.x + self.y * self.y
end

function Vec2f:scale(factor: f32)
  self.x = self.x * factor
  self.y = self.y * factor
end

function test_struct_methods(v: Vec2f, factor: f32): i32
  const LOCAL_EXTRA: f32 = 0.5
  v:scale(factor + LOCAL_EXTRA)
  local d = Direction.SOUTH
  if FLAG_DEBUG and d == 5 then
    return floor(v:len_sq())
  end
  return -1
end

function test_bitwise(a: i32, b: i32): i32
  local o = a | b
  local x = a ~ b
  local n = ~a
  local s1 = (a << 4) | (b & 15)
  local s2 = (a >> 2) + (~b & 3)
  local prec = a | b & 7 ~ 2 << 1
  return (o + x + n + s1 + s2 + prec) & 65535
end

function test_memory_ops(base: i32): i32
  poke(base + 0, 42)
  poke(base + 1, 255)
  local b0 = peek(base + 0)
  local b1 = peek(base + 1)
  poke2(base + 8, 0xABCD)
  local h = peek2(base + 8)
  poke4(base + 16, 0x12345678)
  local w = peek4(base + 16)
  memset(base + 32, 7, 16)
  memcpy(base + 64, base + 32, 16)
  local m0 = peek(base + 64)
  local m15 = peek(base + 79)
  return (b0 + b1 + (h & 255) + (w & 255) + m0 + m15) & 65535
end

function test_compound_ops(x: i32): i32
  x += 5
  x -= 2
  x *= 3
  x &= 0xFF
  x |= 0x100
  x <<= 2
  x >>= 1
  return x
end

function test_compound_struct_array(v: Vec2f, arr: i32[], idx: i32): i32
  v.x += 1.5
  v.y *= 2.0
  arr[idx] += 10
  arr[idx + 1] *= 3
  return floor(v.x + v.y) + arr[idx] + arr[idx + 1]
end

struct Packet
  id: i32
  len: i32
  val: i32
end

function test_struct_ptr(base: i32): i32
  local pkt: Packet = Packet(base)
  pkt.id = 101
  pkt.len = 16
  pkt.val = 2024
  return pkt.id + pkt.len + pkt.val
end

function test_arr_ptr(base: i32, n: i32): i32
  local arr: i32[] = base
  for i = 1, n do
    arr[i] = i * 10
  end
  local sum: i32 = 0
  for i = 1, n do
    sum += arr[i]
  end
  return sum
end

enum Action
  STOP = 0
  RUN = 1
  JUMP = 2
end

function test_switch_case(act: i32, speed: i32): i32
  local result: i32 = 0
  switch act do
  case Action.STOP then
    result = 0
  case Action.RUN then
    result = speed * 2
  case Action.JUMP then
    result = speed * 3 + 10
  default
    result = -1
  end
  return result
end

function test_type_inference(base: i32): i32
  local a = 15
  local b = 25
  local diff = b - a
  local pkt = Packet(base)
  pkt.id = diff
  pkt.len = a * 2
  pkt.val = b * 4
  local total = pkt.id + pkt.len + pkt.val
  return total
end

function test_math_builtins(v: f32): i32
  local c_int = clamp(15, 0, 10) + clamp(-5, 0, 10) + clamp(7, 0, 10)
  local c_flt: f32 = clamp(v, 0.0, 10.0)
  local l_val: f32 = lerp(10.0, 30.0, 0.25)
  local f_val: i32 = flr(15.8) + flr(-2.1)
  local s0: f32 = sin(0.0)
  local c0: f32 = cos(0.0)
  local at0: f32 = atan2(0.0, 1.0)
  return c_int + f_val + flr(c_flt) + flr(l_val) + flr(s0) + flr(c0) + flr(at0)
end

function test_rodata_arrays(idx: i32): i32
  local iv = TEST_LUT[idx]
  local fv = TEST_FLUT[idx]
  return iv + flr(fv)
end

function test_local_arrays(idx: i32): i32
  local a: i32[] = [10, 20, 30, 40, 50]
  local fa: f32[] = [1.5, 2.5, 3.5, 4.5]
  local sum = 0
  for i = 1, #a do
    sum = sum + a[i]
  end
  a[2] = 99
  fa[idx] = fa[idx] + 10.0
  local const_len = #TEST_LUT
  local str_len = #"hello"
  return sum + a[2] + flr(fa[idx]) + const_len + str_len
end





