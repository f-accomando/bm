-- riff: a language of patterns for the music of the games, in Lua, after
-- TidalCycles and Strudel (require "riff"). Rhythms and melodies are
-- written in a line, the mini-notation ("kick*4, ~ snare", "<c3 a2 f2
-- g2>", "c e g"), and changed by functions that chain:
--
--   local R = require "riff"
--   R.play("drums", R.s "kick*4, ~ snare, hat*8")
--   R.play("bass", R.note "<c2 a1 f1 g1>*2" :s "acid" :lpf(R.sine:range(300, 2000):slow(4)))
--   function _update() R.update() end
--
-- Time is counted in cycles (a bar of four beats, by default 2 seconds);
-- a pattern is a function from a span of cycles to the events in it, each
-- with its whole (the span of the note) and its part (the piece of it in
-- the span asked). R.update() asks a little ahead of the sound's clock
-- and queues the notes that start with play_at(): the sound's interrupt
-- starts them on time, whatever the frame rate. The instruments are the
-- ready-made ones (instruments()) and the sounds of the cartridge's bank
-- by name; R.code(text) runs a piece of live code (bm Code: Ctrl+Enter),
-- R.bake() turns a pattern into a song for the bank (bm Sound).
-- docs/API.md (Riff) and docs/RIFF.md say it all.

local NAME, loadtext = ...      -- loadtext: the kernel's loader of Lua text, for R.code
local R = {}

local floor, min, max, huge = math.floor, math.min, math.max, math.huge
local EPS = 1e-9
local unpack = table.unpack

------------------------------------------------------------------ patterns

local P = {}
P.__index = P
R.Pattern = P

local function is_pat(x) return getmetatable(x) == P end
R.is_pattern = is_pat

local function mk(q) return setmetatable({ q = q }, P) end

-- an event: whole [wb, we) (nil for a continuous signal), part [pb, pe),
-- value, and where it was written (a list of {from, to} offsets in the
-- code, for bm Code's highlights)
local function H(wb, we, pb, pe, v, loc)
  return { wb = wb, we = we, pb = pb, pe = pe, v = v, loc = loc }
end

local function join_loc(a, b)
  if not a then return b end
  if not b or a == b then return a end
  local r = {}
  for i = 1, #a do r[#r + 1] = a[i] end
  for i = 1, #b do r[#r + 1] = b[i] end
  return r
end

local silence = mk(function() return {} end)
R.silence = silence

local function pure(v, loc)
  local p = mk(function(b, e)
    local out, n = {}, 0
    local c = floor(b)
    while c < e do
      local sb, se = c > b and c or b, c + 1 < e and c + 1 or e
      if se > sb then n = n + 1; out[n] = H(c, c + 1, sb, se, v, loc) end
      c = c + 1
    end
    return out
  end)
  p.const, p.const_loc = v, loc
  return p
end
R.pure = function(v) return pure(v) end

-- a function of time, sampled in the middle of the span asked
local function signal(f)
  return mk(function(b, e) return { H(nil, nil, b, e, f((b + e) * 0.5)) } end)
end
R.signal = signal

local reify                     -- a string (mini-notation), a number, a list or a pattern -> a pattern

-- the value of p at time t (the event that started last), and the event
local function sample(p, t)
  if p.const ~= nil then return p.const, p end
  local hs, best = p.q(t, t + 1e-7), nil
  for i = 1, #hs do
    local h = hs[i]
    if not best or (h.wb or -huge) > (best.wb or -huge) then best = h end
  end
  if best then return best.v, best end
end

function P:query(b, e) return self.q(b, e) end

-- the events that start in [b, e), in time order (tests, bake)
function P:events(b, e)
  local out = {}
  for _, h in ipairs(self.q(b, e)) do
    if h.wb and h.wb >= b - EPS and h.wb < e - EPS then out[#out + 1] = h end
  end
  table.sort(out, function(x, y) return x.wb < y.wb end)
  return out
end

function P:fmap(f)
  local p = self
  return mk(function(b, e)
    local hs = p.q(b, e)
    for i = 1, #hs do hs[i].v = f(hs[i].v) end
    return hs
  end)
end

-- a parameter that may be a pattern: the pattern f(value) for the events
-- of x, inside each of them
local function tparam(x, f)
  if type(x) ~= "table" and type(x) ~= "string" then return f(x) end
  local xp = reify(x)
  if xp.const ~= nil then return f(xp.const) end
  return mk(function(b, e)
    local out, n = {}, 0
    for _, xh in ipairs(xp.q(b, e)) do
      local hs = f(xh.v).q(xh.pb, xh.pe)
      for i = 1, #hs do n = n + 1; out[n] = hs[i] end
    end
    return out
  end)
end

local function _fast(p, k)
  if k == 0 then return silence end
  if k == 1 then return p end
  if k < 0 then return _fast(p, -k):rev() end
  return mk(function(b, e)
    local hs = p.q(b * k, e * k)
    for i = 1, #hs do
      local h = hs[i]
      h.pb, h.pe = h.pb / k, h.pe / k
      if h.wb then h.wb, h.we = h.wb / k, h.we / k end
    end
    return hs
  end)
end

function P:fast(k) local p = self; return tparam(k, function(v) return _fast(p, v) end) end
function P:slow(k) local p = self; return tparam(k, function(v) return v == 0 and silence or _fast(p, 1 / v) end) end
function P:hurry(k) return self:fast(k) end

local function _late(p, t)
  if t == 0 then return p end
  return mk(function(b, e)
    local hs = p.q(b - t, e - t)
    for i = 1, #hs do
      local h = hs[i]
      h.pb, h.pe = h.pb + t, h.pe + t
      if h.wb then h.wb, h.we = h.wb + t, h.we + t end
    end
    return hs
  end)
end

function P:late(t) local p = self; return tparam(t, function(v) return _late(p, v) end) end
function P:early(t) local p = self; return tparam(t, function(v) return _late(p, -v) end) end

-- each cycle of p squeezed into [sb, se) of the cycle
local function compress(p, sb, se)
  if se <= sb or sb >= 1 or se <= 0 then return silence end
  local w = se - sb
  return mk(function(b, e)
    local out, n = {}, 0
    local c = floor(b)
    while c < e do
      local qb, qe = max(b, c + sb), min(e, c + se)
      if qe > qb then
        local hs = p.q(c + (qb - c - sb) / w, c + (qe - c - sb) / w)
        for i = 1, #hs do
          local h = hs[i]
          h.pb, h.pe = c + sb + (h.pb - c) * w, c + sb + (h.pe - c) * w
          if h.wb then h.wb, h.we = c + sb + (h.wb - c) * w, c + sb + (h.we - c) * w end
          n = n + 1; out[n] = h
        end
      end
      c = c + 1
    end
    return out
  end)
end

local function stack(pats)
  if #pats == 0 then return silence end
  if #pats == 1 then return pats[1] end
  return mk(function(b, e)
    local out, n = {}, 0
    for i = 1, #pats do
      local hs = pats[i].q(b, e)
      for j = 1, #hs do n = n + 1; out[n] = hs[j] end
    end
    return out
  end)
end

-- {weight, pattern}, ...: each in its share of the cycle
local function timecat(items)
  local total = 0
  for _, it in ipairs(items) do total = total + it[1] end
  if total <= 0 then return silence end
  if #items == 1 then return items[1][2] end
  -- only plain values: one loop, no compression (the common case: "kick sd hat sd")
  local plain = true
  for _, it in ipairs(items) do if it[2].const == nil and it[2] ~= silence then plain = false break end end
  if plain then
    local starts, pos = {}, 0
    for i, it in ipairs(items) do starts[i] = pos / total; pos = pos + it[1] end
    starts[#items + 1] = 1
    return mk(function(b, e)
      local out, n = {}, 0
      local c = floor(b)
      while c < e do
        for i = 1, #items do
          local it = items[i][2]
          if it ~= silence then
            local wb, we = c + starts[i], c + starts[i + 1]
            local pb, pe = wb > b and wb or b, we < e and we or e
            if pe > pb then n = n + 1; out[n] = H(wb, we, pb, pe, it.const, it.const_loc) end
          end
        end
        c = c + 1
      end
      return out
    end)
  end
  local parts, pos = {}, 0
  for _, it in ipairs(items) do
    parts[#parts + 1] = compress(it[2], pos / total, (pos + it[1]) / total)
    pos = pos + it[1]
  end
  return stack(parts)
end

-- one pattern a cycle, in turn; each sees its own cycles go by
local function slowcat(pats)
  local n = #pats
  if n == 0 then return silence end
  if n == 1 then return pats[1] end
  return mk(function(b, e)
    local out, k = {}, 0
    local c = floor(b)
    while c < e do
      local sb, se = max(b, c), min(e, c + 1)
      if se > sb then
        local off = c - floor(c / n)
        local hs = pats[c % n + 1].q(sb - off, se - off)
        for i = 1, #hs do
          local h = hs[i]
          h.pb, h.pe = h.pb + off, h.pe + off
          if h.wb then h.wb, h.we = h.wb + off, h.we + off end
          k = k + 1; out[k] = h
        end
      end
      c = c + 1
    end
    return out
  end)
end

-- numbers a little random but the same for the same time (as in Tidal)
local function rand_at(t, seed)
  local x = floor(t * 1048576) * 0x9E3779B1 + (seed or 0) * 0x632BE5AB + 0x5851F42D
  x = x ~ (x << 13)
  x = x ~ (x >> 7)
  x = x ~ (x << 17)
  x = x * 0x2545F4914F6CDD1D
  return ((x >> 11) & 0x1FFFFFFFFFFFFF) / 9007199254740992.0
end
R.rand_at = rand_at

-- one of the patterns at random each cycle
local function randcat(pats, seed)
  local n = #pats
  if n == 0 then return silence end
  return mk(function(b, e)
    local out, k = {}, 0
    local c = floor(b)
    while c < e do
      local sb, se = max(b, c), min(e, c + 1)
      if se > sb then
        local i = floor(rand_at(c + 0.5, 77 + (seed or 0)) * n) + 1
        local hs = pats[i].q(sb, se)
        for j = 1, #hs do k = k + 1; out[k] = hs[j] end
      end
      c = c + 1
    end
    return out
  end)
end

-- per cycle: f(the cycle number, b, e) -> the events of that piece
local function per_cycle(f)
  return mk(function(b, e)
    local out, n = {}, 0
    local c = floor(b)
    while c < e do
      local sb, se = max(b, c), min(e, c + 1)
      if se > sb then
        local hs = f(c, sb, se)
        for i = 1, #hs do n = n + 1; out[n] = hs[i] end
      end
      c = c + 1
    end
    return out
  end)
end

function P:rev()
  local p = self
  return per_cycle(function(c, b, e)
    local m = 2 * c + 1
    local hs = p.q(m - e, m - b)
    for i = 1, #hs do
      local h = hs[i]
      h.pb, h.pe = m - h.pe, m - h.pb
      if h.wb then h.wb, h.we = m - h.we, m - h.wb end
    end
    return hs
  end)
end

-- keeps the events whose start passes keep(event)
local function filter(p, keep)
  return mk(function(b, e)
    local hs, out = p.q(b, e), {}
    for i = 1, #hs do if keep(hs[i]) then out[#out + 1] = hs[i] end end
    return out
  end)
end

local function truthy(v)
  return v ~= nil and v ~= false and v ~= 0 and v ~= "0" and v ~= "f" and v ~= "false" and v ~= "~"
end

-- the structure of the left, the values of x sampled at each start:
-- fn(left value, x value) -> the new value (nil: the event goes)
local function with_values(p, x, fn)
  local xp = reify(x)
  if xp.const ~= nil then
    local xv, xl = xp.const, xp.const_loc
    return mk(function(b, e)
      local hs, out = p.q(b, e), {}
      for i = 1, #hs do
        local h = hs[i]
        local v = fn(h.v, xv)
        if v ~= nil then h.v = v; h.loc = join_loc(h.loc, xl); out[#out + 1] = h end
      end
      return out
    end)
  end
  return mk(function(b, e)
    local hs, out = p.q(b, e), {}
    for i = 1, #hs do
      local h = hs[i]
      local xv, xh = sample(xp, h.wb or h.pb)
      if xv ~= nil then
        local v = fn(h.v, xv)
        if v ~= nil then h.v = v; h.loc = join_loc(h.loc, xh.loc or xh.const_loc); out[#out + 1] = h end
      end
    end
    return out
  end)
end
R.with_values = with_values

function P:struct(x)
  local bp, p = reify(x), self
  return mk(function(b, e)
    local out = {}
    for _, h in ipairs(bp.q(b, e)) do
      if h.wb and truthy(h.v) then
        local v, ph = sample(p, h.wb)
        if v ~= nil then
          h.v = v
          h.loc = ph and (ph.loc or ph.const_loc) or h.loc
          out[#out + 1] = h
        end
      end
    end
    return out
  end)
end

function P:mask(x)
  local mp, p = reify(x), self
  return filter(p, function(h) return truthy(sample(mp, h.wb or h.pb)) end)
end

-- Euclid's rhythms: k beats as even as they can be over n steps, turned
-- left by rot ("x..x..x." for 3, 8)
local function bjorklund(k, n)
  k, n = floor(k), floor(n)
  local r = {}
  if n <= 0 then return r end
  if k <= 0 then for i = 1, n do r[i] = false end return r end
  if k >= n then for i = 1, n do r[i] = true end return r end
  local a, b = {}, {}
  for i = 1, k do a[i] = { true } end
  for i = 1, n - k do b[i] = { false } end
  while #b > 1 do
    local na, nb, m = {}, {}, min(#a, #b)
    for i = 1, m do
      local t = { unpack(a[i]) }
      for _, x in ipairs(b[i]) do t[#t + 1] = x end
      na[i] = t
    end
    if #a > m then for i = m + 1, #a do nb[#nb + 1] = a[i] end
    else for i = m + 1, #b do nb[#nb + 1] = b[i] end end
    a, b = na, nb
  end
  for _, g in ipairs(a) do for _, x in ipairs(g) do r[#r + 1] = x end end
  for _, g in ipairs(b) do for _, x in ipairs(g) do r[#r + 1] = x end end
  return r
end
R.bjorklund = bjorklund

local function euclid_pat(k, n, rot)
  local bits = bjorklund(k, n)
  local m = #bits
  if m == 0 then return silence end
  rot = floor(rot or 0) % m
  local items = {}
  for i = 1, m do items[i] = { 1, bits[(i - 1 + rot) % m + 1] and pure(true) or silence } end
  return timecat(items)
end

function P:euclid(k, n, rot)
  local p = self
  return tparam(k, function(kv) return tparam(n, function(nv) return tparam(rot or 0, function(rv)
    return p:struct(euclid_pat(kv, nv, rv)) end) end) end)
end
P.euclidRot = P.euclid

function P:ply(k)
  local p = self
  return tparam(k, function(n)
    n = floor(n)
    if n <= 1 then return p end
    return mk(function(b, e)
      local out = {}
      for _, h in ipairs(p.q(b, e)) do
        if h.wb then
          local d = (h.we - h.wb) / n
          for j = 0, n - 1 do
            local wb, we = h.wb + j * d, h.wb + (j + 1) * d
            local pb, pe = max(wb, h.pb), min(we, h.pe)
            if pe > pb then out[#out + 1] = H(wb, we, pb, pe, h.v, h.loc) end
          end
        end
      end
      return out
    end)
  end)
end

function P:iter(n)
  local p = self
  return per_cycle(function(c, b, e)
    local off = (c % n) / n
    local hs = _late(p, -off).q(b, e)
    return hs
  end)
end

function P:iterBack(n)
  local p = self
  return per_cycle(function(c, b, e) return _late(p, (c % n) / n).q(b, e) end)
end

function P:palindrome()
  local p, r = self, self:rev()
  return per_cycle(function(c, b, e) return (c % 2 == 1 and r or p).q(b, e) end)
end

-- f(p) on the cycles where c % n == 0 (firstOf), or n-1 (lastOf)
local function on_cycles(p, n, f, which)
  local fp = f(p)
  return per_cycle(function(c, b, e) return (c % n == which and fp or p).q(b, e) end)
end
function P:every(n, f) return on_cycles(self, n, f, 0) end
P.firstOf = P.every
function P:lastOf(n, f) return on_cycles(self, n, f, n - 1) end

-- the events kept with the chance 1 - x (degradeBy), or f on a share x of them
local seeds = 0
function P:degradeBy(x, seed)
  local p = self
  seed = seed or 0
  return tparam(x, function(v) return filter(p, function(h) return rand_at(h.wb or h.pb, seed) >= v end) end)
end
function P:undegradeBy(x, seed)
  local p = self
  seed = seed or 0
  return tparam(x, function(v) return filter(p, function(h) return rand_at(h.wb or h.pb, seed) < v end) end)
end
function P:degrade() return self:degradeBy(0.5) end

function P:sometimesBy(x, f)
  seeds = seeds + 1
  local s = 1000 + seeds
  return stack({ self:degradeBy(x, s), f(self:undegradeBy(x, s)) })
end
function P:sometimes(f) return self:sometimesBy(0.5, f) end
function P:often(f) return self:sometimesBy(0.75, f) end
function P:rarely(f) return self:sometimesBy(0.25, f) end
function P:almostNever(f) return self:sometimesBy(0.1, f) end
function P:almostAlways(f) return self:sometimesBy(0.9, f) end
function P:someCyclesBy(x, f)
  local p, fp = self, f(self)
  seeds = seeds + 1
  local s = 2000 + seeds
  return per_cycle(function(c, b, e) return (rand_at(c + 0.5, s) < x and fp or p).q(b, e) end)
end
function P:someCycles(f) return self:someCyclesBy(0.5, f) end

function P:superimpose(f) return stack({ self, f(self) }) end
function P:off(t, f) return stack({ self, f(self:late(t)) }) end
function P:layer(...)
  local r = {}
  for i, f in ipairs({ ... }) do r[i] = f(self) end
  return stack(r)
end

function P:inside(n, f) return f(self:slow(n)):fast(n) end
function P:outside(n, f) return self:inside(1 / n, f) end

-- the part k of n of each cycle (k = cycle % n) changed by f
function P:chunk(n, f)
  local p, fp = self, f(self)
  return per_cycle(function(c, b, e)
    local k = c % n
    local lo, hi = c + k / n, c + (k + 1) / n
    local out = {}
    for _, h in ipairs(p.q(b, e)) do
      local t = h.wb or h.pb
      if t < lo - EPS or t >= hi - EPS then out[#out + 1] = h end
    end
    for _, h in ipairs(fp.q(b, e)) do
      local t = h.wb or h.pb
      if t >= lo - EPS and t < hi - EPS then out[#out + 1] = h end
    end
    return out
  end)
end

-- the first share fr of each cycle, over and over
function P:linger(fr)
  local p = self
  return tparam(fr, function(f)
    if f <= 0 or f >= 1 then return p end
    return per_cycle(function(c, b, e)
      local out, k = {}, 0
      while k * f < 1 - EPS and c + k * f < e do
        local lo, hi = c + k * f, min(c + (k + 1) * f, c + 1)
        local qb, qe = max(b, lo), min(e, hi)
        if qe > qb then
          local sh = k * f
          for _, h in ipairs(p.q(qb - sh, qe - sh)) do
            if (h.wb or h.pb) < c + f - EPS then
              h.pb, h.pe = h.pb + sh, h.pe + sh
              if h.wb then h.wb, h.we = h.wb + sh, h.we + sh end
              out[#out + 1] = h
            end
          end
        end
        k = k + 1
      end
      return out
    end)
  end)
end

-- n events a cycle, each with the value of p over its span (signals)
function P:segment(k)
  local p = self
  return tparam(k, function(n)
    return mk(function(b, e)
      local out = {}
      local i = floor(b * n)
      while i < e * n do
        local wb, we = i / n, (i + 1) / n
        local pb, pe = max(b, wb), min(e, we)
        if pe > pb then
          local hs = p.q(wb, we)
          local h = hs[1]
          if h then out[#out + 1] = H(wb, we, pb, pe, h.v, h.loc) end
        end
        i = i + 1
      end
      return out
    end)
  end)
end

-- the events in the second half of each 1/n of the cycle later by x/n (swing)
function P:swingBy(x, n)
  return self:inside(n, function(q)
    local function late_half(h)
      local t = h.wb or h.pb
      return t - floor(t + EPS) >= 0.5 - EPS
    end
    return stack({ filter(q, function(h) return not late_half(h) end), _late(filter(q, late_half), x) })
  end)
end
function P:swing(n) return self:swingBy(1 / 3, n) end

------------------------------------------------------------------ values

local NOTE_PC = { c = 0, d = 2, e = 4, f = 5, g = 7, a = 9, b = 11 }

-- "c", "eb4", "F#2", "cs3" -> a MIDI number (the octave 3 when missing:
-- c3 = 48, c4 = 60, as in Strudel); nil if it is not a note
local function note_num(s)
  if type(s) == "number" then return s end
  if type(s) ~= "string" then return nil end
  local l, rest = s:match("^([a-gA-G])(.*)$")
  if not l then return nil end
  local n = NOTE_PC[l:lower()]
  local acc, oct = rest:match("^([#sbf]*)(%-?%d*)$")
  if not acc then return nil end
  for ch in acc:gmatch(".") do n = n + ((ch == "#" or ch == "s") and 1 or -1) end
  oct = tonumber(oct) or 3
  return (oct + 1) * 12 + n
end
R.note_num = note_num

local SCALES = {
  major = { 0, 2, 4, 5, 7, 9, 11 }, minor = { 0, 2, 3, 5, 7, 8, 10 },
  dorian = { 0, 2, 3, 5, 7, 9, 10 }, phrygian = { 0, 1, 3, 5, 7, 8, 10 },
  lydian = { 0, 2, 4, 6, 7, 9, 11 }, mixolydian = { 0, 2, 4, 5, 7, 9, 10 },
  locrian = { 0, 1, 3, 5, 6, 8, 10 }, harmonic = { 0, 2, 3, 5, 7, 8, 11 },
  melodic = { 0, 2, 3, 5, 7, 9, 11 }, pentatonic = { 0, 2, 4, 7, 9 },
  minpent = { 0, 3, 5, 7, 10 }, blues = { 0, 3, 5, 6, 7, 10 },
  chromatic = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, wholetone = { 0, 2, 4, 6, 8, 10 },
}
local SCALE_ALIAS = {
  ionian = "major", aeolian = "minor", maj = "major", min = "minor", maggiore = "major",
  minore = "minor", harmonic_minor = "harmonic", harmonicminor = "harmonic",
  melodic_minor = "melodic", melodicminor = "melodic", major_pentatonic = "pentatonic",
  majpent = "pentatonic", minor_pentatonic = "minpent", whole = "wholetone",
}
R.SCALES = SCALES

local scale_cache = {}
-- "C:minor", "A4:minor_pentatonic", "D:dorian" -> root (MIDI), steps
local function scale_of(name)
  local s = scale_cache[name]
  if s then return s[1], s[2] end
  local root, mode = tostring(name):match("^([^:]+):?(.*)$")
  local r = note_num(root or "c")
  mode = (mode == "" and "major" or mode):lower():gsub(":", "_")
  mode = SCALE_ALIAS[mode] or mode
  local steps = SCALES[mode]
  if not r or not steps then error("riff: no scale \"" .. tostring(name) .. "\"", 0) end
  scale_cache[name] = { r, steps }
  return r, steps
end

local function degree(name, d)
  local r, steps = scale_of(name)
  local m = #steps
  local i = floor(d)
  return r + 12 * (i // m) + steps[i % m + 1] + (d - i)
end

-- chords: "C", "Am", "G7", "Fmaj7", "Bb", "F#m7", "Dsus4", "Edim"...
local CHORDS = {
  [""] = { 0, 4, 7 }, M = { 0, 4, 7 }, maj = { 0, 4, 7 }, m = { 0, 3, 7 }, min = { 0, 3, 7 },
  ["7"] = { 0, 4, 7, 10 }, maj7 = { 0, 4, 7, 11 }, M7 = { 0, 4, 7, 11 }, ["^7"] = { 0, 4, 7, 11 },
  m7 = { 0, 3, 7, 10 }, dim = { 0, 3, 6 }, o = { 0, 3, 6 }, dim7 = { 0, 3, 6, 9 }, o7 = { 0, 3, 6, 9 },
  aug = { 0, 4, 8 }, ["+"] = { 0, 4, 8 }, sus2 = { 0, 2, 7 }, sus4 = { 0, 5, 7 }, sus = { 0, 5, 7 },
  ["6"] = { 0, 4, 7, 9 }, m6 = { 0, 3, 7, 9 }, ["9"] = { 0, 4, 7, 10, 14 }, m9 = { 0, 3, 7, 10, 14 },
  maj9 = { 0, 4, 7, 11, 14 }, add9 = { 0, 4, 7, 14 }, m7b5 = { 0, 3, 6, 10 }, ["5"] = { 0, 7 },
}

-- the notes of a chord near the middle C (a close voicing: no note over D5)
local function chord_notes(sym)
  local l, acc, q = tostring(sym):match("^([A-Ga-g])([#b]?)(.*)$")
  if not l then error("riff: no chord \"" .. tostring(sym) .. "\"", 0) end
  local iv = CHORDS[q]
  if not iv then error("riff: no chord \"" .. tostring(sym) .. "\"", 0) end
  local root = 48 + NOTE_PC[l:lower()] + (acc == "#" and 1 or acc == "b" and -1 or 0)
  local r = {}
  for i, x in ipairs(iv) do
    local n = root + x
    while n >= 74 do n = n - 12 end
    r[i] = n
  end
  table.sort(r)
  return r
end
R.chord_notes = chord_notes

local function copy(t)
  local r = {}
  for k, v in pairs(t) do r[k] = v end
  return r
end

-- a value of the mini-notation as a table of controls: notes go to note,
-- words to s (the instrument)
local function as_ctrl(v)
  if type(v) == "table" then return v end
  local n = note_num(v)
  if type(v) == "number" or n then return { note = n or v } end
  return { s = v }
end

local function set_key(v, k, x)
  local r = copy(as_ctrl(v))
  r[k] = x
  return r
end

-- the MIDI note(s) of a value: note, n on its scale, or a chord
local function note_of(v)
  if type(v) ~= "table" then return note_num(v) end
  if v.note then return note_num(v.note) end
  if v.n then return degree(v.scale or "C:major", v.n) end
end

-- the controls: constructors (R.s "kick*4") and methods (p:lpf(800))
local CONTROLS = {
  "s", "sound", "note", "n", "gain", "velocity", "pan", "legato", "clip",
  "lpf", "cutoff", "ctf", "hpf", "bpf", "res", "resonance", "lpq", "room", "delay",
  "attack", "decay", "sustain", "release", "shape", "drive", "noise", "fenv", "fdecay",
  "lfo", "wah", "pwm", "duty", "wave", "vib", "vibhz", "detune", "pitch", "ptime",
  "fm", "fmh", "raw", "scale",
  "crush", "coarse", "vowel", "chorus", "trem", "duck", "begin",
}
local ALIAS = { sound = "s", clip = "legato", resonance = "res", lpq = "res", ctf = "cutoff" }

local function ctrl_value(k, v)
  if k == "s" and type(v) == "string" then
    local name, var = v:match("^(.-):(%-?%d+)$")
    if name then return { s = name, n = tonumber(var) } end
  end
  if k == "note" then
    local n = note_num(v)
    if not n then error("riff: \"" .. tostring(v) .. "\" is not a note", 0) end
    return { note = n }
  end
  return { [k] = v }
end

for _, name in ipairs(CONTROLS) do
  local k = ALIAS[name] or name
  R[name] = function(x)
    return reify(x):fmap(function(v)
      if type(v) == "table" then return set_key(v, k, v[k]) end
      return ctrl_value(k, v)
    end)
  end
  P[name] = function(self, x)
    return with_values(self, x, function(v, xv)
      local r = copy(as_ctrl(v))
      for kk, vv in pairs(ctrl_value(k, xv)) do r[kk] = vv end
      return r
    end)
  end
end

-- tone{...}: any key of tone() for every event
function P:tone(t)
  return self:fmap(function(v)
    local r = copy(as_ctrl(v))
    local tt = r.tone and copy(r.tone) or {}
    for k, x in pairs(t) do tt[k] = x end
    r.tone = tt
    return r
  end)
end

-- the numbers of the events: notes (or n) moved, scaled...
local function arith(op)
  return function(self, x)
    return with_values(self, x, function(v, xv)
      if type(xv) == "table" then xv = note_of(xv) or xv.n or 0 end
      if type(v) == "number" then return op(v, xv) end
      local r = copy(as_ctrl(v))
      if r.n and not r.note then r.n = op(r.n, xv)
      elseif r.notes then
        local ns = {}
        for i, nn in ipairs(r.notes) do ns[i] = op(nn, xv) end
        r.notes = ns
      else r.note = op(note_of(r) or 0, xv) end
      return r
    end)
  end
end
P.add = arith(function(a, b) return a + b end)
P.sub = arith(function(a, b) return a - b end)
P.mul = arith(function(a, b) return a * b end)
P.div = arith(function(a, b) return b ~= 0 and a / b or 0 end)
P.__add = function(a, b) if is_pat(a) then return a:add(b) end return b:add(a) end
P.__sub = function(a, b) return a:sub(b) end
P.__mul = function(a, b) if is_pat(a) then return a:mul(b) end return b:mul(a) end

-- semitones up (n on a scale becomes a note first)
function P:transpose(x)
  return with_values(self, x, function(v, xv)
    local r = copy(as_ctrl(v))
    if r.notes then
      local ns = {}
      for i, nn in ipairs(r.notes) do ns[i] = nn + xv end
      r.notes = ns
    else
      r.note = (note_of(r) or 48) + xv
    end
    return r
  end)
end
P.trans = P.transpose

function P:range(lo, hi)
  return self:fmap(function(v) return type(v) == "number" and lo + v * (hi - lo) or v end)
end
function P:rangex(lo, hi)
  local a, b = math.log(lo), math.log(hi)
  return self:fmap(function(v) return type(v) == "number" and math.exp(a + v * (b - a)) or v end)
end

-- chord symbols -> the notes together ("<C Am F G>")
function R.chord(x)
  return reify(x):fmap(function(v)
    if type(v) == "table" then return v end
    return { notes = chord_notes(v), chord = v }
  end)
end

local ARP = {
  up = function(ns) return ns end,
  down = function(ns) local r = {} for i = #ns, 1, -1 do r[#r + 1] = ns[i] end return r end,
  updown = function(ns)
    local r = { unpack(ns) }
    for i = #ns - 1, 2, -1 do r[#r + 1] = ns[i] end
    return r
  end,
  downup = function(ns)
    local r = {}
    for i = #ns, 1, -1 do r[#r + 1] = ns[i] end
    for i = 2, #ns - 1 do r[#r + 1] = ns[i] end
    return r
  end,
  converge = function(ns)
    local r, i, j = {}, 1, #ns
    while i <= j do r[#r + 1] = ns[i]; if j ~= i then r[#r + 1] = ns[j] end; i, j = i + 1, j - 1 end
    return r
  end,
  thumbup = function(ns)
    local r = {}
    for i = 2, #ns do r[#r + 1] = ns[1]; r[#r + 1] = ns[i] end
    return r
  end,
}
ARP.diverge = function(ns) local c = ARP.converge(ns); return ARP.down(c) end

-- the notes of each chord one after the other inside its span
function P:arp(mode)
  local p = self
  return tparam(mode or "up", function(m)
    local f = ARP[m]
    if not f then error("riff: no arpeggio \"" .. tostring(m) .. "\" (up down updown downup converge diverge thumbup)", 0) end
    return mk(function(b, e)
      local out = {}
      for _, h in ipairs(p.q(b, e)) do
        local v = h.v
        if h.wb and type(v) == "table" and v.notes then
          local ns = f(v.notes)
          local k = #ns
          local d = (h.we - h.wb) / k
          for j = 0, k - 1 do
            local wb, we = h.wb + j * d, h.wb + (j + 1) * d
            local pb, pe = max(wb, h.pb), min(we, h.pe)
            if pe > pb then
              local r = copy(v)
              r.notes, r.note = nil, ns[j + 1]
              out[#out + 1] = H(wb, we, pb, pe, r, h.loc)
            end
          end
        else
          out[#out + 1] = h
        end
      end
      return out
    end)
  end)
end

-- left and right: p on the left, f(p) on the right (w: how far, 0..1)
function P:juxBy(w, f) return stack({ self:pan(-w), f(self):pan(w) }) end
function P:jux(f) return self:juxBy(1, f) end

------------------------------------------------------------------ the mini-notation

-- "kick [sd sd] ~ hat", "<c e g>", "a*2 b/2", "x(3,8)", "a@3 b", "a!3",
-- "a? b", "[a, b]", "{a b c}%4", "a | b", "a . b c"
local ast_cache, ast_count = {}, 0

local function parse_mini(src)
  local i, n = 1, #src
  local seed = 0
  local function fail(msg) error("riff: " .. msg .. " at " .. i .. " in \"" .. src .. "\"", 0) end
  local function skip()
    while i <= n do
      local c = src:byte(i)
      if c == 32 or c == 9 or c == 10 or c == 13 then i = i + 1 else break end
    end
  end
  local function ch() skip(); return src:sub(i, i) end
  local WORD = "[%w#%.:%-'_^~]"
  local parse_stack, term

  local function number_after()
    local s = i
    while i <= n and src:sub(i, i):match("[%d%.%-]") do i = i + 1 end
    return tonumber(src:sub(s, i - 1))
  end

  -- an argument of * / ( ): a number, or a term in brackets
  local function arg()
    local c = src:sub(i, i)
    if c == "[" or c == "<" or c == "{" then return term() end
    local s = i
    while i <= n and src:sub(i, i):match("[%w%.%-#]") do i = i + 1 end
    local w = src:sub(s, i - 1)
    if w == "" then fail("a number expected") end
    return { k = "atom", v = tonumber(w) or w, s = s, e = i }
  end

  function term()
    local c = ch()
    local node
    if c == "[" then
      i = i + 1
      node = parse_stack("]")
      if ch() ~= "]" then fail("missing ]") end
      i = i + 1
    elseif c == "<" then
      i = i + 1
      node = { k = "alt", parse_stack(">") }
      if ch() ~= ">" then fail("missing >") end
      i = i + 1
    elseif c == "{" then
      i = i + 1
      node = { k = "poly", parse_stack("}") }
      if ch() ~= "}" then fail("missing }") end
      i = i + 1
      if src:sub(i, i) == "%" then i = i + 1; node.steps = arg() end
    else
      local s = i
      while i <= n and src:sub(i, i):match(WORD) do i = i + 1 end
      local w = src:sub(s, i - 1)
      if w == "" then fail("unexpected \"" .. c .. "\"") end
      if w == "~" or w == "-" then node = { k = "rest" }
      else node = { k = "atom", v = tonumber(w) or w, s = s, e = i } end
    end
    while true do
      local m = src:sub(i, i)
      if m == "*" then i = i + 1; node = { k = "fast", node, arg = arg() }
      elseif m == "/" then i = i + 1; node = { k = "slow", node, arg = arg() }
      elseif m == "?" then
        i = i + 1
        seed = seed + 1
        node = { k = "degrade", node, p = number_after() or 0.5, seed = seed }
      elseif m == "@" then i = i + 1; node.w = (number_after() or 1)
      elseif m == "!" then
        i = i + 1
        local r = number_after()
        node.rep = r or 2
      elseif m == "(" then
        i = i + 1
        local a = { k = "euclid", node }
        skip(); a.a = arg(); skip()
        if src:sub(i, i) ~= "," then fail("( needs two numbers: x(3,8)") end
        i = i + 1; skip(); a.b = arg(); skip()
        if src:sub(i, i) == "," then i = i + 1; skip(); a.r = arg(); skip() end
        if src:sub(i, i) ~= ")" then fail("missing )") end
        i = i + 1
        node = a
      else
        break
      end
    end
    return node
  end

  local function sequence(close)
    local groups, steps = {}, {}
    while true do
      local c = ch()
      if c == "" or c == close or c == "," or c == "|" or c == "]" or c == ">" or c == "}" then break end
      local nx = src:sub(i + 1, i + 1)
      local lone = nx == "" or nx:match("[%s%]>},|]")
      if c == "_" and lone then
        if #steps > 0 then steps[#steps].w = (steps[#steps].w or 1) + 1 end
        i = i + 1
      elseif c == "!" and lone then
        if #steps > 0 then steps[#steps + 1] = steps[#steps] end
        i = i + 1
      elseif c == "." and lone then
        groups[#groups + 1] = { k = "seq", steps }
        steps = {}
        i = i + 1
      else
        local t = term()
        steps[#steps + 1] = t
        for _ = 2, t.rep or 1 do steps[#steps + 1] = t end
      end
    end
    if #groups > 0 then
      groups[#groups + 1] = { k = "seq", steps }
      return { k = "seq", groups }
    end
    return { k = "seq", steps }
  end

  function parse_stack(close)
    local seqs, mode = { sequence(close) }, nil
    while true do
      local c = ch()
      if c == "," or c == "|" then
        if mode and mode ~= c then fail("\",\" and \"|\" together") end
        mode = c
        i = i + 1
        seqs[#seqs + 1] = sequence(close)
      else
        break
      end
    end
    if #seqs == 1 then return seqs[1] end
    return { k = mode == "," and "stack" or "choose", seqs }
  end

  local node = parse_stack(nil)
  if i <= n then fail("unexpected \"" .. src:sub(i, i) .. "\"") end
  return node
end

local function weight_of(node) return node.w or 1 end

local compile
local function compile_seq(node, base)
  local items = {}
  for _, st in ipairs(node[1]) do items[#items + 1] = { weight_of(st), compile(st, base) } end
  return timecat(items)
end

local function seq_weight(node)
  if node.k ~= "seq" then return 1 end
  local w = 0
  for _, st in ipairs(node[1]) do w = w + weight_of(st) end
  return w
end

local function compile_arg(a, base)
  if a.k == "atom" and type(a.v) == "number" then return a.v end
  return compile(a, base)
end

function compile(node, base)
  local k = node.k
  if k == "atom" then
    return pure(node.v, base and { { base + node.s - 1, base + node.e - 1 } } or nil)
  elseif k == "rest" then
    return silence
  elseif k == "seq" then
    return compile_seq(node, base)
  elseif k == "stack" then
    local r = {}
    for i, s in ipairs(node[1]) do r[i] = compile(s, base) end
    return stack(r)
  elseif k == "choose" then
    local r = {}
    for i, s in ipairs(node[1]) do r[i] = compile(s, base) end
    return randcat(r, #node[1])
  elseif k == "alt" then
    local inner = node[1]
    if inner.k == "stack" or inner.k == "choose" then
      local r = {}
      for i, s in ipairs(inner[1]) do r[i] = compile({ k = "alt", s }, base) end
      return inner.k == "stack" and stack(r) or randcat(r, #r)
    end
    return _fast(compile(inner, base), 1 / seq_weight(inner))
  elseif k == "poly" then
    local inner = node[1]
    local seqs = (inner.k == "stack" or inner.k == "choose") and inner[1] or { inner }
    local steps = node.steps and compile_arg(node.steps, base) or seq_weight(seqs[1])
    local r = {}
    for i, s in ipairs(seqs) do
      local w = seq_weight(s)
      local p = compile(s, base)
      r[i] = type(steps) == "number" and _fast(p, steps / w) or p:fast(steps):slow(w)
    end
    return stack(r)
  elseif k == "fast" then
    return compile(node[1], base):fast(compile_arg(node.arg, base))
  elseif k == "slow" then
    return compile(node[1], base):slow(compile_arg(node.arg, base))
  elseif k == "degrade" then
    return compile(node[1], base):degradeBy(node.p, node.seed)
  elseif k == "euclid" then
    return compile(node[1], base):euclid(compile_arg(node.a, base), compile_arg(node.b, base),
                                         node.r and compile_arg(node.r, base) or 0)
  end
  error("riff: ? " .. tostring(k))
end

local ctx                       -- R.code at work: where the strings are in the text

function R.mini(src)
  local ast = ast_cache[src]
  if not ast then
    ast = parse_mini(src)
    if ast_count > 400 then ast_cache, ast_count = {}, 0 end
    ast_cache[src] = ast
    ast_count = ast_count + 1
  end
  local base
  if ctx then
    local list = ctx.lits[src]
    local k = (ctx.used[src] or 0) + 1
    ctx.used[src] = k
    base = list and list[k]
  end
  return compile(ast, base)
end

function reify(x)
  if is_pat(x) then return x end
  local t = type(x)
  if t == "string" then return R.mini(x) end
  if t == "table" then
    local items = {}
    for i, v in ipairs(x) do items[i] = { 1, reify(v) } end
    return timecat(items)
  end
  return pure(x)
end
R.reify = reify

------------------------------------------------------------------ building

local function reify_all(...)
  local r = {}
  for i, x in ipairs({ ... }) do r[i] = reify(x) end
  return r
end

function R.seq(...)
  local items = {}
  for i, p in ipairs(reify_all(...)) do items[i] = { 1, p } end
  return timecat(items)
end
R.fastcat = R.seq
function R.cat(...) return slowcat(reify_all(...)) end
R.slowcat = R.cat
function R.stack(...) return stack(reify_all(...)) end
function R.randcat(...) return randcat(reify_all(...)) end
-- {weight, pattern}, ...
function R.timecat(...)
  local items = {}
  for i, it in ipairs({ ... }) do items[i] = { it[1], reify(it[2]) } end
  return timecat(items)
end
-- {cycles, pattern}, ...: one after the other, each for its cycles
function R.arrange(...)
  local items, total = {}, 0
  for i, it in ipairs({ ... }) do
    items[i] = { it[1], _fast(reify(it[2]), it[1]) }
    total = total + it[1]
  end
  return _fast(timecat(items), 1 / total)
end
function R.run(n)
  local items = {}
  for i = 1, n do items[i] = { 1, pure(i - 1) } end
  return timecat(items)
end
function R.euclid(k, n, rot) return euclid_pat(k, n, rot) end

-- the functions for every(), sometimes(), off()...: rev, or fast(2)
-- without a pattern gives a function (as in Strudel: every(4, fast(2)))
function R.rev(p) return reify(p):rev() end
for _, name in ipairs({ "fast", "slow", "ply", "late", "early", "add", "sub", "transpose", "degradeBy" }) do
  R[name] = function(x, p)
    if p == nil then return function(q) return q[name](q, x) end end
    local rp = reify(p)
    return rp[name](rp, x)
  end
end
R.degrade = function(p) return reify(p):degrade() end

-- signals 0..1 over each cycle
local TAU = 2 * math.pi
R.sine = signal(function(t) return 0.5 + 0.5 * math.sin(TAU * t) end)
R.cosine = signal(function(t) return 0.5 + 0.5 * math.cos(TAU * t) end)
R.saw = signal(function(t) return t % 1 end)
R.isaw = signal(function(t) return 1 - t % 1 end)
R.tri = signal(function(t) local x = t % 1; return x < 0.5 and 2 * x or 2 - 2 * x end)
R.square = signal(function(t) return t % 1 < 0.5 and 0 or 1 end)
R.rand = signal(function(t) return rand_at(t, 0) end)
R.perlin = signal(function(t)
  local i = floor(t)
  local a, b = rand_at(i, 9), rand_at(i + 1, 9)
  local x = t - i
  x = x * x * (3 - 2 * x)
  return a + (b - a) * x
end)
function R.irand(n) return R.rand:fmap(function(v) return floor(v * n) end) end
-- one of the values, at random where it is asked (with segment, struct...)
function R.choose(...)
  local xs = { ... }
  return R.rand:fmap(function(v) return xs[floor(v * #xs) + 1] end)
end

------------------------------------------------------------------ playing

local S = {
  cps = 0.5,            -- cycles per second (a cycle of 4 beats at 120 BPM)
  t0 = nil, c0 = 0,     -- the clock: cycle c0 at time t0 (audio_time())
  done = 0,             -- the cycle up to which the notes are queued
  look = 0.2,           -- seconds ahead
}
R._state = S
local slots, order = {}, {}
local tags = {}
local hist = {}                 -- the notes queued, for bm Code's highlights
local errors = {}

local function cyc(t) return S.c0 + (t - S.t0) * S.cps end
local function tim(c) return S.t0 + (c - S.c0) / S.cps end

-- the speed: cycles per second, per minute, or beats per minute (4 to a cycle)
function R.setcps(x)
  if x <= 0 then return end
  if S.t0 then
    S.t0, S.c0 = tim(S.done), S.done
  end
  S.cps = x
end
function R.setcpm(x) R.setcps(x / 60) end
function R.bpm(x, beats) R.setcps(x / 60 / (beats or 4)) end
function R.cps(x) if x then R.setcps(x) end return S.cps end
R.cpm = function(x) if x then R.setcpm(x) end return S.cps * 60 end

local function free_tag()
  for t = 1, 255 do if not tags[t] then return t end end
end

-- a pattern on its way: name -> it, in place of the one with that name
function R.play(name, pat)
  if pat == nil then pat, name = name, "main" end
  pat = reify(pat)
  local sl = slots[name]
  if ctx then ctx.named[name] = true end
  if sl then
    sl.pat, sl.bad = pat, nil
    sl.code = sl.code or ctx ~= nil
    return sl
  end
  local tag = free_tag()
  if not tag then error("riff: too many patterns playing", 2) end
  tags[tag] = name
  sl = { name = name, pat = pat, tag = tag, code = ctx ~= nil }
  slots[name] = sl
  order[#order + 1] = name
  return sl
end
function P:play(name) R.play(name or "main", self); return self end

function R.stop(name)
  local sl = slots[name]
  if not sl then return end
  slots[name] = nil
  tags[sl.tag] = nil
  for i, n in ipairs(order) do if n == name then table.remove(order, i) break end end
  if play_cancel then play_cancel(sl.tag) end
end

function R.hush()
  for i = #order, 1, -1 do R.stop(order[i]) end
  S.t0 = nil
  hist = {}
end

-- the pattern playing under a name; with no name all of them together
function R.get(name)
  if name then
    local sl = slots[name]
    return sl and sl.pat
  end
  local all = {}
  for i, n in ipairs(order) do all[i] = slots[n].pat end
  return stack(all)
end

function R.playing()
  local r = {}
  for i, n in ipairs(order) do r[i] = n end
  return r
end

-- the cycle now (nil when nothing plays)
function R.now()
  if not S.t0 then return nil end
  return cyc(audio_time())
end

local DRUM_NOTE = {
  kick = 36, punch = 36, chipkick = 36, snare = 50, clap = 50, rim = 50, chipsnr = 50,
  hat = 72, openhat = 72, chiphat = 72, shaker = 72, tom = 45, crash = 60, cowbell = 60,
  -- the console's kit by its drums' names: samples, at their own speed on C4
  bd = 60, sd = 60, hh = 60, oh = 60, cp = 60, cb = 60,
}
-- the kits: n ("kit:2") picks the drum, played at its own speed (play()
-- and instrument() know "kit:2" too)
local KIT = { kit = true, pump = true }
local DEFAULT = "triangle"         -- the instrument of a pattern that names none

-- riff's names -> the keys of tone() / play()
local TONE = {
  lpf = "cutoff", cutoff = "cutoff", hpf = "cutoff", bpf = "cutoff", res = "res",
  room = "reverb", delay = "echo", pan = "pan", shape = "drive", drive = "drive",
  attack = "attack", decay = "decay", sustain = "sustain", release = "release",
  noise = "noise", fenv = "fenv", fdecay = "fdecay", lfo = "lfo", wah = "wah", pwm = "pwm",
  duty = "duty", wave = "wave", vib = "vib", vibhz = "vibhz", detune = "detune",
  pitch = "pitch", ptime = "ptime", fm = "depth", fmh = "ratio", raw = "raw",
  crush = "crush", coarse = "coarse", vowel = "vowel", chorus = "chorus", trem = "trem", duck = "duck",
  begin = "begin",
}
local FILTER = { lpf = "lp", hpf = "hp", bpf = "bp" }

-- the sound of a value, as play_at takes it
local function sound_of(v)
  local st = {}
  local has = false
  for k, x in pairs(v) do
    local tk = TONE[k]
    if tk then
      st[tk] = x
      has = true
      if FILTER[k] then st.filter = FILTER[k] end
    end
  end
  if v.tone then for k, x in pairs(v.tone) do st[k] = x; has = true end end
  local s = v.s
  if s == nil then s = DEFAULT end
  if KIT[s] and v.n then s = s .. ":" .. floor(v.n + 0.5) end       -- "kit:2": the kit's drum 2
  if not has then return s end
  st.s = s
  return st
end
R.sound_of = sound_of

-- the notes of a value (a list): note, n on its scale (a drum's n moves
-- it in semitones: "kick:2"; a kit's picks the drum: "kit:2"), or the
-- instrument's own note
local function notes_of(v)
  if v.notes then return v.notes end
  if v.note then return { note_num(v.note) } end
  if KIT[v.s] then return { 60 } end
  local d = DRUM_NOTE[v.s]
  if v.n and (v.scale or not d) then return { degree(v.scale or "C:major", v.n) } end
  return { (d or 48) + (v.n or 0) }
end
R.notes_of = notes_of

local function emit(sl, h)
  local v = as_ctrl(h.v)
  local t = tim(h.wb)
  local dur = (h.we - h.wb) / S.cps * (v.legato or 1)
  local vol = (v.gain or 1) * (v.velocity or 1)
  if vol <= 0 then return end
  local snd = sound_of(v)
  for _, n in ipairs(notes_of(v)) do
    n = floor(n + 0.5)
    if n >= 1 and n <= 127 then play_at(t, snd, n, max(10, dur * 1000), min(vol, 1), sl.tag) end
  end
  if h.loc then
    local t1 = t + max(dur, 0.1)
    hist[#hist + 1] = { t, t1, h.loc }
  end
end

local function chunk()
  local ch = 0.125
  while ch / S.cps > 0.25 and ch > 1 / 64 do ch = ch / 2 end
  return ch
end

-- the notes of the next moments into the queue: call it every frame
function R.update()
  if #order == 0 then S.t0 = nil return end
  local now = audio_time()
  if not S.t0 then S.t0, S.c0, S.done = now + 0.05, 0, 0 end
  local cnow = cyc(now)
  if S.done < cnow then S.done = cnow end     -- after a pause: nothing late
  local target, ch = cyc(now + S.look), chunk()
  local guard, bad = 0, nil
  while S.done < target and guard < 16 do
    local b = S.done
    local e = (floor(b / ch + EPS) + 1) * ch
    for i = 1, #order do
      local sl = slots[order[i]]
      if not sl.bad then
        local ok, err = pcall(function()
          for _, h in ipairs(sl.pat.q(b, e)) do
            if h.wb and h.wb >= b - EPS and h.wb < e - EPS then emit(sl, h) end
          end
        end)
        if not ok then
          sl.bad = true
          err = tostring(err):gsub("^[%w_%.%-]+:%d+: ", ""):gsub("^riff: ", "")
          errors[#errors + 1] = sl.name .. ": " .. err
          if log then log("riff: " .. sl.name .. ": " .. tostring(err)) end
          bad = bad or {}
          bad[#bad + 1] = sl.name
        end
      end
    end
    S.done = e
    guard = guard + 1
  end
  if bad then for _, name in ipairs(bad) do R.stop(name) end end
  -- the highlights over
  local k = 1
  for i = 1, #hist do
    if hist[i][2] > now then hist[k] = hist[i]; k = k + 1 end
  end
  for i = #hist, k, -1 do hist[i] = nil end
end

-- the last errors of the patterns playing (and forgets them)
function R.errors()
  local r = errors
  errors = {}
  return r
end

-- where the notes sounding now were written: a list of {from, to}
-- offsets in the text of R.code (bm Code lights them up)
function R.active()
  local now, r = audio_time(), {}
  for _, x in ipairs(hist) do
    if x[1] <= now and now < x[2] then
      for _, l in ipairs(x[3]) do r[#r + 1] = l end
    end
  end
  return r
end

------------------------------------------------------------------ live code

-- where each string is in the text: content -> {offset of its first
-- character (0-based), ...}; strings with escapes are left out
local function literals(src)
  local r = {}
  local i, n = 1, #src
  local function add(s, at)
    if s:find("\\", 1, true) then return end
    local l = r[s]
    if not l then l = {}; r[s] = l end
    l[#l + 1] = at
  end
  while i <= n do
    local c = src:sub(i, i)
    if c == "-" and src:sub(i, i + 1) == "--" then
      local lb = src:match("^%[(=*)%[", i + 2)
      if lb then
        local close = "]" .. lb .. "]"
        local e = src:find(close, i + 4 + #lb, true)
        i = (e or n) + #close
      else
        local e = src:find("\n", i, true)
        i = (e or n) + 1
      end
    elseif c == "\"" or c == "'" then
      local j = i + 1
      while j <= n do
        local d = src:sub(j, j)
        if d == "\\" then j = j + 2
        elseif d == c or d == "\n" then break
        else j = j + 1 end
      end
      add(src:sub(i + 1, j - 1), i)
      i = j + 1
    elseif c == "[" and src:match("^%[=*%[", i) then
      local lb = src:match("^%[(=*)%[", i)
      local s = i + 2 + #lb
      local close = "]" .. lb .. "]"
      local e = src:find(close, s, true) or n + 1
      local body = src:sub(s, e - 1)
      if body:sub(1, 1) == "\n" then body = body:sub(2); s = s + 1 end
      add(body, s - 1)
      i = e + #close
    else
      i = i + 1
    end
  end
  return r
end
R._literals = literals

-- runs a piece of live code: every global that gets a pattern plays it
-- under its name (d1 = s "kick*4"; names that start with _ do not), the
-- patterns of the code run before and not named now stop. In it the
-- functions of riff are globals (note, s, stack, sine...), then the
-- game's. true, or nil and the error.
function R.code(src, chunkname)
  if not loadtext then return nil, "riff.code: not on this console" end
  local named = {}
  local env = setmetatable({}, {
    __index = function(_, k)
      local v = R[k]
      if v ~= nil then return v end
      return _G[k]
    end,
    __newindex = function(t, k, v)
      rawset(t, k, v)
      if type(k) == "string" and k:sub(1, 1) ~= "_" and is_pat(v) then
        named[k] = v
        named[#named + 1] = k
      end
    end,
  })
  local f, err = loadtext(src, "=" .. (chunkname or "riff"), env)
  if not f then return nil, err end
  seeds = 0
  ctx = { lits = literals(src), used = {}, named = {} }
  local ok, e = pcall(f)
  if ok then
    -- try each pattern on its first cycle: errors now, not while it plays
    for _, k in ipairs(named) do
      local ok2, e2 = pcall(function()
        for _, h in ipairs(named[k]:events(0, 1)) do sound_of(as_ctrl(h.v)); notes_of(as_ctrl(h.v)) end
      end)
      if not ok2 then ok, e = false, k .. ": " .. tostring(e2):gsub("^riff: ", "") break end
    end
  end
  if not ok then
    ctx = nil
    return nil, tostring(e)
  end
  for _, k in ipairs(named) do R.play(k, named[k]) end
  local now = ctx.named
  ctx = nil
  for i = #order, 1, -1 do
    local sl = slots[order[i]]
    if sl.code and not now[sl.name] then R.stop(sl.name) end
  end
  return true
end

-- the voices the patterns may take (none: all 8)
function R.voices(...) return play_voices(...) end

------------------------------------------------------------------ from the assistant

-- A piece of ai.music (or of R.bake) as a pattern, a cycle every `steps`
-- steps (16): beat = piece(ai.music("ritmo rock")), then bpm(beat_bpm)...
function R.piece(piece, steps)
  steps = steps or 16
  local evs, off = {}, 0
  for _, pp in ipairs(piece.patterns or {}) do
    for _, col in pairs(pp.tracks) do
      local open
      for i = 1, pp.len do
        local w = col[i] or 0
        local n = w & 255
        if n > 0 then
          if open then open.len = off + i - 1 - open.at end
          open = nil
          if n < 128 then
            local inst = piece.instruments[(w >> 8 & 255) + 1]
            open = { at = off + i - 1, len = 1, v = { note = n, s = inst, gain = (w >> 16 & 255) / 255 } }
            evs[#evs + 1] = open
          end
        end
      end
      if open then open.len = off + pp.len - open.at end
    end
    off = off + pp.len
  end
  if off == 0 then return silence end
  local cycles = off / steps
  return mk(function(b, e)
    local out = {}
    local r = floor(b / cycles)
    while r * cycles < e do
      for _, ev in ipairs(evs) do
        local wb = r * cycles + ev.at / steps
        local we = wb + ev.len / steps
        local pb, pe = max(b, wb), min(e, we)
        if pe > pb then out[#out + 1] = H(wb, we, pb, pe, ev.v) end
      end
      r = r + 1
    end
    return out
  end)
end

------------------------------------------------------------------ into the bank

-- The events of some cycles of a pattern as a piece of music for bm
-- Sound (the shape of ai.music: instruments by name, patterns of steps,
-- tracks 0..7; a step is note | instrument << 8 | volume << 16).
-- opt: cycles (1), steps a cycle (16), name. What a step cannot say
-- (filter, place, room of each note) stays out: the instrument is the
-- sound as it is.
function R.bake(pat, opt)
  opt = opt or {}
  pat = reify(pat)
  local cycles, steps = opt.cycles or 1, opt.steps or 16
  local total = floor(cycles * steps)
  local inst, inst_of = {}, {}
  local function inst_index(v)
    local s = v.s
    if s == nil then s = DEFAULT end
    if KIT[s] and v.n then s = s .. ":" .. floor(v.n + 0.5) end
    s = tostring(s)
    if not inst_of[s] then
      inst[#inst + 1] = s
      inst_of[s] = #inst - 1
    end
    return inst_of[s]
  end
  -- the notes on a grid of steps, each on a track: one note at a time,
  -- the instrument's track first
  local tracks, busy, home = {}, {}, {}
  for t = 0, 7 do tracks[t] = {}; busy[t] = -1 end
  for _, h in ipairs(pat:events(0, cycles)) do
    local v = as_ctrl(h.v)
    local at = floor(h.wb * steps + 0.5)
    local stop = max(at + 1, floor(h.we * steps * (v.legato or 1) + 0.5))
    local vol = floor(min(1, (v.gain or 1) * (v.velocity or 1)) * 255 + 0.5)
    local ii = inst_index(v)
    for _, n in ipairs(notes_of(v)) do
      n = floor(n + 0.5)
      if at < total and n >= 1 and n <= 127 then
        local pick
        local want = home[ii]
        if want and busy[want] <= at then pick = want end
        if not pick then for t = 0, 7 do if busy[t] <= at then pick = t break end end end
        if pick then
          home[ii] = home[ii] or pick
          tracks[pick][at + 1] = n | ii << 8 | vol << 16
          if stop < total and not tracks[pick][stop + 1] then tracks[pick][stop + 1] = 128 end
          busy[pick] = stop
        end
      end
    end
  end
  -- 64 steps a pattern at most
  local pats = {}
  for off = 0, total - 1, 64 do
    local len = min(64, total - off)
    local tr = {}
    for t = 0, 7 do
      local col, any = {}, false
      for i = 1, len do
        local w = tracks[t][off + i]
        if w then any = true end
        col[i] = w or 0
      end
      if any then tr[t] = col end
    end
    pats[#pats + 1] = { len = len, tracks = tr }
  end
  local bpm = floor(steps * 15 * S.cps + 0.5)
  return {
    gen = "riff", name = opt.name or "riff", kind = "base", seed = 0,
    bpm = max(1, min(255, bpm)), swing = 0, key = 0, minor = false, meter = steps, bars = cycles,
    echo = 0, room = 0, chords = {}, instruments = inst, patterns = pats,
  }
end

return R
