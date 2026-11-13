-- Stage 1 & 2 feature suite for the language of bm (core/tools/bml/bml.py)

function is_even(n: i32): bool
  return n % 2 == 0
end

function clamp(x: i32, lo: i32, hi: i32): i32
  return min(max(x, lo), hi)
end

function gcd(a: i32, b: i32): i32
  a = abs(a)
  b = abs(b)
  while b ~= 0 do
    local r = a % b
    a = b
    b = r
  end
  return a
end

function fib(n: i32): i32
  if n <= 0 then
    return 0
  elseif n == 1 then
    return 1
  else
    return fib(n - 1) + fib(n - 2)
  end
end

function collatz_steps(n: i32): i32
  if n <= 1 then return 0 end
  local steps: i32 = 0
  while n > 1 do
    if is_even(n) then
      n = n // 2
    else
      n = 3 * n + 1
    end
    steps = steps + 1
    if steps >= 500 then
      break
    end
  end
  return steps
end

function is_prime(n: i32): bool
  if n < 2 then return false end
  if n == 2 then return true end
  if is_even(n) then return false end
  local d = 3
  while d * d <= n do
    if n % d == 0 then
      return false
    end
    d = d + 2
  end
  return true
end

function count_primes(limit: i32): i32
  local count = 0
  for k = 2, limit do
    if is_prime(k) then
      count = count + 1
    end
  end
  return count
end

function countdown_sum(n: i32): i32
  local total = 0
  for i = n, 1, -2 do
    total = total + i
  end
  return total
end

function boom(x: i32): bool
  -- Infinite recursion if called: tests that 'and' and 'or' short-circuit
  return boom(x + 1)
end

function short_circuit_ok(x: i32): bool
  local a = (x < 0 and x > 0) and boom(x)
  local b = (x < 0 or x >= 0) or boom(x)
  return (not a) and b
end

function arith_mix(a: i32, b: i32): i32
  local d = b
  if d == 0 then d = 3 end
  local q = a // d
  local r = a % d
  local m = (a & 255) - (b & 15)
  return clamp(q * d + r + m, -100000, 100000)
end
