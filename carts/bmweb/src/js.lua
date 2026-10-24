-- js.lua: a JavaScript (ES2015 subset) interpreter in pure Lua 5.4.
--
-- The page scripts are hostile. So this engine never calls `load`, never
-- touches Lua globals and counts every loop turn and call (step budget).
-- The source is parsed to an AST. The AST is compiled once to Lua closures.
--
-- Values (the contract with the host):
--   undefined = nil          null = js.null          booleans = Lua booleans
--   numbers   = Lua floats   strings = Lua strings (UTF-8 bytes)
--   objects   = Lua tables   (fields: p = properties, proto, cls, host ...)
-- Strings are byte strings. `.length`, indexes, slice and so on count BYTES.
-- Iteration (for-of, spread, split("")) and regex `.` use whole UTF-8 chars.
-- Timezone is always UTC. Holes in arrays read as undefined.
--
-- Sections: 1 core, 2 regex engine, 3 tokenizer + parser, 4 compiler,
-- 5 built-ins, 6 public API.

local js = {}

local type, tostring, tonumber, pairs, ipairs, select = type, tostring, tonumber, pairs, ipairs, select
local setmetatable, getmetatable, pcall, error, next, rawget = setmetatable, getmetatable, pcall, error, next, rawget
local floor, huge, fmod, mabs, mtoint = math.floor, math.huge, math.fmod, math.abs, math.tointeger
local sfmt, ssub, sbyte, schar, sfind, srep = string.format, string.sub, string.byte, string.char, string.find, string.rep
local sgsub, smatch, supper, slower, sgmatch = string.gsub, string.match, string.upper, string.lower, string.gmatch
local tconcat, tunpack, tmove, tsort = table.concat, table.unpack, table.move, table.sort
local os_time = os and os.time
local os_clock = os and os.clock

local MAX_STR, MAX_ARR, MAX_DEPTH, MAX_SRC = 1048576, 1000000, 200, 524288
local NaN = 0 / 0

---------------------------------------------------------------------------
-- 1. Core: values, objects, conversions
---------------------------------------------------------------------------
local null = setmetatable({}, { __tostring = function() return "null" end })
js.null = null
local UNDEF = {}              -- an own property whose value is undefined
local NANKEY, UNDEFKEY = {}, {}
local Throw = {}; Throw.__index = Throw
local Fatal = {}; Fatal.__index = Fatal
js.Throw, js.Fatal = Throw, Fatal
local SHORT = {}              -- optional chain short-circuit marker
local symIterator = { sym = true, desc = "Symbol.iterator" }
js.symIterator = symIterator

-- The runtime that is active now, and its main prototypes.
local R, G, ObjectProto, FunctionProto, ArrayProto, StringProto, NumberProto, BooleanProto, SymbolProto
local curline = 0

local function activate(rt)
  if R ~= rt then
    R = rt
    G, ObjectProto, FunctionProto, ArrayProto = rt.global, rt.ObjectProto, rt.FunctionProto, rt.ArrayProto
    StringProto, NumberProto, BooleanProto, SymbolProto = rt.StringProto, rt.NumberProto, rt.BooleanProto, rt.SymbolProto
  end
end

local function is_obj(v) return type(v) == "table" and v.p ~= nil end

-- Number to string, the JS way.
local function numstr(n)
  if n ~= n then return "NaN" end
  if n == huge then return "Infinity" elseif n == -huge then return "-Infinity" end
  if n == 0 then return "0" end
  if n == floor(n) and n > -9007199254740992 and n < 9007199254740992 then return sfmt("%d", n) end
  local s = sfmt("%.15g", n)
  if tonumber(s) == n and not sfind(s, "e", 1, true) then return s end
  local es
  for prec = 0, 16 do
    es = sfmt("%." .. prec .. "e", n)
    if tonumber(es) == n then break end
  end
  local sign, d1, drest, ex = smatch(es, "^(-?)(%d)%.?(%d*)e([+-]%d+)$")
  local digits = sgsub(d1 .. drest, "0+$", "")
  if digits == "" then digits = "0" end
  local k, nn = #digits, tonumber(ex) + 1
  local r
  if k <= nn and nn <= 21 then r = digits .. srep("0", nn - k)
  elseif 0 < nn and nn <= 21 then r = ssub(digits, 1, nn) .. "." .. ssub(digits, nn + 1)
  elseif -6 < nn and nn <= 0 then r = "0." .. srep("0", -nn) .. digits
  else
    local m = ssub(digits, 1, 1)
    if k > 1 then m = m .. "." .. ssub(digits, 2) end
    local e = nn - 1
    r = m .. "e" .. (e < 0 and "-" or "+") .. mabs(e)
  end
  return sign .. r
end

local function str_to_num(s)
  s = smatch(s, "^[%s\194\160]*(.-)[%s\194\160]*$")
  if s == "" then return 0.0 end
  local c = sbyte(s, 1)
  if c == 48 and #s > 2 then
    local base = ({ x = 16, X = 16, o = 8, O = 8, b = 2, B = 2 })[ssub(s, 2, 2)]
    if base then
      local n = 0.0
      for i = 3, #s do
        local d = tonumber(ssub(s, i, i), 36)
        if not d or d >= base then return NaN end
        n = n * base + d
      end
      return n
    end
  end
  if s == "Infinity" or s == "+Infinity" then return huge end
  if s == "-Infinity" then return -huge end
  local base, e = s, smatch(s, "[eE][+-]?%d+$")
  if e then base = ssub(s, 1, #s - #e) end
  if smatch(base, "^[+-]?%d+%.?%d*$") or smatch(base, "^[+-]?%.%d+$") then return tonumber(s) + 0.0 end
  return NaN
end

-- UTF-8 helpers (own code: no dependency on the utf8 library).
local function utf8_encode(cp)
  if cp < 0x80 then return schar(cp)
  elseif cp < 0x800 then return schar(0xC0 | (cp >> 6), 0x80 | (cp & 63))
  elseif cp < 0x10000 then return schar(0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63))
  elseif cp < 0x110000 then
    return schar(0xF0 | (cp >> 18), 0x80 | ((cp >> 12) & 63), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63))
  end
  return "\239\191\189"
end

-- Decode the UTF-8 char at byte i. Returns code point and byte length.
local function utf8_decode(s, i)
  local c = sbyte(s, i)
  if not c then return nil, 0 end
  if c < 0x80 then return c, 1 end
  local n, cp
  if c >= 0xF0 then n, cp = 4, c & 7 elseif c >= 0xE0 then n, cp = 3, c & 15 elseif c >= 0xC0 then n, cp = 2, c & 31
  else return c, 1 end
  for j = 1, n - 1 do
    local b = sbyte(s, i + j)
    if not b or b < 0x80 or b > 0xBF then return c, 1 end
    cp = (cp << 6) | (b & 63)
  end
  return cp, n
end

-- Objects. `k` (the enumeration order list) is created when needed.
-- A key that is only in `p` is not enumerable (methods of built-ins).
local function new_obj(proto) return { p = {}, proto = proto, cls = "Object" } end
local function new_arr(list, n) return { p = {}, proto = ArrayProto, cls = "Array", a = list, len = n or #list } end
local function new_native(fn, name, nargs, noctor)
  return { p = {}, proto = FunctionProto, cls = "Function", fn = fn, fname = name or "", nargs = nargs or 0,
    noctor = noctor, lazyproto = not noctor or nil }
end

local function setown(o, key, v)
  local p = o.p
  if p[key] == nil then
    local acc = o.acc
    if not (acc and acc[key]) then
      local k = o.k
      if k then k[#k + 1] = key else o.k = { key } end
    end
  end
  if v == nil then v = UNDEF end
  p[key] = v
end

local function sethidden(o, key, v)
  if v == nil then v = UNDEF end
  o.p[key] = v
end

local function remove_key(o, key)
  local k = o.k
  if k then
    for i = 1, #k do
      if k[i] == key then table.remove(k, i); break end
    end
  end
end

local function throw(v) error(setmetatable({ value = v }, Throw), 0) end

local function make_error(name, msg)
  local o = new_obj(R.errproto[name] or R.errproto.Error)
  o.cls = "Error"
  o.p.message = msg
  o.p.stack = name .. ": " .. msg
  return o
end
local function throw_error(name, msg) throw(make_error(name, msg)) end
local function fatal(msg) error(setmetatable({ msg = msg }, Fatal), 0) end
-- Native work costs steps too: big strings and arrays must not give free CPU time.
local function charge(n)
  local st = R.steps_left - n
  R.steps_left = st
  if st < 0 then fatal("script too slow (step limit)") end
end

local function keystr(k)
  if type(k) == "string" then return k end
  if type(k) == "number" then return numstr(k) end
  if type(k) == "table" and k.sym then return "Symbol(" .. (k.desc or "") .. ")" end
  return tostring(k)
end

-- canonical array index of a string key, or nil
local function idx_of(key)
  local c = sbyte(key, 1)
  if c and c >= 48 and c <= 57 and #key < 16 then
    local n = tonumber(key)
    if n and n % 1 == 0 and (c ~= 48 or #key == 1) and sfind(key, "^%d+$") then return n end
  end
end

local to_prim, to_str, to_num

local function to_bool(v)
  if v == nil or v == false or v == null or v == 0 or v == "" or v ~= v then return false end
  return true
end

function to_str(v)
  local t = type(v)
  if t == "string" then return v
  elseif t == "number" then return numstr(v)
  elseif t == "nil" then return "undefined"
  elseif t == "boolean" then return v and "true" or "false"
  elseif v == null then return "null"
  elseif v.sym then throw_error("TypeError", "Cannot convert a Symbol value to a string")
  end
  return to_str(to_prim(v, "string"))
end

function to_num(v)
  local t = type(v)
  if t == "number" then return v
  elseif t == "string" then return str_to_num(v)
  elseif t == "nil" then return NaN
  elseif t == "boolean" then return v and 1.0 or 0.0
  elseif v == null then return 0.0
  elseif v.sym then throw_error("TypeError", "Cannot convert a Symbol value to a number")
  end
  return to_num(to_prim(v, "number"))
end

local getv, setv, call_fn

function to_prim(v, hint)
  if type(v) ~= "table" or v.p == nil then return v end
  if hint == nil and v.cls == "Date" then hint = "string" end
  local first, second = "valueOf", "toString"
  if hint == "string" then first, second = second, first end
  for i = 1, 2 do
    local f = getv(v, i == 1 and first or second)
    if type(f) == "table" and f.fn then
      local r = f.fn(v)
      if type(r) ~= "table" or r.p == nil then return r end
    end
  end
  throw_error("TypeError", "Cannot convert object to primitive value")
end

local function typeof(v)
  local t = type(v)
  if t == "nil" then return "undefined"
  elseif t == "table" then
    if v == null then return "object" end
    if v.sym then return "symbol" end
    if v.fn then return "function" end
    return "object"
  end
  return t
end

local function to_int32(n)
  if n ~= n or n == huge or n == -huge then return 0 end
  local i = mtoint(n)
  if not i then
    n = fmod(n >= 0 and floor(n) or -floor(-n), 4294967296.0)
    i = mtoint(n)
  end
  i = i & 0xFFFFFFFF
  if i >= 0x80000000 then i = i - 0x100000000 end
  return i
end

-- Property access ---------------------------------------------------------
local function acc_get(a, this)
  local lg = a.lg
  if lg then return lg(this) end
  local g = a.g
  if g then return (g.fn(this)) end
  return nil
end

local function getp(o, key, this)
  repeat
    local v = o.p[key]
    if v ~= nil then
      if v == UNDEF then return nil end
      return v
    end
    local acc = o.acc
    if acc then
      local a = acc[key]
      if a then return acc_get(a, this) end
    end
    o = o.proto
  until not o
  return nil
end

function getv(o, key)
  local t = type(o)
  if t == "table" then
    local p = o.p
    if not p then
      if o == null then throw_error("TypeError", "Cannot read properties of null (reading '" .. keystr(key) .. "')") end
      return getp(SymbolProto, key, o)
    end
    local a = o.a
    if type(key) == "number" then
      if a then
        local v = a[key + 1]
        if v ~= nil then return v end
        if key >= 0 and key % 1 == 0 then return nil end
      end
      key = numstr(key)
    elseif a then
      if key == "length" then return o.len + 0.0 end
      if type(key) == "string" then
        local n = idx_of(key)
        if n then return a[n + 1] end
      end
    end
    return getp(o, key, o)
  elseif t == "string" then
    if type(key) == "number" then
      if key >= 0 and key % 1 == 0 then
        local c = ssub(o, key + 1, key + 1)
        if c ~= "" then return c end
        return nil
      end
      key = numstr(key)
    elseif key == "length" then return #o + 0.0
    elseif type(key) == "string" then
      local n = idx_of(key)
      if n then
        local c = ssub(o, n + 1, n + 1)
        if c ~= "" then return c end
        return nil
      end
    end
    return getp(StringProto, key, o)
  elseif t == "number" then
    return getp(NumberProto, type(key) == "number" and numstr(key) or key, o)
  elseif t == "boolean" then
    return getp(BooleanProto, type(key) == "number" and numstr(key) or key, o)
  end
  throw_error("TypeError", "Cannot read properties of undefined (reading '" .. keystr(key) .. "')")
end

local function set_length(o, v)
  local n = to_num(v)
  if n ~= n or n < 0 or n % 1 ~= 0 or n > MAX_ARR then throw_error("RangeError", "Invalid array length") end
  if o.frozen then return end
  local len, a = o.len, o.a
  if n < len then for i = len, n + 1, -1 do a[i] = nil end end
  o.len = n
end

function setv(o, key, v)
  if type(o) ~= "table" then
    if o == nil then throw_error("TypeError", "Cannot set properties of undefined (setting '" .. keystr(key) .. "')") end
    return
  end
  local p = o.p
  if not p then
    if o == null then throw_error("TypeError", "Cannot set properties of null (setting '" .. keystr(key) .. "')") end
    return
  end
  local a = o.a
  if type(key) == "number" then
    if a and key >= 0 and key % 1 == 0 then
      if o.frozen then return end
      if key >= o.len then
        if key >= MAX_ARR then throw_error("RangeError", "Invalid array length") end
        o.len = key + 1
      end
      a[key + 1] = v
      return
    end
    key = numstr(key)
  elseif a then
    if key == "length" then set_length(o, v); return end
    if type(key) == "string" then
      local n = idx_of(key)
      if n then return setv(o, n + 0.0, v) end
    end
  end
  local cur = p[key]
  if cur ~= nil then
    if o.frozen then return end
    local ro = o.ro
    if ro and ro[key] then return end
    if v == nil then v = UNDEF end
    p[key] = v
    return
  end
  local obj = o
  repeat
    local acc = obj.acc
    if acc then
      local ac = acc[key]
      if ac then
        if ac.ls then ac.ls(o, v)
        elseif ac.s then ac.s.fn(o, v) end
        return
      end
    end
    obj = obj.proto
  until not obj
  if o.frozen or o.noext then return end
  setown(o, key, v)
end

-- A JS value as a property key (string, number or symbol table).
local function to_key(k)
  local t = type(k)
  if t == "string" or t == "number" then return k end
  if t == "table" and k.sym then return k end
  return to_str(k)
end

local function has_prop(o, key)
  if type(key) == "number" then
    if o.a and key >= 0 and key % 1 == 0 then return key < o.len and o.a[key + 1] ~= nil end
    key = numstr(key)
  elseif o.a and type(key) == "string" then
    if key == "length" then return true end
    local n = idx_of(key)
    if n then return n < o.len and o.a[n + 1] ~= nil end
  end
  repeat
    if o.p[key] ~= nil or (o.acc and o.acc[key]) then return true end
    o = o.proto
  until not o
  return false
end

local function has_own(o, key)
  if type(key) == "number" then
    if o.a and key >= 0 and key % 1 == 0 then return key < o.len and o.a[key + 1] ~= nil end
    key = numstr(key)
  elseif o.a and type(key) == "string" then
    if key == "length" then return true end
    local n = idx_of(key)
    if n then return n < o.len and o.a[n + 1] ~= nil end
  end
  return o.p[key] ~= nil or (o.acc ~= nil and o.acc[key] ~= nil)
end

local function del_prop(o, key)
  if type(o) ~= "table" or o.p == nil then
    if o == nil or o == null then throw_error("TypeError", "Cannot convert undefined or null to object") end
    return true
  end
  if o.frozen then return false end
  if type(key) == "number" then
    if o.a and key >= 0 and key % 1 == 0 then
      if key < o.len then o.a[key + 1] = nil end
      return true
    end
    key = numstr(key)
  elseif o.a and type(key) == "string" then
    local n = idx_of(key)
    if n then if n < o.len then o.a[n + 1] = nil end; return true end
  end
  if o.p[key] ~= nil or (o.acc and o.acc[key]) then
    o.p[key] = nil
    if o.acc then o.acc[key] = nil end
    remove_key(o, key)
  end
  return true
end

-- Own enumerable string keys: array indexes, then integer keys ascending, then the rest.
local function own_keys(o)
  local out, n = {}, 0
  if o.a then for i = 0, o.len - 1 do n = n + 1; out[n] = numstr(i) end end
  local k = o.k
  if k then
    local ints, others, no = nil, {}, 0
    for i = 1, #k do
      local key = k[i]
      if type(key) == "string" then
        if idx_of(key) then
          ints = ints or {}
          ints[#ints + 1] = key
        else
          no = no + 1; others[no] = key
        end
      end
    end
    if ints then
      tsort(ints, function(x, y) return tonumber(x) < tonumber(y) end)
      for i = 1, #ints do n = n + 1; out[n] = ints[i] end
    end
    for i = 1, no do n = n + 1; out[n] = others[i] end
  end
  return out, n
end

-- Calls ---------------------------------------------------------------------
local function describe(v)
  if v == nil then return "undefined" elseif v == null then return "null" end
  local t = type(v)
  if t == "string" then return '"' .. v .. '"' end
  if t == "table" then return v.sym and "Symbol()" or "object" end
  return to_str(v)
end

function call_fn(f, this, ...)
  if type(f) == "table" then
    local fn = f.fn
    if fn then return (fn(this, ...)) end
  end
  throw_error("TypeError", describe(f) .. " is not a function")
end

local function proto_of_nt(nt)
  local pr = getv(nt, "prototype")
  if is_obj(pr) then return pr end
  return ObjectProto
end

local function init_fields(F, obj)
  local fields = F.fields
  for i = 1, #fields do
    local f = fields[i]
    local v = f.init and f.init.fn(obj)
    local key = f.key
    if type(key) == "string" and sbyte(key, 1) == 35 then sethidden(obj, key, v) else setown(obj, key, v) end
  end
end

local function construct(F, nt, ...)
  if type(F) ~= "table" or not F.fn or F.noctor then
    throw_error("TypeError", describe(F) .. " is not a constructor")
  end
  local nc = F.ctor
  if nc then return nc(proto_of_nt(nt), ...) end
  if F.derived then
    R.nt = nt
    return (F.fn(nil, ...))
  end
  local obj = new_obj(proto_of_nt(nt))
  if F.fields then init_fields(F, obj) end
  if F.isclass then R.nt = nt end
  local r = F.fn(obj, ...)
  if type(r) == "table" and r.p ~= nil then return r end
  return obj
end

-- Equality, arithmetic and comparison (slow paths) ------------------------
local function loose_eq(a, b)
  if a == b then return true end
  if a == nil or a == null then return b == nil or b == null end
  if b == nil or b == null then return false end
  local ta, tb = type(a), type(b)
  if ta == tb then return false end
  if ta == "number" and tb == "string" then return a == str_to_num(b) end
  if ta == "string" and tb == "number" then return str_to_num(a) == b end
  if ta == "boolean" then return loose_eq(a and 1.0 or 0.0, b) end
  if tb == "boolean" then return loose_eq(a, b and 1.0 or 0.0) end
  if ta == "table" and a.p and (tb == "number" or tb == "string") then return loose_eq(to_prim(a), b) end
  if tb == "table" and b.p and (ta == "number" or ta == "string") then return loose_eq(a, to_prim(b)) end
  return false
end

local function concat(a, b)
  local r = a .. b
  local n = #r
  if n > 2048 then
    if n > MAX_STR then throw_error("RangeError", "Invalid string length") end
    R.steps_left = R.steps_left - (n >> 8)
  end
  return r
end

local function add(a, b)
  local ta, tb = type(a), type(b)
  if ta == "number" and tb == "number" then return a + b end
  if ta == "string" and tb == "string" then return concat(a, b) end
  if ta == "table" then a = to_prim(a); ta = type(a) end
  if tb == "table" then b = to_prim(b); tb = type(b) end
  if ta == "string" or tb == "string" then return concat(to_str(a), to_str(b)) end
  return to_num(a) + to_num(b)
end

local function lt(a, b)
  if type(a) == "table" then a = to_prim(a, "number") end
  if type(b) == "table" then b = to_prim(b, "number") end
  if type(a) == "string" and type(b) == "string" then return a < b end
  return to_num(a) < to_num(b)
end
local function le(a, b)
  if type(a) == "table" then a = to_prim(a, "number") end
  if type(b) == "table" then b = to_prim(b, "number") end
  if type(a) == "string" and type(b) == "string" then return a <= b end
  return to_num(a) <= to_num(b)
end

local function pow(a, b)
  if b ~= b then return NaN end
  if b == 0 then return 1.0 end
  if (a == 1 or a == -1) and (b == huge or b == -huge) then return NaN end
  return a ^ b
end

local function jsmod(a, b)
  if b == 0 or a ~= a or b ~= b or a == huge or a == -huge then return NaN end
  return fmod(a, b)
end

local function instance_of(v, F)
  if type(F) ~= "table" or not F.fn then throw_error("TypeError", "Right-hand side of 'instanceof' is not callable") end
  if F.bound then F = F.bound end
  if not is_obj(v) then return false end
  local pr = getv(F, "prototype")
  v = v.proto
  while v do
    if v == pr then return true end
    v = v.proto
  end
  return false
end

-- Iteration ----------------------------------------------------------------
-- get_iter(v) returns a Lua function: each call returns (true, value) or false.
local get_iter
local UCHAR = "[\0-\127\194-\244][\128-\191]*"
function get_iter(v)
  local t = type(v)
  if t == "table" and v.p then
    local cls = v.cls
    if cls == "Array" or cls == "Arguments" then
      local i = 0
      return function()
        if i < v.len then i = i + 1; return true, v.a[i] end
        return false
      end
    elseif cls == "Iterator" then return v.nextf
    elseif cls == "Map" or cls == "Set" then
      local i, isset = 0, cls == "Set"
      return function()
        while i < v.top do
          i = i + 1
          local k = v.keys[i]
          if k ~= nil then
            if k == NANKEY then k = NaN elseif k == UNDEFKEY then k = nil end
            if isset then return true, k end
            return true, new_arr({ k, v.vals[i] }, 2)
          end
        end
        return false
      end
    end
    local f = getv(v, symIterator)
    if type(f) == "table" and f.fn then
      local it = f.fn(v)
      if is_obj(it) then
        if it.cls == "Iterator" then return it.nextf end
        local nextm = getv(it, "next")
        return function()
          local r = call_fn(nextm, it)
          if not is_obj(r) then throw_error("TypeError", "Iterator result is not an object") end
          if to_bool(getv(r, "done")) then return false end
          return true, getv(r, "value")
        end
      end
    end
  elseif t == "string" then
    local pos, len = 1, #v
    return function()
      if pos > len then return false end
      local s, e = sfind(v, UCHAR, pos)
      if not s then return false end
      pos = e + 1
      return true, ssub(v, s, e)
    end
  end
  throw_error("TypeError", (v == nil and "undefined" or v == null and "null" or typeof(v)) .. " is not iterable")
end

local function iter_list(v)
  if type(v) == "table" and v.cls == "Array" then
    return tmove(v.a, 1, v.len, 1, {}), v.len
  end
  local nextf, out, n = get_iter(v), {}, 0
  while true do
    local ok, x = nextf()
    if not ok then break end
    n = n + 1
    out[n] = x
    if n > MAX_ARR then throw_error("RangeError", "Invalid array length") end
  end
  if n > 256 then charge(n >> 5) end
  return out, n
end

local function make_iter(nextf)
  local o = new_obj(R.IteratorProto)
  o.cls = "Iterator"
  o.nextf = nextf
  return o
end

local function array_check_len(n)
  if n > MAX_ARR then throw_error("RangeError", "Invalid array length") end
end

---------------------------------------------------------------------------
-- 2. Regex engine: backtracking, compiled to closures
---------------------------------------------------------------------------
-- A pattern is parsed to a node tree. Then each node is compiled to a Lua
-- closure m(i) that matches at byte i and calls its continuation. The shared
-- locals subj/slen/caps/steps are set by exec. A match never calls JS code,
-- so sharing them is safe.
local regex_compile, regex_exec
do
  local subj, slen, caps, steps, attempts = "", 0, {}, 0, 0
  local STEP_LIMIT = 400000
  local DIGIT = { 48, 57 }
  local WORD = { 48, 57, 65, 90, 95, 95, 97, 122 }
  local SPACE = { 9, 13, 32, 32, 160, 160, 0x1680, 0x1680, 0x2000, 0x200a, 0x2028, 0x2029, 0x202f, 0x202f, 0x205f, 0x205f,
    0x3000, 0x3000, 0xFEFF, 0xFEFF }

  local function isword(c)
    return c ~= nil and ((c >= 48 and c <= 57) or (c >= 65 and c <= 90) or c == 95 or (c >= 97 and c <= 122))
  end

  local function inranges(rs, cp)
    for i = 1, #rs, 2 do
      if cp >= rs[i] and cp <= rs[i + 1] then return true end
    end
    return false
  end

  local function make_set(neg, ranges, subs, icase)
    local function test0(cp)
      if inranges(ranges, cp) then return true end
      for i = 1, #subs do
        local sb = subs[i]
        if inranges(sb.ranges, cp) ~= sb.neg then return true end
      end
      return false
    end
    local function test(cp)
      local f = test0(cp)
      if not f and icase then
        if cp >= 65 and cp <= 90 then f = test0(cp + 32) elseif cp >= 97 and cp <= 122 then f = test0(cp - 32) end
      end
      return f ~= neg
    end
    local tbl = {}
    for c = 0, 127 do tbl[c] = test(c) end
    return { t = "set", test = test, tbl = tbl }
  end

  ------------------------------------------------------------------ parser
  local function parse(src, flags)
    local pos, len = 1, #src
    local ngroups, names = 0, {}
    local icase = sfind(flags, "i", 1, true) ~= nil
    local function perr(msg)
      throw_error("SyntaxError", "Invalid regular expression: /" .. src .. "/: " .. msg)
    end
    local parse_alt

    local function hexval(n)
      local h = ssub(src, pos, pos + n - 1)
      if #h < n or not sfind(h, "^%x+$") then return nil end
      pos = pos + n
      return tonumber(h, 16)
    end

    -- Escape after the backslash. Returns kind, value:
    --   "cp", code point  |  "class", {ranges, neg}  |  "node", node
    local function parse_escape(inclass)
      local c = ssub(src, pos, pos)
      if c == "" then perr("\\ at end of pattern") end
      pos = pos + 1
      if c == "d" then return "class", { DIGIT, false } elseif c == "D" then return "class", { DIGIT, true }
      elseif c == "w" then return "class", { WORD, false } elseif c == "W" then return "class", { WORD, true }
      elseif c == "s" then return "class", { SPACE, false } elseif c == "S" then return "class", { SPACE, true }
      elseif c == "n" then return "cp", 10 elseif c == "r" then return "cp", 13 elseif c == "t" then return "cp", 9
      elseif c == "f" then return "cp", 12 elseif c == "v" then return "cp", 11
      elseif c == "0" and not sfind(ssub(src, pos, pos), "%d") then return "cp", 0
      elseif c == "x" then
        local v = hexval(2)
        if v then return "cp", v end
        return "cp", 120
      elseif c == "u" then
        if ssub(src, pos, pos) == "{" then
          local e = sfind(src, "}", pos, true)
          local v = e and tonumber(ssub(src, pos + 1, e - 1), 16)
          if v then pos = e + 1; return "cp", v end
          return "cp", 117
        end
        local v = hexval(4)
        if not v then return "cp", 117 end
        if v >= 0xD800 and v <= 0xDBFF and ssub(src, pos, pos + 1) == "\\u" then
          local save = pos
          pos = pos + 2
          local lo = hexval(4)
          if lo and lo >= 0xDC00 and lo <= 0xDFFF then return "cp", 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00) end
          pos = save
        end
        if v >= 0xD800 and v <= 0xDFFF then v = 0xFFFD end
        return "cp", v
      elseif c == "c" then
        local l = sbyte(src, pos)
        if l and sfind(schar(l), "%a") then pos = pos + 1; return "cp", l % 32 end
        return "cp", 92
      elseif not inclass then
        if c == "b" then return "node", { t = "wordb", neg = false } end
        if c == "B" then return "node", { t = "wordb", neg = true } end
        if sfind(c, "[1-9]") then
          local d = smatch(src, "^%d*", pos)
          pos = pos + #d
          return "node", { t = "bref", idx = tonumber(c .. d) }
        end
        if c == "k" and ssub(src, pos, pos) == "<" then
          local e = sfind(src, ">", pos, true)
          if not e then perr("Invalid named reference") end
          local nm = ssub(src, pos + 1, e - 1)
          pos = e + 1
          return "node", { t = "bref", name = nm }
        end
      elseif c == "b" then return "cp", 8
      end
      -- an escaped literal char (may be a multi-byte UTF-8 char)
      pos = pos - 1
      local cp, n = utf8_decode(src, pos)
      pos = pos + n
      return "cp", cp
    end

    local function parse_class()
      local neg = false
      if ssub(src, pos, pos) == "^" then neg = true; pos = pos + 1 end
      local ranges, subs = {}, {}
      local function item()
        local c = ssub(src, pos, pos)
        if c == "\\" then
          pos = pos + 1
          local kind, v = parse_escape(true)
          if kind == "class" then return nil, v end
          return v
        end
        local cp, n = utf8_decode(src, pos)
        pos = pos + n
        return cp
      end
      while true do
        if pos > len then perr("Unterminated character class") end
        if ssub(src, pos, pos) == "]" then pos = pos + 1; break end
        local lo, cls = item()
        if cls then
          subs[#subs + 1] = { ranges = cls[1], neg = cls[2] }
        elseif ssub(src, pos, pos) == "-" and ssub(src, pos + 1, pos + 1) ~= "]" and pos + 1 <= len then
          pos = pos + 1
          local hi, cls2 = item()
          if cls2 then
            ranges[#ranges + 1] = lo; ranges[#ranges + 1] = lo
            ranges[#ranges + 1] = 45; ranges[#ranges + 1] = 45
            subs[#subs + 1] = { ranges = cls2[1], neg = cls2[2] }
          else
            if hi < lo then perr("Range out of order in character class") end
            ranges[#ranges + 1] = lo; ranges[#ranges + 1] = hi
          end
        else
          ranges[#ranges + 1] = lo; ranges[#ranges + 1] = lo
        end
      end
      return make_set(neg, ranges, subs, icase)
    end

    local function parse_quant(node)
      local c = ssub(src, pos, pos)
      local min, max
      if c == "*" then min, max = 0, huge; pos = pos + 1
      elseif c == "+" then min, max = 1, huge; pos = pos + 1
      elseif c == "?" then min, max = 0, 1; pos = pos + 1
      elseif c == "{" then
        local a, b, e = smatch(src, "^{(%d+)(,?%d*)}()", pos)
        if not a then return node end
        min = tonumber(a)
        if b == "" then max = min elseif b == "," then max = huge else max = tonumber(ssub(b, 2)) end
        if max < min then perr("numbers out of order in {} quantifier") end
        pos = e
      else
        return node
      end
      local lazy = false
      if ssub(src, pos, pos) == "?" then lazy = true; pos = pos + 1 end
      if node.t == "bol" or node.t == "eol" or node.t == "wordb" then perr("Nothing to repeat") end
      return { t = "rep", node = node, min = min, max = max, lazy = lazy }
    end

    local function parse_atom()
      local c = ssub(src, pos, pos)
      if c == "(" then
        pos = pos + 1
        local node
        if ssub(src, pos, pos) == "?" then
          local k = ssub(src, pos + 1, pos + 1)
          if k == ":" then
            pos = pos + 2
            node = { t = "ncgroup", node = parse_alt() }
          elseif k == "=" or k == "!" then
            pos = pos + 2
            node = { t = "look", neg = (k == "!"), node = parse_alt() }
          elseif k == "<" and (ssub(src, pos + 2, pos + 2) == "=" or ssub(src, pos + 2, pos + 2) == "!") then
            perr("lookbehind is not supported")
          elseif k == "<" then
            local nm, e = smatch(src, "^%?<([%w_$]+)>()", pos)
            if not nm then perr("Invalid capture group name") end
            pos = e
            ngroups = ngroups + 1
            local idx = ngroups
            names[#names + 1] = { name = nm, idx = idx }
            node = { t = "group", idx = idx, node = parse_alt() }
          else
            perr("Invalid group")
          end
        else
          ngroups = ngroups + 1
          local idx = ngroups
          node = { t = "group", idx = idx, node = parse_alt() }
        end
        if ssub(src, pos, pos) ~= ")" then perr("Unterminated group") end
        pos = pos + 1
        return node
      elseif c == "[" then pos = pos + 1; return parse_class()
      elseif c == "." then pos = pos + 1; return { t = "any" }
      elseif c == "^" then pos = pos + 1; return { t = "bol" }
      elseif c == "$" then pos = pos + 1; return { t = "eol" }
      elseif c == "\\" then
        pos = pos + 1
        local kind, v = parse_escape(false)
        if kind == "cp" then return { t = "char", c = utf8_encode(v) }
        elseif kind == "class" then return make_set(v[2], v[1], {}, false)
        else return v end
      elseif c == "*" or c == "+" or c == "?" then perr("Nothing to repeat")
      elseif c == ")" then perr("Unmatched ')'")
      end
      local cp, n = utf8_decode(src, pos)
      local ch = ssub(src, pos, pos + n - 1)
      pos = pos + n
      return { t = "char", c = ch }
    end

    local function parse_seq()
      local list = {}
      while pos <= len do
        local c = ssub(src, pos, pos)
        if c == "|" or c == ")" then break end
        local node = parse_quant(parse_atom())
        local last = list[#list]
        if node.t == "char" and last and last.t == "char" then last.c = last.c .. node.c
        else list[#list + 1] = node end
      end
      if #list == 1 then return list[1] end
      return { t = "seq", list = list }
    end

    function parse_alt()
      local alts = { parse_seq() }
      while ssub(src, pos, pos) == "|" do
        pos = pos + 1
        alts[#alts + 1] = parse_seq()
      end
      if #alts == 1 then return alts[1] end
      return { t = "alt", list = alts }
    end

    local tree = parse_alt()
    if pos <= len then perr("Unmatched ')'") end
    return tree, ngroups, names
  end

  ---------------------------------------------------------------- compiler
  local function step_limit()
    fatal("regular expression too slow (step limit)")
  end

  local compile

  -- Single-char nodes: returns step(i) -> next index or nil.
  local function step_of(node, icase, dotall)
    local t = node.t
    if t == "char" then
      local c = node.c
      if #c == 1 and not icase then
        local b = sbyte(c)
        return function(i) if sbyte(subj, i) == b then return i + 1 end end
      elseif #c == 1 then
        local lo, up = sbyte(slower(c)), sbyte(supper(c))
        return function(i)
          local x = sbyte(subj, i)
          if x == lo or x == up then return i + 1 end
        end
      elseif not icase and select(2, utf8_decode(c, 1)) == #c then
        local n = #c
        return function(i) if ssub(subj, i, i + n - 1) == c then return i + n end end
      end
    elseif t == "any" then
      return function(i)
        local c = sbyte(subj, i)
        if not c then return nil end
        if c < 128 then
          if not dotall and (c == 10 or c == 13) then return nil end
          return i + 1
        end
        return i + (c >= 0xF0 and 4 or c >= 0xE0 and 3 or c >= 0xC0 and 2 or 1)
      end
    elseif t == "set" then
      local tbl, test = node.tbl, node.test
      return function(i)
        local c = sbyte(subj, i)
        if not c then return nil end
        if c < 128 then
          if tbl[c] then return i + 1 end
          return nil
        end
        local cp, n = utf8_decode(subj, i)
        if test(cp) then return i + n end
      end
    end
  end

  function compile(node, k, o)
    local t = node.t
    local step = step_of(node, o.icase, o.dotall)
    if step then
      return function(i)
        local j = step(i)
        if j then return k(j) end
      end
    end
    if t == "char" then
      local c, n = node.c, #node.c
      if o.icase then
        c = slower(c)
        return function(i) if slower(ssub(subj, i, i + n - 1)) == c then return k(i + n) end end
      end
      return function(i) if ssub(subj, i, i + n - 1) == c then return k(i + n) end end
    elseif t == "seq" then
      local m = k
      for idx = #node.list, 1, -1 do m = compile(node.list[idx], m, o) end
      return m
    elseif t == "alt" then
      local ms = {}
      for idx = 1, #node.list do ms[idx] = compile(node.list[idx], k, o) end
      local n = #ms
      return function(i)
        steps = steps + 1
        if steps > STEP_LIMIT then step_limit() end
        for idx = 1, n do
          local r = ms[idx](i)
          if r then return r end
        end
      end
    elseif t == "ncgroup" then
      return compile(node.node, k, o)
    elseif t == "group" then
      local a, b, idx = node.idx * 2 + 1, node.idx * 2 + 2, node.idx
      local gstart = -1
      local inner = compile(node.node, function(j)
        local os, oe = caps[a], caps[b]
        caps[a], caps[b] = gstart, j
        local r = k(j)
        if not r then caps[a], caps[b] = os, oe end
        return r
      end, o)
      return function(i)
        local prev = gstart
        gstart = i
        local r = inner(i)
        gstart = prev
        return r
      end
    elseif t == "bol" then
      local ml = o.multiline
      return function(i)
        if i == 1 or (ml and sbyte(subj, i - 1) == 10) then return k(i) end
      end
    elseif t == "eol" then
      local ml = o.multiline
      return function(i)
        if i == slen + 1 or (ml and sbyte(subj, i) == 10) then return k(i) end
      end
    elseif t == "wordb" then
      local neg = node.neg
      return function(i)
        local at = isword(sbyte(subj, i - 1)) ~= isword(sbyte(subj, i))
        if at ~= neg then return k(i) end
      end
    elseif t == "bref" then
      local idx = node.idx
      if node.name then
        for _, g in ipairs(o.names) do if g.name == node.name then idx = g.idx end end
        if not idx then throw_error("SyntaxError", "Invalid named capture referenced") end
      end
      local icase = o.icase
      return function(i)
        local s0, e0 = caps[idx * 2 + 1], caps[idx * 2 + 2]
        if not s0 then return k(i) end
        local sub = ssub(subj, s0, e0 - 1)
        local n = #sub
        local here = ssub(subj, i, i + n - 1)
        if here == sub or (icase and slower(here) == slower(sub)) then return k(i + n) end
      end
    elseif t == "look" then
      local inner = compile(node.node, function(j) return j end, o)
      local neg = node.neg
      return function(i)
        local r = inner(i)
        if (r ~= nil) ~= neg then return k(i) end
      end
    elseif t == "rep" then
      local min, max, lazy = node.min, node.max, node.lazy
      local rstep = step_of(node.node, o.icase, o.dotall)
      if rstep then
        if lazy then
          return function(i)
            local j, cnt = i, 0
            while cnt < min do
              j = rstep(j)
              if not j then return nil end
              cnt = cnt + 1
            end
            while true do
              steps = steps + 1
              if steps > STEP_LIMIT then step_limit() end
              local r = k(j)
              if r then return r end
              if cnt >= max then return nil end
              j = rstep(j)
              if not j then return nil end
              cnt = cnt + 1
            end
          end
        end
        return function(i)
          local j, cnt = i, 0
          while cnt < max do
            local nj = rstep(j)
            if not nj then break end
            j = nj
            cnt = cnt + 1
          end
          if cnt < min then return nil end
          while true do
            steps = steps + 1
            if steps > STEP_LIMIT then step_limit() end
            local r = k(j)
            if r then return r end
            if cnt <= min then return nil end
            cnt = cnt - 1
            j = j - 1
            while j > i do
              local b = sbyte(subj, j)
              if b >= 0x80 and b < 0xC0 then j = j - 1 else break end
            end
          end
        end
      end
      -- general loop: state in count/lastpos, always restored
      local count, lastpos = 0, -1
      local xm
      local function try(i)
        steps = steps + 1
        if steps > STEP_LIMIT then step_limit() end
        local c = count
        if lazy then
          if c >= min then
            local r = k(i)
            if r then return r end
          end
          if c < max and not (c >= min and i == lastpos) then
            local sl = lastpos
            count = c + 1; lastpos = i
            local r = xm(i)
            count = c; lastpos = sl
            return r
          end
          return nil
        end
        if c < max and not (c >= min and i == lastpos) then
          local sl = lastpos
          count = c + 1; lastpos = i
          local r = xm(i)
          count = c; lastpos = sl
          if r then return r end
        end
        if c >= min then return k(i) end
        return nil
      end
      xm = compile(node.node, try, o)
      return function(i)
        local sc, sl = count, lastpos
        count = 0; lastpos = -1
        local r = try(i)
        count = sc; lastpos = sl
        return r
      end
    end
    error("regex: unknown node " .. tostring(t))
  end

  function regex_compile(source, flags)
    local seen = {}
    for c in sgmatch(flags, ".") do
      if not sfind("gimsuyd", c, 1, true) or seen[c] then
        throw_error("SyntaxError", "Invalid flags supplied to RegExp constructor '" .. flags .. "'")
      end
      seen[c] = true
    end
    if #source > 4096 then throw_error("SyntaxError", "Invalid regular expression: pattern too long") end
    local tree, ngroups, names = parse(source, flags)
    local o = { icase = seen.i, multiline = seen.m, dotall = seen.s, names = names }
    local prog = { ngroups = ngroups, names = names }
    prog.m = compile(tree, function(j) return j end, o)
    -- literal prefix: lets exec skip with a plain find
    local first = tree
    if first.t == "seq" then first = first.list[1] end
    if first and first.t == "char" and not seen.i then prog.prefix = first.c end
    if first and first.t == "bol" and not seen.m then prog.anchored = true end
    return prog
  end

  -- Match prog against s from byte `init`. Returns start, end (exclusive), caps or nil.
  local function regex_exec_raw(prog, s, init, sticky)
    subj, slen, steps = s, #s, 0
    local m, prefix = prog.m, prog.prefix
    local i = init
    while i <= slen + 1 do
      attempts = attempts + 1
      if prefix and not sticky then
        i = sfind(s, prefix, i, true)
        if not i then return nil end
      end
      caps = {}
      local e = m(i)
      if e then return i, e, caps end
      if sticky or prog.anchored then return nil end
      i = i + 1
      -- do not start inside a UTF-8 sequence
      local b = sbyte(s, i)
      while b and b >= 0x80 and b < 0xC0 do i = i + 1; b = sbyte(s, i) end
    end
    return nil
  end

  function regex_exec(prog, s, init, sticky)
    attempts = 0
    local a, b, c = regex_exec_raw(prog, s, init, sticky)
    charge((steps >> 2) + (attempts >> 5))
    return a, b, c
  end
end

---------------------------------------------------------------------------
-- 3. Tokenizer and parser
---------------------------------------------------------------------------
local parse_program
do
  local REGEX_AFTER = {}
  for w in sgmatch("return typeof instanceof in of new delete void throw case do else yield await", "%a+") do
    REGEX_AFTER[w] = true
  end
  local P4 = { [">>>="] = true }
  local P3 = {}
  for w in sgmatch("=== !== **= <<= >>= >>> ... &&= ||= ??=", "%S+") do P3[w] = true end
  local P2 = {}
  for w in sgmatch("=> == != <= >= && || ?? ?. ++ -- += -= *= /= %= &= |= ^= << >> **", "%S+") do P2[w] = true end
  local ESC = { n = "\n", t = "\t", r = "\r", b = "\b", f = "\f", v = "\v" }

  local function lerr(msg, line) error({ syntax = msg, line = line }, 0) end

  -- Reads an escape. `pos` is just after the backslash. Returns text, new pos, newlines.
  local function read_escape(src, pos, line)
    local c = ssub(src, pos, pos)
    if ESC[c] then return ESC[c], pos + 1, 0 end
    if c == "0" and not sfind(ssub(src, pos + 1, pos + 1), "%d") then return "\0", pos + 1, 0 end
    if c == "x" then
      local h = smatch(src, "^%x%x", pos + 1)
      if not h then lerr("Invalid hexadecimal escape sequence", line) end
      return utf8_encode(tonumber(h, 16)), pos + 3, 0
    elseif c == "u" then
      local cp, np
      if ssub(src, pos + 1, pos + 1) == "{" then
        local h, e = smatch(src, "^{(%x+)}()", pos + 1)
        if not h then lerr("Invalid Unicode escape sequence", line) end
        cp, np = tonumber(h, 16), e
        if cp > 0x10FFFF then lerr("Undefined Unicode code-point", line) end
      else
        local h = smatch(src, "^%x%x%x%x", pos + 1)
        if not h then lerr("Invalid Unicode escape sequence", line) end
        cp, np = tonumber(h, 16), pos + 5
        if cp >= 0xD800 and cp <= 0xDBFF then
          local lo = smatch(src, "^\\u(%x%x%x%x)", np)
          lo = lo and tonumber(lo, 16)
          if lo and lo >= 0xDC00 and lo <= 0xDFFF then
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00)
            np = np + 6
          end
        end
      end
      if cp >= 0xD800 and cp <= 0xDFFF then cp = 0xFFFD end
      return utf8_encode(cp), np, 0
    elseif c == "\n" then return "", pos + 1, 1
    elseif c == "\r" then
      if ssub(src, pos + 1, pos + 1) == "\n" then return "", pos + 2, 1 end
      return "", pos + 1, 1
    elseif c == "" then lerr("Invalid or unexpected token", line)
    end
    local _, n = utf8_decode(src, pos)
    return ssub(src, pos, pos + n - 1), pos + n, 0
  end

  local tdepth = 0
  local function tokenize(src, pos, line, tplmode)
    local T, V, L, N = {}, {}, {}, {}
    local n, nl, len, depth = 0, false, #src, 0
    local function push(t, v)
      n = n + 1
      T[n], V[n], L[n], N[n] = t, v, line, nl
      nl = false
    end
    while true do
      local c = sbyte(src, pos)
      if not c then
        if tplmode then lerr("Unterminated template literal", line) end
        break
      end
      if c == 10 then line = line + 1; nl = true; pos = pos + 1
      elseif c == 32 or c == 9 or c == 13 or c == 11 or c == 12 then pos = pos + 1
      elseif c == 194 and sbyte(src, pos + 1) == 160 then pos = pos + 2
      elseif c == 239 and sbyte(src, pos + 1) == 187 and sbyte(src, pos + 2) == 191 then pos = pos + 3
      elseif c == 226 and sbyte(src, pos + 1) == 128 and (sbyte(src, pos + 2) == 168 or sbyte(src, pos + 2) == 169) then
        nl = true; pos = pos + 3
      elseif c == 47 and sbyte(src, pos + 1) == 47 then pos = sfind(src, "\n", pos, true) or (len + 1)
      elseif c == 47 and sbyte(src, pos + 1) == 42 then
        local e = sfind(src, "*/", pos + 2, true)
        if not e then lerr("Unterminated comment", line) end
        for _ in sgmatch(ssub(src, pos, e), "\n") do line = line + 1; nl = true end
        pos = e + 2
      elseif c == 60 and ssub(src, pos, pos + 3) == "<!--" then pos = sfind(src, "\n", pos, true) or (len + 1)
      elseif (c >= 48 and c <= 57) or (c == 46 and (sbyte(src, pos + 1) or 0) >= 48 and (sbyte(src, pos + 1) or 0) <= 57) then
        local s, e = sfind(src, "^0[xX][%x_]+", pos)
        local v
        if s then v = tonumber((sgsub(ssub(src, pos + 2, e), "_", "")), 16)
        else
          s, e = sfind(src, "^0[oO][0-7_]+", pos)
          if s then v = tonumber((sgsub(ssub(src, pos + 2, e), "_", "")), 8)
          else
            s, e = sfind(src, "^0[bB][01_]+", pos)
            if s then v = tonumber((sgsub(ssub(src, pos + 2, e), "_", "")), 2)
            else
              s, e = sfind(src, "^[%d_]*%.?[%d_]*", pos)
              local e2 = select(2, sfind(src, "^[eE][+-]?%d+", e + 1))
              if e2 then e = e2 end
              v = tonumber((sgsub(ssub(src, pos, e), "_", "")))
            end
          end
        end
        if not v then lerr("Invalid or unexpected token", line) end
        if sbyte(src, e + 1) == 110 then lerr("BigInt is not supported", line) end
        if sfind(ssub(src, e + 1, e + 1), "[%a_$]") then lerr("Invalid or unexpected token", line) end
        push("num", v + 0.0)
        pos = e + 1
      elseif (c >= 65 and c <= 90) or (c >= 97 and c <= 122) or c == 95 or c == 36 or c >= 128 or c == 35 then
        local s, e = sfind(src, "^#?[%w_$\128-\255]+", pos)
        if not s then lerr("Invalid or unexpected token", line) end
        push("name", ssub(src, s, e))
        pos = e + 1
      elseif c == 34 or c == 39 then
        local q = schar(c)
        local pat = "[" .. q .. "\\\n]"
        local buf, nb, p = {}, 0, pos + 1
        while true do
          local e = sfind(src, pat, p)
          if not e then lerr("Invalid or unexpected token", line) end
          nb = nb + 1; buf[nb] = ssub(src, p, e - 1)
          local ch = sbyte(src, e)
          if ch == c then p = e + 1; break end
          if ch == 10 then lerr("Invalid or unexpected token", line) end
          local piece, np, nls = read_escape(src, e + 1, line)
          nb = nb + 1; buf[nb] = piece
          line = line + nls
          p = np
          if #buf > 100000 then lerr("string too long", line) end
        end
        push("str", tconcat(buf))
        pos = p
      elseif c == 96 then
        local strs, exprs, buf, nb = {}, {}, {}, 0
        local startline = line
        pos = pos + 1
        while true do
          local ch = sbyte(src, pos)
          if not ch then lerr("Unterminated template literal", startline) end
          if ch == 96 then pos = pos + 1; break
          elseif ch == 92 then
            local piece, np, nls = read_escape(src, pos + 1, line)
            nb = nb + 1; buf[nb] = piece
            line = line + nls
            pos = np
          elseif ch == 36 and sbyte(src, pos + 1) == 123 then
            strs[#strs + 1] = tconcat(buf)
            buf, nb = {}, 0
            tdepth = tdepth + 1
            if tdepth > 40 then lerr("Template literals nested too deep", line) end
            local sub, np, nline = tokenize(src, pos + 2, line, true)
            tdepth = tdepth - 1
            exprs[#exprs + 1] = sub
            pos, line = np, nline
          else
            local e = sfind(src, "[`\\$\n]", pos + 1) or (len + 1)
            if ch == 10 then line = line + 1 end
            nb = nb + 1; buf[nb] = ssub(src, pos, e - 1)
            pos = e
          end
        end
        strs[#strs + 1] = tconcat(buf)
        local savel = line
        line = startline
        push("tpl", { strs = strs, exprs = exprs })
        line = savel
      else
        local ch = schar(c)
        local pt, tv = n > 0 and T[n], n > 0 and V[n]
        local divide = pt == "num" or pt == "str" or pt == "tpl" or pt == "re"
          or (pt == "name" and not REGEX_AFTER[tv])
          or (pt == "p" and (tv == ")" or tv == "]" or tv == "}" or tv == "++" or tv == "--"))
        if c == 47 and not divide then
          local p, incls = pos + 1, false
          while true do
            local d = sbyte(src, p)
            if not d or d == 10 then lerr("Invalid regular expression: missing /", line) end
            if d == 92 then p = p + 2
            else
              if d == 91 then incls = true elseif d == 93 then incls = false
              elseif d == 47 and not incls then break end
              p = p + 1
            end
          end
          local body = ssub(src, pos + 1, p - 1)
          local fs, fe = sfind(src, "^[%a]*", p + 1)
          push("re", { body = body, flags = ssub(src, fs, fe) })
          pos = fe + 1
        else
          local tok
          local s4, s3, s2 = ssub(src, pos, pos + 3), ssub(src, pos, pos + 2), ssub(src, pos, pos + 1)
          if P4[s4] then tok = s4 elseif P3[s3] then tok = s3 elseif P2[s2] then tok = s2 else tok = ch end
          if tok == "?." and sfind(ssub(src, pos + 2, pos + 2), "%d") then tok = "?" end
          if not sfind("{}()[];,<>+-*/%&|^!~?:=.@", tok, 1, true) and #tok == 1 then
            lerr("Invalid or unexpected token", line)
          end
          if tplmode then
            if tok == "{" then depth = depth + 1
            elseif tok == "}" then
              if depth == 0 then
                push("eof")
                return { T = T, V = V, L = L, N = N, n = n }, pos + 1, line
              end
              depth = depth - 1
            end
          end
          push("p", tok)
          pos = pos + #tok
        end
      end
    end
    push("eof")
    return { T = T, V = V, L = L, N = N, n = n }, pos, line
  end

  ---------------------------------------------------------------- parser
  local RESERVED = {}
  for w in sgmatch("break case catch class const continue debugger default delete do else export extends finally for "
    .. "function if import in instanceof new return super switch this throw try typeof var void while with null true false enum", "%a+") do
    RESERVED[w] = true
  end
  local BINPREC = {
    ["??"] = 1, ["||"] = 2, ["&&"] = 3, ["|"] = 4, ["^"] = 5, ["&"] = 6,
    ["=="] = 7, ["!="] = 7, ["==="] = 7, ["!=="] = 7,
    ["<"] = 8, [">"] = 8, ["<="] = 8, [">="] = 8, ["instanceof"] = 8, ["in"] = 8,
    ["<<"] = 9, [">>"] = 9, [">>>"] = 9, ["+"] = 10, ["-"] = 10, ["*"] = 11, ["/"] = 11, ["%"] = 11, ["**"] = 12,
  }
  local ASSIGNOPS = {}
  for w in sgmatch("= += -= *= /= %= **= <<= >>= >>>= &= |= ^= &&= ||= ??=", "%S+") do ASSIGNOPS[w] = true end

  local T, V, L, N, pi          -- current token arrays and position
  local fc, bc                  -- function context, block context
  local fncount, depth, noin = 0, 0, false

  local function perr(msg, line) error({ syntax = msg, line = line or L[pi] }, 0) end
  local function unexpected()
    local t, v = T[pi], V[pi]
    if t == "eof" then perr("Unexpected end of input") end
    if t == "str" then perr("Unexpected string") end
    if t == "num" then perr("Unexpected number") end
    if t == "tpl" then perr("Unexpected template string") end
    if t == "name" and not RESERVED[v] then perr("Unexpected identifier '" .. v .. "'") end
    perr("Unexpected token '" .. tostring(type(v) == "string" and v or t) .. "'")
  end
  local function is_p(v) return T[pi] == "p" and V[pi] == v end
  local function is_kw(v) return T[pi] == "name" and V[pi] == v end
  local function eat_p(v)
    if T[pi] == "p" and V[pi] == v then pi = pi + 1; return true end
    return false
  end
  local function expect_p(v)
    if T[pi] == "p" and V[pi] == v then pi = pi + 1; return end
    unexpected()
  end
  local function eat_kw(v)
    if T[pi] == "name" and V[pi] == v then pi = pi + 1; return true end
    return false
  end
  local function semicolon()
    if T[pi] == "p" and V[pi] == ";" then pi = pi + 1; return end
    if (T[pi] == "p" and V[pi] == "}") or T[pi] == "eof" or N[pi] then return end
    unexpected()
  end
  local function enter()
    depth = depth + 1
    if depth > MAX_DEPTH then perr("Nesting too deep") end
  end

  local function declare_var(name)
    if not fc.varset[name] then
      fc.varset[name] = true
      fc.vars[#fc.vars + 1] = name
    end
  end

  local function pattern_names(pat, out)
    local t = pat.t
    if t == "Ident" then out[#out + 1] = pat.name
    elseif t == "ArrPat" then
      for _, e in ipairs(pat.elems) do if e then pattern_names(e.target, out) end end
    elseif t == "ObjPat" then
      for _, pr in ipairs(pat.props) do pattern_names(pr.target, out) end
      if pat.rest then pattern_names(pat.rest, out) end
    end
    return out
  end

  local parse_assign, parse_expr, parse_statement, parse_block_body, parse_function, parse_class, parse_unary
  local parse_binding, parse_primary

  local function ident_name()
    local t, v = T[pi], V[pi]
    if t ~= "name" or RESERVED[v] then unexpected() end
    if v == "await" or v == "yield" then perr(v .. " is not supported") end
    pi = pi + 1
    return v
  end

  -- binding pattern: identifier, [..] or {..}
  function parse_binding()
    if is_p("[") then
      pi = pi + 1
      local elems = {}
      while not is_p("]") do
        if is_p(",") then pi = pi + 1; elems[#elems + 1] = false
        else
          local e
          if eat_p("...") then e = { target = parse_binding(), rest = true }
          else
            e = { target = parse_binding() }
            if eat_p("=") then e.def = parse_assign() end
          end
          elems[#elems + 1] = e
          if not is_p("]") then expect_p(",") end
        end
      end
      pi = pi + 1
      return { t = "ArrPat", elems = elems }
    elseif is_p("{") then
      pi = pi + 1
      local props, rest = {}, nil
      while not is_p("}") do
        if eat_p("...") then rest = parse_binding()
        else
          local pr = {}
          local tk, tv = T[pi], V[pi]
          if tk == "p" and tv == "[" then
            pi = pi + 1
            pr.ckey = parse_assign()
            expect_p("]")
          elseif tk == "name" or tk == "str" then pr.key = tv; pi = pi + 1
          elseif tk == "num" then pr.key = numstr(tv); pi = pi + 1
          else unexpected() end
          if eat_p(":") then pr.target = parse_binding()
          else
            if tk ~= "name" then unexpected() end
            pr.target = { t = "Ident", name = tv, line = L[pi - 1] }
          end
          if eat_p("=") then pr.def = parse_assign() end
          props[#props + 1] = pr
        end
        if not is_p("}") then expect_p(",") end
      end
      pi = pi + 1
      return { t = "ObjPat", props = props, rest = rest }
    end
    local line = L[pi]
    return { t = "Ident", name = ident_name(), line = line }
  end

  local function to_target(e)
    local t = e.t
    if t == "Ident" or t == "Member" then return e end
    if t == "ArrayLit" then
      local elems = {}
      for i, x in ipairs(e.elems) do
        if not x then elems[i] = false
        elseif x.t == "Spread" then elems[i] = { target = to_target(x.e), rest = true }
        elseif x.t == "Assign" and x.op == "=" then elems[i] = { target = x.target, def = x.value }
        else elems[i] = { target = to_target(x) } end
      end
      return { t = "ArrPat", elems = elems }
    elseif t == "ObjectLit" then
      local props, rest = {}, nil
      for _, pr in ipairs(e.props) do
        if pr.kind == "spread" then rest = to_target(pr.value)
        elseif pr.kind ~= "init" then perr("Invalid destructuring assignment target")
        else
          local v = pr.value
          local np = { key = pr.key, ckey = pr.ckey }
          if pr.short then np.target = v; np.def = pr.def
          elseif v.t == "Assign" and v.op == "=" then np.target = v.target; np.def = v.value
          else np.target = to_target(v) end
          props[#props + 1] = np
        end
      end
      return { t = "ObjPat", props = props, rest = rest }
    elseif t == "ArrPat" or t == "ObjPat" then return e
    end
    perr("Invalid left-hand side in assignment")
  end

  -- Matching close bracket of every open bracket, computed once per token list.
  local closecache = setmetatable({}, { __mode = "k" })
  local function find_close(i)
    local cl = closecache[T]
    if not cl then
      cl = {}
      local stack, sp = {}, 0
      for j = 1, #T do
        if T[j] == "p" then
          local v = V[j]
          if v == "(" or v == "[" or v == "{" then
            sp = sp + 1
            stack[sp] = j
          elseif (v == ")" or v == "]" or v == "}") and sp > 0 then
            cl[stack[sp]] = j
            sp = sp - 1
          end
        end
      end
      closecache[T] = cl
    end
    return cl[i]
  end

  local function new_fc(arrow)
    local f = { vars = {}, varset = {}, arrow = arrow, up = fc }
    fc = f
    return f
  end

  local function parse_params()
    expect_p("(")
    local params, nreq = {}, 0
    while not is_p(")") do
      if eat_p("...") then
        params[#params + 1] = { target = parse_binding(), rest = true }
        break
      end
      local e = { target = parse_binding() }
      if eat_p("=") then e.def = parse_assign() else nreq = nreq + 1 end
      params[#params + 1] = e
      if not is_p(")") then expect_p(",") end
    end
    expect_p(")")
    return params, nreq
  end

  -- Parses `{ body }` as a function body. Returns stmts, lex, funcs.
  local function parse_fn_body()
    expect_p("{")
    local saveb, savenoin = bc, noin
    noin = false
    bc = { lex = {}, funcs = {}, fnlevel = true }
    local body = {}
    while not is_p("}") do
      if T[pi] == "eof" then unexpected() end
      body[#body + 1] = parse_statement()
    end
    pi = pi + 1
    local lex, funcs = bc.lex, bc.funcs
    bc, noin = saveb, savenoin
    local first = body[1]
    local strict = first and first.t == "ExprStmt" and first.e.t == "Str" and first.e.v == "use strict"
    return body, lex, funcs, strict
  end

  -- kind: "normal" | "arrow" | "method" | "get" | "set" | "ctor"
  function parse_function(name, kind, params, nreq, line)
    fncount = fncount + 1
    local node = { t = "Func", name = name, kind = kind, line = line }
    local savef = fc
    local f = new_fc(kind == "arrow")
    node.params, node.nreq = params, nreq
    if not params then
      node.params, node.nreq = parse_params()
    end
    if kind == "arrow" then
      expect_p("=>")
      if is_p("{") then
        node.body, node.lex, node.funcs, node.strict = parse_fn_body()
      else
        local savenoin = noin
        node.expr = parse_assign()
        noin = savenoin
      end
    else
      node.body, node.lex, node.funcs, node.strict = parse_fn_body()
    end
    node.vars, node.usesArgs = f.vars, f.usesArgs
    fc = savef
    return node
  end

  local function parse_args()
    expect_p("(")
    local args = {}
    local savenoin = noin
    noin = false
    while not is_p(")") do
      if eat_p("...") then args[#args + 1] = { t = "Spread", e = parse_assign() }
      else args[#args + 1] = parse_assign() end
      if not is_p(")") then expect_p(",") end
    end
    pi = pi + 1
    noin = savenoin
    return args
  end

  local function parse_prop_key()
    local t, v = T[pi], V[pi]
    if t == "p" and v == "[" then
      pi = pi + 1
      local e = parse_assign()
      expect_p("]")
      return nil, e
    elseif t == "name" or t == "str" then pi = pi + 1; return v
    elseif t == "num" then pi = pi + 1; return numstr(v)
    end
    unexpected()
  end

  local function parse_object()
    expect_p("{")
    local props = {}
    local savenoin = noin
    noin = false
    while not is_p("}") do
      local pr
      local line = L[pi]
      if eat_p("...") then pr = { kind = "spread", value = parse_assign() }
      else
        local t, v = T[pi], V[pi]
        local nt, nv = T[pi + 1], V[pi + 1]
        if t == "name" and (v == "get" or v == "set") and not (nt == "p" and (nv == "," or nv == ":" or nv == "(" or nv == "}" or nv == "=")) then
          pi = pi + 1
          local key, ckey = parse_prop_key()
          local fn = parse_function(key, v, nil, nil, line)
          pr = { kind = v, key = key, ckey = ckey, value = fn }
        elseif t == "name" and v == "async" and not (nt == "p" and (nv == "," or nv == ":" or nv == "(" or nv == "}")) then
          perr("async functions are not supported")
        elseif t == "p" and v == "*" then perr("generators are not supported")
        else
          local key, ckey = parse_prop_key()
          if is_p(":") then
            pi = pi + 1
            pr = { kind = "init", key = key, ckey = ckey, value = parse_assign() }
          elseif is_p("(") then
            local fn = parse_function(key, "method", nil, nil, line)
            pr = { kind = "init", key = key, ckey = ckey, value = fn, method = true }
          else
            if t ~= "name" or RESERVED[v] then unexpected() end
            pr = { kind = "init", key = key, value = { t = "Ident", name = v, line = line }, short = true }
            if v == "arguments" then
              local f = fc
              while f.arrow and f.up do f = f.up end
              f.usesArgs = true
            end
            if is_p("=") then pi = pi + 1; pr.def = parse_assign() end
          end
        end
      end
      props[#props + 1] = pr
      if not is_p("}") then expect_p(",") end
    end
    pi = pi + 1
    noin = savenoin
    return { t = "ObjectLit", props = props }
  end

  local function parse_template(tk)
    local exprs = {}
    for i, sub in ipairs(tk.exprs) do
      local sT, sV, sL, sN, spi = T, V, L, N, pi
      T, V, L, N, pi = sub.T, sub.V, sub.L, sub.N, 1
      local savenoin = noin
      noin = false
      exprs[i] = parse_expr()
      if T[pi] ~= "eof" then unexpected() end
      noin = savenoin
      T, V, L, N, pi = sT, sV, sL, sN, spi
    end
    return { t = "Template", strs = tk.strs, exprs = exprs }
  end

  function parse_class(isdecl)
    local line = L[pi]
    pi = pi + 1
    local name
    if T[pi] == "name" and not RESERVED[V[pi]] then name = V[pi]; pi = pi + 1 end
    if isdecl and not name then unexpected() end
    local super
    if eat_kw("extends") then super = parse_unary() end
    expect_p("{")
    local members = {}
    local ctor
    while not is_p("}") do
      if eat_p(";") then goto continue end
      do
        local static = false
        local mline = L[pi]
        if is_kw("static") and not (T[pi + 1] == "p" and (V[pi + 1] == "(" or V[pi + 1] == "=")) then
          pi = pi + 1
          static = true
          if is_p("{") then perr("static blocks are not supported") end
        end
        local kind = "method"
        local t, v = T[pi], V[pi]
        local nt, nv = T[pi + 1], V[pi + 1]
        if t == "name" and (v == "get" or v == "set") and not (nt == "p" and (nv == "(" or nv == "=" or nv == ";" or nv == "}")) then
          kind = v
          pi = pi + 1
        elseif t == "name" and v == "async" and not (nt == "p" and (nv == "(" or nv == "=" or nv == ";")) then
          perr("async functions are not supported")
        elseif t == "p" and v == "*" then perr("generators are not supported")
        end
        local key, ckey = parse_prop_key()
        if is_p("(") then
          local isctor = (not static and key == "constructor" and kind == "method")
          local fn = parse_function(key, isctor and "ctor" or kind, nil, nil, mline)
          if isctor then ctor = fn
          else members[#members + 1] = { kind = kind, static = static, key = key, ckey = ckey, value = fn } end
        else
          -- a field
          local init
          if eat_p("=") then
            fncount = fncount + 1
            local savef = fc
            local f = new_fc(false)
            local e = parse_assign()
            init = { t = "Func", kind = "method", params = {}, nreq = 0, expr = e, vars = f.vars, line = mline, name = key }
            fc = savef
          end
          semicolon()
          members[#members + 1] = { kind = "field", static = static, key = key, ckey = ckey, value = init }
        end
      end
      ::continue::
    end
    pi = pi + 1
    fncount = fncount + 1
    return { t = "Class", name = name, super = super, ctor = ctor, members = members, line = line }
  end

  function parse_primary()
    local t, v = T[pi], V[pi]
    local line = L[pi]
    if t == "num" then pi = pi + 1; return { t = "Num", v = v }
    elseif t == "str" then pi = pi + 1; return { t = "Str", v = v }
    elseif t == "tpl" then pi = pi + 1; return parse_template(v)
    elseif t == "re" then pi = pi + 1; return { t = "Regex", body = v.body, flags = v.flags, line = line }
    elseif t == "name" then
      if v == "function" then
        pi = pi + 1
        if is_p("*") then perr("generators are not supported") end
        local name
        if T[pi] == "name" and not RESERVED[V[pi]] then name = V[pi]; pi = pi + 1 end
        return parse_function(name, "normal", nil, nil, line)
      elseif v == "class" then return parse_class(false)
      elseif v == "this" then pi = pi + 1; return { t = "This", line = line }
      elseif v == "null" then pi = pi + 1; return { t = "Null" }
      elseif v == "true" then pi = pi + 1; return { t = "Bool", v = true }
      elseif v == "false" then pi = pi + 1; return { t = "Bool", v = false }
      elseif v == "super" then pi = pi + 1; return { t = "Super", line = line }
      elseif v == "async" and (T[pi + 1] == "name" and V[pi + 1] == "function" or (T[pi + 1] == "p" and V[pi + 1] == "(" and not N[pi + 1])) then
        local close = (T[pi + 1] == "p" and V[pi + 1] == "(") and find_close(pi + 1)
        if V[pi + 1] == "function" or (close and T[close + 1] == "p" and V[close + 1] == "=>") then
          perr("async functions are not supported")
        end
      elseif v == "async" and T[pi + 1] == "name" and T[pi + 2] == "p" and V[pi + 2] == "=>" then
        perr("async functions are not supported")
      elseif v == "await" then perr("await is not supported")
      elseif v == "yield" then perr("yield is not supported")
      elseif v == "import" or v == "export" then perr("Cannot use import statement outside a module")
      elseif v == "new" then unexpected()
      end
      if RESERVED[v] then unexpected() end
      pi = pi + 1
      if v == "arguments" then
        local f = fc
        while f.arrow and f.up do f = f.up end
        f.usesArgs = true
      end
      return { t = "Ident", name = v, line = line }
    elseif t == "p" then
      if v == "(" then
        pi = pi + 1
        local savenoin = noin
        noin = false
        local e = parse_expr()
        noin = savenoin
        expect_p(")")
        return e
      elseif v == "[" then
        pi = pi + 1
        local elems = {}
        local savenoin = noin
        noin = false
        while not is_p("]") do
          if is_p(",") then pi = pi + 1; elems[#elems + 1] = false
          else
            if eat_p("...") then elems[#elems + 1] = { t = "Spread", e = parse_assign() }
            else elems[#elems + 1] = parse_assign() end
            if not is_p("]") then expect_p(",") end
          end
        end
        pi = pi + 1
        noin = savenoin
        return { t = "ArrayLit", elems = elems }
      elseif v == "{" then return parse_object()
      end
    end
    unexpected()
  end

  local function parse_new()
    local line = L[pi]
    pi = pi + 1
    if is_p(".") then perr("new.target is not supported") end
    local callee
    if is_kw("new") then callee = parse_new() else callee = parse_primary() end
    while true do
      if is_p(".") then
        pi = pi + 1
        if T[pi] ~= "name" then unexpected() end
        callee = { t = "Member", obj = callee, prop = V[pi], line = L[pi] }
        pi = pi + 1
      elseif is_p("[") then
        pi = pi + 1
        local k = parse_expr()
        expect_p("]")
        callee = { t = "Member", obj = callee, key = k, line = line }
      else break end
    end
    local args = {}
    if is_p("(") then args = parse_args() end
    return { t = "New", callee = callee, args = args, line = line }
  end

  local function parse_lhs()
    local e
    if is_kw("new") then e = parse_new() else e = parse_primary() end
    local inchain, anyopt = false, false
    while true do
      local t, v = T[pi], V[pi]
      local line = L[pi]
      if t == "p" then
        if v == "." then
          pi = pi + 1
          if T[pi] ~= "name" then unexpected() end
          local name = V[pi]
          pi = pi + 1
          if e.t == "Super" then e = { t = "SuperMember", prop = name, line = line }
          else e = { t = "Member", obj = e, prop = name, line = line, ch = inchain or nil } end
        elseif v == "?." then
          pi = pi + 1
          inchain, anyopt = true, true
          if is_p("(") then
            e = { t = "Call", callee = e, args = parse_args(), line = line, opt = true, ch = true }
          elseif is_p("[") then
            pi = pi + 1
            local k = parse_expr()
            expect_p("]")
            e = { t = "Member", obj = e, key = k, line = line, opt = true, ch = true }
          else
            if T[pi] ~= "name" then unexpected() end
            e = { t = "Member", obj = e, prop = V[pi], line = line, opt = true, ch = true }
            pi = pi + 1
          end
        elseif v == "[" then
          pi = pi + 1
          local savenoin = noin
          noin = false
          local k = parse_expr()
          noin = savenoin
          expect_p("]")
          if e.t == "Super" then e = { t = "SuperMember", key = k, line = line }
          else e = { t = "Member", obj = e, key = k, line = line, ch = inchain or nil } end
        elseif v == "(" then
          local args = parse_args()
          if e.t == "Super" then e = { t = "SuperCall", args = args, line = line }
          else e = { t = "Call", callee = e, args = args, line = line, ch = inchain or nil } end
        else break end
      elseif t == "tpl" then perr("Tagged templates are not supported")
      else break end
    end
    if anyopt then e = { t = "OptChain", e = e } end
    return e
  end

  function parse_unary()
    local t, v = T[pi], V[pi]
    local line = L[pi]
    if t == "p" then
      if v == "!" or v == "-" or v == "+" or v == "~" then
        pi = pi + 1
        enter()
        local e = parse_unary()
        depth = depth - 1
        if v == "-" and e.t == "Num" then return { t = "Num", v = -e.v } end
        return { t = "Unary", op = v, e = e }
      elseif v == "++" or v == "--" then
        pi = pi + 1
        enter()
        local e = parse_unary()
        depth = depth - 1
        if e.t == "Paren" then e = e.e end
        if e.t ~= "Ident" and e.t ~= "Member" then perr("Invalid left-hand side expression in prefix operation") end
        return { t = "Update", op = v, prefix = true, target = e, line = line }
      end
    elseif t == "name" then
      if v == "typeof" or v == "void" or v == "delete" then
        pi = pi + 1
        enter()
        local e = parse_unary()
        depth = depth - 1
        return { t = "Unary", op = v, e = e, line = line }
      elseif v == "await" then perr("await is not supported")
      end
    end
    local e = parse_lhs()
    if T[pi] == "p" and (V[pi] == "++" or V[pi] == "--") and not N[pi] then
      local op = V[pi]
      pi = pi + 1
      if e.t == "Paren" then e = e.e end
      if e.t ~= "Ident" and e.t ~= "Member" then perr("Invalid left-hand side expression in postfix operation") end
      return { t = "Update", op = op, prefix = false, target = e, line = line }
    end
    return e
  end

  local function parse_binary(minprec)
    local left = parse_unary()
    while true do
      local t, v = T[pi], V[pi]
      local prec
      if t == "p" then prec = BINPREC[v]
      elseif t == "name" and (v == "instanceof" or (v == "in" and not noin)) then prec = BINPREC[v] end
      if not prec or prec < minprec then break end
      local line = L[pi]
      pi = pi + 1
      enter()
      local right = parse_binary(v == "**" and prec or prec + 1)
      depth = depth - 1
      if v == "&&" or v == "||" or v == "??" then left = { t = "Logical", op = v, l = left, r = right }
      else left = { t = "Bin", op = v, l = left, r = right, line = line } end
    end
    return left
  end

  local function parse_cond()
    local test = parse_binary(1)
    if is_p("?") then
      pi = pi + 1
      local savenoin = noin
      noin = false
      local a = parse_assign()
      noin = savenoin
      expect_p(":")
      local b = parse_assign()
      return { t = "Cond", test = test, a = a, b = b }
    end
    return test
  end

  function parse_assign()
    enter()
    local t, v = T[pi], V[pi]
    local line = L[pi]
    local res
    if t == "name" and not RESERVED[v] and T[pi + 1] == "p" and V[pi + 1] == "=>" and not N[pi + 1] then
      if v == "async" or v == "await" then perr(v .. " is not supported") end
      pi = pi + 1
      res = parse_function(nil, "arrow", { { target = { t = "Ident", name = v, line = line } } }, 1, line)
    elseif t == "p" and v == "(" then
      local close = find_close(pi)
      if close and T[close + 1] == "p" and V[close + 1] == "=>" then
        res = parse_function(nil, "arrow", nil, nil, line)
      end
    end
    if not res then
      local left = parse_cond()
      if T[pi] == "p" and ASSIGNOPS[V[pi]] then
        local op = V[pi]
        pi = pi + 1
        if left.t == "Paren" then left = left.e end
        local target
        if op == "=" then target = to_target(left)
        else
          if left.t ~= "Ident" and left.t ~= "Member" then perr("Invalid left-hand side in assignment") end
          target = left
        end
        local value = parse_assign()
        res = { t = "Assign", op = op, target = target, value = value, line = line }
      else
        res = left
      end
    end
    depth = depth - 1
    return res
  end

  function parse_expr()
    local e = parse_assign()
    if is_p(",") then
      local list = { e }
      while eat_p(",") do list[#list + 1] = parse_assign() end
      return { t = "Seq", list = list }
    end
    return e
  end

  ------------------------------------------------------------ statements
  local function parse_var_decl(kind, isfor)
    local decls = {}
    repeat
      local line = L[pi]
      local target = parse_binding()
      local init
      if eat_p("=") then init = parse_assign()
      elseif kind == "const" and not isfor then perr("Missing initializer in const declaration") end
      decls[#decls + 1] = { target = target, init = init, line = line }
      local names = pattern_names(target, {})
      for _, nm in ipairs(names) do
        if kind == "var" then declare_var(nm)
        else bc.lex[#bc.lex + 1] = { name = nm, const = (kind == "const") } end
      end
    until not eat_p(",")
    return { t = "VarDecl", kind = kind, decls = decls }
  end

  local function parse_block_stmt()
    local startfn = fncount
    expect_p("{")
    local saveb = bc
    bc = { lex = {}, funcs = {} }
    local body = {}
    while not is_p("}") do
      if T[pi] == "eof" then unexpected() end
      body[#body + 1] = parse_statement()
    end
    pi = pi + 1
    local node = { t = "Block", body = body, lex = bc.lex, funcs = bc.funcs, hasfn = fncount ~= startfn }
    bc = saveb
    return node
  end

  local function parse_for()
    local line = L[pi]
    pi = pi + 1
    if is_kw("await") then perr("for await is not supported") end
    expect_p("(")
    local startfn = fncount
    local saveb, savenoin = bc, noin
    bc = { lex = {}, funcs = {} }
    local init, kind, target
    local node
    if is_p(";") then
      -- no init
    elseif (is_kw("var") or is_kw("const") or (is_kw("let") and (T[pi + 1] == "name" or (T[pi + 1] == "p" and (V[pi + 1] == "[" or V[pi + 1] == "{"))))) then
      kind = V[pi]
      pi = pi + 1
      noin = true
      -- single declarator followed by in/of ?
      local save = pi
      local tg = parse_binding()
      if is_kw("of") or is_kw("in") then
        target = tg
        local names = pattern_names(tg, {})
        for _, nm in ipairs(names) do
          if kind == "var" then declare_var(nm) else bc.lex[#bc.lex + 1] = { name = nm, const = (kind == "const") } end
        end
      else
        pi = save
        init = parse_var_decl(kind, true)
      end
      noin = savenoin
    else
      noin = true
      local e = parse_expr()
      noin = savenoin
      if is_kw("of") or is_kw("in") then target = to_target(e.t == "Paren" and e.e or e) else init = { t = "ExprStmt", e = e } end
    end
    if target then
      local isof = is_kw("of")
      pi = pi + 1
      local iter = isof and parse_assign() or parse_expr()
      expect_p(")")
      enter()
      local body = parse_statement()
      depth = depth - 1
      node = { t = isof and "ForOf" or "ForIn", kind = kind, target = target, iter = iter, body = body, lex = bc.lex,
        hasfn = fncount ~= startfn, line = line }
    else
      expect_p(";")
      local test, update
      if not is_p(";") then test = parse_expr() end
      expect_p(";")
      if not is_p(")") then update = parse_expr() end
      expect_p(")")
      enter()
      local body = parse_statement()
      depth = depth - 1
      node = { t = "For", init = init, test = test, update = update, body = body, lex = bc.lex,
        hasfn = fncount ~= startfn, line = line }
    end
    bc, noin = saveb, savenoin
    return node
  end

  function parse_statement()
    enter()
    local t, v = T[pi], V[pi]
    local line = L[pi]
    local res
    if t == "p" then
      if v == "{" then res = parse_block_stmt()
      elseif v == ";" then pi = pi + 1; res = { t = "Empty" } end
    elseif t == "name" then
      if v == "var" or v == "const" or (v == "let" and (T[pi + 1] == "name" or (T[pi + 1] == "p" and (V[pi + 1] == "[" or V[pi + 1] == "{")))) then
        pi = pi + 1
        res = parse_var_decl(v, false)
        semicolon()
      elseif v == "function" then
        pi = pi + 1
        if is_p("*") then perr("generators are not supported") end
        if T[pi] ~= "name" then unexpected() end
        local name = V[pi]
        pi = pi + 1
        local fn = parse_function(name, "normal", nil, nil, line)
        -- a function declared in a block is also a var of the function (web compatibility)
        if not bc.fnlevel then declare_var(name) end
        bc.funcs[#bc.funcs + 1] = fn
        res = { t = "Empty" }
      elseif v == "async" and T[pi + 1] == "name" and V[pi + 1] == "function" then perr("async functions are not supported")
      elseif v == "class" then
        local cls = parse_class(true)
        bc.lex[#bc.lex + 1] = { name = cls.name, const = false }
        res = { t = "ClassDecl", name = cls.name, cls = cls }
      elseif v == "if" then
        pi = pi + 1
        expect_p("(")
        local test = parse_expr()
        expect_p(")")
        local a = parse_statement()
        local b
        if eat_kw("else") then b = parse_statement() end
        res = { t = "If", test = test, a = a, b = b }
      elseif v == "for" then res = parse_for()
      elseif v == "while" then
        pi = pi + 1
        expect_p("(")
        local test = parse_expr()
        expect_p(")")
        res = { t = "While", test = test, body = parse_statement() }
      elseif v == "do" then
        pi = pi + 1
        local body = parse_statement()
        if not eat_kw("while") then unexpected() end
        expect_p("(")
        local test = parse_expr()
        expect_p(")")
        eat_p(";")
        res = { t = "DoWhile", test = test, body = body }
      elseif v == "return" then
        pi = pi + 1
        local e
        if not (is_p(";") or is_p("}") or T[pi] == "eof" or N[pi]) then e = parse_expr() end
        semicolon()
        res = { t = "Return", e = e }
      elseif v == "break" or v == "continue" then
        pi = pi + 1
        local label
        if T[pi] == "name" and not N[pi] and not RESERVED[V[pi]] then label = V[pi]; pi = pi + 1 end
        semicolon()
        res = { t = v == "break" and "Break" or "Continue", label = label }
      elseif v == "throw" then
        pi = pi + 1
        if N[pi] then perr("Illegal newline after throw") end
        local e = parse_expr()
        semicolon()
        res = { t = "Throw", e = e, line = line }
      elseif v == "try" then
        pi = pi + 1
        local block = parse_block_stmt()
        local node = { t = "Try", block = block }
        if eat_kw("catch") then
          local startfn = fncount
          if eat_p("(") then
            node.param = parse_binding()
            expect_p(")")
          end
          node.handler = parse_block_stmt()
          node.hasfn = fncount ~= startfn
        end
        if eat_kw("finally") then node.finalizer = parse_block_stmt() end
        if not node.handler and not node.finalizer then perr("Missing catch or finally after try") end
        res = node
      elseif v == "switch" then
        pi = pi + 1
        expect_p("(")
        local disc = parse_expr()
        expect_p(")")
        expect_p("{")
        local startfn = fncount
        local saveb = bc
        bc = { lex = {}, funcs = {} }
        local cases = {}
        while not is_p("}") do
          local c = { body = {} }
          if eat_kw("case") then c.test = parse_expr()
          elseif eat_kw("default") then c.default = true
          else unexpected() end
          expect_p(":")
          while not (is_p("}") or is_kw("case") or is_kw("default")) do
            if T[pi] == "eof" then unexpected() end
            c.body[#c.body + 1] = parse_statement()
          end
          cases[#cases + 1] = c
        end
        pi = pi + 1
        res = { t = "Switch", disc = disc, cases = cases, lex = bc.lex, funcs = bc.funcs, hasfn = fncount ~= startfn }
        bc = saveb
      elseif v == "debugger" then pi = pi + 1; semicolon(); res = { t = "Empty" }
      elseif v == "with" then perr("with is not supported")
      elseif v == "import" and not (T[pi + 1] == "p" and (V[pi + 1] == "(" or V[pi + 1] == ".")) then
        perr("Cannot use import statement outside a module")
      elseif v == "export" then perr("Unexpected token 'export'")
      elseif not RESERVED[v] and T[pi + 1] == "p" and V[pi + 1] == ":" then
        pi = pi + 2
        res = { t = "Labeled", label = v, body = parse_statement() }
      end
    end
    if not res then
      local e = parse_expr()
      semicolon()
      res = { t = "ExprStmt", e = e, line = line }
    end
    res.line = res.line or line
    depth = depth - 1
    return res
  end

  function parse_program(src)
    if #src > MAX_SRC then error({ syntax = "Source too large", line = 1 }, 0) end
    tdepth = 0
    local tk = tokenize(src, 1, 1, false)
    T, V, L, N, pi = tk.T, tk.V, tk.L, tk.N, 1
    fncount, depth, noin = 0, 0, false
    fc = { vars = {}, varset = {}, script = true }
    bc = { lex = {}, funcs = {}, top = true, fnlevel = true }
    local body = {}
    while T[pi] ~= "eof" do body[#body + 1] = parse_statement() end
    local first = body[1]
    local prog = { t = "Program", body = body, lex = bc.lex, funcs = bc.funcs, vars = fc.vars,
      strict = first and first.t == "ExprStmt" and first.e.t == "Str" and first.e.v == "use strict" }
    T, V, L, N, fc, bc = nil, nil, nil, nil, nil, nil
    return prog
  end
end

---------------------------------------------------------------------------
-- 4. Compiler: AST to Lua closures
---------------------------------------------------------------------------
-- Every expression becomes `function(s) return value end`. `s` is the scope
-- table of the nearest scope that holds variables: s[1] is the parent scope,
-- the other slots are variables (slot 2 = this, slot 3 = the function object).
-- Every statement becomes `function(s) return code, value end`. The code is
-- nil (normal), BRK, CNT or RET. This avoids pcall for control flow.
local RET, BRK, CNT = 3, 1, 2
local new_regexp, compile_program, err_value, def_acc   -- set in sections 4 and 5
do

local function stepfail() fatal("script too slow (step limit)") end

function err_value(e)
  local mt = getmetatable(e)
  if mt == Throw then return e.value end
  if type(e) == "string" then
    if sfind(e, "stack overflow", 1, true) then return make_error("RangeError", "Maximum call stack size exceeded") end
    if sfind(e, "not enough memory", 1, true) then fatal("out of memory") end
    return make_error("Error", (sgsub(e, "^[^\n]-:%d+: ", "")))
  end
  return make_error("Error", tostring(e))
end

function def_acc(o, key, g, s, enumerable)
  local acc = o.acc
  if not acc then acc = {}; o.acc = acc end
  local a = acc[key]
  if not a then
    a = {}
    if o.p[key] ~= nil then o.p[key] = nil
    elseif enumerable then
      local k = o.k
      if k then k[#k + 1] = key else o.k = { key } end
    end
    acc[key] = a
  end
  if g ~= nil then a.g = g end
  if s ~= nil then a.s = s end
  return a
end

-- Compile-time scopes ---------------------------------------------------------
local function new_scope(parent, mat, first)
  local c = { vars = {}, up = parent, mat = mat }
  if mat then
    c.home = c
    c.uphome = parent and parent.home
    c.next = first or 2
  else
    c.home = parent.home
  end
  c.fn = parent and parent.fn
  return c
end

local function declare(cs, name, const)
  local h = cs.home
  local slot = h.next
  h.next = slot + 1
  cs.vars[name] = { slot = slot, home = h, const = const }
  return slot
end

local function resolve(cs, name)
  local c = cs
  while c do
    local info = c.vars[name]
    if info then
      local d, h = 0, cs.home
      while h ~= info.home do h = h.uphome; d = d + 1 end
      return d, info.slot, info
    end
    c = c.up
  end
end

local function mk_get(d, slot)
  if d == 0 then return function(s) return s[slot] end
  elseif d == 1 then return function(s) return s[1][slot] end
  elseif d == 2 then return function(s) return s[1][1][slot] end
  elseif d == 3 then return function(s) return s[1][1][1][slot] end
  end
  return function(s)
    for _ = 1, d do s = s[1] end
    return s[slot]
  end
end

local function mk_set(d, slot)
  if d == 0 then return function(s, v) s[slot] = v end
  elseif d == 1 then return function(s, v) s[1][slot] = v end
  elseif d == 2 then return function(s, v) s[1][1][slot] = v end
  end
  return function(s, v)
    for _ = 1, d do s = s[1] end
    s[slot] = v
  end
end

local cx, cstmt, cstmts, ccond, compile_function, compile_class, compile_target
local gconst = {}          -- top-level const names of the script that is compiled now
local EX, ST = {}, {}

function cx(node, cs)
  local f = EX[node.t]
  if not f then error("compile: unsupported node " .. tostring(node.t)) end
  return f(node, cs)
end

local COMPARE = { ["<"] = true, [">"] = true, ["<="] = true, [">="] = true, ["=="] = true, ["!="] = true,
  ["==="] = true, ["!=="] = true, ["instanceof"] = true, ["in"] = true }
function ccond(node, cs)
  local f = cx(node, cs)
  local t = node.t
  if (t == "Bin" and COMPARE[node.op]) or (t == "Unary" and node.op == "!") or t == "Bool" then return f end
  return function(s)
    local v = f(s)
    if v == nil or v == false or v == null or v == 0 or v == "" or v ~= v then return false end
    return true
  end
end

local function ctext(node)
  local t = node.t
  if t == "Ident" then return node.name
  elseif t == "This" then return "this"
  elseif t == "Member" then
    if node.prop then return ctext(node.obj) .. "." .. node.prop end
    return ctext(node.obj) .. "[...]"
  elseif t == "Call" then return ctext(node.callee) .. "(...)"
  elseif t == "SuperMember" then return "(intermediate value)." .. (node.prop or "[...]")
  end
  return "(intermediate value)"
end

local function notfn(text) throw_error("TypeError", text .. " is not a function") end

-- Globals ----------------------------------------------------------------------
local function global_get(name, line)
  if has_prop(G, name) then return getp(G, name, G) end
  curline = line or curline
  throw_error("ReferenceError", name .. " is not defined")
end

-- Literals ----------------------------------------------------------------------
EX.Num = function(n) local v = n.v; return function() return v end end
EX.Str = function(n) local v = n.v; return function() return v end end
EX.Bool = function(n) local v = n.v; return function() return v end end
EX.Null = function() return function() return null end end

EX.Ident = function(n, cs)
  local name, line = n.name, n.line
  local d, slot = resolve(cs, name)
  if d then return mk_get(d, slot) end
  if name == "undefined" then return function() return nil end end
  local Gp = G.p
  return function()
    local v = Gp[name]
    if v ~= nil then
      if v == UNDEF then return nil end
      return v
    end
    return global_get(name, line)
  end
end

EX.This = function(n, cs)
  local d, slot, info = resolve(cs, "this")
  if not d then return function() return G end end
  local get = mk_get(d, slot)
  if info.strict then return get end
  -- sloppy functions see the global object when called without a receiver
  return function(s)
    local v = get(s)
    if v == nil or v == null then return G end
    return v
  end
end

EX.Template = function(n, cs)
  local strs, fs = n.strs, {}
  for i, e in ipairs(n.exprs) do fs[i] = cx(e, cs) end
  local cnt = #fs
  if cnt == 1 then
    local a, b, f = strs[1], strs[2], fs[1]
    return function(s) return concat(a .. to_str(f(s)), b) end
  end
  return function(s)
    local buf = { strs[1] }
    for i = 1, cnt do
      buf[2 * i] = to_str(fs[i](s))
      buf[2 * i + 1] = strs[i + 1]
    end
    return concat(tconcat(buf), "")
  end
end

EX.Regex = function(n)
  local prog
  local body, flags = n.body, n.flags
  return function()
    if not prog then prog = regex_compile(body, flags) end
    return new_regexp(prog, body, flags)
  end
end

EX.ArrayLit = function(n, cs)
  local fs, spread, any = {}, {}, false
  for i, e in ipairs(n.elems) do
    if not e then fs[i] = function() return nil end
    elseif e.t == "Spread" then fs[i] = cx(e.e, cs); spread[i] = true; any = true
    else fs[i] = cx(e, cs) end
  end
  local cnt = #n.elems
  if not any then
    return function(s)
      local a = {}
      for i = 1, cnt do a[i] = fs[i](s) end
      return new_arr(a, cnt)
    end
  end
  return function(s)
    local out, k = {}, 0
    for i = 1, cnt do
      if spread[i] then
        local l, ln = iter_list(fs[i](s))
        array_check_len(k + ln)
        for j = 1, ln do out[k + j] = l[j] end
        k = k + ln
      else
        k = k + 1
        out[k] = fs[i](s)
      end
    end
    return new_arr(out, k)
  end
end

local function copy_props(o, src)
  if type(src) == "string" then
    local i = 0
    for ch in sgmatch(src, UCHAR) do setown(o, numstr(i), ch); i = i + 1 end
  elseif is_obj(src) then
    local ks, kn = own_keys(src)
    for i = 1, kn do setown(o, ks[i], getv(src, ks[i])) end
  end
end

local function infer_name(valnode, name)
  if (valnode.t == "Func" or valnode.t == "Class") and not valnode.name and not valnode.iname then
    valnode.iname = name
  end
end

EX.ObjectLit = function(n, cs)
  local props = {}
  for i, pr in ipairs(n.props) do
    local c = { kind = pr.kind, key = pr.key, method = pr.method }
    if pr.ckey then c.ckey = cx(pr.ckey, cs) end
    if pr.kind == "init" and pr.short and pr.def then
      local f, d = cx(pr.value, cs), cx(pr.def, cs)
      c.value = function(s) local v = f(s); if v == nil then v = d(s) end; return v end
    else
      if pr.key and pr.kind == "init" then infer_name(pr.value, pr.key) end
      c.value = cx(pr.value, cs)
    end
    props[i] = c
  end
  local cnt = #props
  return function(s)
    local o = { p = {}, proto = ObjectProto, cls = "Object" }
    for i = 1, cnt do
      local c = props[i]
      local kind = c.kind
      if kind == "init" then
        local key = c.key
        if c.ckey then key = to_key(c.ckey(s)) end
        local v = c.value(s)
        if c.method then v.home = o end
        if key == "__proto__" and not c.ckey and not c.method then
          if v == null then o.proto = nil elseif is_obj(v) then o.proto = v end
        else
          if type(key) == "number" then key = numstr(key) end
          setown(o, key, v)
        end
      elseif kind == "spread" then
        copy_props(o, c.value(s))
      else
        local key = c.key
        if c.ckey then key = to_key(c.ckey(s)) end
        if type(key) == "number" then key = numstr(key) end
        local f = c.value(s)
        f.home = o
        if kind == "get" then def_acc(o, key, f, nil, true) else def_acc(o, key, nil, f, true) end
      end
    end
    return o
  end
end

-- Members and calls -------------------------------------------------------------
EX.Member = function(n, cs)
  local of = cx(n.obj, cs)
  local line = n.line
  if n.ch or n.opt then
    local opt = n.opt
    local kf = n.key and cx(n.key, cs)
    local key = n.prop
    return function(s)
      local o = of(s)
      if o == SHORT then return SHORT end
      if opt and (o == nil or o == null) then return SHORT end
      local k = key
      if kf then k = to_key(kf(s)) end
      curline = line
      return getv(o, k)
    end
  end
  if n.prop then
    local key = n.prop
    if key == "length" then
      return function(s)
        local o = of(s)
        if type(o) == "string" then return #o + 0.0 end
        if type(o) == "table" and o.a then return o.len + 0.0 end
        curline = line
        return getv(o, key)
      end
    end
    return function(s)
      local o = of(s)
      if type(o) == "table" then
        local p = o.p
        if p then
          local v = p[key]
          if v ~= nil then
            if v == UNDEF then return nil end
            return v
          end
        end
      end
      curline = line
      return getv(o, key)
    end
  end
  local kf = cx(n.key, cs)
  return function(s)
    local o, k = of(s), kf(s)
    if type(o) == "table" and type(k) == "number" then
      local a = o.a
      if a then
        local v = a[k + 1]
        if v ~= nil then return v end
      end
    end
    curline = line
    local tk = type(k)
    if tk ~= "number" and tk ~= "string" then k = to_key(k) end
    return getv(o, k)
  end
end

EX.OptChain = function(n, cs)
  local f = cx(n.e, cs)
  return function(s)
    local v = f(s)
    if v == SHORT then return nil end
    return v
  end
end

-- Compile a list of argument nodes. Returns closures, has_spread.
local function cargs(args, cs)
  local fs, spread, any = {}, {}, false
  for i, a in ipairs(args) do
    if a.t == "Spread" then fs[i] = cx(a.e, cs); spread[i] = true; any = true
    else fs[i] = cx(a, cs) end
  end
  return fs, any and spread
end

local function eval_args(fs, spread)
  local cnt = #fs
  if not spread then
    return function(s)
      local out = {}
      for i = 1, cnt do out[i] = fs[i](s) end
      return out, cnt
    end
  end
  return function(s)
    local out, k = {}, 0
    for i = 1, cnt do
      if spread[i] then
        local l, ln = iter_list(fs[i](s))
        array_check_len(k + ln)
        for j = 1, ln do out[k + j] = l[j] end
        k = k + ln
      else
        k = k + 1
        out[k] = fs[i](s)
      end
    end
    return out, k
  end
end

-- Returns a closure f(s) -> function, this for the callee of a call.
local function ccallee(callee, cs)
  local t = callee.t
  if t == "Member" and not (callee.ch or callee.opt) then
    local of = cx(callee.obj, cs)
    local line = callee.line
    if callee.prop then
      local key = callee.prop
      return function(s)
        local o = of(s)
        local f
        if type(o) == "table" then
          local p = o.p
          if p then
            f = p[key]
            if f == nil then
              local pr = o.proto
              if pr then
                f = pr.p[key]
                if f == UNDEF then f = nil end
              end
            elseif f == UNDEF then f = nil end
          end
        end
        if f == nil then
          curline = line
          f = getv(o, key)
        end
        return f, o
      end
    end
    local kf = cx(callee.key, cs)
    return function(s)
      local o, k = of(s), kf(s)
      curline = line
      local tk = type(k)
      if tk ~= "number" and tk ~= "string" then k = to_key(k) end
      return getv(o, k), o
    end
  elseif t == "SuperMember" then
    local fnget = cx({ t = "Ident", name = "%fn" }, cs)
    local thisf = cx({ t = "This" }, cs)
    local key, kf = callee.prop, callee.key and cx(callee.key, cs)
    return function(s)
      local home = fnget(s).home
      local this = thisf(s)
      local k = key
      if kf then k = to_key(kf(s)) end
      return getp(home.proto, k, this), this
    end
  elseif t == "Member" then
    local of = cx(callee.obj, cs)
    local kf = callee.key and cx(callee.key, cs)
    local key, opt = callee.prop, callee.opt
    return function(s)
      local o = of(s)
      if o == SHORT then return SHORT end
      if opt and (o == nil or o == null) then return SHORT end
      local k = key
      if kf then k = to_key(kf(s)) end
      return getv(o, k), o
    end
  end
  return cx(callee, cs)
end

EX.Call = function(n, cs)
  local cf = ccallee(n.callee, cs)
  local fs, spread = cargs(n.args, cs)
  local line, text, cnt = n.line, ctext(n.callee), #fs
  if n.ch or n.opt then
    local ea = eval_args(fs, spread)
    local opt = n.opt
    return function(s)
      local f, o = cf(s)
      if f == SHORT then return SHORT end
      if opt and (f == nil or f == null) then return SHORT end
      curline = line
      if type(f) == "table" and f.fn then
        local list, k = ea(s)
        return (f.fn(o, tunpack(list, 1, k)))
      end
      notfn(text)
    end
  end
  if not spread then
    if cnt == 0 then
      return function(s)
        local f, o = cf(s)
        curline = line
        if type(f) == "table" then
          local fn = f.fn
          if fn then return (fn(o)) end
        end
        notfn(text)
      end
    elseif cnt == 1 then
      local a1 = fs[1]
      return function(s)
        local f, o = cf(s)
        curline = line
        if type(f) == "table" then
          local fn = f.fn
          if fn then return (fn(o, a1(s))) end
        end
        notfn(text)
      end
    elseif cnt == 2 then
      local a1, a2 = fs[1], fs[2]
      return function(s)
        local f, o = cf(s)
        curline = line
        if type(f) == "table" then
          local fn = f.fn
          if fn then
            local x = a1(s)
            return (fn(o, x, a2(s)))
          end
        end
        notfn(text)
      end
    elseif cnt == 3 then
      local a1, a2, a3 = fs[1], fs[2], fs[3]
      return function(s)
        local f, o = cf(s)
        curline = line
        if type(f) == "table" then
          local fn = f.fn
          if fn then
            local x = a1(s)
            local y = a2(s)
            return (fn(o, x, y, a3(s)))
          end
        end
        notfn(text)
      end
    end
  end
  local ea = eval_args(fs, spread)
  return function(s)
    local f, o = cf(s)
    curline = line
    if type(f) == "table" then
      local fn = f.fn
      if fn then
        local list, k = ea(s)
        return (fn(o, tunpack(list, 1, k)))
      end
    end
    notfn(text)
  end
end

EX.New = function(n, cs)
  local cf = cx(n.callee, cs)
  local ea = eval_args(cargs(n.args, cs))
  local line, text = n.line, ctext(n.callee)
  return function(s)
    local F = cf(s)
    local list, k = ea(s)
    curline = line
    if type(F) ~= "table" or not F.fn or F.noctor then throw_error("TypeError", text .. " is not a constructor") end
    return construct(F, F, tunpack(list, 1, k))
  end
end

EX.SuperCall = function(n, cs)
  local fnget = cx({ t = "Ident", name = "%fn" }, cs)
  local ntd, ntslot = resolve(cs, "%nt")
  if not ntd then error({ syntax = "'super' keyword unexpected here", line = n.line }, 0) end
  local ntget = mk_get(ntd, ntslot)
  local thisd, thisslot = resolve(cs, "this")
  local thisget, thisset = mk_get(thisd, thisslot), mk_set(thisd, thisslot)
  local ea = eval_args(cargs(n.args, cs))
  local line = n.line
  return function(s)
    local F = fnget(s)
    local list, k = ea(s)
    curline = line
    if thisget(s) ~= nil then throw_error("ReferenceError", "Super constructor may only be called once") end
    local parent = F.proto
    if type(parent) ~= "table" or not parent.fn then throw_error("TypeError", "Super constructor is not a constructor") end
    local obj = construct(parent, ntget(s), tunpack(list, 1, k))
    thisset(s, obj)
    if F.fields then init_fields(F, obj) end
    return obj
  end
end

EX.SuperMember = function(n, cs)
  local fnget = cx({ t = "Ident", name = "%fn" }, cs)
  local thisf = cx({ t = "This" }, cs)
  local key, kf = n.prop, n.key and cx(n.key, cs)
  return function(s)
    local home = fnget(s).home
    local k = key
    if kf then k = to_key(kf(s)) end
    if type(k) == "number" then k = numstr(k) end
    return getp(home.proto, k, thisf(s))
  end
end

-- Operators -----------------------------------------------------------------------
local BINF = {
  ["+"] = add,
  ["-"] = function(a, b)
    if type(a) == "number" and type(b) == "number" then return a - b end
    return to_num(a) - to_num(b)
  end,
  ["*"] = function(a, b)
    if type(a) == "number" and type(b) == "number" then return a * b end
    return to_num(a) * to_num(b)
  end,
  ["/"] = function(a, b)
    if type(a) == "number" and type(b) == "number" then return a / b end
    return to_num(a) / to_num(b)
  end,
  ["%"] = function(a, b) return jsmod(to_num(a), to_num(b)) end,
  ["**"] = function(a, b) return pow(to_num(a), to_num(b)) end,
  ["<"] = function(a, b)
    if type(a) == "number" and type(b) == "number" then return a < b end
    return lt(a, b)
  end,
  [">"] = function(a, b)
    if type(a) == "number" and type(b) == "number" then return a > b end
    return lt(b, a)
  end,
  ["<="] = function(a, b)
    if type(a) == "number" and type(b) == "number" then return a <= b end
    return le(a, b)
  end,
  [">="] = function(a, b)
    if type(a) == "number" and type(b) == "number" then return a >= b end
    return le(b, a)
  end,
  ["=="] = loose_eq,
  ["!="] = function(a, b) return not loose_eq(a, b) end,
  ["==="] = function(a, b) return a == b end,
  ["!=="] = function(a, b) return a ~= b end,
  ["&"] = function(a, b) return (to_int32(to_num(a)) & to_int32(to_num(b))) + 0.0 end,
  ["|"] = function(a, b) return (to_int32(to_num(a)) | to_int32(to_num(b))) + 0.0 end,
  ["^"] = function(a, b) return (to_int32(to_num(a)) ~ to_int32(to_num(b))) + 0.0 end,
  ["<<"] = function(a, b)
    local r = (to_int32(to_num(a)) << (to_int32(to_num(b)) & 31)) & 0xFFFFFFFF
    if r >= 0x80000000 then r = r - 0x100000000 end
    return r + 0.0
  end,
  [">>"] = function(a, b) return (to_int32(to_num(a)) // (1 << (to_int32(to_num(b)) & 31))) + 0.0 end,
  [">>>"] = function(a, b) return ((to_int32(to_num(a)) & 0xFFFFFFFF) >> (to_int32(to_num(b)) & 31)) + 0.0 end,
  ["instanceof"] = instance_of,
  ["in"] = function(a, b)
    if not is_obj(b) then throw_error("TypeError", "Cannot use 'in' operator to search for a key in a non-object") end
    return has_prop(b, to_key(a))
  end,
}

EX.Bin = function(n, cs)
  local op = n.op
  local l, r = cx(n.l, cs), cx(n.r, cs)
  local f = BINF[op]
  local line = n.line
  local rc = n.r.t == "Num" and n.r.v
  if op == "+" then
    if rc then
      return function(s)
        local a = l(s)
        if type(a) == "number" then return a + rc end
        return add(a, rc)
      end
    end
    return function(s)
      local a = l(s)
      local b = r(s)
      if type(a) == "number" and type(b) == "number" then return a + b end
      return add(a, b)
    end
  elseif op == "-" then
    if rc then
      return function(s)
        local a = l(s)
        if type(a) == "number" then return a - rc end
        return to_num(a) - rc
      end
    end
  elseif op == "<" then
    if rc then
      return function(s)
        local a = l(s)
        if type(a) == "number" then return a < rc end
        return lt(a, rc)
      end
    end
  elseif op == "===" then
    return function(s)
      local a = l(s)
      return a == r(s)
    end
  elseif op == "!==" then
    return function(s)
      local a = l(s)
      return a ~= r(s)
    end
  end
  return function(s)
    local a = l(s)
    local b = r(s)
    curline = line
    return f(a, b)
  end
end

EX.Logical = function(n, cs)
  local l, r, op = cx(n.l, cs), cx(n.r, cs), n.op
  if op == "&&" then
    return function(s)
      local a = l(s)
      if a == nil or a == false or a == null or a == 0 or a == "" or a ~= a then return a end
      return r(s)
    end
  elseif op == "||" then
    return function(s)
      local a = l(s)
      if a == nil or a == false or a == null or a == 0 or a == "" or a ~= a then return r(s) end
      return a
    end
  end
  return function(s)
    local a = l(s)
    if a == nil or a == null then return r(s) end
    return a
  end
end

EX.Cond = function(n, cs)
  local t, a, b = ccond(n.test, cs), cx(n.a, cs), cx(n.b, cs)
  return function(s)
    if t(s) then return a(s) end
    return b(s)
  end
end

EX.Seq = function(n, cs)
  local fs = {}
  for i, e in ipairs(n.list) do fs[i] = cx(e, cs) end
  local cnt = #fs
  return function(s)
    for i = 1, cnt - 1 do fs[i](s) end
    return fs[cnt](s)
  end
end

EX.Unary = function(n, cs)
  local op = n.op
  local node = n.e
  if op == "typeof" then
    if node.t == "Ident" and not resolve(cs, node.name) and node.name ~= "undefined" then
      local name, Gp = node.name, G.p
      return function()
        local v = Gp[name]
        if v == nil then
          if not has_prop(G, name) then return "undefined" end
          v = getp(G, name, G)
        elseif v == UNDEF then v = nil end
        return typeof(v)
      end
    end
    local f = cx(node, cs)
    return function(s) return typeof(f(s)) end
  elseif op == "delete" then
    if node.t == "Member" then
      local of = cx(node.obj, cs)
      local key = node.prop
      local kf = node.key and cx(node.key, cs)
      return function(s)
        local o = of(s)
        local k = key
        if kf then k = to_key(kf(s)) end
        return del_prop(o, k)
      end
    end
    local f = cx(node, cs)
    return function(s) f(s); return node.t ~= "Ident" end
  end
  local f = cx(node, cs)
  if op == "!" then
    return function(s)
      local v = f(s)
      if v == nil or v == false or v == null or v == 0 or v == "" or v ~= v then return true end
      return false
    end
  elseif op == "-" then
    return function(s)
      local v = f(s)
      if type(v) == "number" then return -v end
      return -to_num(v)
    end
  elseif op == "+" then
    return function(s) return to_num(f(s)) end
  elseif op == "~" then
    return function(s) return (~to_int32(to_num(f(s)))) + 0.0 end
  end
  return function(s) f(s); return nil end -- void
end

-- Assignment targets --------------------------------------------------------------
-- compile_target returns set(s, v). `isdecl` skips the const check.
function compile_target(node, cs, isdecl)
  local t = node.t
  if t == "Ident" then
    local name = node.name
    local d, slot, info = resolve(cs, name)
    if d then
      if info.const and not isdecl then
        return function() throw_error("TypeError", "Assignment to constant variable.") end
      end
      return mk_set(d, slot)
    end
    if not isdecl and gconst[name] then
      return function() throw_error("TypeError", "Assignment to constant variable.") end
    end
    return function(s, v) setv(G, name, v) end
  elseif t == "Member" then
    local of = cx(node.obj, cs)
    local key = node.prop
    local kf = node.key and cx(node.key, cs)
    return function(s, v)
      local o = of(s)
      local k = key
      if kf then k = to_key(kf(s)) end
      setv(o, k, v)
    end
  elseif t == "ArrPat" then
    local elems = {}
    for i, e in ipairs(node.elems) do
      if e then
        elems[i] = { set = compile_target(e.target, cs, isdecl), def = e.def and cx(e.def, cs), rest = e.rest }
      else
        elems[i] = false
      end
    end
    local cnt = #node.elems
    return function(s, v)
      local list, n
      if type(v) == "table" and v.cls == "Array" then list, n = v.a, v.len else list, n = iter_list(v) end
      for i = 1, cnt do
        local e = elems[i]
        if e then
          if e.rest then
            local rest = {}
            for j = i, n do rest[j - i + 1] = list[j] end
            e.set(s, new_arr(rest, n >= i and n - i + 1 or 0))
          else
            local x = list[i]
            if x == nil and e.def then x = e.def(s) end
            e.set(s, x)
          end
        end
      end
    end
  elseif t == "ObjPat" then
    local props = {}
    for i, pr in ipairs(node.props) do
      props[i] = { key = pr.key, ckey = pr.ckey and cx(pr.ckey, cs), set = compile_target(pr.target, cs, isdecl),
        def = pr.def and cx(pr.def, cs) }
    end
    local rest = node.rest and compile_target(node.rest, cs, isdecl)
    return function(s, v)
      if v == nil or v == null then
        throw_error("TypeError", "Cannot destructure '" .. to_str(v) .. "' as it is " .. to_str(v) .. ".")
      end
      local used = rest and {}
      for i = 1, #props do
        local pr = props[i]
        local key = pr.key
        if pr.ckey then key = to_key(pr.ckey(s)) end
        local x = getv(v, key)
        if x == nil and pr.def then x = pr.def(s) end
        if used then used[to_str(key)] = true end
        pr.set(s, x)
      end
      if rest then
        local o = new_obj(ObjectProto)
        if is_obj(v) then
          local ks, kn = own_keys(v)
          for i = 1, kn do
            if not used[ks[i]] then setown(o, ks[i], getv(v, ks[i])) end
          end
        end
        rest(s, o)
      end
    end
  end
  error({ syntax = "Invalid assignment target", line = node.line }, 0)
end

local OPFOR = { ["+="] = "+", ["-="] = "-", ["*="] = "*", ["/="] = "/", ["%="] = "%", ["**="] = "**", ["<<="] = "<<",
  [">>="] = ">>", [">>>="] = ">>>", ["&="] = "&", ["|="] = "|", ["^="] = "^" }

EX.Assign = function(n, cs)
  local op, target = n.op, n.target
  if op == "=" then
    if target.t == "Ident" then infer_name(n.value, target.name) end
    local vf = cx(n.value, cs)
    if target.t == "Ident" then
      local d, slot, info = resolve(cs, target.name)
      if d and d == 0 and not info.const then
        return function(s)
          local v = vf(s)
          s[slot] = v
          return v
        end
      end
      local set = compile_target(target, cs)
      return function(s)
        local v = vf(s)
        set(s, v)
        return v
      end
    elseif target.t == "Member" then
      local of = cx(target.obj, cs)
      local key = target.prop
      local kf = target.key and cx(target.key, cs)
      local line = target.line
      if key then
        return function(s)
          local o = of(s)
          local v = vf(s)
          if type(o) == "table" then
            local p = o.p
            if p then
              local cur = p[key]
              if cur ~= nil and not o.frozen and not o.ro then
                p[key] = (v == nil) and UNDEF or v
                return v
              end
            end
          end
          curline = line
          setv(o, key, v)
          return v
        end
      end
      return function(s)
        local o = of(s)
        local k = kf(s)
        local v = vf(s)
        curline = line
        if type(o) == "table" and type(k) == "number" then
          local a = o.a
          if a and k >= 0 and k < o.len and k % 1 == 0 and not o.frozen then
            a[k + 1] = v
            return v
          end
        end
        local tk = type(k)
        if tk ~= "number" and tk ~= "string" then k = to_key(k) end
        setv(o, k, v)
        return v
      end
    end
    local set = compile_target(target, cs)
    return function(s)
      local v = vf(s)
      set(s, v)
      return v
    end
  end
  -- compound and logical assignment
  local vf = cx(n.value, cs)
  local logical = (op == "&&=" or op == "||=" or op == "??=")
  local f = BINF[OPFOR[op]]
  local line = n.line
  local function combine(old, s)
    -- returns new value, or SHORT when a logical form keeps the old value
    if logical then
      if op == "&&=" then
        if to_bool(old) then return vf(s) end
      elseif op == "||=" then
        if not to_bool(old) then return vf(s) end
      elseif old == nil or old == null then return vf(s) end
      return SHORT
    end
    curline = line
    return f(old, vf(s))
  end
  if target.t == "Ident" then
    local get, set = cx(target, cs), compile_target(target, cs)
    return function(s)
      local old = get(s)
      local nv = combine(old, s)
      if nv == SHORT then return old end
      set(s, nv)
      return nv
    end
  end
  local of = cx(target.obj, cs)
  local key = target.prop
  local kf = target.key and cx(target.key, cs)
  return function(s)
    local o = of(s)
    local k = key
    if kf then k = to_key(kf(s)) end
    curline = line
    local old = getv(o, k)
    local nv = combine(old, s)
    if nv == SHORT then return old end
    setv(o, k, nv)
    return nv
  end
end

EX.Update = function(n, cs)
  local target, delta, prefix = n.target, (n.op == "++") and 1 or -1, n.prefix
  if target.t == "Ident" then
    local d, slot, info = resolve(cs, target.name)
    if d == 0 and not info.const then
      return function(s)
        local v = s[slot]
        if type(v) ~= "number" then v = to_num(v) end
        s[slot] = v + delta
        if prefix then return v + delta end
        return v
      end
    end
    local get, set = cx(target, cs), compile_target(target, cs)
    return function(s)
      local v = get(s)
      if type(v) ~= "number" then v = to_num(v) end
      set(s, v + delta)
      if prefix then return v + delta end
      return v
    end
  end
  local of = cx(target.obj, cs)
  local key = target.prop
  local kf = target.key and cx(target.key, cs)
  local line = n.line
  return function(s)
    local o = of(s)
    local k = key
    if kf then k = to_key(kf(s)) end
    curline = line
    local v = getv(o, k)
    if type(v) ~= "number" then v = to_num(v) end
    setv(o, k, v + delta)
    if prefix then return v + delta end
    return v
  end
end

-- Functions ---------------------------------------------------------------------------
-- Slot layout of a function scope: 1 = parent scope, 2 = this, 3 = function object,
-- then the parameters in order, then other variables.
local function collect_names(params)
  local out = {}
  local function walk(pat)
    if pat.t == "Ident" then out[#out + 1] = pat.name
    elseif pat.t == "ArrPat" then for _, e in ipairs(pat.elems) do if e then walk(e.target) end end
    elseif pat.t == "ObjPat" then
      for _, pr in ipairs(pat.props) do walk(pr.target) end
      if pat.rest then walk(pat.rest) end
    end
  end
  for _, prm in ipairs(params) do walk(prm.target) end
  return out
end

local function depth_fail() throw_error("RangeError", "Maximum call stack size exceeded") end

-- Returns maker(s) -> function object.
function compile_function(node, cs, isdecl, classinfo)
  local kind = node.kind
  local fcs = new_scope(cs, true, 4)
  fcs.fn = fcs
  fcs.isfn = true
  fcs.arrow = (kind == "arrow")
  local strict = node.strict
  local sc = cs
  while sc and not strict do
    strict = sc.strict
    sc = sc.up
  end
  fcs.strict = strict
  fcs.vars["this"] = { slot = 2, home = fcs, strict = strict }
  fcs.vars["%fn"] = { slot = 3, home = fcs }
  if kind == "arrow" then fcs.vars["this"] = nil; fcs.vars["%fn"] = nil end
  local isclass = (kind == "ctor")
  local derived = isclass and classinfo and classinfo.derived
  local params = node.params
  local simple = #params <= 3 and not node.usesArgs and not isclass
  for _, prm in ipairs(params) do
    if prm.rest or prm.def or prm.target.t ~= "Ident" then simple = false end
  end
  -- declare parameter names first (slots 4, 5, ...)
  local pnames = collect_names(params)
  for _, nm in ipairs(pnames) do declare(fcs, nm) end
  local argslot
  if node.usesArgs and kind ~= "arrow" then argslot = declare(fcs, "arguments") end
  local ntslot
  if derived then ntslot = declare(fcs, "%nt") end
  local selfslot
  if node.name and not isdecl and kind == "normal" and not fcs.vars[node.name] then selfslot = declare(fcs, node.name) end
  for _, nm in ipairs(node.vars or {}) do
    if not fcs.vars[nm] then declare(fcs, nm) end
  end
  for _, fnode in ipairs(node.funcs or {}) do
    if not fcs.vars[fnode.name] then declare(fcs, fnode.name) end
  end
  for _, lx in ipairs(node.lex or {}) do declare(fcs, lx.name, lx.const) end

  -- hoisted function declarations
  local hoist
  if node.funcs and #node.funcs > 0 then
    hoist = {}
    for i, fnode in ipairs(node.funcs) do
      hoist[i] = { slot = fcs.vars[fnode.name].slot, make = compile_function(fnode, fcs, true) }
    end
  end
  local nh = hoist and #hoist or 0

  local ex, body
  if node.expr then ex = cx(node.expr, fcs) else body = cstmts(node.body, fcs) end

  -- parameter binders for the general case
  local binders, nb = {}, #params
  if not simple then
    for i, prm in ipairs(params) do
      if prm.rest then
        local set = compile_target(prm.target, fcs, true)
        binders[i] = function(s, args, n)
          local rest, k = {}, 0
          for j = i, n do k = k + 1; rest[k] = args[j] end
          set(s, new_arr(rest, k))
        end
      else
        local set = compile_target(prm.target, fcs, true)
        local def = prm.def and cx(prm.def, fcs)
        binders[i] = function(s, args)
          local v = args[i]
          if v == nil and def then v = def(s) end
          set(s, v)
        end
      end
    end
  end

  local fname = node.name or node.iname or ""
  local nreq = node.nreq or 0
  local noctor = (kind == "arrow" or kind == "method" or kind == "get" or kind == "set")
  local lazy = (kind == "normal") or nil
  local home_ok = (kind == "method" or kind == "get" or kind == "set" or kind == "ctor")
  local np = #params

  local function run(s)
    -- the body of the function: returns the value
    if ex then return ex(s) end
    local c, v = body(s)
    if c == RET then return v end
  end

  local function factory(up, fobj)
    if simple and np <= 3 then
      if np == 0 then
        return function(this)
          local d = R.depth + 1
          if d > MAX_DEPTH then depth_fail() end
          local st = R.steps_left - 1
          R.steps_left = st
          if st < 0 then stepfail() end
          R.depth = d
          local s = { up, this, fobj }
          if selfslot then s[selfslot] = fobj end
          for i = 1, nh do local h = hoist[i]; s[h.slot] = h.make(s) end
          local v
          if ex then v = ex(s) else
            local c, rv = body(s)
            if c == RET then v = rv end
          end
          R.depth = d - 1
          return v
        end
      elseif np == 1 then
        return function(this, a)
          local d = R.depth + 1
          if d > MAX_DEPTH then depth_fail() end
          local st = R.steps_left - 1
          R.steps_left = st
          if st < 0 then stepfail() end
          R.depth = d
          local s = { up, this, fobj, a }
          if selfslot then s[selfslot] = fobj end
          for i = 1, nh do local h = hoist[i]; s[h.slot] = h.make(s) end
          local v
          if ex then v = ex(s) else
            local c, rv = body(s)
            if c == RET then v = rv end
          end
          R.depth = d - 1
          return v
        end
      elseif np == 2 then
        return function(this, a, b)
          local d = R.depth + 1
          if d > MAX_DEPTH then depth_fail() end
          local st = R.steps_left - 1
          R.steps_left = st
          if st < 0 then stepfail() end
          R.depth = d
          local s = { up, this, fobj, a, b }
          if selfslot then s[selfslot] = fobj end
          for i = 1, nh do local h = hoist[i]; s[h.slot] = h.make(s) end
          local v
          if ex then v = ex(s) else
            local c, rv = body(s)
            if c == RET then v = rv end
          end
          R.depth = d - 1
          return v
        end
      end
      return function(this, a, b, c3)
        local d = R.depth + 1
        if d > MAX_DEPTH then depth_fail() end
        local st = R.steps_left - 1
        R.steps_left = st
        if st < 0 then stepfail() end
        R.depth = d
        local s = { up, this, fobj, a, b, c3 }
        if selfslot then s[selfslot] = fobj end
        for i = 1, nh do local h = hoist[i]; s[h.slot] = h.make(s) end
        local v
        if ex then v = ex(s) else
          local c, rv = body(s)
          if c == RET then v = rv end
        end
        R.depth = d - 1
        return v
      end
    end
    -- general case
    return function(this, ...)
      local d = R.depth + 1
      if d > MAX_DEPTH then depth_fail() end
      local st = R.steps_left - 1
      R.steps_left = st
      if st < 0 then stepfail() end
      local nt
      if isclass then
        nt = R.nt
        R.nt = nil
        if not nt then throw_error("TypeError", "Class constructor " .. fname .. " cannot be invoked without 'new'") end
        if derived then this = nil end
      end
      R.depth = d
      local s = { up, this, fobj }
      local n = select("#", ...)
      local args = { ... }
      if ntslot then s[ntslot] = nt end
      if selfslot then s[selfslot] = fobj end
      if argslot then s[argslot] = { p = {}, proto = ObjectProto, cls = "Arguments", a = tmove(args, 1, n, 1, {}), len = n } end
      if simple then
        for i = 1, np do s[3 + i] = args[i] end
      else
        for i = 1, nb do binders[i](s, args, n) end
      end
      for i = 1, nh do local h = hoist[i]; s[h.slot] = h.make(s) end
      local v
      if ex then v = ex(s) else
        local c, rv = body(s)
        if c == RET then v = rv end
      end
      R.depth = d - 1
      if isclass then
        if type(v) == "table" and v.p then return v end
        if s[2] == nil then throw_error("ReferenceError", "Must call super constructor in derived class before accessing 'this' or returning from derived constructor") end
        return s[2]
      end
      return v
    end
  end

  return function(up)
    local fobj = { p = {}, proto = FunctionProto, cls = "Function", fname = fname, nargs = nreq, lazyproto = lazy,
      noctor = noctor or nil }
    fobj.isjs = true
    if isclass then
      fobj.isclass = true
      fobj.derived = derived or nil
      fobj.lazyproto = nil
    end
    fobj.fn = factory(up, fobj)
    return fobj
  end
end

-- Classes --------------------------------------------------------------------------------
function compile_class(node, cs)
  local ccs = new_scope(cs, true, 2)
  ccs.strict = true
  local nameslot
  if node.name then nameslot = declare(ccs, node.name, true) end
  local superf = node.super and cx(node.super, cs)
  local derived = node.super ~= nil
  local ctor_make
  if node.ctor then
    node.ctor.name = nil
    ctor_make = compile_function(node.ctor, ccs, true, { derived = derived })
  end
  local members = {}
  for i, m in ipairs(node.members) do
    local c = { kind = m.kind, static = m.static, key = m.key }
    if m.ckey then c.ckey = cx(m.ckey, ccs) end
    if m.value then
      if m.kind == "field" then
        m.value.iname = m.key
        c.make = compile_function(m.value, ccs, true)
      else
        c.make = compile_function(m.value, ccs, true)
      end
    end
    members[i] = c
  end
  local cname = node.name or node.iname or ""
  local line = node.line
  return function(s)
    local cls_s = { s }
    local parent
    local protoparent = ObjectProto
    if superf then
      parent = superf(s)
      if parent == null then protoparent = nil
      else
        if type(parent) ~= "table" or not parent.fn or parent.noctor then
          throw_error("TypeError", "Class extends value " .. describe(parent) .. " is not a constructor or null")
        end
        local pp = getv(parent, "prototype")
        if pp == null then protoparent = nil elseif is_obj(pp) then protoparent = pp else protoparent = ObjectProto end
      end
    end
    local F
    if ctor_make then
      F = ctor_make(cls_s)
      F.fname = cname
    else
      F = { p = {}, proto = FunctionProto, cls = "Function", fname = cname, nargs = 0, isclass = true, derived = derived or nil }
      if derived and parent ~= null then
        F.fn = function(this, ...)
          local nt = R.nt
          R.nt = nil
          if not nt then throw_error("TypeError", "Class constructor " .. cname .. " cannot be invoked without 'new'") end
          local obj = construct(F.proto, nt, ...)
          if F.fields then init_fields(F, obj) end
          return obj
        end
      else
        F.derived = nil
        F.fn = function()
          local nt = R.nt
          R.nt = nil
          if not nt then throw_error("TypeError", "Class constructor " .. cname .. " cannot be invoked without 'new'") end
        end
      end
    end
    if derived and parent ~= null then F.proto = parent end
    if derived and parent == null then F.derived = nil end
    local proto = new_obj(protoparent)
    proto.p.constructor = F
    F.p.prototype = proto
    F.home = proto
    if nameslot then cls_s[nameslot] = F end
    local fields, statics = nil, nil
    for i = 1, #members do
      local m = members[i]
      local key = m.key
      if m.ckey then key = to_key(m.ckey(cls_s)) end
      if type(key) == "number" then key = numstr(key) end
      local target = m.static and F or proto
      local kind = m.kind
      if kind == "field" then
        local init = m.make and m.make(cls_s)
        if init then init.home = target end
        if m.static then
          statics = statics or {}
          statics[#statics + 1] = { key = key, init = init }
        else
          fields = fields or {}
          fields[#fields + 1] = { key = key, init = init }
        end
      else
        local f = m.make(cls_s)
        f.home = target
        if kind == "method" then sethidden(target, key, f)
        elseif kind == "get" then def_acc(target, key, f, nil, false)
        else def_acc(target, key, nil, f, false) end
      end
    end
    if fields then
      -- inherit the field list of the parent only through construct (the parent runs its own)
      F.fields = fields
    end
    if statics then
      for i = 1, #statics do
        local st = statics[i]
        local v = st.init and st.init.fn(F)
        if type(st.key) == "string" and sbyte(st.key, 1) == 35 then sethidden(F, st.key, v) else setown(F, st.key, v) end
      end
    end
    return F
  end
end

EX.Class = function(n, cs) return compile_class(n, cs) end
EX.Func = function(n, cs) return compile_function(n, cs, false) end

-- Statements --------------------------------------------------------------------------------
function cstmt(node, cs, labels)
  local f = ST[node.t]
  if f then return f(node, cs, labels) end
  -- an expression node used as statement is not expected
  error("compile: unsupported statement " .. tostring(node.t))
end

local function seq_run(list)
  local n = #list
  if n == 0 then return function() end end
  if n == 1 then return list[1] end
  if n == 2 then
    local a, b = list[1], list[2]
    return function(s)
      local c, v = a(s)
      if c then return c, v end
      return b(s)
    end
  elseif n == 3 then
    local a, b, d = list[1], list[2], list[3]
    return function(s)
      local c, v = a(s)
      if c then return c, v end
      c, v = b(s)
      if c then return c, v end
      return d(s)
    end
  end
  return function(s)
    for i = 1, n do
      local c, v = list[i](s)
      if c then return c, v end
    end
  end
end

function cstmts(stmts, cs)
  local list = {}
  for _, st in ipairs(stmts) do
    if st.t ~= "Empty" then list[#list + 1] = cstmt(st, cs) end
  end
  return seq_run(list)
end

ST.Empty = function() return function() end end

ST.ExprStmt = function(n, cs)
  local f = cx(n.e, cs)
  if cs.fn and cs.fn.isscript then
    return function(s) R.last = f(s) end
  end
  return function(s) f(s) end
end

ST.VarDecl = function(n, cs)
  local list = {}
  for _, d in ipairs(n.decls) do
    local target = d.target
    if d.init then
      if target.t == "Ident" then infer_name(d.init, target.name) end
      local vf = cx(d.init, cs)
      local set = compile_target(target, cs, true)
      list[#list + 1] = function(s) set(s, vf(s)) end
    elseif n.kind ~= "var" then
      local set = compile_target(target, cs, true)
      list[#list + 1] = function(s) set(s, nil) end
    end
  end
  local cnt = #list
  if cnt == 1 then
    local f = list[1]
    return function(s) f(s) end
  end
  return function(s)
    for i = 1, cnt do list[i](s) end
  end
end

ST.ClassDecl = function(n, cs)
  local f = compile_class(n.cls, cs)
  local set = compile_target({ t = "Ident", name = n.name }, cs, true)
  return function(s) set(s, f(s)) end
end

ST.Return = function(n, cs)
  if not n.e then return function() return RET, nil end end
  local f = cx(n.e, cs)
  return function(s) return RET, f(s) end
end

ST.Break = function(n) local l = n.label; return function() return BRK, l end end
ST.Continue = function(n) local l = n.label; return function() return CNT, l end end

ST.Throw = function(n, cs)
  local f = cx(n.e, cs)
  local line = n.line
  return function(s)
    local v = f(s)
    curline = line
    throw(v)
  end
end

ST.If = function(n, cs)
  local t, a = ccond(n.test, cs), cstmt(n.a, cs)
  if n.b then
    local b = cstmt(n.b, cs)
    return function(s)
      if t(s) then return a(s) end
      return b(s)
    end
  end
  return function(s)
    if t(s) then return a(s) end
  end
end

-- Declares the lexical names and function declarations of a block.
local function declare_block(bcs, lex, funcs)
  for _, lx in ipairs(lex) do declare(bcs, lx.name, lx.const) end
  for _, fnode in ipairs(funcs) do
    if not bcs.vars[fnode.name] then declare(bcs, fnode.name) end
  end
end

-- Returns an initializer init(s) that creates the hoisted functions of a block.
local function hoist_funcs(funcs, bcs, mat, isblock)
  if #funcs == 0 then return nil end
  local list = {}
  for i, fnode in ipairs(funcs) do
    local make = compile_function(fnode, bcs, true)
    local d, slot = resolve(bcs, fnode.name)
    local outer
    if isblock then outer = compile_target({ t = "Ident", name = fnode.name }, bcs.up, true) end
    if d then
      list[i] = { set = mk_set(d, slot), make = make, outer = outer }
    else
      local name = fnode.name
      list[i] = { set = function(s, v) setv(G, name, v) end, make = make, outer = outer }
    end
  end
  return function(s)
    for i = 1, #list do
      local h = list[i]
      local f = h.make(s)
      h.set(s, f)
      if h.outer then h.outer(mat and s[1] or s, f) end
    end
  end
end

ST.Block = function(n, cs)
  local mat = n.hasfn and (#n.lex > 0 or #n.funcs > 0)
  local bcs = new_scope(cs, mat or false)
  declare_block(bcs, n.lex, n.funcs)
  local init = hoist_funcs(n.funcs, bcs, mat, true)
  local body = cstmts(n.body, bcs)
  if mat then
    return function(s)
      local ns = { s }
      if init then init(ns) end
      return body(ns)
    end
  end
  if init then
    return function(s)
      init(s)
      return body(s)
    end
  end
  return body
end

-- Loop completion handling: returns true to leave the loop, or code, value to pass up.
local function loop_exit(c, v, labels)
  -- returns action: 1 = break, 2 = continue, 3 = propagate
  if c == BRK then
    if v == nil or (labels and labels[v]) then return 1 end
    return 3
  elseif c == CNT then
    if v == nil or (labels and labels[v]) then return 2 end
    return 3
  end
  return 3
end

ST.While = function(n, cs, labels)
  local t, body, line = ccond(n.test, cs), cstmt(n.body, cs), n.line
  return function(s)
    while t(s) do
      curline = line
      local st = R.steps_left - 1
      R.steps_left = st
      if st < 0 then stepfail() end
      local c, v = body(s)
      if c then
        local a = loop_exit(c, v, labels)
        if a == 1 then break elseif a == 3 then return c, v end
      end
    end
  end
end

ST.DoWhile = function(n, cs, labels)
  local t, body, line = ccond(n.test, cs), cstmt(n.body, cs), n.line
  return function(s)
    repeat
      curline = line
      local st = R.steps_left - 1
      R.steps_left = st
      if st < 0 then stepfail() end
      local c, v = body(s)
      if c then
        local a = loop_exit(c, v, labels)
        if a == 1 then break elseif a == 3 then return c, v end
      end
    until not t(s)
  end
end

ST.For = function(n, cs, labels)
  local mat = n.hasfn and #n.lex > 0
  local lcs = new_scope(cs, mat or false)
  declare_block(lcs, n.lex, {})
  local init = n.init and cstmt(n.init, lcs)
  local test = n.test and ccond(n.test, lcs)
  local update = n.update and cx(n.update, lcs)
  local body = cstmt(n.body, lcs)
  local line = n.line
  if mat then
    local nslots
    return function(s)
      nslots = nslots or (lcs.next - 1)
      local ls = { s }
      if init then init(ls) end
      while true do
        if test and not test(ls) then break end
        curline = line
        local st = R.steps_left - 1
        R.steps_left = st
        if st < 0 then stepfail() end
        local c, v = body(ls)
        if c then
          local a = loop_exit(c, v, labels)
          if a == 1 then break elseif a == 3 then return c, v end
        end
        ls = tmove(ls, 1, nslots, 1, {})
        if update then update(ls) end
      end
    end
  end
  return function(s)
    if init then init(s) end
    while true do
      if test and not test(s) then break end
      curline = line
      local st = R.steps_left - 1
      R.steps_left = st
      if st < 0 then stepfail() end
      local c, v = body(s)
      if c then
        local a = loop_exit(c, v, labels)
        if a == 1 then break elseif a == 3 then return c, v end
      end
      if update then update(s) end
    end
  end
end

local function forin_keys(o)
  if o == nil or o == null then return {}, 0 end
  if type(o) == "string" then
    local out = {}
    for i = 1, #o do out[i] = numstr(i - 1) end
    return out, #o
  end
  if not is_obj(o) then return {}, 0 end
  local out, n, seen = {}, 0, {}
  local cur = o
  while cur do
    local ks, kn = own_keys(cur)
    for i = 1, kn do
      local k = ks[i]
      if not seen[k] then seen[k] = true; n = n + 1; out[n] = k end
    end
    cur = cur.proto
  end
  return out, n
end

local function compile_forx(n, cs, labels, isof)
  local mat = n.hasfn and #n.lex > 0
  local lcs = new_scope(cs, mat or false)
  declare_block(lcs, n.lex, {})
  local iterf = cx(n.iter, cs)
  local set = compile_target(n.target, lcs, true)
  local body = cstmt(n.body, lcs)
  local line = n.line
  return function(s)
    local iterable = iterf(s)
    local nextf, keys, kn, ki
    if isof then nextf = get_iter(iterable) else keys, kn = forin_keys(iterable); ki = 0 end
    while true do
      local x
      if isof then
        local ok
        ok, x = nextf()
        if not ok then break end
      else
        ki = ki + 1
        if ki > kn then break end
        x = keys[ki]
      end
      curline = line
      local st = R.steps_left - 1
      R.steps_left = st
      if st < 0 then stepfail() end
      local ns = s
      if mat then ns = { s } end
      set(ns, x)
      local c, v = body(ns)
      if c then
        local a = loop_exit(c, v, labels)
        if a == 1 then break elseif a == 3 then return c, v end
      end
    end
  end
end
ST.ForOf = function(n, cs, labels) return compile_forx(n, cs, labels, true) end
ST.ForIn = function(n, cs, labels) return compile_forx(n, cs, labels, false) end

ST.Labeled = function(n, cs, labels)
  local ls = {}
  if labels then for k in pairs(labels) do ls[k] = true end end
  ls[n.label] = true
  local label = n.label
  local body = cstmt(n.body, cs, ls)
  return function(s)
    local c, v = body(s)
    if c == BRK and v == label then return end
    return c, v
  end
end

ST.Switch = function(n, cs, labels)
  local mat = n.hasfn and (#n.lex > 0 or #n.funcs > 0)
  local scs = new_scope(cs, mat or false)
  declare_block(scs, n.lex, n.funcs)
  local init = hoist_funcs(n.funcs, scs, mat, true)
  local disc = cx(n.disc, cs)
  local tests, bodies, default = {}, {}, nil
  for i, c in ipairs(n.cases) do
    if c.test then tests[i] = cx(c.test, scs) else default = i end
    bodies[i] = cstmts(c.body, scs)
  end
  local cnt = #n.cases
  return function(s)
    local ns = s
    if mat then ns = { s } end
    if init then init(ns) end
    local d = disc(s)
    local start
    for i = 1, cnt do
      local t = tests[i]
      if t and t(ns) == d then start = i; break end
    end
    start = start or default
    if not start then return end
    for i = start, cnt do
      local c, v = bodies[i](ns)
      if c then
        if c == BRK and (v == nil or (labels and labels[v])) then return end
        return c, v
      end
    end
  end
end

ST.Try = function(n, cs)
  local tryb = cstmt(n.block, cs)
  local handler, catchrun, finalizer
  if n.handler then
    local mat = n.hasfn and n.param ~= nil
    local ccs = new_scope(cs, mat or false)
    local set
    if n.param then
      for _, nm in ipairs(collect_names({ { target = n.param } })) do declare(ccs, nm) end
      set = compile_target(n.param, ccs, true)
    end
    local hb = cstmt(n.handler, ccs)
    catchrun = function(s, val)
      local ns = s
      if mat then ns = { s } end
      if set then set(ns, val) end
      return hb(ns)
    end
    handler = true
  end
  if n.finalizer then finalizer = cstmt(n.finalizer, cs) end
  return function(s)
    local depth = R.depth
    local ok, c, v = pcall(tryb, s)
    local err
    if not ok then
      err = c
      if getmetatable(err) == Fatal then error(err, 0) end
      if handler then
        local val = err_value(err)
        R.depth = depth
        if finalizer then
          ok, c, v = pcall(catchrun, s, val)
          if not ok then
            err = c
            if getmetatable(err) == Fatal then error(err, 0) end
          end
        else
          return catchrun(s, val)
        end
      end
    end
    if finalizer then
      R.depth = depth
      local fc, fv = finalizer(s)
      if fc then return fc, fv end
    end
    if not ok then error(err, 0) end
    return c, v
  end
end

---------------------------------------------------------------------------
-- Program
---------------------------------------------------------------------------
function compile_program(ast)
  gconst = {}
  for _, lx in ipairs(ast.lex) do
    if lx.const then gconst[lx.name] = true end
  end
  local cs = new_scope(nil, true, 3)
  cs.fn = cs
  cs.isscript = true
  cs.strict = ast.strict
  cs.vars["this"] = { slot = 2, home = cs, strict = true }
  local funcs = ast.funcs
  local hoist = {}
  for i, fnode in ipairs(funcs) do
    hoist[i] = { name = fnode.name, make = compile_function(fnode, cs, true) }
  end
  local body = cstmts(ast.body, cs)
  local vars = ast.vars
  return function()
    local s = { nil, G }
    for i = 1, #vars do
      local nm = vars[i]
      if G.p[nm] == nil and not (G.acc and G.acc[nm]) then setown(G, nm, nil) end
    end
    for i = 1, #hoist do
      local h = hoist[i]
      setown(G, h.name, h.make(s))
    end
    body(s)
  end
end
end

---------------------------------------------------------------------------
-- 5. Built-ins
---------------------------------------------------------------------------
local function mkfn(fn, name, nargs) return new_native(fn, name, nargs, true) end
local function def(o, name, fn, nargs)
  local key = name
  local fname = name
  if type(name) == "table" then fname = "[" .. (name.desc or "") .. "]" end
  sethidden(o, key, mkfn(fn, fname, nargs))
end
-- a host accessor: getter(this), setter(this, v), both Lua functions
local function defacc(o, name, getter, setter)
  local acc = o.acc
  if not acc then acc = {}; o.acc = acc end
  acc[name] = { lg = getter, ls = setter }
end
local function mkctor(name, nargs, callf, ctorf, proto)
  local F = new_native(callf, name, nargs, false)
  F.ctor = ctorf
  F.lazyproto = nil
  F.p.prototype = proto
  proto.p.constructor = F
  sethidden(G, name, F)
  R.ctors[name] = F
  return F
end

local function callable(f) return type(f) == "table" and f.fn ~= nil end
local function need_fn(f, what)
  if type(f) ~= "table" or not f.fn then throw_error("TypeError", (what or describe(f)) .. " is not a function") end
  return f.fn
end

-- ToIntegerOrInfinity
local function toint(v, default)
  if v == nil then return default end
  local n = to_num(v)
  if n ~= n then return 0 end
  if n == huge or n == -huge then return n end
  if n >= 0 then return floor(n) end
  return -floor(-n)
end
-- relative index clamped to 0..len
local function relidx(v, len, default)
  local n = toint(v, default)
  if n < 0 then
    n = n + len
    if n < 0 then n = 0 end
  elseif n > len then n = len end
  return n
end

local function ensure_enumerable(o, key)
  local k = o.k
  if k then
    for i = 1, #k do if k[i] == key then return end end
    k[#k + 1] = key
  else
    o.k = { key }
  end
end

local function pairs_to_obj(list)
  local o = new_obj(ObjectProto)
  for i = 1, #list, 2 do setown(o, list[i], list[i + 1]) end
  return o
end

local function iter_result(ok, v)
  local o = new_obj(ObjectProto)
  setown(o, "value", v)
  setown(o, "done", not ok)
  return o
end

-- array-like to list: arrays use their storage, other objects use `length`
local function arraylike(v)
  if type(v) == "table" and v.a then return v.a, v.len end
  if type(v) == "string" then
    local l, n = {}, 0
    for ch in sgmatch(v, UCHAR) do n = n + 1; l[n] = ch end
    return l, n
  end
  if not is_obj(v) then return {}, 0 end
  local n = toint(getv(v, "length"), 0)
  if n < 0 then n = 0 end
  if n > MAX_ARR then throw_error("RangeError", "Invalid array length") end
  local l = {}
  for i = 1, n do l[i] = getv(v, i - 1.0) end
  return l, n
end

local function type_tag(v)
  if v == nil then return "Undefined" elseif v == null then return "Null" end
  local t = type(v)
  if t == "string" then return "String" elseif t == "number" then return "Number" elseif t == "boolean" then return "Boolean" end
  if v.sym then return "Symbol" end
  if v.fn then return "Function" end
  local c = v.cls
  if c ~= "Object" and c ~= "Iterator" and c ~= "Function" then return c end
  return "Object"
end

-- Inspect (console output) ---------------------------------------------------------
local inspect
local function inspect_key(k)
  if type(k) == "table" then return "[Symbol(" .. (k.desc or "") .. ")]" end
  if sfind(k, "^[%a_$][%w_$]*$") then return k end
  return "'" .. k .. "'"
end
function inspect(v, depth, seen, top)
  local t = type(v)
  if t == "string" then
    if top then return v end
    return "'" .. sgsub(v, "'", "\\'") .. "'"
  elseif t == "number" then return numstr(v)
  elseif t == "nil" then return "undefined"
  elseif t == "boolean" then return tostring(v)
  elseif v == null then return "null"
  elseif v.sym then return "Symbol(" .. (v.desc or "") .. ")"
  elseif v.fn then
    if v.isclass then return "[class " .. (v.fname ~= "" and v.fname or "(anonymous)") .. "]" end
    return "[Function" .. (v.fname ~= "" and (": " .. v.fname) or " (anonymous)") .. "]"
  end
  if seen[v] then return "[Circular]" end
  local cls = v.cls
  if cls == "Error" then
    local ok, s = pcall(function()
      local name, msg = to_str(getv(v, "name")), to_str(getv(v, "message"))
      return msg ~= "" and (name .. ": " .. msg) or name
    end)
    return ok and s or "Error"
  elseif cls == "RegExp" then return "/" .. v.source .. "/" .. v.flags
  elseif cls == "Date" then
    return v.t == v.t and R.date_iso(v.t) or "Invalid Date"
  elseif cls == "Promise" then return "Promise { " .. (v.state == "pending" and "<pending>" or inspect(v.value, depth + 1, seen)) .. " }"
  end
  if depth > 2 then return v.a and "[Array]" or "[Object]" end
  seen[v] = true
  local parts, n = {}, 0
  local out
  if v.a then
    for i = 1, v.len do
      if n >= 100 then parts[n + 1] = "... " .. (v.len - 100) .. " more items"; n = n + 1; break end
      n = n + 1
      parts[n] = inspect(v.a[i], depth + 1, seen)
    end
    local ks, kn = own_keys(v)
    for i = v.len + 1, kn do
      n = n + 1
      parts[n] = inspect_key(ks[i]) .. ": " .. inspect(getv(v, ks[i]), depth + 1, seen)
    end
    out = "[" .. tconcat(parts, ", ") .. "]"
  elseif cls == "Map" or cls == "Set" then
    for i = 1, v.top do
      local k = v.keys[i]
      if k ~= nil then
        if k == NANKEY then k = NaN elseif k == UNDEFKEY then k = nil end
        n = n + 1
        if cls == "Map" then parts[n] = inspect(k, depth + 1, seen) .. " => " .. inspect(v.vals[i], depth + 1, seen)
        else parts[n] = inspect(k, depth + 1, seen) end
      end
    end
    out = cls .. "(" .. v.count .. ") {" .. tconcat(parts, ", ") .. "}"
  else
    local ks, kn = own_keys(v)
    for i = 1, kn do
      local k = ks[i]
      local val = v.p[k]
      n = n + 1
      if val == nil then
        parts[n] = inspect_key(k) .. ": [Getter/Setter]"
      else
        if val == UNDEF then val = nil end
        parts[n] = inspect_key(k) .. ": " .. inspect(val, depth + 1, seen)
      end
    end
    local pre = ""
    local proto = v.proto
    if proto and proto ~= ObjectProto then
      local c = proto.p.constructor
      if type(c) == "table" and c.fname and c.fname ~= "" and c.fname ~= "Object" then pre = c.fname .. " " end
    elseif not proto then pre = "[Object: null prototype] " end
    out = pre .. "{" .. tconcat(parts, ", ") .. "}"
    if kn == 0 then out = pre .. "{}" end
  end
  seen[v] = nil
  return out
end
local function inspect_args(...)
  local n = select("#", ...)
  local parts = {}
  for i = 1, n do
    local v = select(i, ...)
    parts[i] = inspect(v, 0, {}, true)
  end
  local s = tconcat(parts, " ")
  if #s > 200 then s = ssub(s, 1, 200) .. "..." end
  return s
end

---------------------------------------------------------------------------
-- Object, Function, Symbol, Error
---------------------------------------------------------------------------
local function this_obj(this, what)
  if not is_obj(this) then throw_error("TypeError", what .. " called on non-object") end
  return this
end

local function to_object_keys(v)
  if v == nil or v == null then throw_error("TypeError", "Cannot convert undefined or null to object") end
  if type(v) == "string" then
    local o = new_arr({}, 0)
    local n = 0
    for ch in sgmatch(v, UCHAR) do n = n + 1; o.a[n] = ch end
    o.len = n
    return o
  end
  if not is_obj(v) then return new_obj(ObjectProto) end
  return v
end

local function define_property(o, key, desc)
  if not is_obj(o) then throw_error("TypeError", "Object.defineProperty called on non-object") end
  if not is_obj(desc) then throw_error("TypeError", "Property description must be an object") end
  key = to_key(key)
  if o.frozen then throw_error("TypeError", "Cannot define property " .. keystr(key) .. ", object is frozen") end
  local g, s = getv(desc, "get"), getv(desc, "set")
  local has_en = has_prop(desc, "enumerable")
  local en = to_bool(getv(desc, "enumerable"))
  if g ~= nil or s ~= nil then
    if g ~= nil and not callable(g) then throw_error("TypeError", "Getter must be a function") end
    if s ~= nil and not callable(s) then throw_error("TypeError", "Setter must be a function") end
    def_acc(o, key, g, s, en)
    return
  end
  if o.a and (type(key) == "number" or (type(key) == "string" and idx_of(key))) then
    setv(o, key, getv(desc, "value"))
    return
  end
  if type(key) == "number" then key = numstr(key) end
  local existed = o.p[key] ~= nil
  if has_prop(desc, "value") or not existed then
    if o.acc and o.acc[key] then o.acc[key] = nil end
    sethidden(o, key, getv(desc, "value"))
  end
  if has_en then
    if en then ensure_enumerable(o, key) else remove_key(o, key) end
  end
  local w = has_prop(desc, "writable")
  if (w and not to_bool(getv(desc, "writable"))) or (not w and not existed) then
    o.ro = o.ro or {}
    o.ro[key] = true
  elseif w then
    if o.ro then o.ro[key] = nil end
  end
end

local function setup_object(rt)
  local OP = ObjectProto
  local function ctorf(proto, v)
    if v == nil or v == null then return new_obj(proto) end
    if is_obj(v) then return v end
    return new_obj(proto)
  end
  local Obj = mkctor("Object", 1, function(this, v) return ctorf(ObjectProto, v) end, ctorf, OP)
  def(OP, "hasOwnProperty", function(this, k)
    if type(this) == "string" then return k == "length" or (idx_of(to_str(k)) or 1e99) < #this end
    if not is_obj(this) then return false end
    return has_own(this, to_key(k))
  end, 1)
  def(OP, "isPrototypeOf", function(this, v)
    if not is_obj(v) then return false end
    v = v.proto
    while v do
      if v == this then return true end
      v = v.proto
    end
    return false
  end, 1)
  def(OP, "propertyIsEnumerable", function(this, k)
    if not is_obj(this) then return false end
    k = to_key(k)
    if this.a and (type(k) == "number" or idx_of(to_str(k))) then return has_own(this, k) end
    k = type(k) == "number" and numstr(k) or k
    local ks = this.k
    if ks then for i = 1, #ks do if ks[i] == k then return true end end end
    return false
  end, 1)
  def(OP, "toString", function(this) return "[object " .. type_tag(this) .. "]" end, 0)
  def(OP, "toLocaleString", function(this) return to_str(this) end, 0)
  def(OP, "valueOf", function(this) return this end, 0)
  defacc(OP, "__proto__", function(this)
    if is_obj(this) then return this.proto or null end
    if type(this) == "string" then return StringProto end
    return nil
  end, function(this, v)
    if is_obj(this) then
      if v == null then this.proto = nil elseif is_obj(v) then this.proto = v end
    end
  end)

  def(Obj, "keys", function(this, v)
    local o = to_object_keys(v)
    local ks, n = own_keys(o)
    return new_arr(ks, n)
  end, 1)
  def(Obj, "values", function(this, v)
    local o = to_object_keys(v)
    local ks, n = own_keys(o)
    local out = {}
    for i = 1, n do out[i] = getv(o, ks[i]) end
    return new_arr(out, n)
  end, 1)
  def(Obj, "entries", function(this, v)
    local o = to_object_keys(v)
    local ks, n = own_keys(o)
    local out = {}
    for i = 1, n do out[i] = new_arr({ ks[i], getv(o, ks[i]) }, 2) end
    return new_arr(out, n)
  end, 1)
  def(Obj, "assign", function(this, target, ...)
    if target == nil or target == null then throw_error("TypeError", "Cannot convert undefined or null to object") end
    for i = 1, select("#", ...) do
      local src = select(i, ...)
      if src ~= nil and src ~= null then
        local o = to_object_keys(src)
        local ks, n = own_keys(o)
        for j = 1, n do setv(target, ks[j], getv(o, ks[j])) end
      end
    end
    return target
  end, 2)
  def(Obj, "freeze", function(this, o) if is_obj(o) then o.frozen = true; o.noext = true end; return o end, 1)
  def(Obj, "isFrozen", function(this, o) return not is_obj(o) or o.frozen == true end, 1)
  def(Obj, "seal", function(this, o) if is_obj(o) then o.noext = true end; return o end, 1)
  def(Obj, "isSealed", function(this, o) return not is_obj(o) or o.noext == true end, 1)
  def(Obj, "preventExtensions", function(this, o) if is_obj(o) then o.noext = true end; return o end, 1)
  def(Obj, "isExtensible", function(this, o) return is_obj(o) and not o.noext end, 1)
  def(Obj, "create", function(this, proto, props)
    if proto ~= null and not is_obj(proto) then throw_error("TypeError", "Object prototype may only be an Object or null") end
    local o = new_obj(proto ~= null and proto or nil)
    if props ~= nil then
      local ks, n = own_keys(props)
      for i = 1, n do define_property(o, ks[i], getv(props, ks[i])) end
    end
    return o
  end, 2)
  def(Obj, "getPrototypeOf", function(this, o)
    if o == nil or o == null then throw_error("TypeError", "Cannot convert undefined or null to object") end
    if is_obj(o) then return o.proto or null end
    if type(o) == "string" then return StringProto elseif type(o) == "number" then return NumberProto end
    return BooleanProto
  end, 1)
  def(Obj, "setPrototypeOf", function(this, o, p)
    if is_obj(o) then
      if p == null then o.proto = nil elseif is_obj(p) then o.proto = p
      else throw_error("TypeError", "Object prototype may only be an Object or null") end
    end
    return o
  end, 2)
  def(Obj, "defineProperty", function(this, o, k, d) define_property(o, k, d); return o end, 3)
  def(Obj, "defineProperties", function(this, o, props)
    local ks, n = own_keys(props)
    for i = 1, n do define_property(o, ks[i], getv(props, ks[i])) end
    return o
  end, 2)
  def(Obj, "getOwnPropertyNames", function(this, v)
    local o = to_object_keys(v)
    local ks, n = own_keys(o)
    local seen = {}
    for i = 1, n do seen[ks[i]] = true end
    local extra = {}
    for k in pairs(o.p) do if type(k) == "string" and not seen[k] then extra[#extra + 1] = k end end
    if o.acc then for k in pairs(o.acc) do if type(k) == "string" and not seen[k] and o.p[k] == nil then extra[#extra + 1] = k end end end
    tsort(extra)
    for i = 1, #extra do n = n + 1; ks[n] = extra[i] end
    if o.a then n = n + 1; ks[n] = "length" end
    return new_arr(ks, n)
  end, 1)
  def(Obj, "getOwnPropertyDescriptor", function(this, o, k)
    if not is_obj(o) then return nil end
    k = to_key(k)
    if not has_own(o, k) then return nil end
    local d = new_obj(ObjectProto)
    local key = type(k) == "number" and numstr(k) or k
    local acc = o.acc and o.acc[key]
    if acc then
      setown(d, "get", acc.g)
      setown(d, "set", acc.s)
    else
      setown(d, "value", getv(o, k))
      setown(d, "writable", not (o.frozen or (o.ro and o.ro[key])))
    end
    local en = false
    if o.a and (type(k) == "number" or idx_of(key)) then en = true end
    local ks = o.k
    if ks then for i = 1, #ks do if ks[i] == key then en = true end end end
    setown(d, "enumerable", en)
    setown(d, "configurable", not o.frozen)
    return d
  end, 2)
  def(Obj, "fromEntries", function(this, it)
    local o = new_obj(ObjectProto)
    local nextf = get_iter(it)
    while true do
      local ok, e = nextf()
      if not ok then break end
      local k = getv(e, 0.0)
      setown(o, type(k) == "number" and numstr(k) or to_key(k), getv(e, 1.0))
    end
    return o
  end, 1)
  def(Obj, "hasOwn", function(this, o, k)
    if not is_obj(o) then return false end
    return has_own(o, to_key(k))
  end, 2)
  def(Obj, "is", function(this, a, b)
    if a ~= a and b ~= b then return true end
    if a == 0 and b == 0 and type(a) == "number" and type(b) == "number" then return (1 / a) == (1 / b) end
    return a == b
  end, 2)
end

local function setup_function(rt)
  local FP = FunctionProto
  local F = mkctor("Function", 1, function() throw_error("EvalError", "Function constructor is not supported") end,
    function() throw_error("EvalError", "Function constructor is not supported") end, FP)
  def(FP, "call", function(this, thisArg, ...)
    return call_fn(this, thisArg, ...)
  end, 1)
  def(FP, "apply", function(this, thisArg, args)
    if not callable(this) then throw_error("TypeError", "Function.prototype.apply was called on a non-function") end
    if args == nil or args == null then return call_fn(this, thisArg) end
    local l, n = arraylike(args)
    return call_fn(this, thisArg, tunpack(l, 1, n))
  end, 2)
  def(FP, "bind", function(this, thisArg, ...)
    local target = this
    if not callable(target) then throw_error("TypeError", "Bind must be called on a function") end
    local bargs = { ... }
    local bn = select("#", ...)
    local bf = new_native(function(_, ...)
      if bn == 0 then return target.fn(thisArg, ...) end
      local args = tmove(bargs, 1, bn, 1, {})
      local n = select("#", ...)
      local extra = { ... }
      for i = 1, n do args[bn + i] = extra[i] end
      return target.fn(thisArg, tunpack(args, 1, bn + n))
    end, "bound " .. (target.fname or ""), math.max(0, (target.nargs or 0) - bn), target.noctor)
    bf.bound = target
    bf.lazyproto = nil
    if not target.noctor then
      bf.ctor = function(proto, ...)
        local args = tmove(bargs, 1, bn, 1, {})
        local n = select("#", ...)
        local extra = { ... }
        for i = 1, n do args[bn + i] = extra[i] end
        return construct(target, target, tunpack(args, 1, bn + n))
      end
    end
    return bf
  end, 1)
  def(FP, "toString", function(this)
    if not callable(this) then throw_error("TypeError", "Function.prototype.toString requires that 'this' be a Function") end
    local native = not this.isjs and (this.noctor or this.ctor)
    return "function " .. (this.fname or "") .. "() { [" .. (native and "native code" or "code") .. "] }"
  end, 0)
  defacc(FP, "name", function(f) return f.fname or "" end, nil)
  defacc(FP, "length", function(f) return (f.nargs or 0) + 0.0 end, nil)
  defacc(FP, "prototype", function(f)
    if f.lazyproto then
      f.lazyproto = nil
      local pr = new_obj(ObjectProto)
      pr.p.constructor = f
      f.p.prototype = pr
      return pr
    end
  end, function(f, v) f.p.prototype = (v == nil) and UNDEF or v end)
  FP.cls = "Function"
  FP.fn = function() end
  FP.fname = ""
  FP.nargs = 0
end

local function setup_symbol(rt)
  local registry = {}
  local function sym_new(this, desc)
    local s = { sym = true, desc = desc ~= nil and to_str(desc) or nil }
    return s
  end
  local S = mkctor("Symbol", 0, sym_new, function() throw_error("TypeError", "Symbol is not a constructor") end, SymbolProto)
  S.noctor = true
  S.ctor = nil
  sethidden(S, "iterator", symIterator)
  for _, nm in ipairs({ "hasInstance", "toPrimitive", "toStringTag", "asyncIterator", "species" }) do
    sethidden(S, nm, { sym = true, desc = "Symbol." .. nm })
  end
  def(S, "for", function(this, key)
    key = to_str(key)
    local s = registry[key]
    if not s then s = { sym = true, desc = key }; registry[key] = s end
    return s
  end, 1)
  def(S, "keyFor", function(this, s)
    for k, v in pairs(registry) do if v == s then return k end end
    return nil
  end, 1)
  def(SymbolProto, "toString", function(this)
    if type(this) ~= "table" or not this.sym then throw_error("TypeError", "not a symbol") end
    return "Symbol(" .. (this.desc or "") .. ")"
  end, 0)
  def(SymbolProto, "valueOf", function(this) return this end, 0)
  defacc(SymbolProto, "description", function(this) return this.desc end, nil)
end

local ERROR_NAMES = { "Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError", "EvalError", "URIError" }
local function setup_errors(rt)
  rt.errproto = {}
  local base
  for i, name in ipairs(ERROR_NAMES) do
    local proto = new_obj(i == 1 and ObjectProto or rt.errproto.Error)
    proto.p.name = name
    proto.p.message = ""
    rt.errproto[name] = proto
    local function build(pr, msg, opts)
      local o = new_obj(pr)
      o.cls = "Error"
      local m = ""
      if msg ~= nil then m = to_str(msg); o.p.message = m end
      local nm = to_str(getv(pr, "name"))
      o.p.stack = (m ~= "" and (nm .. ": " .. m)) or nm
      if is_obj(opts) and has_prop(opts, "cause") then o.p.cause = getv(opts, "cause") end
      return o
    end
    local F = mkctor(name, 1, function(this, msg, opts) return build(proto, msg, opts) end,
      function(pr, msg, opts) return build(pr, msg, opts) end, proto)
    if i == 1 then
      base = F
      def(F, "captureStackTrace", function() end, 1)
    else
      F.proto = base
    end
  end
  def(rt.errproto.Error, "toString", function(this)
    local name = this ~= nil and getv(this, "name")
    local msg = this ~= nil and getv(this, "message")
    name = name == nil and "Error" or to_str(name)
    msg = msg == nil and "" or to_str(msg)
    if name == "" then return msg end
    if msg == "" then return name end
    return name .. ": " .. msg
  end, 0)
end

local setup_array
do

---------------------------------------------------------------------------
-- Array
---------------------------------------------------------------------------
-- Stable merge sort of list[1..n]. le(a, b) is true when a may stay before b.
local function merge_sort(list, n, le_fn)
  if n < 2 then return list end
  local src, dst = list, {}
  local width = 1
  while width < n do
    local i = 1
    while i <= n do
      local mid = i + width
      if mid > n + 1 then mid = n + 1 end
      local hi = i + 2 * width
      if hi > n + 1 then hi = n + 1 end
      local l, r, k = i, mid, i
      while l < mid and r < hi do
        if le_fn(src[l], src[r]) then dst[k] = src[l]; l = l + 1 else dst[k] = src[r]; r = r + 1 end
        k = k + 1
      end
      while l < mid do dst[k] = src[l]; l = l + 1; k = k + 1 end
      while r < hi do dst[k] = src[r]; r = r + 1; k = k + 1 end
      i = i + 2 * width
    end
    src, dst = dst, src
    width = width * 2
  end
  return src
end

local joining = {}
local function join_arr(this, sep)
  local a, len = arraylike(this)
  if joining[this] then return "" end
  joining[this] = true
  local ok, res = pcall(function()
    local parts = {}
    for i = 1, len do
      local v = a[i]
      if v == nil or v == null then parts[i] = "" else parts[i] = to_str(v) end
    end
    local s = tconcat(parts, sep, 1, len)
    if #s > MAX_STR then throw_error("RangeError", "Invalid string length") end
    return s
  end)
  joining[this] = nil
  if not ok then error(res, 0) end
  return res
end

function setup_array(rt)
  local AP = ArrayProto
  local function array_ctor(proto, ...)
    local n = select("#", ...)
    local arr
    local first = ...
    if n == 1 and type(first) == "number" then
      if first < 0 or first % 1 ~= 0 or first > MAX_ARR then throw_error("RangeError", "Invalid array length") end
      arr = new_arr({}, first)
    else
      arr = new_arr({ ... }, n)
    end
    arr.proto = proto
    return arr
  end
  local Arr = mkctor("Array", 1, function(this, ...) return array_ctor(ArrayProto, ...) end, array_ctor, AP)
  local function MA(this)
    if type(this) ~= "table" or not this.a then throw_error("TypeError", "Array method called on incompatible receiver") end
    if this.frozen then throw_error("TypeError", "Cannot modify a frozen array") end
    if this.len > 256 then charge(this.len >> 5) end
    return this.a
  end
  local function RA(this)
    if this == nil or this == null then throw_error("TypeError", "Array.prototype method called on null or undefined") end
    local a, n = arraylike(this)
    if n > 256 then charge(n >> 5) end
    return a, n
  end

  def(Arr, "isArray", function(this, v) return type(v) == "table" and v.cls == "Array" end, 1)
  def(Arr, "of", function(this, ...) return new_arr({ ... }, select("#", ...)) end, 0)
  def(Arr, "from", function(this, src, mapfn, thisArg)
    if src == nil or src == null then throw_error("TypeError", to_str(src) .. " is not iterable") end
    local list, n
    if type(src) == "string" or (is_obj(src) and (src.a or src.cls == "Map" or src.cls == "Set" or src.cls == "Iterator"
      or getv(src, symIterator) ~= nil)) then
      list, n = iter_list(src)
    else
      list, n = arraylike(src)
      list = tmove(list, 1, n, 1, {})
    end
    if mapfn ~= nil then
      local fn = need_fn(mapfn)
      for i = 1, n do list[i] = fn(thisArg, list[i], i - 1.0) end
    end
    return new_arr(list, n)
  end, 1)

  def(AP, "push", function(this, ...)
    local a = MA(this)
    local n, len = select("#", ...), this.len
    if len + n > MAX_ARR then throw_error("RangeError", "Invalid array length") end
    if n == 1 then a[len + 1] = (...)
    else
      local args = { ... }
      for i = 1, n do a[len + i] = args[i] end
    end
    this.len = len + n
    return this.len + 0.0
  end, 1)
  def(AP, "pop", function(this)
    local a = MA(this)
    local len = this.len
    if len == 0 then return nil end
    local v = a[len]
    a[len] = nil
    this.len = len - 1
    return v
  end, 0)
  def(AP, "shift", function(this)
    local a = MA(this)
    local len = this.len
    if len == 0 then return nil end
    local v = a[1]
    tmove(a, 2, len, 1)
    a[len] = nil
    this.len = len - 1
    return v
  end, 0)
  def(AP, "unshift", function(this, ...)
    local a = MA(this)
    local n, len = select("#", ...), this.len
    if len + n > MAX_ARR then throw_error("RangeError", "Invalid array length") end
    if n > 0 then
      tmove(a, 1, len, n + 1)
      local args = { ... }
      for i = 1, n do a[i] = args[i] end
      this.len = len + n
    end
    return this.len + 0.0
  end, 1)
  def(AP, "slice", function(this, s, e)
    local a, len = RA(this)
    local from, to = relidx(s, len, 0), relidx(e, len, len)
    if to < from then to = from end
    return new_arr(tmove(a, from + 1, to, 1, {}), to - from)
  end, 2)
  def(AP, "splice", function(this, ...)
    local a = MA(this)
    local len, nargs = this.len, select("#", ...)
    local start, dcount = ...
    start = relidx(start, len, 0)
    local dc
    if nargs == 0 then dc = 0
    elseif nargs == 1 then dc = len - start
    else
      dc = toint(dcount, 0)
      if dc < 0 then dc = 0 elseif dc > len - start then dc = len - start end
    end
    local items = nargs > 2 and { select(3, ...) } or {}
    local k = nargs > 2 and nargs - 2 or 0
    if len - dc + k > MAX_ARR then throw_error("RangeError", "Invalid array length") end
    local removed = tmove(a, start + 1, start + dc, 1, {})
    if k ~= dc then
      tmove(a, start + dc + 1, len, start + k + 1)
      for i = len - dc + k + 1, len do a[i] = nil end
    end
    for i = 1, k do a[start + i] = items[i] end
    this.len = len - dc + k
    return new_arr(removed, dc)
  end, 2)
  def(AP, "concat", function(this, ...)
    local a, len = RA(this)
    local out = tmove(a, 1, len, 1, {})
    local n = len
    for i = 1, select("#", ...) do
      local v = select(i, ...)
      if type(v) == "table" and v.cls == "Array" then
        array_check_len(n + v.len)
        tmove(v.a, 1, v.len, n + 1, out)
        n = n + v.len
      else
        n = n + 1
        array_check_len(n)
        out[n] = v
      end
    end
    return new_arr(out, n)
  end, 1)
  def(AP, "join", function(this, sep)
    if sep == nil then sep = "," else sep = to_str(sep) end
    return join_arr(this, sep)
  end, 1)
  def(AP, "toString", function(this) return join_arr(this, ",") end, 0)
  def(AP, "toLocaleString", function(this) return join_arr(this, ",") end, 0)
  def(AP, "reverse", function(this)
    local a = MA(this)
    local len = this.len
    local i, j = 1, len
    while i < j do
      a[i], a[j] = a[j], a[i]
      i = i + 1; j = j - 1
    end
    return this
  end, 0)
  def(AP, "indexOf", function(this, x, from)
    local a, len = RA(this)
    for i = relidx(from, len, 0) + 1, len do
      if a[i] == x then return i - 1.0 end
    end
    return -1.0
  end, 1)
  def(AP, "lastIndexOf", function(this, x, from)
    local a, len = RA(this)
    local start = len
    if from ~= nil then
      local f = toint(from, 0)
      start = f < 0 and f + len + 1 or f + 1
      if start > len then start = len end
    end
    for i = start, 1, -1 do
      if a[i] == x then return i - 1.0 end
    end
    return -1.0
  end, 1)
  def(AP, "includes", function(this, x, from)
    local a, len = RA(this)
    local nan = x ~= x
    for i = relidx(from, len, 0) + 1, len do
      local v = a[i]
      if v == x or (nan and v ~= v) then return true end
    end
    return false
  end, 1)
  def(AP, "at", function(this, i)
    local a, len = RA(this)
    i = toint(i, 0)
    if i < 0 then i = i + len end
    if i < 0 or i >= len then return nil end
    return a[i + 1]
  end, 1)
  def(AP, "forEach", function(this, cb, thisArg)
    local a, len = RA(this)
    local fn = need_fn(cb)
    for i = 1, len do fn(thisArg, a[i], i - 1.0, this) end
  end, 1)
  def(AP, "map", function(this, cb, thisArg)
    local a, len = RA(this)
    local fn = need_fn(cb)
    local out = {}
    for i = 1, len do out[i] = fn(thisArg, a[i], i - 1.0, this) end
    return new_arr(out, len)
  end, 1)
  def(AP, "filter", function(this, cb, thisArg)
    local a, len = RA(this)
    local fn = need_fn(cb)
    local out, n = {}, 0
    for i = 1, len do
      local v = a[i]
      if to_bool((fn(thisArg, v, i - 1.0, this))) then n = n + 1; out[n] = v end
    end
    return new_arr(out, n)
  end, 1)
  local function finder(name, fromend, wantindex)
    def(AP, name, function(this, cb, thisArg)
      local a, len = RA(this)
      local fn = need_fn(cb)
      local i, last, step = 1, len, 1
      if fromend then i, last, step = len, 1, -1 end
      for j = i, last, step do
        local v = a[j]
        if to_bool((fn(thisArg, v, j - 1.0, this))) then
          if wantindex then return j - 1.0 end
          return v
        end
      end
      if wantindex then return -1.0 end
    end, 1)
  end
  finder("find", false, false); finder("findIndex", false, true)
  finder("findLast", true, false); finder("findLastIndex", true, true)
  def(AP, "some", function(this, cb, thisArg)
    local a, len = RA(this)
    local fn = need_fn(cb)
    for i = 1, len do
      if to_bool((fn(thisArg, a[i], i - 1.0, this))) then return true end
    end
    return false
  end, 1)
  def(AP, "every", function(this, cb, thisArg)
    local a, len = RA(this)
    local fn = need_fn(cb)
    for i = 1, len do
      if not to_bool((fn(thisArg, a[i], i - 1.0, this))) then return false end
    end
    return true
  end, 1)
  def(AP, "reduce", function(this, cb, ...)
    local a, len = RA(this)
    local fn = need_fn(cb)
    local acc, start = ...
    if select("#", ...) == 0 then
      if len == 0 then throw_error("TypeError", "Reduce of empty array with no initial value") end
      acc, start = a[1], 2
    else
      start = 1
    end
    for i = start, len do acc = fn(nil, acc, a[i], i - 1.0, this) end
    return acc
  end, 1)
  def(AP, "reduceRight", function(this, cb, ...)
    local a, len = RA(this)
    local fn = need_fn(cb)
    local acc, start = ...
    if select("#", ...) == 0 then
      if len == 0 then throw_error("TypeError", "Reduce of empty array with no initial value") end
      acc, start = a[len], len - 1
    else
      start = len
    end
    for i = start, 1, -1 do acc = fn(nil, acc, a[i], i - 1.0, this) end
    return acc
  end, 1)
  def(AP, "sort", function(this, cmp)
    local a = MA(this)
    local len = this.len
    if cmp ~= nil then need_fn(cmp, "The comparison function must be either a function or undefined") end
    local vals, m = {}, 0
    for i = 1, len do
      local v = a[i]
      if v ~= nil then m = m + 1; vals[m] = v end
    end
    charge(m >> 3)
    local sorted
    if cmp ~= nil then
      local fn = cmp.fn
      sorted = merge_sort(vals, m, function(x, y)
        local r = fn(nil, x, y)
        if type(r) ~= "number" then r = to_num(r) end
        return not (r > 0)
      end)
    else
      local items = {}
      for i = 1, m do items[i] = { to_str(vals[i]), vals[i] } end
      local s = merge_sort(items, m, function(x, y) return x[1] <= y[1] end)
      sorted = {}
      for i = 1, m do sorted[i] = s[i][2] end
    end
    for i = 1, m do a[i] = sorted[i] end
    for i = m + 1, len do a[i] = nil end
    return this
  end, 1)
  local function flatten(a, len, depth, out, n, lvl)
    if lvl > MAX_DEPTH then throw_error("RangeError", "Maximum call stack size exceeded") end
    for i = 1, len do
      local v = a[i]
      if depth > 0 and type(v) == "table" and v.cls == "Array" then
        n = flatten(v.a, v.len, depth - 1, out, n, lvl + 1)
      else
        n = n + 1
        array_check_len(n)
        out[n] = v
      end
    end
    return n
  end
  def(AP, "flat", function(this, depth)
    local a, len = RA(this)
    local out = {}
    local n = flatten(a, len, toint(depth, 1), out, 0, 0)
    return new_arr(out, n)
  end, 0)
  def(AP, "flatMap", function(this, cb, thisArg)
    local a, len = RA(this)
    local fn = need_fn(cb)
    local mapped = {}
    for i = 1, len do mapped[i] = fn(thisArg, a[i], i - 1.0, this) end
    local out = {}
    local n = flatten(mapped, len, 1, out, 0, 0)
    return new_arr(out, n)
  end, 1)
  def(AP, "fill", function(this, v, s, e)
    local a = MA(this)
    local len = this.len
    for i = relidx(s, len, 0) + 1, relidx(e, len, len) do a[i] = v end
    return this
  end, 1)
  def(AP, "keys", function(this)
    local i = 0
    return make_iter(function()
      if i < (this.len or 0) then i = i + 1; return true, i - 1.0 end
      return false
    end)
  end, 0)
  local function values_iter(this)
    local a, i = nil, 0
    return make_iter(function()
      local len
      a, len = arraylike(this)
      if i < len then i = i + 1; return true, a[i] end
      return false
    end)
  end
  def(AP, "values", values_iter, 0)
  sethidden(AP, symIterator, AP.p.values)
  def(AP, "entries", function(this)
    local i = 0
    return make_iter(function()
      local a, len = arraylike(this)
      if i < len then i = i + 1; return true, new_arr({ i - 1.0, a[i] }, 2) end
      return false
    end)
  end, 0)
  def(AP, "copyWithin", function(this, target, s, e)
    local a = MA(this)
    local len = this.len
    local to, from, fin = relidx(target, len, 0), relidx(s, len, 0), relidx(e, len, len)
    local cnt = math.min(fin - from, len - to)
    if cnt > 0 then tmove(a, from + 1, from + cnt, to + 1) end
    return this
  end, 2)
end
end

local setup_regexp, setup_string, trim_left, trim_right
do

---------------------------------------------------------------------------
-- RegExp objects and String
---------------------------------------------------------------------------
local rxcache, rxcount = {}, 0
local function get_prog(source, flags)
  local key = flags .. "/" .. source
  local p = rxcache[key]
  if not p then
    p = regex_compile(source, flags)
    if rxcount >= 100 then rxcache, rxcount = {}, 0 end
    rxcache[key] = p
    rxcount = rxcount + 1
  end
  return p
end

function new_regexp(prog, source, flags)
  local o = new_obj(R.RegExpProto)
  o.cls = "RegExp"
  o.prog, o.source, o.flags, o.li = prog, source, flags, 0
  o.global = sfind(flags, "g", 1, true) ~= nil
  o.sticky = sfind(flags, "y", 1, true) ~= nil
  return o
end

local function char_width(s, i)
  local c = sbyte(s, i)
  if not c or c < 0xC0 then return 1 end
  return c >= 0xF0 and 4 or c >= 0xE0 and 3 or 2
end

local function build_match(re, s, st, en, caps)
  local prog = re.prog
  local list = { ssub(s, st, en - 1) }
  local ng = prog.ngroups
  for g = 1, ng do
    local a, b = caps[2 * g + 1], caps[2 * g + 2]
    if a then list[g + 1] = ssub(s, a, b - 1) end
  end
  local arr = new_arr(list, ng + 1)
  setown(arr, "index", st - 1.0)
  setown(arr, "input", s)
  local groups
  if #prog.names > 0 then
    groups = new_obj(nil)
    for _, nm in ipairs(prog.names) do setown(groups, nm.name, list[nm.idx + 1]) end
  end
  setown(arr, "groups", groups)
  return arr
end

-- Runs the regex honoring lastIndex. Returns st, en, caps or nil.
local function re_run(re, s)
  local use_li = re.global or re.sticky
  local init = 1
  if use_li then
    init = re.li + 1
    if init > #s + 1 then re.li = 0; return nil end
  end
  local st, en, caps = regex_exec(re.prog, s, init, re.sticky)
  if not st then
    if use_li then re.li = 0 end
    return nil
  end
  if use_li then re.li = en - 1 end
  return st, en, caps
end

local function to_regexp(v, flags)
  if type(v) == "table" and v.cls == "RegExp" then return v end
  local src = v == nil and "(?:)" or to_str(v)
  return new_regexp(get_prog(src, flags or ""), src, flags or "")
end

local function group_str(s, caps, g)
  local a, b = caps[2 * g + 1], caps[2 * g + 2]
  if a then return ssub(s, a, b - 1) end
  return ""
end

-- Expand $1, $&, $<name> ... in a replacement string.
local function expand_repl(rep, s, st, en, caps, prog)
  if not sfind(rep, "$", 1, true) then return rep end
  local out, i, ng = {}, 1, prog and prog.ngroups or 0
  while true do
    local d = sfind(rep, "$", i, true)
    if not d then out[#out + 1] = ssub(rep, i); break end
    out[#out + 1] = ssub(rep, i, d - 1)
    local c = ssub(rep, d + 1, d + 1)
    if c == "$" then out[#out + 1] = "$"; i = d + 2
    elseif c == "&" then out[#out + 1] = ssub(s, st, en - 1); i = d + 2
    elseif c == "`" then out[#out + 1] = ssub(s, 1, st - 1); i = d + 2
    elseif c == "'" then out[#out + 1] = ssub(s, en); i = d + 2
    elseif sfind(c, "^%d$") then
      local two = smatch(rep, "^%d%d", d + 1)
      if two and tonumber(two) >= 1 and tonumber(two) <= ng then out[#out + 1] = group_str(s, caps, tonumber(two)); i = d + 3
      elseif tonumber(c) >= 1 and tonumber(c) <= ng then out[#out + 1] = group_str(s, caps, tonumber(c)); i = d + 2
      else out[#out + 1] = "$"; i = d + 1 end
    elseif c == "<" and prog and #prog.names > 0 then
      local e = sfind(rep, ">", d + 2, true)
      if e then
        local nm = ssub(rep, d + 2, e - 1)
        for _, g in ipairs(prog.names) do if g.name == nm then out[#out + 1] = group_str(s, caps, g.idx) end end
        i = e + 1
      else out[#out + 1] = "$"; i = d + 1 end
    else out[#out + 1] = "$"; i = d + 1 end
  end
  return tconcat(out)
end

local function call_replacer(fn, s, st, en, caps, prog)
  local args = { ssub(s, st, en - 1) }
  local n = 1
  for g = 1, prog.ngroups do
    n = n + 1
    local a, b = caps[2 * g + 1], caps[2 * g + 2]
    args[n] = a and ssub(s, a, b - 1) or nil
  end
  args[n + 1] = st - 1.0
  args[n + 2] = s
  n = n + 2
  if #prog.names > 0 then
    local groups = new_obj(nil)
    for _, nm in ipairs(prog.names) do setown(groups, nm.name, args[nm.idx + 1]) end
    n = n + 1
    args[n] = groups
  end
  return to_str((fn(nil, tunpack(args, 1, n))))
end

local function regex_replace(re, s, rep, forceall)
  local fn = callable(rep) and rep.fn
  if not fn then rep = to_str(rep) end
  local global = re.global or forceall
  local out, pos, init = {}, 1, 1
  if re.global then re.li = 0 elseif re.sticky then init = re.li + 1 end
  local len = #s
  while init <= len + 1 do
    local st, en, caps = regex_exec(re.prog, s, init, re.sticky)
    if not st then break end
    out[#out + 1] = ssub(s, pos, st - 1)
    if fn then out[#out + 1] = call_replacer(fn, s, st, en, caps, re.prog)
    else out[#out + 1] = expand_repl(rep, s, st, en, caps, re.prog) end
    pos = en
    if not global then break end
    if en == st then init = en + char_width(s, en) else init = en end
  end
  out[#out + 1] = ssub(s, pos)
  local r = tconcat(out)
  if #r > MAX_STR then throw_error("RangeError", "Invalid string length") end
  return r
end

local function string_replace(s, pat, rep, all)
  if type(pat) == "table" and pat.cls == "RegExp" then
    if all and not pat.global then throw_error("TypeError", "replaceAll must be called with a global RegExp") end
    return regex_replace(pat, s, rep, false)
  end
  pat = to_str(pat)
  local fn = callable(rep) and rep.fn
  if not fn then rep = to_str(rep) end
  local out, pos, init = {}, 1, 1
  local len = #s
  while init <= len + 1 do
    local st = sfind(s, pat, init, true)
    if not st then break end
    local en = st + #pat
    out[#out + 1] = ssub(s, pos, st - 1)
    if fn then out[#out + 1] = to_str((fn(nil, pat, st - 1.0, s)))
    else out[#out + 1] = expand_repl(rep, s, st, en, {}, nil) end
    pos = en
    if not all then break end
    init = (#pat == 0) and (en + char_width(s, en)) or en
  end
  out[#out + 1] = ssub(s, pos)
  local r = tconcat(out)
  if #r > MAX_STR then throw_error("RangeError", "Invalid string length") end
  return r
end

local function string_split(s, sep, limit)
  local lim = limit == nil and MAX_ARR or toint(limit, MAX_ARR)
  local out, n = {}, 0
  if lim <= 0 then return new_arr(out, 0) end
  if sep == nil then return new_arr({ s }, 1) end
  local len = #s
  if type(sep) == "table" and sep.cls == "RegExp" then
    if len == 0 then
      local st = regex_exec(sep.prog, s, 1, true)
      if st then return new_arr({}, 0) end
      return new_arr({ s }, 1)
    end
    local p, q = 1, 1
    while q <= len do
      local st, en, caps = regex_exec(sep.prog, s, q, false)
      if not st or st > len then break end
      if en == st and st == p then
        q = st + char_width(s, st)
      else
        n = n + 1; out[n] = ssub(s, p, st - 1)
        if n >= lim then return new_arr(out, n) end
        for g = 1, sep.prog.ngroups do
          n = n + 1
          local a, b = caps[2 * g + 1], caps[2 * g + 2]
          out[n] = a and ssub(s, a, b - 1) or nil
          if n >= lim then return new_arr(out, n) end
        end
        p = en
        q = en
      end
    end
    n = n + 1; out[n] = ssub(s, p)
    return new_arr(out, n)
  end
  sep = to_str(sep)
  if sep == "" then
    for ch in sgmatch(s, UCHAR) do
      n = n + 1; out[n] = ch
      if n >= lim then break end
    end
    return new_arr(out, n)
  end
  local init = 1
  while true do
    local st = sfind(s, sep, init, true)
    if not st then break end
    n = n + 1; out[n] = ssub(s, init, st - 1)
    if n >= lim then return new_arr(out, n) end
    init = st + #sep
  end
  n = n + 1; out[n] = ssub(s, init)
  return new_arr(out, n)
end

function trim_left(s)
  local i = 1
  while true do
    local c = sbyte(s, i)
    if c == 32 or (c and c >= 9 and c <= 13) then i = i + 1
    elseif c == 194 and sbyte(s, i + 1) == 160 then i = i + 2
    elseif c == 239 and sbyte(s, i + 1) == 187 and sbyte(s, i + 2) == 191 then i = i + 3
    else break end
  end
  return ssub(s, i)
end
function trim_right(s)
  local j = #s
  while j > 0 do
    local c = sbyte(s, j)
    if c == 32 or (c >= 9 and c <= 13) then j = j - 1
    elseif c == 160 and sbyte(s, j - 1) == 194 then j = j - 2
    elseif c == 191 and sbyte(s, j - 1) == 187 and sbyte(s, j - 2) == 239 then j = j - 3
    else break end
  end
  return ssub(s, 1, j)
end

function setup_regexp(rt)
  local RP = rt.RegExpProto
  local function ctorf(proto, pattern, flags)
    local o
    if type(pattern) == "table" and pattern.cls == "RegExp" then
      o = new_regexp(pattern.prog, pattern.source, pattern.flags)
      if flags ~= nil then o = to_regexp(pattern.source, to_str(flags)) end
    else
      local src = pattern == nil and "(?:)" or to_str(pattern)
      local fl = flags == nil and "" or to_str(flags)
      o = new_regexp(get_prog(src, fl), src, fl)
    end
    o.proto = proto
    return o
  end
  mkctor("RegExp", 2, function(this, p, f) return ctorf(RP, p, f) end, ctorf, RP)
  def(RP, "test", function(this, s)
    if type(this) ~= "table" or this.cls ~= "RegExp" then throw_error("TypeError", "RegExp.prototype.test called on incompatible receiver") end
    return re_run(this, to_str(s)) ~= nil
  end, 1)
  def(RP, "exec", function(this, s)
    if type(this) ~= "table" or this.cls ~= "RegExp" then throw_error("TypeError", "RegExp.prototype.exec called on incompatible receiver") end
    s = to_str(s)
    local st, en, caps = re_run(this, s)
    if not st then return null end
    return build_match(this, s, st, en, caps)
  end, 1)
  def(RP, "toString", function(this) return "/" .. this.source .. "/" .. this.flags end, 0)
  defacc(RP, "source", function(this) return this.source end)
  defacc(RP, "flags", function(this) return this.flags end)
  defacc(RP, "lastIndex", function(this) return this.li + 0.0 end, function(this, v) this.li = toint(v, 0) end)
  for name, ch in pairs({ global = "g", ignoreCase = "i", multiline = "m", sticky = "y", unicode = "u", dotAll = "s" }) do
    defacc(RP, name, function(this) return sfind(this.flags, ch, 1, true) ~= nil end)
  end
end

function setup_string(rt)
  local SP = StringProto
  local function strctor(...)
    if select("#", ...) == 0 then return "" end
    local v = ...
    if type(v) == "table" and v.sym then return "Symbol(" .. (v.desc or "") .. ")" end
    return to_str(v)
  end
  local Str = mkctor("String", 1, function(this, ...) return strctor(...) end,
    function(proto, ...) return strctor(...) end, SP)
  local function S(this, name)
    if type(this) == "string" then return this end
    if this == nil or this == null then
      throw_error("TypeError", "String.prototype." .. name .. " called on null or undefined")
    end
    return to_str(this)
  end
  local function defs(name, nargs, fn)
    def(SP, name, function(this, ...)
      local s = S(this, name)
      if #s > 4096 then charge(#s >> 10) end
      return fn(s, ...)
    end, nargs)
  end

  def(Str, "fromCharCode", function(this, ...)
    local codes, n = { ... }, select("#", ...)
    local out, i = {}, 1
    while i <= n do
      local c = to_int32(to_num(codes[i])) & 0xFFFF
      if c >= 0xD800 and c <= 0xDBFF and i < n then
        local d = to_int32(to_num(codes[i + 1])) & 0xFFFF
        if d >= 0xDC00 and d <= 0xDFFF then
          c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00)
          i = i + 1
        end
      end
      if c >= 0xD800 and c <= 0xDFFF then c = 0xFFFD end
      out[#out + 1] = utf8_encode(c)
      i = i + 1
    end
    return tconcat(out)
  end, 1)
  def(Str, "fromCodePoint", function(this, ...)
    local out = {}
    for i = 1, select("#", ...) do
      local c = to_num((select(i, ...)))
      if c ~= c or c < 0 or c > 0x10FFFF or c % 1 ~= 0 then throw_error("RangeError", "Invalid code point " .. to_str(c)) end
      out[i] = utf8_encode(floor(c))
    end
    return tconcat(out)
  end, 1)
  def(Str, "raw", function(this, strs, ...)
    local raw = getv(strs, "raw")
    local l, n = arraylike(raw)
    local out = {}
    for i = 1, n do
      out[#out + 1] = to_str(l[i])
      if i < n and i <= select("#", ...) then out[#out + 1] = to_str((select(i, ...))) end
    end
    return tconcat(out)
  end, 1)

  defs("toString", 0, function(s) return s end)
  defs("valueOf", 0, function(s) return s end)
  defs("charAt", 1, function(s, i)
    i = toint(i, 0)
    return ssub(s, i + 1, i + 1)
  end)
  defs("charCodeAt", 1, function(s, i)
    i = toint(i, 0)
    local c = sbyte(s, i + 1)
    if not c or i < 0 then return NaN end
    if c >= 0xC0 then
      local cp = utf8_decode(s, i + 1)
      if cp > 0xFFFF then return 0xD800 + ((cp - 0x10000) >> 10) + 0.0 end
      return cp + 0.0
    end
    return c + 0.0
  end)
  defs("codePointAt", 1, function(s, i)
    i = toint(i, 0)
    local c = sbyte(s, i + 1)
    if not c or i < 0 then return nil end
    return utf8_decode(s, i + 1) + 0.0
  end)
  defs("at", 1, function(s, i)
    i = toint(i, 0)
    if i < 0 then i = i + #s end
    if i < 0 or i >= #s then return nil end
    return ssub(s, i + 1, i + 1)
  end)
  defs("indexOf", 1, function(s, search, from)
    search = to_str(search)
    from = relidx(from, #s, 0)
    if search == "" then return from + 0.0 end
    local p = sfind(s, search, from + 1, true)
    return p and (p - 1.0) or -1.0
  end)
  defs("lastIndexOf", 1, function(s, search, from)
    search = to_str(search)
    local limit = #s
    if from ~= nil then
      local f = to_num(from)
      if f == f then limit = relidx(f, #s, #s) end
    end
    if search == "" then return limit + 0.0 end
    local best, init = -1.0, 1
    while true do
      local p = sfind(s, search, init, true)
      if not p or p - 1 > limit then break end
      best = p - 1.0
      init = p + 1
    end
    return best
  end)
  defs("includes", 1, function(s, search, from)
    if type(search) == "table" and search.cls == "RegExp" then throw_error("TypeError", "First argument to String.prototype.includes must not be a regular expression") end
    search = to_str(search)
    return sfind(s, search, relidx(from, #s, 0) + 1, true) ~= nil
  end)
  defs("startsWith", 1, function(s, search, pos)
    search = to_str(search)
    local p = relidx(pos, #s, 0)
    return ssub(s, p + 1, p + #search) == search
  end)
  defs("endsWith", 1, function(s, search, e)
    search = to_str(search)
    local en = relidx(e, #s, #s)
    return en >= #search and ssub(s, en - #search + 1, en) == search
  end)
  defs("slice", 2, function(s, a, b)
    local len = #s
    local from, to = relidx(a, len, 0), relidx(b, len, len)
    return ssub(s, from + 1, to)
  end)
  defs("substring", 2, function(s, a, b)
    local len = #s
    local from, to = toint(a, 0), toint(b, len)
    if from < 0 then from = 0 elseif from > len then from = len end
    if to < 0 then to = 0 elseif to > len then to = len end
    if from > to then from, to = to, from end
    return ssub(s, from + 1, to)
  end)
  defs("substr", 2, function(s, a, l)
    local len = #s
    local from = relidx(a, len, 0)
    local cnt = toint(l, len - from)
    if cnt <= 0 then return "" end
    return ssub(s, from + 1, from + cnt)
  end)
  defs("toUpperCase", 0, function(s) return supper(s) end)
  defs("toLowerCase", 0, function(s) return slower(s) end)
  defs("toLocaleUpperCase", 0, function(s) return supper(s) end)
  defs("toLocaleLowerCase", 0, function(s) return slower(s) end)
  defs("trim", 0, function(s) return trim_right(trim_left(s)) end)
  defs("trimStart", 0, trim_left)
  defs("trimEnd", 0, trim_right)
  defs("trimLeft", 0, trim_left)
  defs("trimRight", 0, trim_right)
  defs("split", 2, string_split)
  defs("replace", 2, function(s, pat, rep) return string_replace(s, pat, rep, false) end)
  defs("replaceAll", 2, function(s, pat, rep) return string_replace(s, pat, rep, true) end)
  defs("match", 1, function(s, re)
    re = to_regexp(re)
    if not re.global then
      local st, en, caps = re_run(re, s)
      if not st then return null end
      return build_match(re, s, st, en, caps)
    end
    re.li = 0
    local out, n, init = {}, 0, 1
    while init <= #s + 1 do
      local st, en, caps = regex_exec(re.prog, s, init, re.sticky)
      if not st then break end
      n = n + 1
      out[n] = ssub(s, st, en - 1)
      array_check_len(n)
      if en == st then init = en + char_width(s, en) else init = en end
    end
    if n == 0 then return null end
    return new_arr(out, n)
  end)
  defs("matchAll", 1, function(s, re)
    re = to_regexp(re, "g")
    if not re.global then throw_error("TypeError", "String.prototype.matchAll called with a non-global RegExp argument") end
    local init = 1
    return make_iter(function()
      if init > #s + 1 then return false end
      local st, en, caps = regex_exec(re.prog, s, init, false)
      if not st then init = #s + 2; return false end
      if en == st then init = en + char_width(s, en) else init = en end
      return true, build_match(re, s, st, en, caps)
    end)
  end)
  defs("search", 1, function(s, re)
    re = to_regexp(re)
    local st = regex_exec(re.prog, s, 1, re.sticky)
    return st and (st - 1.0) or -1.0
  end)
  defs("repeat", 1, function(s, n)
    n = toint(n, 0)
    if n < 0 or n == huge then throw_error("RangeError", "Invalid count value: " .. to_str(n)) end
    if #s * n > MAX_STR then throw_error("RangeError", "Invalid string length") end
    return srep(s, n)
  end)
  local function pad(s, len, fill, atstart)
    len = toint(len, 0)
    fill = fill == nil and " " or to_str(fill)
    if len <= #s or fill == "" then return s end
    if len > MAX_STR then throw_error("RangeError", "Invalid string length") end
    local need = len - #s
    local p = srep(fill, need // #fill + 1)
    p = ssub(p, 1, need)
    if atstart then return p .. s end
    return s .. p
  end
  defs("padStart", 2, function(s, len, fill) return pad(s, len, fill, true) end)
  defs("padEnd", 2, function(s, len, fill) return pad(s, len, fill, false) end)
  defs("concat", 1, function(s, ...)
    local parts = { s }
    for i = 1, select("#", ...) do parts[i + 1] = to_str((select(i, ...))) end
    local r = tconcat(parts)
    if #r > MAX_STR then throw_error("RangeError", "Invalid string length") end
    return r
  end)
  defs("localeCompare", 1, function(s, other)
    other = to_str(other)
    if s == other then return 0.0 end
    local la, lb = slower(s), slower(other)
    if la ~= lb then return la < lb and -1.0 or 1.0 end
    return s > other and -1.0 or 1.0
  end)
  defs("normalize", 0, function(s) return s end)
  sethidden(SP, symIterator, mkfn(function(this)
    return make_iter(get_iter(S(this, "iterator")))
  end, "[Symbol.iterator]", 0))
end
end

local setup_number
do

---------------------------------------------------------------------------
-- Number, Boolean, Math, global functions
---------------------------------------------------------------------------
local function incr_digits(d)
  local i = #d
  while i >= 1 do
    local c = sbyte(d, i)
    if c == 57 then
      d = ssub(d, 1, i - 1) .. "0" .. ssub(d, i + 1)
      i = i - 1
    else
      return ssub(d, 1, i - 1) .. schar(c + 1) .. ssub(d, i + 1)
    end
  end
  return "1" .. d
end

local function to_fixed(x, f)
  if x ~= x then return "NaN" end
  if mabs(x) >= 1e21 then return numstr(x) end
  local neg = x < 0
  if neg then x = -x end
  local L = sfmt("%." .. math.min(f + 30, 99) .. "f", x)
  local ip, fp = smatch(L, "^(%d+)%.?(%d*)$")
  local digits = ip .. ssub(fp, 1, f)
  if #fp > f and ssub(fp, f + 1, f + 1) >= "5" then digits = incr_digits(digits) end
  if #digits <= f then digits = srep("0", f - #digits + 1) .. digits end
  local r = digits
  if f > 0 then r = ssub(digits, 1, #digits - f) .. "." .. ssub(digits, #digits - f + 1) end
  return (neg and "-" or "") .. r
end

local function to_precision(x, p)
  if x ~= x or x == huge or x == -huge then return numstr(x) end
  local neg = x < 0
  if neg then x = -x end
  local es = sfmt("%." .. (p - 1) .. "e", x)
  local mant, ex = smatch(es, "^(.-)e([+-]%d+)$")
  local e = tonumber(ex)
  local r
  if x == 0 then e = 0 end
  if e < -6 or e >= p then
    r = mant .. "e" .. (e < 0 and "-" or "+") .. mabs(e)
  else
    local digits = sgsub(mant, "%.", "")
    if e >= 0 then
      r = ssub(digits, 1, e + 1)
      if #digits > e + 1 then r = r .. "." .. ssub(digits, e + 2) end
    else
      r = "0." .. srep("0", -e - 1) .. digits
    end
  end
  return (neg and "-" or "") .. r
end

local DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz"
local function num_radix(n, radix)
  if n ~= n then return "NaN" end
  if n == huge then return "Infinity" elseif n == -huge then return "-Infinity" end
  if radix == 10 then return numstr(n) end
  local neg = n < 0
  if neg then n = -n end
  local ip = floor(n)
  local fp = n - ip
  local out = {}
  if ip == 0 then out[1] = "0" end
  while ip > 0 do
    local d = fmod(ip, radix)
    table.insert(out, 1, ssub(DIGITS, d + 1, d + 1))
    ip = floor(ip / radix)
  end
  local r = tconcat(out)
  if fp > 0 then
    local fr = {}
    local cnt = 0
    while fp > 0 and cnt < 52 do
      fp = fp * radix
      local d = floor(fp)
      fr[#fr + 1] = ssub(DIGITS, d + 1, d + 1)
      fp = fp - d
      cnt = cnt + 1
    end
    r = r .. "." .. tconcat(fr)
  end
  return (neg and "-" or "") .. r
end

local function parse_int(str, radix)
  local s = trim_left(to_str(str))
  local neg = false
  local c = ssub(s, 1, 1)
  if c == "-" then neg = true; s = ssub(s, 2) elseif c == "+" then s = ssub(s, 2) end
  local r = to_int32(to_num(radix))
  if r == 0 then
    r = 10
    if sfind(s, "^0[xX]") then r = 16; s = ssub(s, 3) end
  elseif r == 16 then
    if sfind(s, "^0[xX]") then s = ssub(s, 3) end
  elseif r < 2 or r > 36 then return NaN end
  local n, any = 0.0, false
  for i = 1, #s do
    local d = tonumber(ssub(s, i, i), 36)
    if not d or d >= r then break end
    n = n * r + d
    any = true
  end
  if not any then return NaN end
  return neg and -n or n
end

local function parse_float(str)
  local s = trim_left(to_str(str))
  local m = smatch(s, "^[+-]?%d+%.?%d*[eE][+-]?%d+") or smatch(s, "^[+-]?%.%d+[eE][+-]?%d+")
    or smatch(s, "^[+-]?%d+%.?%d*") or smatch(s, "^[+-]?%.%d+")
  if m then return tonumber(m) + 0.0 end
  if sfind(s, "^%+?Infinity") then return huge end
  if sfind(s, "^%-Infinity") then return -huge end
  return NaN
end

local function ffloor(x) return floor(x) + 0.0 end

function setup_number(rt)
  local NP = NumberProto
  local function numctor(proto, ...)
    if select("#", ...) == 0 then return 0.0 end
    return to_num((...))
  end
  local Num = mkctor("Number", 1, function(this, ...) return numctor(nil, ...) end, numctor, NP)
  local function N(this)
    if type(this) == "number" then return this end
    throw_error("TypeError", "Number.prototype method called on incompatible receiver")
  end
  for k, v in pairs({ MAX_SAFE_INTEGER = 9007199254740991.0, MIN_SAFE_INTEGER = -9007199254740991.0,
    EPSILON = 2.0 ^ -52, MAX_VALUE = 1.7976931348623157e308, MIN_VALUE = 5e-324,
    POSITIVE_INFINITY = huge, NEGATIVE_INFINITY = -huge, NaN = NaN }) do
    sethidden(Num, k, v)
  end
  def(Num, "isInteger", function(this, v) return type(v) == "number" and v == v and v ~= huge and v ~= -huge and v % 1 == 0 end, 1)
  def(Num, "isSafeInteger", function(this, v)
    return type(v) == "number" and v == v and v % 1 == 0 and mabs(v) <= 9007199254740991.0
  end, 1)
  def(Num, "isFinite", function(this, v) return type(v) == "number" and v == v and v ~= huge and v ~= -huge end, 1)
  def(Num, "isNaN", function(this, v) return type(v) == "number" and v ~= v end, 1)
  def(Num, "parseFloat", function(this, s) return parse_float(s) end, 1)
  def(Num, "parseInt", function(this, s, r) return parse_int(s, r) end, 2)
  def(NP, "toString", function(this, radix)
    local n = N(this)
    if radix == nil then return numstr(n) end
    radix = toint(radix, 10)
    if radix < 2 or radix > 36 then throw_error("RangeError", "toString() radix must be between 2 and 36") end
    return num_radix(n, radix)
  end, 1)
  def(NP, "valueOf", function(this) return N(this) end, 0)
  def(NP, "toFixed", function(this, d)
    local n = N(this)
    d = toint(d, 0)
    if d < 0 or d > 100 then throw_error("RangeError", "toFixed() digits argument must be between 0 and 100") end
    return to_fixed(n, d)
  end, 1)
  def(NP, "toPrecision", function(this, p)
    local n = N(this)
    if p == nil then return numstr(n) end
    p = toint(p, 1)
    if p < 1 or p > 100 then throw_error("RangeError", "toPrecision() argument must be between 1 and 100") end
    return to_precision(n, p)
  end, 1)
  def(NP, "toExponential", function(this, d)
    local n = N(this)
    if n ~= n or n == huge or n == -huge then return numstr(n) end
    local es
    if d == nil then
      es = sfmt("%.16e", n)
      for p = 0, 16 do
        local t = sfmt("%." .. p .. "e", n)
        if tonumber(t) == n then es = t; break end
      end
    else
      d = toint(d, 0)
      if d < 0 or d > 100 then throw_error("RangeError", "toExponential() argument must be between 0 and 100") end
      es = sfmt("%." .. d .. "e", n)
    end
    local mant, ex = smatch(es, "^(.-)e([+-]%d+)$")
    local e = tonumber(ex)
    return mant .. "e" .. (e < 0 and "-" or "+") .. mabs(e)
  end, 1)
  def(NP, "toLocaleString", function(this)
    local n = N(this)
    if n ~= n or mabs(n) >= 1e21 then return numstr(n) end
    local s = to_fixed(n, 3)
    local sign, ip, fp = smatch(s, "^(-?)(%d+)%.?(%d*)$")
    fp = sgsub(fp, "0+$", "")
    local r = string.reverse(sgsub(string.reverse(ip), "(%d%d%d)", "%1,"))
    if ssub(r, 1, 1) == "," then r = ssub(r, 2) end
    ip = r
    return sign .. ip .. (fp ~= "" and ("." .. fp) or "")
  end, 0)

  -- Boolean
  local BP = BooleanProto
  mkctor("Boolean", 1, function(this, v) return to_bool(v) end, function(proto, v) return to_bool(v) end, BP)
  def(BP, "toString", function(this) return this and "true" or "false" end, 0)
  def(BP, "valueOf", function(this) return this end, 0)

  -- Math
  local M = new_obj(ObjectProto)
  sethidden(G, "Math", M)
  sethidden(M, "PI", math.pi)
  sethidden(M, "E", math.exp(1))
  sethidden(M, "LN2", math.log(2))
  sethidden(M, "LN10", math.log(10))
  sethidden(M, "LOG2E", 1 / math.log(2))
  sethidden(M, "LOG10E", 1 / math.log(10))
  sethidden(M, "SQRT2", math.sqrt(2))
  sethidden(M, "SQRT1_2", math.sqrt(0.5))
  local function m1(name, f)
    def(M, name, function(this, x)
      if type(x) ~= "number" then x = to_num(x) end
      return f(x)
    end, 1)
  end
  m1("abs", mabs)
  m1("floor", function(x) if x ~= x or x == huge or x == -huge then return x end return ffloor(x) end)
  m1("ceil", function(x) if x ~= x or x == huge or x == -huge then return x end return math.ceil(x) + 0.0 end)
  m1("trunc", function(x)
    if x ~= x or x == huge or x == -huge then return x end
    if x >= 0 then return ffloor(x) end
    return -ffloor(-x)
  end)
  m1("round", function(x)
    if x ~= x or x == huge or x == -huge or x % 1 == 0 then return x end
    return ffloor(x + 0.5)
  end)
  m1("sign", function(x) if x ~= x or x == 0 then return x end return x > 0 and 1.0 or -1.0 end)
  m1("sqrt", function(x) if x < 0 then return NaN end return math.sqrt(x) end)
  m1("cbrt", function(x)
    if x ~= x or x == 0 or x == huge or x == -huge then return x end
    local r = mabs(x) ^ (1 / 3)
    local rr = floor(r + 0.5)
    if rr * rr * rr == mabs(x) then r = rr + 0.0 end
    return x < 0 and -r or r
  end)
  m1("exp", math.exp)
  m1("expm1", function(x) return math.exp(x) - 1 end)
  m1("log", function(x) if x < 0 then return NaN end return math.log(x) end)
  m1("log1p", function(x) if x < -1 then return NaN end return math.log(1 + x) end)
  m1("log2", function(x) if x < 0 then return NaN end return math.log(x, 2) end)
  m1("log10", function(x) if x < 0 then return NaN end return math.log(x, 10) end)
  m1("sin", math.sin); m1("cos", math.cos); m1("tan", math.tan)
  m1("asin", function(x) if x > 1 or x < -1 then return NaN end return math.asin(x) end)
  m1("acos", function(x) if x > 1 or x < -1 then return NaN end return math.acos(x) end)
  m1("atan", math.atan)
  m1("sinh", function(x) return (math.exp(x) - math.exp(-x)) / 2 end)
  m1("cosh", function(x) return (math.exp(x) + math.exp(-x)) / 2 end)
  m1("tanh", function(x)
    if x > 20 then return 1.0 elseif x < -20 then return -1.0 end
    local a, b = math.exp(x), math.exp(-x)
    return (a - b) / (a + b)
  end)
  m1("fround", function(x) return x end)
  m1("clz32", function(x)
    local n = to_int32(x) & 0xFFFFFFFF
    if n == 0 then return 32.0 end
    local c = 0
    while n & 0x80000000 == 0 do n = n << 1; c = c + 1 end
    return c + 0.0
  end)
  def(M, "atan2", function(this, y, x) return math.atan(to_num(y), to_num(x)) end, 2)
  def(M, "pow", function(this, a, b) return pow(to_num(a), to_num(b)) end, 2)
  def(M, "imul", function(this, a, b)
    local r = (to_int32(to_num(a)) * to_int32(to_num(b))) & 0xFFFFFFFF
    if r >= 0x80000000 then r = r - 0x100000000 end
    return r + 0.0
  end, 2)
  def(M, "random", function() return math.random() end, 0)
  def(M, "max", function(this, ...)
    local r = -huge
    for i = 1, select("#", ...) do
      local v = to_num((select(i, ...)))
      if v ~= v then return NaN end
      if v > r then r = v end
    end
    return r
  end, 2)
  def(M, "min", function(this, ...)
    local r = huge
    for i = 1, select("#", ...) do
      local v = to_num((select(i, ...)))
      if v ~= v then return NaN end
      if v < r then r = v end
    end
    return r
  end, 2)
  def(M, "hypot", function(this, ...)
    local sum = 0.0
    for i = 1, select("#", ...) do
      local v = to_num((select(i, ...)))
      sum = sum + v * v
    end
    return math.sqrt(sum)
  end, 2)

  -- global functions
  local g = G
  def(g, "parseInt", function(this, s, r) return parse_int(s, r) end, 2)
  def(g, "parseFloat", function(this, s) return parse_float(s) end, 1)
  def(g, "isNaN", function(this, v) local n = to_num(v); return n ~= n end, 1)
  def(g, "isFinite", function(this, v) local n = to_num(v); return n == n and n ~= huge and n ~= -huge end, 1)
  sethidden(g, "NaN", NaN)
  sethidden(g, "Infinity", huge)
  sethidden(g, "undefined", nil)
  sethidden(g, "globalThis", g)
  sethidden(g, "window", g)
  sethidden(g, "self", g)
  def(g, "eval", function() throw_error("EvalError", "eval is not supported") end, 1)
  -- URI functions (bytes in, bytes out)
  local UNRES = "A-Za-z0-9%-_%.!~%*'%(%)"
  local RESERVED_URI = ";/%?:@&=%+%$,#"
  local function encoder(keep)
    return function(this, v)
      local s = to_str(v)
      return (sgsub(s, "[^" .. keep .. "]", function(c) return sfmt("%%%02X", sbyte(c)) end))
    end
  end
  def(g, "encodeURIComponent", encoder(UNRES), 1)
  def(g, "encodeURI", encoder(UNRES .. RESERVED_URI), 1)
  local function decoder(keepset)
    return function(this, v)
      local s = to_str(v)
      local bad = false
      local r = sgsub(s, "%%(.?.?)", function(h)
        if not sfind(h, "^%x%x$") then bad = true; return "" end
        local c = schar(tonumber(h, 16))
        if keepset and sfind(keepset, c, 1, true) then return "%" .. h end
        return c
      end)
      if bad then throw_error("URIError", "URI malformed") end
      return r
    end
  end
  def(g, "decodeURIComponent", decoder(nil), 1)
  def(g, "decodeURI", decoder(";/?:@&=+$,#"), 1)
end
end

local setup_json, setup_date, setup_collections, setup_promise, setup_timers, setup_console, setup_misc
do

---------------------------------------------------------------------------
-- JSON
---------------------------------------------------------------------------
local JSON_ESC = { ['"'] = '"', ["\\"] = "\\", ["/"] = "/", b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" }

local function json_parse(s)
  local pos, len, depth = 1, #s, 0
  charge(len >> 6)
  local function err(what)
    local c = ssub(s, pos, pos)
    if pos > len then throw_error("SyntaxError", "Unexpected end of JSON input") end
    throw_error("SyntaxError", (what or "Unexpected token") .. " " .. c .. " in JSON at position " .. (pos - 1))
  end
  local function skip() pos = sfind(s, "[^ \t\r\n]", pos) or (len + 1) end
  local parse_value
  local function parse_string()
    local buf, nb = {}, 0
    pos = pos + 1
    while true do
      local e = sfind(s, '["\\\0-\31]', pos)
      if not e then pos = len + 1; err() end
      nb = nb + 1; buf[nb] = ssub(s, pos, e - 1)
      local c = sbyte(s, e)
      if c == 34 then pos = e + 1; break
      elseif c == 92 then
        local x = ssub(s, e + 1, e + 1)
        if JSON_ESC[x] then nb = nb + 1; buf[nb] = JSON_ESC[x]; pos = e + 2
        elseif x == "u" then
          local h = smatch(s, "^%x%x%x%x", e + 2)
          if not h then pos = e + 1; err("Bad escaped character") end
          local cp = tonumber(h, 16)
          pos = e + 6
          if cp >= 0xD800 and cp <= 0xDBFF then
            local lo = smatch(s, "^\\u(%x%x%x%x)", pos)
            lo = lo and tonumber(lo, 16)
            if lo and lo >= 0xDC00 and lo <= 0xDFFF then
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00)
              pos = pos + 6
            end
          end
          if cp >= 0xD800 and cp <= 0xDFFF then cp = 0xFFFD end
          nb = nb + 1; buf[nb] = utf8_encode(cp)
        else pos = e + 1; err("Bad escaped character") end
      else pos = e; err("Bad control character in string literal") end
    end
    return tconcat(buf)
  end
  function parse_value()
    skip()
    local c = sbyte(s, pos)
    if c == 123 then
      depth = depth + 1
      if depth > MAX_DEPTH then throw_error("RangeError", "JSON nesting too deep") end
      local o = new_obj(ObjectProto)
      pos = pos + 1
      skip()
      if sbyte(s, pos) == 125 then pos = pos + 1; depth = depth - 1; return o end
      while true do
        skip()
        if sbyte(s, pos) ~= 34 then err() end
        local k = parse_string()
        skip()
        if sbyte(s, pos) ~= 58 then err() end
        pos = pos + 1
        setown(o, k, parse_value())
        skip()
        local d = sbyte(s, pos)
        if d == 44 then pos = pos + 1 elseif d == 125 then pos = pos + 1; break else err() end
      end
      depth = depth - 1
      return o
    elseif c == 91 then
      depth = depth + 1
      if depth > MAX_DEPTH then throw_error("RangeError", "JSON nesting too deep") end
      local list, n = {}, 0
      pos = pos + 1
      skip()
      if sbyte(s, pos) == 93 then pos = pos + 1; depth = depth - 1; return new_arr(list, 0) end
      while true do
        n = n + 1
        list[n] = parse_value()
        array_check_len(n)
        skip()
        local d = sbyte(s, pos)
        if d == 44 then pos = pos + 1 elseif d == 93 then pos = pos + 1; break else err() end
      end
      depth = depth - 1
      return new_arr(list, n)
    elseif c == 34 then return parse_string()
    elseif c == 116 and ssub(s, pos, pos + 3) == "true" then pos = pos + 4; return true
    elseif c == 102 and ssub(s, pos, pos + 4) == "false" then pos = pos + 5; return false
    elseif c == 110 and ssub(s, pos, pos + 3) == "null" then pos = pos + 4; return null
    elseif c == 45 or (c and c >= 48 and c <= 57) then
      local m = smatch(s, "^-?%d+", pos)
      if not m then err() end
      local digits = sgsub(m, "^-", "")
      if #digits > 1 and ssub(digits, 1, 1) == "0" then pos = pos + #m - #digits + 1; err() end
      local e = pos + #m
      local frac = smatch(s, "^%.%d+", e)
      if frac then e = e + #frac elseif sbyte(s, e) == 46 then pos = e + 1; err() end
      local ex = smatch(s, "^[eE][+-]?%d+", e)
      if ex then e = e + #ex end
      local v = tonumber(ssub(s, pos, e - 1)) + 0.0
      pos = e
      return v
    end
    err()
  end
  local v = parse_value()
  skip()
  if pos <= len then err() end
  return v
end

local function json_quote(s)
  return '"' .. sgsub(s, '[\0-\31"\\]', function(c)
    if c == '"' then return '\\"' elseif c == "\\" then return "\\\\"
    elseif c == "\n" then return "\\n" elseif c == "\r" then return "\\r" elseif c == "\t" then return "\\t"
    elseif c == "\b" then return "\\b" elseif c == "\f" then return "\\f" end
    return sfmt("\\u%04x", sbyte(c))
  end) .. '"'
end

local function json_stringify(value, replacer, space)
  local indent = ""
  if type(space) == "number" then indent = srep(" ", math.max(0, math.min(10, floor(space))))
  elseif type(space) == "string" then indent = ssub(space, 1, 10) end
  local rfn, whitelist
  if callable(replacer) then rfn = replacer.fn
  elseif type(replacer) == "table" and replacer.cls == "Array" then
    whitelist = {}
    for i = 1, replacer.len do
      local k = replacer.a[i]
      if type(k) == "string" or type(k) == "number" then whitelist[#whitelist + 1] = to_str(k) end
    end
  end
  local stack, depth = {}, 0
  local ser
  function ser(holder, key, v, cur)
    if is_obj(v) then
      local tj = getv(v, "toJSON")
      if callable(tj) then v = tj.fn(v, key) end
    end
    if rfn then v = rfn(holder, key, v) end
    local t = type(v)
    if v == null then return "null"
    elseif t == "boolean" then return v and "true" or "false"
    elseif t == "number" then
      if v ~= v or v == huge or v == -huge then return "null" end
      return numstr(v)
    elseif t == "string" then return json_quote(v)
    elseif t ~= "table" or v.sym or v.fn then return nil end
    if stack[v] then throw_error("TypeError", "Converting circular structure to JSON") end
    depth = depth + 1
    if depth > MAX_DEPTH then throw_error("RangeError", "JSON nesting too deep") end
    stack[v] = true
    local inner = cur .. indent
    local parts, n = {}, 0
    local res
    if v.cls == "Array" then
      for i = 1, v.len do
        n = n + 1
        parts[n] = ser(v, numstr(i - 1), v.a[i], inner) or "null"
      end
      if n == 0 then res = "[]"
      elseif indent == "" then res = "[" .. tconcat(parts, ",") .. "]"
      else res = "[\n" .. inner .. tconcat(parts, ",\n" .. inner) .. "\n" .. cur .. "]" end
    else
      local ks, kn
      if whitelist then ks, kn = whitelist, #whitelist else ks, kn = own_keys(v) end
      for i = 1, kn do
        local k = ks[i]
        if not whitelist or has_prop(v, k) then
          local sv = ser(v, k, getv(v, k), inner)
          if sv then
            n = n + 1
            parts[n] = json_quote(k) .. (indent == "" and ":" or ": ") .. sv
          end
        end
      end
      if n == 0 then res = "{}"
      elseif indent == "" then res = "{" .. tconcat(parts, ",") .. "}"
      else res = "{\n" .. inner .. tconcat(parts, ",\n" .. inner) .. "\n" .. cur .. "}" end
    end
    stack[v] = nil
    depth = depth - 1
    if #res > MAX_STR then throw_error("RangeError", "Invalid string length") end
    return res
  end
  local holder = new_obj(ObjectProto)
  setown(holder, "", value)
  local res = ser(holder, "", value, "")
  if res then charge(#res >> 6) end
  return res
end

function setup_json(rt)
  local J = new_obj(ObjectProto)
  sethidden(G, "JSON", J)
  def(J, "parse", function(this, text, reviver)
    local v = json_parse(to_str(text))
    if callable(reviver) then
      local function walk(holder, key)
        local val = getv(holder, key)
        if is_obj(val) then
          local ks, n = own_keys(val)
          for i = 1, n do
            local nv = walk(val, ks[i])
            if nv == nil then del_prop(val, ks[i]) else setv(val, ks[i], nv) end
          end
        end
        return reviver.fn(holder, key, val)
      end
      local root = new_obj(ObjectProto)
      setown(root, "", v)
      return walk(root, "")
    end
    return v
  end, 2)
  def(J, "stringify", function(this, v, replacer, space) return json_stringify(v, replacer, space) end, 3)
end

---------------------------------------------------------------------------
-- Date (UTC only)
---------------------------------------------------------------------------
local MONTHS = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }
local DAYS = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }
local MONTH_IDX = {}
for i, m in ipairs(MONTHS) do MONTH_IDX[slower(m)] = i end

local function days_from_civil(y, m, d)
  if m <= 2 then y = y - 1 end
  local era = y // 400
  local yoe = y - era * 400
  local mp = (m + 9) % 12
  local doy = (153 * mp + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end
local function civil_from_days(z)
  z = z + 719468
  local era = z // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local y = yoe + era * 400
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp < 10 and mp + 3 or mp - 9
  if m <= 2 then y = y + 1 end
  return y, m, d
end

local function make_time(y, mo, d, h, mi, s, ms)
  for _, v in ipairs({ y, mo, d, h, mi, s, ms }) do
    if v ~= v or v == huge or v == -huge then return NaN end
  end
  y, mo = floor(y) + 0.0, floor(mo) + 0.0
  y = y + mo // 12
  mo = mo % 12
  local days = days_from_civil(y, mo + 1, 1.0) + floor(d) - 1
  local t = days * 86400000.0 + floor(h) * 3600000.0 + floor(mi) * 60000.0 + floor(s) * 1000.0 + floor(ms)
  if mabs(t) > 8.64e15 then return NaN end
  return t + 0.0
end

-- t -> y, month (0-11), day, h, mi, s, ms, weekday
local function decompose(t)
  local days = t // 86400000
  local rem = t - days * 86400000
  local y, m, d = civil_from_days(days)
  local h = rem // 3600000
  rem = rem - h * 3600000
  local mi = rem // 60000
  rem = rem - mi * 60000
  local s = rem // 1000
  return y, m - 1, d, h, mi, s, rem - s * 1000, (days + 4) % 7
end

local function pad2(n) return sfmt("%02d", n) end
local function date_iso(t)
  local y, mo, d, h, mi, s, ms = decompose(t)
  local ys
  if y >= 0 and y <= 9999 then ys = sfmt("%04d", y) elseif y < 0 then ys = sfmt("-%06d", -y) else ys = sfmt("+%06d", y) end
  return sfmt("%s-%02d-%02dT%02d:%02d:%02d.%03dZ", ys, mo + 1, d, h, mi, s, ms)
end

local function date_parse(str)
  local s = trim_right(trim_left(str))
  local y, mo, d, rest = smatch(s, "^([+-]?%d%d%d%d%d?%d?)-(%d%d)-(%d%d)(.*)$")
  if not y then
    y, mo = smatch(s, "^(%d%d%d%d)-(%d%d)$")
    if y then d, rest = "01", "" end
  end
  if not y then
    y = smatch(s, "^(%d%d%d%d)$")
    if y then mo, d, rest = "01", "01", "" end
  end
  local h, mi, sec, ms, tz = 0, 0, 0, 0, ""
  if y then
    if rest ~= "" then
      local hh, mm, ss, frac, tzs = smatch(rest, "^[T ](%d%d):(%d%d):?(%d*)%.?(%d*)(.*)$")
      if not hh then return NaN end
      h, mi, sec = tonumber(hh), tonumber(mm), tonumber(ss) or 0
      if frac ~= "" then ms = floor(tonumber("0." .. frac) * 1000 + 0.5) end
      tz = tzs
    end
    y, mo, d = tonumber(y), tonumber(mo), tonumber(d)
  else
    local dd, mn, yy, tail = smatch(s, "^%a+,%s*(%d+)%s+(%a+)%s+(%d+)%s*(.*)$")
    if not dd then dd, mn, yy, tail = smatch(s, "^(%d+)%s+(%a+)%s+(%d+)%s*(.*)$") end
    if not dd then mn, dd, yy, tail = smatch(s, "^%a+,%s*(%a+)%.?%s+(%d+),?%s+(%d+)%s*(.*)$") end
    if not dd then mn, dd, yy, tail = smatch(s, "^(%a+)%.?%s+(%d+),?%s+(%d+)%s*(.*)$") end
    if not dd then return NaN end
    mo = MONTH_IDX[slower(ssub(mn, 1, 3))]
    if not mo then return NaN end
    y, d = tonumber(yy), tonumber(dd)
    local hh, mm, ss, tzs = smatch(tail, "^(%d+):(%d+):?(%d*)%s*(.*)$")
    if hh then h, mi, sec, tz = tonumber(hh), tonumber(mm), tonumber(ss) or 0, tzs end
  end
  if mo < 1 or mo > 12 or d < 1 or d > 31 or h > 24 or mi > 59 or sec > 59 then return NaN end
  local t = make_time(y + 0.0, mo - 1.0, d + 0.0, h + 0.0, mi + 0.0, sec + 0.0, ms + 0.0)
  local sign, th, tm = smatch(tz, "([+-])(%d%d):?(%d%d)")
  if sign then
    local off = (tonumber(th) * 60 + tonumber(tm)) * 60000.0
    t = sign == "+" and t - off or t + off
  end
  return t
end

function setup_date(rt)
  local DP = new_obj(ObjectProto)
  rt.date_iso = date_iso
  rt.DateProto = DP
  local function now()
    local n = R.now_ms
    if n then return n + 0.0 end
    if os_time then return os_time() * 1000.0 end
    return 0.0
  end
  local function clip(t)
    if t ~= t or mabs(t) > 8.64e15 then return NaN end
    return (t >= 0 and floor(t) or -floor(-t)) + 0.0
  end
  local function new_date(proto, t)
    local o = new_obj(proto)
    o.cls = "Date"
    o.t = t
    return o
  end
  local function tostr(t)
    if t ~= t then return "Invalid Date" end
    local y, mo, d, h, mi, s, _, wd = decompose(t)
    return sfmt("%s %s %02d %04d %02d:%02d:%02d GMT+0000 (UTC)", DAYS[wd + 1], MONTHS[mo + 1], d, y, h, mi, s)
  end
  local function ctor(proto, ...)
    local n = select("#", ...)
    if n == 0 then return new_date(proto, now()) end
    local a = ...
    if n == 1 then
      if type(a) == "table" and a.cls == "Date" then return new_date(proto, a.t) end
      if type(a) == "table" then a = to_prim(a) end
      if type(a) == "string" then return new_date(proto, clip(date_parse(a))) end
      return new_date(proto, clip(to_num(a)))
    end
    local c = { ... }
    local y = to_num(c[1])
    if y == y and y >= 0 and y <= 99 then y = 1900 + floor(y) end
    return new_date(proto, clip(make_time(y, to_num(c[2]), c[3] ~= nil and to_num(c[3]) or 1.0,
      c[4] ~= nil and to_num(c[4]) or 0.0, c[5] ~= nil and to_num(c[5]) or 0.0,
      c[6] ~= nil and to_num(c[6]) or 0.0, c[7] ~= nil and to_num(c[7]) or 0.0)))
  end
  local D = mkctor("Date", 7, function() return tostr(now()) end, ctor, DP)
  def(D, "now", function() return now() end, 0)
  def(D, "parse", function(this, s) return clip(date_parse(to_str(s))) end, 1)
  def(D, "UTC", function(this, ...)
    local c = { ... }
    local y = to_num(c[1])
    if y == y and y >= 0 and y <= 99 then y = 1900 + floor(y) end
    return clip(make_time(y, c[2] ~= nil and to_num(c[2]) or 0.0, c[3] ~= nil and to_num(c[3]) or 1.0,
      c[4] ~= nil and to_num(c[4]) or 0.0, c[5] ~= nil and to_num(c[5]) or 0.0,
      c[6] ~= nil and to_num(c[6]) or 0.0, c[7] ~= nil and to_num(c[7]) or 0.0))
  end, 7)
  local function T(this)
    if type(this) ~= "table" or this.cls ~= "Date" then throw_error("TypeError", "this is not a Date object.") end
    return this.t
  end
  local function both(name, fn, nargs)
    def(DP, "get" .. name, fn, nargs or 0)
    def(DP, "getUTC" .. name, fn, nargs or 0)
  end
  local function getter(name, idx)
    both(name, function(this)
      local t = T(this)
      if t ~= t then return NaN end
      return (select(idx, decompose(t))) + 0.0
    end)
  end
  getter("FullYear", 1); getter("Month", 2); getter("Date", 3); getter("Hours", 4)
  getter("Minutes", 5); getter("Seconds", 6); getter("Milliseconds", 7); getter("Day", 8)
  def(DP, "getYear", function(this) local t = T(this); if t ~= t then return NaN end return (decompose(t)) - 1900.0 end, 0)
  def(DP, "getTime", function(this) return T(this) end, 0)
  def(DP, "valueOf", function(this) return T(this) end, 0)
  def(DP, "getTimezoneOffset", function(this) local t = T(this); if t ~= t then return NaN end return 0.0 end, 0)
  def(DP, "setTime", function(this, v) T(this); this.t = clip(to_num(v)); return this.t end, 1)
  local function setter(name, start, maxn)
    local fn = function(this, ...)
      local t = T(this)
      local n = select("#", ...)
      if t ~= t then
        if start ~= 1 then return NaN end
        t = 0.0
      end
      local y, mo, d, h, mi, s, ms = decompose(t)
      local comps = { y + 0.0, mo + 0.0, d + 0.0, h + 0.0, mi + 0.0, s + 0.0, ms + 0.0 }
      local args = { ... }
      if n == 0 then this.t = NaN; return NaN end
      for i = 1, math.min(n, maxn) do comps[start + i - 1] = to_num(args[i]) end
      this.t = clip(make_time(tunpack(comps)))
      return this.t
    end
    def(DP, "set" .. name, fn, maxn)
    def(DP, "setUTC" .. name, fn, maxn)
  end
  setter("Milliseconds", 7, 1); setter("Seconds", 6, 2); setter("Minutes", 5, 3); setter("Hours", 4, 4)
  setter("Date", 3, 1); setter("Month", 2, 2); setter("FullYear", 1, 3)
  def(DP, "toISOString", function(this)
    local t = T(this)
    if t ~= t then throw_error("RangeError", "Invalid time value") end
    return date_iso(t)
  end, 0)
  def(DP, "toJSON", function(this)
    local t = T(this)
    if t ~= t then return null end
    return date_iso(t)
  end, 1)
  def(DP, "toString", function(this) return tostr(T(this)) end, 0)
  def(DP, "toDateString", function(this)
    local t = T(this)
    if t ~= t then return "Invalid Date" end
    local y, mo, d, _, _, _, _, wd = decompose(t)
    return sfmt("%s %s %02d %04d", DAYS[wd + 1], MONTHS[mo + 1], d, y)
  end, 0)
  def(DP, "toTimeString", function(this)
    local t = T(this)
    if t ~= t then return "Invalid Date" end
    local _, _, _, h, mi, s = decompose(t)
    return sfmt("%02d:%02d:%02d GMT+0000 (UTC)", h, mi, s)
  end, 0)
  local function utcstr(this)
    local t = T(this)
    if t ~= t then return "Invalid Date" end
    local y, mo, d, h, mi, s, _, wd = decompose(t)
    return sfmt("%s, %02d %s %04d %02d:%02d:%02d GMT", DAYS[wd + 1], d, MONTHS[mo + 1], y, h, mi, s)
  end
  def(DP, "toUTCString", utcstr, 0)
  def(DP, "toGMTString", utcstr, 0)
  local function ldate(t)
    local y, mo, d = decompose(t)
    return sfmt("%d/%d/%d", mo + 1, d, y)
  end
  local function ltime(t)
    local _, _, _, h, mi, s = decompose(t)
    return sfmt("%d:%02d:%02d %s", (h % 12 == 0) and 12 or h % 12, mi, s, h < 12 and "AM" or "PM")
  end
  def(DP, "toLocaleDateString", function(this) local t = T(this); if t ~= t then return "Invalid Date" end return ldate(t) end, 0)
  def(DP, "toLocaleTimeString", function(this) local t = T(this); if t ~= t then return "Invalid Date" end return ltime(t) end, 0)
  def(DP, "toLocaleString", function(this)
    local t = T(this)
    if t ~= t then return "Invalid Date" end
    return ldate(t) .. ", " .. ltime(t)
  end, 0)
  rt.new_date = new_date
end

---------------------------------------------------------------------------
-- Map, Set, WeakMap, WeakSet
---------------------------------------------------------------------------
local function ckey(k)
  if k ~= k then return NANKEY end
  if k == nil then return UNDEFKEY end
  return k
end

local function coll_new(proto, cls)
  local m = new_obj(proto)
  m.cls = cls
  m.keys, m.vals, m.idx, m.top, m.count = {}, {}, {}, 0, 0
  return m
end

local function coll_compact(m)
  local keys, vals, idx, n = {}, {}, {}, 0
  for i = 1, m.top do
    local k = m.keys[i]
    if k ~= nil then
      n = n + 1
      keys[n], vals[n], idx[k] = k, m.vals[i], n
    end
  end
  m.keys, m.vals, m.idx, m.top = keys, vals, idx, n
end

local function coll_set(m, k, v)
  local nk = ckey(k)
  local i = m.idx[nk]
  if i then m.vals[i] = v; return end
  if m.top > 64 and m.count * 4 < m.top then coll_compact(m) end
  local t = m.top + 1
  m.top = t
  m.keys[t], m.vals[t], m.idx[nk] = nk, v, t
  m.count = m.count + 1
  if m.count > MAX_ARR then throw_error("RangeError", "Map/Set too large") end
end

local function coll_delete(m, k)
  local nk = ckey(k)
  local i = m.idx[nk]
  if not i then return false end
  m.idx[nk], m.keys[i], m.vals[i] = nil, nil, nil
  m.count = m.count - 1
  return true
end

function setup_collections(rt)
  local MP, SP = new_obj(ObjectProto), new_obj(ObjectProto)
  local function chk(this, cls, name)
    if type(this) ~= "table" or this.cls ~= cls then
      throw_error("TypeError", "Method " .. cls .. ".prototype." .. name .. " called on incompatible receiver")
    end
    return this
  end
  local function need_new(name)
    return function() throw_error("TypeError", "Constructor " .. name .. " requires 'new'") end
  end
  mkctor("Map", 0, need_new("Map"), function(proto, it)
    local m = coll_new(proto, "Map")
    if it ~= nil and it ~= null then
      local nextf = get_iter(it)
      while true do
        local ok, e = nextf()
        if not ok then break end
        if not is_obj(e) then throw_error("TypeError", "Iterator value " .. to_str(e) .. " is not an entry object") end
        coll_set(m, getv(e, 0.0), getv(e, 1.0))
      end
    end
    return m
  end, MP)
  mkctor("Set", 0, need_new("Set"), function(proto, it)
    local m = coll_new(proto, "Set")
    if it ~= nil and it ~= null then
      local nextf = get_iter(it)
      while true do
        local ok, e = nextf()
        if not ok then break end
        coll_set(m, e, true)
      end
    end
    return m
  end, SP)
  def(MP, "get", function(this, k)
    local m = chk(this, "Map", "get")
    local i = m.idx[ckey(k)]
    if i then return m.vals[i] end
  end, 1)
  def(MP, "set", function(this, k, v) coll_set(chk(this, "Map", "set"), k, v); return this end, 2)
  def(MP, "has", function(this, k) return chk(this, "Map", "has").idx[ckey(k)] ~= nil end, 1)
  def(SP, "has", function(this, k) return chk(this, "Set", "has").idx[ckey(k)] ~= nil end, 1)
  def(SP, "add", function(this, k) coll_set(chk(this, "Set", "add"), k, true); return this end, 1)
  def(MP, "delete", function(this, k) return coll_delete(chk(this, "Map", "delete"), k) end, 1)
  def(SP, "delete", function(this, k) return coll_delete(chk(this, "Set", "delete"), k) end, 1)
  local function clear(this)
    this.keys, this.vals, this.idx, this.top, this.count = {}, {}, {}, 0, 0
  end
  def(MP, "clear", function(this) clear(chk(this, "Map", "clear")) end, 0)
  def(SP, "clear", function(this) clear(chk(this, "Set", "clear")) end, 0)
  defacc(MP, "size", function(this) return chk(this, "Map", "size").count + 0.0 end)
  defacc(SP, "size", function(this) return chk(this, "Set", "size").count + 0.0 end)
  local function unkey(k)
    if k == NANKEY then return NaN elseif k == UNDEFKEY then return nil end
    return k
  end
  local function iterator(m, mode)
    local i = 0
    return make_iter(function()
      while i < m.top do
        i = i + 1
        local k = m.keys[i]
        if k ~= nil then
          k = unkey(k)
          if mode == "keys" then return true, k
          elseif mode == "values" then return true, m.vals[i]
          else return true, new_arr({ k, m.vals[i] }, 2) end
        end
      end
      return false
    end)
  end
  local function foreach(cls, isset)
    return function(this, cb, thisArg)
      local m = chk(this, cls, "forEach")
      local fn = need_fn(cb)
      local i = 0
      while i < m.top do
        i = i + 1
        local k = m.keys[i]
        if k ~= nil then
          k = unkey(k)
          if isset then fn(thisArg, k, k, m) else fn(thisArg, m.vals[i], k, m) end
        end
      end
    end
  end
  def(MP, "forEach", foreach("Map", false), 1)
  def(SP, "forEach", foreach("Set", true), 1)
  def(MP, "keys", function(this) return iterator(chk(this, "Map", "keys"), "keys") end, 0)
  def(MP, "values", function(this) return iterator(chk(this, "Map", "values"), "values") end, 0)
  def(MP, "entries", function(this) return iterator(chk(this, "Map", "entries"), "entries") end, 0)
  sethidden(MP, symIterator, MP.p.entries)
  def(SP, "values", function(this) return iterator(chk(this, "Set", "values"), "keys") end, 0)
  sethidden(SP, "keys", SP.p.values)
  def(SP, "entries", function(this)
    local m = chk(this, "Set", "entries")
    local it = iterator(m, "keys")
    local nf = it.nextf
    it.nextf = function()
      local ok, k = nf()
      if ok then return true, new_arr({ k, k }, 2) end
      return false
    end
    return it
  end, 0)
  sethidden(SP, symIterator, SP.p.values)

  -- weak collections: keys are objects, held weakly
  local function weak(name, isset)
    local proto = new_obj(ObjectProto)
    mkctor(name, 0, need_new(name), function(pr, it)
      local m = new_obj(pr)
      m.cls = name
      m.weak = setmetatable({}, { __mode = "k" })
      if it ~= nil and it ~= null then
        local nextf = get_iter(it)
        while true do
          local ok, e = nextf()
          if not ok then break end
          local k, v = e, true
          if not isset then k, v = getv(e, 0.0), getv(e, 1.0) end
          if not is_obj(k) then throw_error("TypeError", "Invalid value used in weak collection") end
          m.weak[k] = (v == nil) and UNDEF or v
        end
      end
      return m
    end, proto)
    def(proto, "has", function(this, k) return is_obj(k) and chk(this, name, "has").weak[k] ~= nil end, 1)
    def(proto, "delete", function(this, k)
      local m = chk(this, name, "delete")
      if is_obj(k) and m.weak[k] ~= nil then m.weak[k] = nil; return true end
      return false
    end, 1)
    if isset then
      def(proto, "add", function(this, k)
        if not is_obj(k) then throw_error("TypeError", "Invalid value used in weak set") end
        chk(this, name, "add").weak[k] = true
        return this
      end, 1)
    else
      def(proto, "get", function(this, k)
        if not is_obj(k) then return nil end
        local v = chk(this, name, "get").weak[k]
        if v == UNDEF then return nil end
        return v
      end, 1)
      def(proto, "set", function(this, k, v)
        if not is_obj(k) then throw_error("TypeError", "Invalid value used as weak map key") end
        chk(this, name, "set").weak[k] = (v == nil) and UNDEF or v
        return this
      end, 2)
    end
  end
  weak("WeakMap", false)
  weak("WeakSet", true)
end

---------------------------------------------------------------------------
-- Promise, timers, console, misc globals
---------------------------------------------------------------------------
local function enqueue(job)
  local rt = R
  rt.mqt = rt.mqt + 1
  rt.mq[rt.mqt] = job
end

local function report(msg)
  local cb = R.on_console
  if cb then cb("error", msg) end
end

local function guard(ok, e)
  if not ok and getmetatable(e) == Fatal then error(e, 0) end
end

function setup_promise(rt)
  local PP = new_obj(ObjectProto)
  rt.PromiseProto = PP
  local resolve_promise, reject_promise
  local function new_promise(proto)
    local p = new_obj(proto or PP)
    p.cls = "Promise"
    p.state = "pending"
    p.reactions = {}
    return p
  end
  local function run_reaction(p, r)
    enqueue(function()
      local handler = (p.state == "fulfilled") and r.onF or r.onR
      local child = r.child
      if callable(handler) then
        local ok, res = pcall(handler.fn, nil, p.value)
        guard(ok, res)
        if ok then
          if child then resolve_promise(child, res) end
        elseif child then reject_promise(child, err_value(res)) end
      elseif child then
        if p.state == "fulfilled" then resolve_promise(child, p.value) else reject_promise(child, p.value) end
      end
    end)
  end
  local function settle(p, state, value)
    if p.state ~= "pending" then return end
    p.state, p.value = state, value
    local rs = p.reactions
    p.reactions = nil
    for i = 1, #rs do run_reaction(p, rs[i]) end
  end
  function reject_promise(p, reason) settle(p, "rejected", reason) end
  local function resolving(p)
    local done = false
    local res = mkfn(function(_, v)
      if done then return end
      done = true
      resolve_promise(p, v)
    end, "", 1)
    local rej = mkfn(function(_, r)
      if done then return end
      done = true
      reject_promise(p, r)
    end, "", 1)
    return res, rej
  end
  function resolve_promise(p, x)
    if p.state ~= "pending" then return end
    if x == p then return reject_promise(p, make_error("TypeError", "Chaining cycle detected for promise")) end
    if is_obj(x) then
      local ok, th = pcall(getv, x, "then")
      guard(ok, th)
      if not ok then return reject_promise(p, err_value(th)) end
      if callable(th) then
        enqueue(function()
          local res, rej = resolving(p)
          local ok2, e = pcall(th.fn, x, res, rej)
          guard(ok2, e)
          if not ok2 then rej.fn(nil, err_value(e)) end
        end)
        return
      end
    end
    settle(p, "fulfilled", x)
  end
  local function promise_then(p, onF, onR, child)
    local r = { onF = onF, onR = onR, child = child }
    if p.state == "pending" then p.reactions[#p.reactions + 1] = r else run_reaction(p, r) end
    return child
  end
  local function as_promise(v)
    if type(v) == "table" and v.cls == "Promise" then return v end
    local p = new_promise()
    resolve_promise(p, v)
    return p
  end
  local function ctor(proto, executor)
    if not callable(executor) then throw_error("TypeError", "Promise resolver " .. describe(executor) .. " is not a function") end
    local p = new_promise(proto)
    local res, rej = resolving(p)
    local ok, e = pcall(executor.fn, nil, res, rej)
    guard(ok, e)
    if not ok then rej.fn(nil, err_value(e)) end
    return p
  end
  local P = mkctor("Promise", 1, function() throw_error("TypeError", "Promise constructor cannot be invoked without 'new'") end,
    ctor, PP)
  local function chk(this)
    if type(this) ~= "table" or this.cls ~= "Promise" then throw_error("TypeError", "Method Promise.prototype.then called on incompatible receiver") end
    return this
  end
  def(PP, "then", function(this, onF, onR) return promise_then(chk(this), onF, onR, new_promise()) end, 2)
  def(PP, "catch", function(this, onR) return promise_then(chk(this), nil, onR, new_promise()) end, 1)
  def(PP, "finally", function(this, f)
    if not callable(f) then return promise_then(chk(this), nil, nil, new_promise()) end
    local onF = mkfn(function(_, v) f.fn(nil); return v end, "", 1)
    local onR = mkfn(function(_, e) f.fn(nil); throw(e) end, "", 1)
    return promise_then(chk(this), onF, onR, new_promise())
  end, 1)
  def(P, "resolve", function(this, v) return as_promise(v) end, 1)
  def(P, "reject", function(this, r)
    local p = new_promise()
    reject_promise(p, r)
    return p
  end, 1)
  def(P, "all", function(this, it)
    local out = new_promise()
    local ok, list, n = pcall(iter_list, it)
    guard(ok, list)
    if not ok then reject_promise(out, err_value(list)); return out end
    local results, remaining = {}, n
    if n == 0 then resolve_promise(out, new_arr({}, 0)); return out end
    for i = 1, n do
      local p = as_promise(list[i])
      promise_then(p, mkfn(function(_, v)
        results[i] = v
        remaining = remaining - 1
        if remaining == 0 then resolve_promise(out, new_arr(results, n)) end
      end, "", 1), mkfn(function(_, e) reject_promise(out, e) end, "", 1), nil)
    end
    return out
  end, 1)
  def(P, "allSettled", function(this, it)
    local out = new_promise()
    local list, n = iter_list(it)
    local results, remaining = {}, n
    if n == 0 then resolve_promise(out, new_arr({}, 0)); return out end
    local function done(i, o)
      results[i] = o
      remaining = remaining - 1
      if remaining == 0 then resolve_promise(out, new_arr(results, n)) end
    end
    for i = 1, n do
      promise_then(as_promise(list[i]), mkfn(function(_, v)
        done(i, pairs_to_obj({ "status", "fulfilled", "value", v }))
      end, "", 1), mkfn(function(_, e)
        done(i, pairs_to_obj({ "status", "rejected", "reason", e }))
      end, "", 1), nil)
    end
    return out
  end, 1)
  def(P, "race", function(this, it)
    local out = new_promise()
    local list, n = iter_list(it)
    for i = 1, n do
      promise_then(as_promise(list[i]), mkfn(function(_, v) resolve_promise(out, v) end, "", 1),
        mkfn(function(_, e) reject_promise(out, e) end, "", 1), nil)
    end
    return out
  end, 1)
  def(P, "any", function(this, it)
    local out = new_promise()
    local list, n = iter_list(it)
    local errors, remaining = {}, n
    local function fail()
      local e = make_error("Error", "All promises were rejected")
      e.p.name = "AggregateError"
      e.p.errors = new_arr(errors, n)
      reject_promise(out, e)
    end
    if n == 0 then fail(); return out end
    for i = 1, n do
      promise_then(as_promise(list[i]), mkfn(function(_, v) resolve_promise(out, v) end, "", 1),
        mkfn(function(_, e)
          errors[i] = e
          remaining = remaining - 1
          if remaining == 0 then fail() end
        end, "", 1), nil)
    end
    return out
  end, 1)
  def(G, "queueMicrotask", function(this, fn)
    need_fn(fn)
    enqueue(function()
      local ok, e = pcall(fn.fn, nil)
      guard(ok, e)
      if not ok then report(rt:format_error(e, "microtask")) end
    end)
  end, 1)
end

function setup_timers(rt)
  local function now() return R.now_ms or (os_time and os_time() * 1000.0) or 0.0 end
  local function add(fn, ms, repeating, ...)
    if not callable(fn) then return 0.0 end
    ms = to_num(ms)
    if ms ~= ms or ms < 0 then ms = 0 end
    if R.ntimers >= 200 then throw_error("RangeError", "Too many timers") end
    R.timer_id = R.timer_id + 1
    local id = R.timer_id
    R.timers[id] = { id = id, due = now() + ms, ms = ms, fn = fn, args = { ... }, n = select("#", ...), rep = repeating }
    R.ntimers = R.ntimers + 1
    return id + 0.0
  end
  local function clear(this, id)
    id = to_num(id)
    if R.timers[id] then
      R.timers[id] = nil
      R.ntimers = R.ntimers - 1
    end
  end
  def(G, "setTimeout", function(this, fn, ms, ...) return add(fn, ms, false, ...) end, 2)
  def(G, "setInterval", function(this, fn, ms, ...) return add(fn, ms, true, ...) end, 2)
  def(G, "clearTimeout", clear, 1)
  def(G, "clearInterval", clear, 1)
  def(G, "requestAnimationFrame", function(this, fn)
    if not callable(fn) then return 0.0 end
    local t = now()
    return add(mkfn(function() return fn.fn(nil, t) end, "raf", 0), 16, false)
  end, 1)
  def(G, "cancelAnimationFrame", clear, 1)
end

function setup_console(rt)
  local C = new_obj(ObjectProto)
  sethidden(G, "console", C)
  local function emit(level)
    return function(this, ...)
      local cb = R.on_console
      if cb then cb(level, inspect_args(...)) end
    end
  end
  for _, lvl in ipairs({ "log", "info", "warn", "error", "debug" }) do def(C, lvl, emit(lvl), 0) end
  def(C, "trace", emit("debug"), 0)
  def(C, "dir", emit("log"), 0)
  def(C, "assert", function(this, cond, ...)
    if not to_bool(cond) then
      local cb = R.on_console
      if cb then cb("error", "Assertion failed: " .. inspect_args(...)) end
    end
  end, 0)
  for _, nm in ipairs({ "group", "groupEnd", "groupCollapsed", "time", "timeEnd", "timeLog", "count", "table", "clear" }) do
    def(C, nm, function() end, 0)
  end
end

function setup_misc(rt)
  local function clone(v, seen)
    if type(v) ~= "table" or v == null then return v end
    if v.sym or v.fn then
      local e = make_error("Error", "could not be cloned.")
      e.p.name = "DataCloneError"
      throw(e)
    end
    if seen[v] then return seen[v] end
    local cls = v.cls
    local o
    if cls == "Array" then
      o = new_arr({}, v.len)
      seen[v] = o
      for i = 1, v.len do o.a[i] = clone(v.a[i], seen) end
    elseif cls == "Date" then o = rt.new_date(rt.DateProto, v.t); seen[v] = o
    elseif cls == "RegExp" then o = new_regexp(v.prog, v.source, v.flags); seen[v] = o
    elseif cls == "Map" or cls == "Set" then
      o = coll_new(v.proto, cls)
      seen[v] = o
      for i = 1, v.top do
        local k = v.keys[i]
        if k ~= nil then
          if k == NANKEY then k = NaN elseif k == UNDEFKEY then k = nil end
          coll_set(o, clone(k, seen), clone(v.vals[i], seen))
        end
      end
    else
      o = new_obj(ObjectProto)
      seen[v] = o
      local ks, n = own_keys(v)
      for i = 1, n do setown(o, ks[i], clone(getv(v, ks[i]), seen)) end
    end
    return o
  end
  def(G, "structuredClone", function(this, v) return clone(v, {}) end, 1)
end
end

---------------------------------------------------------------------------
-- 6. Public API
---------------------------------------------------------------------------
local RT = {}
RT.__index = RT

local function drain(rt)
  local n = 0
  local base = rt.depth
  while rt.mqh <= rt.mqt do
    local job = rt.mq[rt.mqh]
    rt.mq[rt.mqh] = nil
    rt.mqh = rt.mqh + 1
    rt.depth = base
    local ok, e = pcall(job)
    if not ok and getmetatable(e) == Fatal then
      rt.mq, rt.mqh, rt.mqt = {}, 1, 0
      return false, e
    end
    n = n + 1
    if n > 200000 then
      rt.mq, rt.mqh, rt.mqt = {}, 1, 0
      return false, setmetatable({ msg = "script too slow (microtask limit)" }, Fatal)
    end
  end
  rt.mq, rt.mqh, rt.mqt = {}, 1, 0
  return true
end

function RT:new_object(proto)
  activate(self)
  if proto == nil then proto = ObjectProto elseif proto == null then proto = nil end
  return new_obj(proto)
end

function RT:new_array(list, n)
  activate(self)
  return new_arr(list or {}, n)
end

function RT:native(fn, name, nargs)
  activate(self)
  return new_native(fn, name, nargs)
end

function RT:get(obj, key)
  activate(self)
  return getv(obj, to_key(key))
end

function RT:set(obj, key, v)
  activate(self)
  setv(obj, to_key(key), v)
end

function RT:define_accessor(obj, key, getter, setter)
  activate(self)
  key = to_key(key)
  if type(key) == "number" then key = numstr(key) end
  obj.p[key] = nil
  defacc(obj, key, getter, setter)
end

function RT:call(fn, this, ...)
  activate(self)
  return call_fn(fn, this, ...)
end

function RT:construct(ctor, ...)
  activate(self)
  return construct(ctor, ctor, ...)
end

function RT:to_string(v) activate(self); return to_str(v) end
function RT:to_number(v) activate(self); return to_num(v) end
function RT:to_boolean(v) return to_bool(v) end
function RT:typeof(v) return typeof(v) end
function RT:is_callable(v) return type(v) == "table" and v.fn ~= nil end
function RT:is_array(v) return type(v) == "table" and v.cls == "Array" end

function RT:array_to_list(arr)
  activate(self)
  local a, n = arraylike(arr)
  return tmove(a, 1, n, 1, {}), n
end

function RT:keys(obj)
  activate(self)
  if not is_obj(obj) then return {}, 0 end
  return own_keys(obj)
end

function RT:format_error(err, name)
  activate(self)
  name = name or "script"
  local function at(line) return " (" .. name .. ":" .. tostring(line or curline) .. ")" end
  if type(err) == "table" then
    if err.syntax then return "SyntaxError: " .. err.syntax .. at(err.line) end
    local mt = getmetatable(err)
    if mt == Fatal then return err.msg .. at() end
    if mt == Throw then
      local v = err.value
      local ok, s = pcall(function()
        if is_obj(v) and (v.cls == "Error" or (has_prop(v, "message") and has_prop(v, "name"))) then
          local nm, msg = to_str(getv(v, "name")), to_str(getv(v, "message"))
          return msg ~= "" and (nm .. ": " .. msg) or nm
        end
        return "Uncaught " .. inspect(v, 0, {}, true)
      end)
      return (ok and s or "Error: (unprintable)") .. at()
    end
  end
  local ok, v = pcall(err_value, err)
  if not ok then
    if type(v) == "table" and v.msg then return v.msg .. at() end
    return "Error: internal error" .. at()
  end
  local ok2, s = pcall(function()
    return to_str(getv(v, "name")) .. ": " .. to_str(getv(v, "message"))
  end)
  return (ok2 and s or "Error") .. at()
end

function RT:run(src, name)
  activate(self)
  name = name or "script"
  local outer = self.level == 0
  if outer then self.steps_left = self.budget; self.depth = 0 end
  self.level = self.level + 1
  self.last = nil
  self.nt = nil
  curline = 0
  local ok, err = pcall(function()
    local ast = parse_program(src)
    local prog = compile_program(ast)
    prog()
  end)
  if outer then
    local dok, derr = drain(self)
    if ok and not dok then ok, err = false, derr end
  end
  self.level = self.level - 1
  if ok then return true, self.last end
  return false, self:format_error(err, name)
end

function RT:call_safe(fn, this, ...)
  activate(self)
  local outer = self.level == 0
  if outer then self.steps_left = self.budget; self.depth = 0 end
  self.level = self.level + 1
  self.nt = nil
  curline = 0
  local ok, res = pcall(call_fn, fn, this, ...)
  local dok, derr = true, nil
  if outer then dok, derr = drain(self) end
  self.level = self.level - 1
  if ok then
    if not dok and self.on_console then self.on_console("error", self:format_error(derr, "microtask")) end
    return true, res
  end
  return false, self:format_error(res, "call")
end

function RT:drain()
  activate(self)
  self.level = self.level + 1
  local ok, e = drain(self)
  self.level = self.level - 1
  if ok then return true end
  return false, self:format_error(e, "microtask")
end

function RT:tick(now_ms)
  activate(self)
  local due = {}
  for _, t in pairs(self.timers) do
    if t.due <= now_ms then due[#due + 1] = t end
  end
  if #due == 0 then return 0 end
  tsort(due, function(a, b)
    if a.due ~= b.due then return a.due < b.due end
    return a.id < b.id
  end)
  local ran = 0
  for i = 1, #due do
    if ran >= 50 then break end
    local t = due[i]
    if self.timers[t.id] == t then
      if t.rep then
        t.due = now_ms + math.max(t.ms, 4)
      else
        self.timers[t.id] = nil
        self.ntimers = self.ntimers - 1
      end
      ran = ran + 1
      local ok, msg = self:call_safe(t.fn, nil, tunpack(t.args, 1, t.n))
      if not ok and self.on_console then self.on_console("error", msg) end
    end
  end
  return ran
end

function RT:next_timer()
  local best
  for _, t in pairs(self.timers) do
    if not best or t.due < best then best = t.due end
  end
  return best
end

function js.new(opts)
  opts = opts or {}
  local rt = setmetatable({}, RT)
  rt.budget = opts.budget or 2000000
  rt.steps_left = rt.budget
  rt.depth, rt.level = 0, 0
  rt.timers, rt.ntimers, rt.timer_id = {}, 0, 0
  rt.mq, rt.mqh, rt.mqt = {}, 1, 0
  rt.ctors = {}
  local OP = { p = {}, cls = "Object" }
  rt.ObjectProto = OP
  rt.FunctionProto = { p = {}, proto = OP, cls = "Function" }
  rt.ArrayProto = { p = {}, proto = OP, cls = "Object" }
  rt.StringProto = { p = {}, proto = OP, cls = "Object" }
  rt.NumberProto = { p = {}, proto = OP, cls = "Object" }
  rt.BooleanProto = { p = {}, proto = OP, cls = "Object" }
  rt.SymbolProto = { p = {}, proto = OP, cls = "Object" }
  rt.RegExpProto = { p = {}, proto = OP, cls = "Object" }
  rt.IteratorProto = { p = {}, proto = OP, cls = "Object" }
  rt.global = { p = {}, proto = OP, cls = "Object" }
  R = nil
  activate(rt)
  setup_errors(rt)
  setup_object(rt)
  setup_function(rt)
  setup_symbol(rt)
  setup_array(rt)
  setup_regexp(rt)
  setup_string(rt)
  setup_number(rt)
  setup_json(rt)
  setup_date(rt)
  setup_collections(rt)
  setup_promise(rt)
  setup_timers(rt)
  setup_console(rt)
  setup_misc(rt)
  local IP = rt.IteratorProto
  def(IP, "next", function(this)
    if type(this) ~= "table" or this.cls ~= "Iterator" then throw_error("TypeError", "next called on incompatible receiver") end
    return iter_result(this.nextf())
  end, 0)
  sethidden(IP, symIterator, mkfn(function(this) return this end, "[Symbol.iterator]", 0))
  return rt
end

js.number_to_string = numstr

return js
