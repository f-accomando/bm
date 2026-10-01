-- Xl: the carts' Lua dialect, translated into Lua 5.4 before it runs.
--
-- What the dialect adds: compound assignments (+= -= *= /= \= %= ^= ..=
-- |= &= ^^= <<= >>= >>>= <<>= >><=), != for ~=, "if (c) x" and
-- "while (c) x" on one line without then/do, "?" for print, \ for floor
-- division, the bit operators & | ^^ ~ << >> >>> <<> >>< on 16.16 numbers,
-- @ % $ for peek, peek2, peek4, // comments, the symbols (P8SCII 128-255)
-- as names, and the escapes \* \# \- \| \+ \^ in strings.
--
-- Numbers stay Lua floats: the literals are written as floats (wrapped to
-- 16.16 as the carts have them), so "a % 0" is nan, not an error; ".."
-- becomes __cat(), which prints numbers the carts' way ("3", not "3.0").
-- The parser builds the output as text; every line keeps its line number
-- (a marker \1n\2 on its first token), so errors point at the cart's code.

local NAME, NUM, STR, OP, EOF = 1, 2, 3, 4, 0

local KEYWORDS = {}
for w in ("and break do else elseif end false for function goto if in local nil not or repeat return then true until while"):gmatch("%a+") do
  KEYWORDS[w] = true
end

local OPS4 = { [">>>="] = true, ["<<>="] = true, [">><="] = true }
local OPS3 = { ["..."] = true, ["..="] = true, [">>>"] = true, ["<<>"] = true, [">><"] = true,
               ["<<="] = true, [">>="] = true, ["^^="] = true }
local OPS2 = { [".."] = true, ["=="] = true, ["~="] = true, ["!="] = true, ["<="] = true, [">="] = true,
               ["<<"] = true, [">>"] = true, ["::"] = true, ["+="] = true, ["-="] = true, ["*="] = true,
               ["/="] = true, ["\\="] = true, ["%="] = true, ["^="] = true, ["|="] = true, ["&="] = true,
               ["^^"] = true }

local ESC = { n = "\n", t = "\t", r = "\r", a = "\a", b = "\b", f = "\f", v = "\v", ["\\"] = "\\",
              ['"'] = '"', ["'"] = "'", ["\n"] = "\n", ["*"] = "\1", ["#"] = "\2", ["-"] = "\3",
              ["|"] = "\4", ["+"] = "\5", ["^"] = "\6" }

-- binary operators: left and right priority (Lua 5.4's, with the dialect's)
local BIN = {
  ["or"] = { 1, 1 }, ["and"] = { 2, 2 },
  ["<"] = { 3, 3 }, [">"] = { 3, 3 }, ["<="] = { 3, 3 }, [">="] = { 3, 3 }, ["~="] = { 3, 3 }, ["=="] = { 3, 3 },
  ["|"] = { 4, 4 }, ["^^"] = { 5, 5 }, ["~"] = { 5, 5 }, ["&"] = { 6, 6 },
  ["<<"] = { 7, 7 }, [">>"] = { 7, 7 }, [">>>"] = { 7, 7 }, ["<<>"] = { 7, 7 }, [">><"] = { 7, 7 },
  [".."] = { 9, 8 }, ["+"] = { 10, 10 }, ["-"] = { 10, 10 },
  ["*"] = { 11, 11 }, ["/"] = { 11, 11 }, ["\\"] = { 11, 11 }, ["%"] = { 11, 11 },
  ["^"] = { 14, 13 },
}
local UNARY = 12
local UNOPS = { ["-"] = true, ["not"] = true, ["#"] = true, ["~"] = "__bnot", ["@"] = "__peek",
                ["%"] = "__peek2", ["$"] = "__peek4" }
local FUNC = { ["&"] = "__band", ["|"] = "__bor", ["^^"] = "__bxor", ["~"] = "__bxor", ["<<"] = "__shl",
               [">>"] = "__shr", [">>>"] = "__lshr", ["<<>"] = "__rotl", [">><"] = "__rotr" }
local COMPOUND = { ["+="] = "+", ["-="] = "-", ["*="] = "*", ["/="] = "/", ["\\="] = "\\", ["%="] = "%",
                   ["^="] = "^", ["..="] = "..", ["|="] = "|", ["&="] = "&", ["^^="] = "^^",
                   ["<<="] = "<<", [">>="] = ">>", [">>>="] = ">>>", ["<<>="] = "<<>", [">><="] = ">><" }
local BLOCK_END = { ["end"] = true, ["else"] = true, ["elseif"] = true, ["until"] = true }

-- ---------------------------------------------------------------- lexer

local K, V, LN, M, P   -- kinds, values, lines, line markers; P = the token at hand

local function lex(src)
  local k, v, ln, mk = {}, {}, {}, {}
  local n, pos, line, len, last = 0, 1, 1, #src, 0
  local function push(kind, val, l)
    n = n + 1
    k[n], v[n], ln[n] = kind, val, l
    if l ~= last then mk[n] = "\1" .. l .. "\2"; last = l else mk[n] = "" end
  end
  local function lines(a, b)
    for _ in sub(src, a, b):gmatch("\n") do line = line + 1 end
  end
  while pos <= len do
    local c = byte(src, pos)
    if c == 10 then
      line = line + 1
      pos = pos + 1
    elseif c == 32 or c == 9 or c == 13 or c == 11 or c == 12 then
      pos = pos + 1
    elseif (c == 45 and byte(src, pos + 1) == 45) or (c == 47 and byte(src, pos + 1) == 47) then
      local eq = c == 45 and src:match("^%[(=*)%[", pos + 2)
      if eq then
        local _, b = find(src, "]" .. eq .. "]", pos + 4 + #eq, true)
        b = b or len
        lines(pos, b)
        pos = b + 1
      else
        pos = find(src, "\n", pos, true) or len + 1
      end
    elseif (c >= 97 and c <= 122) or (c >= 65 and c <= 90) or c == 95 or c >= 128 then
      local a, b = find(src, "^[%w_\128-\255]+", pos)
      local w = sub(src, a, b)
      push(KEYWORDS[w] and OP or NAME, w, line)
      pos = b + 1
    elseif (c >= 48 and c <= 57) or (c == 46 and find(src, "^%.%d", pos)) then
      local a, b = find(src, "^0[xX]%x*%.?%x*", pos)
      if not a then a, b = find(src, "^0[bB][01]*%.?[01]*", pos) end
      if not a then
        a, b = find(src, "^%d*%.?%d*", pos)
        if sub(src, b, b) == "." and sub(src, b + 1, b + 1) == "." then b = b - 1 end
        local _, eb = find(src, "^[eE][%+%-]?%d+", b + 1)
        if eb then b = eb end
      end
      push(NUM, sub(src, a, b), line)
      pos = b + 1
    elseif c == 34 or c == 39 then
      local start, parts, i = line, {}, pos + 1
      while true do
        local a = find(src, "[\\\n\"']", i)
        if not a then error(fmt("line %d: unfinished string", start), 0) end
        parts[#parts + 1] = sub(src, i, a - 1)
        local ch = byte(src, a)
        if ch == c then
          pos = a + 1
          break
        elseif ch == 10 then
          error(fmt("line %d: unfinished string", start), 0)
        elseif ch == 92 then
          local e = sub(src, a + 1, a + 1)
          if ESC[e] then
            parts[#parts + 1] = ESC[e]
            if e == "\n" then line = line + 1 end
            i = a + 2
          elseif find(e, "%d") then
            local d = src:match("^%d%d?%d?", a + 1)
            parts[#parts + 1] = char(tonumber(d) % 256)
            i = a + 1 + #d
          elseif e == "x" and src:match("^%x%x", a + 2) then
            parts[#parts + 1] = char(tonumber(sub(src, a + 2, a + 3), 16))
            i = a + 4
          elseif e == "z" then
            local _, b = find(src, "^%s*", a + 2)
            lines(a, b)
            i = b + 1
          else
            parts[#parts + 1] = e
            i = a + 2
          end
        else
          parts[#parts + 1] = char(ch)
          i = a + 1
        end
      end
      push(STR, concat(parts), start)
    elseif c == 91 and find(src, "^%[=*%[", pos) then
      local eq = src:match("^%[(=*)%[", pos)
      local a0 = pos + 2 + #eq
      local a, b = find(src, "]" .. eq .. "]", a0, true)
      if not a then error(fmt("line %d: unfinished long string", line), 0) end
      local s = sub(src, a0, a - 1)
      if sub(s, 1, 1) == "\r" then s = sub(s, 2) end
      if sub(s, 1, 1) == "\n" then s = sub(s, 2) end
      push(STR, s, line)
      lines(pos, b)
      pos = b + 1
    else
      local o = sub(src, pos, pos + 3)
      if not OPS4[o] then
        o = sub(src, pos, pos + 2)
        if not OPS3[o] then
          o = sub(src, pos, pos + 1)
          if not OPS2[o] then o = sub(src, pos, pos) end
        end
      end
      pos = pos + #o
      push(OP, o == "!=" and "~=" or o, line)
    end
  end
  n = n + 1
  k[n], v[n], ln[n], mk[n] = EOF, "<eof>", line, ""
  return k, v, ln, mk
end

-- ---------------------------------------------------------------- output pieces

local function err(msg)
  error(fmt("line %d: %s", LN[P] or 0, msg), 0)
end

local function near()
  return K[P] == EOF and "<eof>" or tostring(V[P])
end

local function is(v)
  return K[P] == OP and V[P] == v
end

local function check(v)
  if not is(v) then err(fmt("'%s' expected near '%s'", v, near())) end
  local m = M[P]
  P = P + 1
  return m
end

local function mapname(n)
  if find(n, "[\128-\255]") then
    return "__g" .. n:gsub("[\128-\255]", function(ch) return fmt("%02x", byte(ch)) end)
  end
  return n
end

local function name()
  if K[P] ~= NAME then err(fmt("name expected near '%s'", near())) end
  local s = M[P] .. mapname(V[P])
  P = P + 1
  return s
end

local wrapnum

local function numlit(t)
  local v
  local h = t:match("^0[xX](.*)")
  local b = not h and t:match("^0[bB](.*)")
  if h or b then
    local base = h and 16 or 2
    local ip, fp = (h or b):match("^(%w*)%.?(%w*)$")
    v = ip ~= "" and tonumber(ip, base) or 0
    if fp and fp ~= "" then
      local f = tonumber(fp, base)
      v = f and v + f / base ^ #fp
    end
    if (h or b) == "" then v = nil end
  else
    v = tonumber(t)
  end
  if not v then err(fmt("malformed number near '%s'", t)) end
  return wrapnum(v)
end

-- a number as the carts have it (16.16, wrapped), written as a float
function wrapnum(v)
  local f = floor(v * 65536 + 0.5) % 4294967296
  if f >= 2147483648 then f = f - 4294967296 end
  v = f / 65536
  local s = fmt("%.17g", v)
  if not find(s, "[%.eEni]") then s = s .. ".0" end
  if v < 0 then s = "(" .. s .. ")" end
  return s
end

local function strlit(s)
  return '"' .. (s:gsub('[%z\1-\31"\\\127]', function(ch)
    if ch == '"' then return '\\"' elseif ch == "\\" then return "\\\\" end
    return fmt("\\%03d", byte(ch))
  end)) .. '"'
end

-- ---------------------------------------------------------------- expressions

local expr, explist, block, funcbody, statement

local function binop(l, op, r, rcat, m)
  if op == ".." then
    local parts = { l }
    if rcat then
      for i = 1, #rcat do parts[#parts + 1] = rcat[i] end
      parts[2] = m .. parts[2]
    else
      parts[2] = m .. r
    end
    return "__cat(" .. concat(parts, ",") .. ")", parts
  end
  local f = FUNC[op]
  if f then return f .. "(" .. l .. "," .. m .. r .. ")" end
  if op == "\\" then return l .. " // " .. m .. r end
  return l .. " " .. op .. " " .. m .. r
end

local function constructor()
  local m = check("{")
  local f = {}
  while not is("}") do
    if is("[") then
      local m2 = M[P]
      P = P + 1
      local key = expr()
      check("]")
      check("=")
      f[#f + 1] = m2 .. "[" .. key .. "]=" .. expr()
    elseif K[P] == NAME and K[P + 1] == OP and V[P + 1] == "=" then
      local n = name()
      P = P + 1
      f[#f + 1] = n .. "=" .. expr()
    else
      f[#f + 1] = expr()
    end
    if is(",") or is(";") then P = P + 1 else break end
  end
  local m2 = check("}")
  return m .. "{" .. concat(f, ",") .. m2 .. "}"
end

local function args()
  if K[P] == STR then
    local s = M[P] .. "(" .. strlit(V[P]) .. ")"
    P = P + 1
    return s
  end
  if is("{") then return "(" .. constructor() .. ")" end
  local m = check("(")
  if is(")") then P = P + 1; return m .. "()" end
  local e = explist()
  local m2 = check(")")
  return m .. "(" .. e .. m2 .. ")"
end

local function primaryexp()
  if K[P] == NAME then return name() end
  if is("(") then
    local m = M[P]
    P = P + 1
    local e = expr()
    local m2 = check(")")
    return m .. "(" .. e .. m2 .. ")"
  end
  err(fmt("unexpected symbol near '%s'", near()))
end

local function suffixedexp()
  local s = primaryexp()
  while true do
    if K[P] == OP then
      local v = V[P]
      if v == "." then
        P = P + 1
        s = s .. "." .. name()
      elseif v == "[" then
        local m = M[P]
        P = P + 1
        local e = expr()
        local m2 = check("]")
        s = s .. m .. "[" .. e .. m2 .. "]"
      elseif v == ":" then
        P = P + 1
        s = s .. ":" .. name() .. args()
      elseif v == "(" or v == "{" then
        s = s .. args()
      else
        return s
      end
    elseif K[P] == STR then
      s = s .. args()
    else
      return s
    end
  end
end

local function simpleexp()
  local k, v, m = K[P], V[P], M[P]
  if k == NUM then
    P = P + 1
    return m .. numlit(v)
  elseif k == STR then
    P = P + 1
    return m .. strlit(v)
  elseif k == OP then
    if v == "nil" or v == "true" or v == "false" or v == "..." then
      P = P + 1
      return m .. v
    elseif v == "{" then
      return constructor()
    elseif v == "function" then
      P = P + 1
      return m .. "function" .. funcbody()
    end
  end
  return suffixedexp()
end

local function subexpr(limit)
  local s, cat
  local u = K[P] == OP and UNOPS[V[P]]
  if u then
    local v, m = V[P], M[P]
    P = P + 1
    local e = subexpr(UNARY)
    if u ~= true then
      s = m .. u .. "(" .. e .. ")"
    elseif v == "-" then
      -- minus on a literal: folded, then wrapped (-32768 stays -32768)
      local lit = e:match("^%((%-[%d%.e%-%+]+)%)$") or e:match("^([%d%.][%d%.e%-%+]*)$")
      if lit and tonumber(lit) then
        s = m .. wrapnum(-tonumber(lit))
      else
        s = m .. (find(e, "^%d") and "-" .. e or "-(" .. e .. ")")
      end
    elseif v == "not" then
      s = m .. "not " .. e
    else
      s = m .. "#" .. e
    end
  else
    s = simpleexp()
  end
  while K[P] == OP do
    local op = V[P]
    local pr = BIN[op]
    if not pr or pr[1] <= limit then break end
    local m = M[P]
    P = P + 1
    local r, rcat = subexpr(pr[2])
    s, cat = binop(s, op, r, rcat, m)
  end
  if cat and not find(s, "^__cat%(") then cat = nil end
  return s, cat
end

function expr()
  local s, cat = subexpr(0)
  return s, cat
end

function explist()
  local e = { (expr()) }
  while is(",") do
    P = P + 1
    e[#e + 1] = (expr())
  end
  return concat(e, ",")
end

-- ---------------------------------------------------------------- statements

function funcbody()
  local m = check("(")
  local ps = {}
  if not is(")") then
    repeat
      if is("...") then
        ps[#ps + 1] = "..."
        P = P + 1
        break
      end
      ps[#ps + 1] = name()
      local more = is(",")
      if more then P = P + 1 end
    until not more
  end
  check(")")
  local b = block()
  local m2 = check("end")
  return m .. "(" .. concat(ps, ",") .. ") " .. b .. " " .. m2 .. "end"
end

local function retstat(on_line)
  local m = M[P]
  P = P + 1
  local s = m .. "return"
  local stop = K[P] == EOF or (K[P] == OP and (BLOCK_END[V[P]] or V[P] == ";"))
            or (on_line and LN[P] ~= LN[P - 1])
  if not stop then s = s .. " " .. explist() end
  if is(";") then P = P + 1 end
  return s
end

-- the statements of a one-line "if (c) ..." (they go on while they are
-- on the line where the last one ended)
local function lineblock()
  local buf, first = {}, true
  while K[P] ~= EOF do
    if not first and LN[P] ~= LN[P - 1] then break end
    if K[P] == OP and BLOCK_END[V[P]] then break end
    if is("return") then
      buf[#buf + 1] = retstat(true)
      break
    end
    buf[#buf + 1] = statement()
    first = false
  end
  return concat(buf, " ")
end

function block()
  local buf = {}
  while K[P] ~= EOF do
    if K[P] == OP and BLOCK_END[V[P]] then break end
    if is("return") then
      buf[#buf + 1] = retstat(false)
      break
    end
    buf[#buf + 1] = statement()
  end
  return concat(buf, " ")
end

-- the closing parenthesis of the one at P, or nil
local function matching(i)
  local depth = 0
  while K[i] ~= EOF do
    if K[i] == OP then
      local v = V[i]
      if v == "(" then depth = depth + 1
      elseif v == ")" then
        depth = depth - 1
        if depth == 0 then return i end
      end
    end
    i = i + 1
  end
end

-- can the token at i carry on the expression before it?
local function continues(i)
  if K[i] == STR then return true end
  if K[i] ~= OP then return false end
  local v = V[i]
  return v == "then" or v == "do" or BIN[v] ~= nil or v == "." or v == "[" or v == ":" or v == "(" or v == "{"
end

local function short_form()
  if not is("(") then return false end
  local close = matching(P)
  return close and not continues(close + 1)
end

local function ifstat()
  local m = M[P]
  P = P + 1
  if short_form() then
    local c = expr()
    local s = m .. "if " .. c .. " then " .. lineblock()
    if is("else") and LN[P] == LN[P - 1] then
      P = P + 1
      s = s .. " else " .. lineblock()
    end
    return s .. " end"
  end
  local c = expr()
  local parts = { m .. "if " .. c .. " " .. check("then") .. "then " .. block() }
  while is("elseif") do
    local m2 = M[P]
    P = P + 1
    local c2 = expr()
    parts[#parts + 1] = m2 .. "elseif " .. c2 .. " " .. check("then") .. "then " .. block()
  end
  if is("else") then
    local m2 = M[P]
    P = P + 1
    parts[#parts + 1] = m2 .. "else " .. block()
  end
  return concat(parts, " ") .. " " .. check("end") .. "end"
end

local function whilestat()
  local m = M[P]
  P = P + 1
  if short_form() then
    local c = expr()
    return m .. "while " .. c .. " do " .. lineblock() .. " end"
  end
  local c = expr()
  local m2 = check("do")
  local b = block()
  return m .. "while " .. c .. " " .. m2 .. "do " .. b .. " " .. check("end") .. "end"
end

local function forstat()
  local m = M[P]
  P = P + 1
  local n1 = name()
  local head
  if is("=") then
    P = P + 1
    local a = expr()
    check(",")
    local b = expr()
    head = n1 .. " = " .. a .. ", " .. b
    if is(",") then
      P = P + 1
      head = head .. ", " .. expr()
    end
  else
    local names = { n1 }
    while is(",") do
      P = P + 1
      names[#names + 1] = name()
    end
    check("in")
    head = concat(names, ",") .. " in " .. explist()
  end
  local m2 = check("do")
  local b = block()
  return m .. "for " .. head .. " " .. m2 .. "do " .. b .. " " .. check("end") .. "end"
end

local function exprstat()
  local e = suffixedexp()
  if is("=") or is(",") then
    local vars = { e }
    while is(",") do
      P = P + 1
      vars[#vars + 1] = suffixedexp()
    end
    local m = check("=")
    return concat(vars, ",") .. " " .. m .. "= " .. explist()
  end
  local op = K[P] == OP and COMPOUND[V[P]]
  if op then
    local m = M[P]
    P = P + 1
    local r = expr()
    return e .. " = " .. binop(e, op, "(" .. r .. ")", nil, m)
  end
  return e
end

function statement()
  local k, v, m = K[P], V[P], M[P]
  if k == OP then
    if v == ";" then P = P + 1; return m .. ";" end
    if v == "if" then return ifstat() end
    if v == "while" then return whilestat() end
    if v == "for" then return forstat() end
    if v == "do" then
      P = P + 1
      local b = block()
      return m .. "do " .. b .. " " .. check("end") .. "end"
    end
    if v == "repeat" then
      P = P + 1
      local b = block()
      local m2 = check("until")
      return m .. "repeat " .. b .. " " .. m2 .. "until " .. expr()
    end
    if v == "function" then
      P = P + 1
      local n = name()
      while is(".") do
        P = P + 1
        n = n .. "." .. name()
      end
      if is(":") then
        P = P + 1
        n = n .. ":" .. name()
      end
      return m .. "function " .. n .. funcbody()
    end
    if v == "local" then
      P = P + 1
      if is("function") then
        P = P + 1
        local n = name()
        return m .. "local function " .. n .. funcbody()
      end
      local names = { name() }
      while is(",") do
        P = P + 1
        names[#names + 1] = name()
      end
      if is("=") then
        P = P + 1
        return m .. "local " .. concat(names, ",") .. " = " .. explist()
      end
      return m .. "local " .. concat(names, ",")
    end
    if v == "::" then
      P = P + 1
      local n = name()
      check("::")
      return m .. "::" .. n .. "::"
    end
    if v == "break" then P = P + 1; return m .. "break" end
    if v == "goto" then
      P = P + 1
      return m .. "goto " .. name()
    end
    if v == "?" then
      P = P + 1
      return m .. "print(" .. explist() .. ")"
    end
  end
  return exprstat()
end

-- ---------------------------------------------------------------- the whole

-- the markers become newlines: every first token of a line goes back on
-- the line it had in the cart
local function assemble(s)
  local out, cur, i = {}, 1, 1
  while true do
    local a, b, l = find(s, "\1(%d+)\2", i)
    local piece = sub(s, i, (a or #s + 1) - 1)
    out[#out + 1] = piece
    for _ in piece:gmatch("\n") do cur = cur + 1 end
    if not a then break end
    l = tonumber(l)
    if l > cur then
      out[#out + 1] = ("\n"):rep(l - cur)
      cur = l
    else
      out[#out + 1] = " "
    end
    i = b + 1
  end
  return concat(out)
end

-- translate(code) -> Lua 5.4 source, or nil and "line n: message"
function Xl.translate(src)
  local ok, res = pcall(function()
    K, V, LN, M = lex(src)
    P = 1
    local b = block()
    if K[P] ~= EOF then err(fmt("'<eof>' expected near '%s'", near())) end
    return b
  end)
  K, V, LN, M = nil, nil, nil, nil
  if not ok then return nil, res end
  return assemble(res)
end

-- the names of the symbols, as the translation writes them (Env defines
-- the glyph constants with them)
function Xl.glyph_name(code)
  return mapname(char(code))
end
