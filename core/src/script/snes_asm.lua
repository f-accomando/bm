-- snes_asm.lua: a 65C816 assembler and a SNES ROM builder for bm (require "snes_asm").
-- Original code. The Lua of the kernel and the Lua of the host tests (build/host/luahost) both run it.
--
--   local A = require("snes_asm")
--   local res = A.assemble(source, { name = "main.s", read = function(path) return text_or_nil end })
--   if not res.ok then print(A.format_errors(res)) end
--   local rom, info = A.build_rom(res, { title = "MY GAME" })     -- a string, ready for snes_load(rom)
--   print(A.symbol_table(res))
--
-- Or in one call: local rom, res = A.build(source, opts, header).  The rules of the language are in
-- docs/progress/snes-sdk.md.

local M = {}

local sfmt, schar, sbyte, ssub, slower = string.format, string.char, string.byte, string.sub, string.lower

-- ===================================================================================================
-- The opcode table: T[mnemonic][mode] = opcode
-- ===================================================================================================
-- Modes: imp acc imm dp dpx dpy abs absx absy long longx ind indx indy indl indly sr sry absind absindx
--        absindl rel rell blk sig
local T = {}
local IMMCLASS = {}                      -- "m": the width follows M; "x": it follows X; 1 or 2: fixed

do
    local grp = { { "ora", 0x09 }, { "and", 0x29 }, { "eor", 0x49 }, { "adc", 0x69 },
                  { "sta", 0x89 }, { "lda", 0xA9 }, { "cmp", 0xC9 }, { "sbc", 0xE9 } }
    local off = { dp = -4, dpx = 12, abs = 4, absx = 20, absy = 16, long = 6, longx = 22, ind = 9,
                  indx = -8, indy = 8, indl = -2, indly = 14, sr = -6, sry = 10 }
    for _, g in ipairs(grp) do
        local m = {}
        if g[1] ~= "sta" then m.imm = g[2]; IMMCLASS[g[1]] = "m" end
        for k, d in pairs(off) do m[k] = g[2] + d end
        T[g[1]] = m
    end
    local function sh(a, dp, ab, dx, ax) return { acc = a, dp = dp, abs = ab, dpx = dx, absx = ax } end
    T.asl = sh(0x0A, 0x06, 0x0E, 0x16, 0x1E)
    T.rol = sh(0x2A, 0x26, 0x2E, 0x36, 0x3E)
    T.lsr = sh(0x4A, 0x46, 0x4E, 0x56, 0x5E)
    T.ror = sh(0x6A, 0x66, 0x6E, 0x76, 0x7E)
    T.inc = sh(0x1A, 0xE6, 0xEE, 0xF6, 0xFE)
    T.dec = sh(0x3A, 0xC6, 0xCE, 0xD6, 0xDE)
    T.bit = { imm = 0x89, dp = 0x24, abs = 0x2C, dpx = 0x34, absx = 0x3C }; IMMCLASS.bit = "m"
    T.cpx = { imm = 0xE0, dp = 0xE4, abs = 0xEC }
    T.cpy = { imm = 0xC0, dp = 0xC4, abs = 0xCC }
    T.ldx = { imm = 0xA2, dp = 0xA6, abs = 0xAE, dpy = 0xB6, absy = 0xBE }
    T.ldy = { imm = 0xA0, dp = 0xA4, abs = 0xAC, dpx = 0xB4, absx = 0xBC }
    for _, n in ipairs({ "cpx", "cpy", "ldx", "ldy" }) do IMMCLASS[n] = "x" end
    T.stx = { dp = 0x86, abs = 0x8E, dpy = 0x96 }
    T.sty = { dp = 0x84, abs = 0x8C, dpx = 0x94 }
    T.stz = { dp = 0x64, dpx = 0x74, abs = 0x9C, absx = 0x9E }
    T.trb = { dp = 0x14, abs = 0x1C }
    T.tsb = { dp = 0x04, abs = 0x0C }
    T.jmp = { abs = 0x4C, long = 0x5C, absind = 0x6C, absindx = 0x7C, absindl = 0xDC }
    T.jml = { long = 0x5C, absindl = 0xDC }
    T.jsr = { abs = 0x20, absindx = 0xFC, long = 0x22 }
    T.jsl = { long = 0x22 }
    T.pea = { abs = 0xF4 }
    T.pei = { ind = 0xD4 }
    T.per = { rell = 0x62 }
    T.rep = { imm = 0xC2 }; IMMCLASS.rep = 1
    T.sep = { imm = 0xE2 }; IMMCLASS.sep = 1
    T.brk = { sig = 0x00 }
    T.cop = { sig = 0x02 }
    T.wdm = { sig = 0x42 }
    T.mvn = { blk = 0x54 }
    T.mvp = { blk = 0x44 }
    local br = { bpl = 0x10, bmi = 0x30, bvc = 0x50, bvs = 0x70, bra = 0x80, bcc = 0x90, bcs = 0xB0,
                 bne = 0xD0, beq = 0xF0 }
    for n, o in pairs(br) do T[n] = { rel = o } end
    T.brl = { rell = 0x82 }
    local imp = { clc = 0x18, sec = 0x38, cli = 0x58, sei = 0x78, clv = 0xB8, cld = 0xD8, sed = 0xF8,
                  dex = 0xCA, dey = 0x88, inx = 0xE8, iny = 0xC8, nop = 0xEA, pha = 0x48, php = 0x08,
                  phx = 0xDA, phy = 0x5A, phb = 0x8B, phd = 0x0B, phk = 0x4B, pla = 0x68, plp = 0x28,
                  plx = 0xFA, ply = 0x7A, plb = 0xAB, pld = 0x2B, rti = 0x40, rtl = 0x6B, rts = 0x60,
                  stp = 0xDB, wai = 0xCB, tax = 0xAA, tay = 0xA8, tsx = 0xBA, txa = 0x8A, txs = 0x9A,
                  txy = 0x9B, tya = 0x98, tyx = 0xBB, tcd = 0x5B, tcs = 0x1B, tdc = 0x7B, tsc = 0x3B,
                  xba = 0xEB, xce = 0xFB }
    for n, o in pairs(imp) do T[n] = { imp = o } end
end
-- aliases
local ALIAS = { bge = "bcs", blt = "bcc", ina = "inc", dea = "dec", tad = "tcd", tda = "tdc",
                tas = "tcs", tsa = "tsc", swa = "xba" }
local ALIAS_ACC = { ina = true, dea = true }

local LEN = { imp = 1, acc = 1, dp = 2, dpx = 2, dpy = 2, abs = 3, absx = 3, absy = 3, long = 4, longx = 4,
              ind = 2, indx = 2, indy = 2, indl = 2, indly = 2, sr = 2, sry = 2, absind = 3, absindx = 3,
              absindl = 3, rel = 2, rell = 3, blk = 3, sig = 2 }
local MAXV = { dp = 0xFF, dpx = 0xFF, dpy = 0xFF, sr = 0xFF, sry = 0xFF, ind = 0xFF, indx = 0xFF,
               indy = 0xFF, indl = 0xFF, indly = 0xFF, abs = 0xFFFF, absx = 0xFFFF, absy = 0xFFFF,
               absind = 0xFFFF, absindx = 0xFFFF, absindl = 0xFFFF, long = 0xFFFFFF, longx = 0xFFFFFF }
local FAM = {
    direct = { "dp", "abs", "long" }, x = { "dpx", "absx", "longx" }, y = { "dpy", "absy" }, s = { "sr" },
    ind = { "ind", "absind" }, indx = { "indx", "absindx" }, indy = { "indy" },
    indl = { "indl", "absindl" }, indly = { "indly" }, sry = { "sry" },
}
-- jmp and jsr jump inside the program bank: an in-bank target is an absolute operand.
local BANKREL = { jmp = { abs = true, absindx = true }, jsr = { abs = true, absindx = true } }

M.opcodes = T            -- read only: the table of the assembler (for a disassembler)
M.lengths = LEN

-- ===================================================================================================
-- Text helpers
-- ===================================================================================================
local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end

local function fail(msg) error({ msg = msg }, 0) end

-- Remove a ; comment (not inside a string or a character literal).
local function strip_comment(s)
    local q
    local i, n = 1, #s
    while i <= n do
        local c = ssub(s, i, i)
        if q then
            if c == "\\" then i = i + 1
            elseif c == q then q = nil end
        elseif c == '"' or c == "'" then q = c
        elseif c == ";" then return ssub(s, 1, i - 1) end
        i = i + 1
    end
    return s
end

-- Split at the commas that are outside (), [] and quotes.
local function splitargs(s)
    local out, depth, q, start = {}, 0, nil, 1
    local i, n = 1, #s
    while i <= n do
        local c = ssub(s, i, i)
        if q then
            if c == "\\" then i = i + 1
            elseif c == q then q = nil end
        elseif c == '"' or c == "'" then q = c
        elseif c == "(" or c == "[" then depth = depth + 1
        elseif c == ")" or c == "]" then depth = depth - 1
        elseif c == "," and depth == 0 then
            out[#out + 1] = trim(ssub(s, start, i - 1)); start = i + 1
        end
        i = i + 1
    end
    out[#out + 1] = trim(ssub(s, start))
    return out
end

-- The index of the bracket that closes the one at index 1, or nil.
local function matching(s)
    local depth, q = 0, nil
    local i, n = 1, #s
    while i <= n do
        local c = ssub(s, i, i)
        if q then
            if c == "\\" then i = i + 1
            elseif c == q then q = nil end
        elseif c == '"' or c == "'" then q = c
        elseif c == "(" or c == "[" then depth = depth + 1
        elseif c == ")" or c == "]" then
            depth = depth - 1
            if depth == 0 then return i end
        end
        i = i + 1
    end
    return nil
end

local ESC = { n = 10, r = 13, t = 9, ["0"] = 0, ["\\"] = 92, ['"'] = 34, ["'"] = 39 }

local function unquote(s)       -- "text" -> bytes
    local out, i, n = {}, 2, #s - 1
    if ssub(s, #s) ~= '"' then fail("a string has no closing quote") end
    while i <= n do
        local c = ssub(s, i, i)
        if c == "\\" then
            i = i + 1
            local e = ESC[ssub(s, i, i)]
            if not e then fail("unknown escape \\" .. ssub(s, i, i)) end
            out[#out + 1] = e
        else
            out[#out + 1] = sbyte(c)
        end
        i = i + 1
    end
    return out
end

-- ===================================================================================================
-- Expressions: a tokenizer, a parser to a tree, an evaluator
-- ===================================================================================================
local BINPREC = { ["||"] = 1, ["&&"] = 2, ["=="] = 3, ["!="] = 3, ["<"] = 3, [">"] = 3, ["<="] = 3,
                  [">="] = 3, ["|"] = 4, ["~"] = 5, ["&"] = 6, ["<<"] = 7, [">>"] = 7, ["+"] = 8,
                  ["-"] = 8, ["*"] = 9, ["/"] = 9, ["%"] = 9 }

local function tokenize(s, scope)
    local toks, i, n = {}, 1, #s
    while i <= n do
        local c = ssub(s, i, i)
        if c:match("%s") then
            i = i + 1
        elseif c == "$" then
            local h = s:match("^%x+", i + 1)
            if not h then fail("bad hex number") end
            toks[#toks + 1] = { t = "num", v = tonumber(h, 16) }; i = i + 1 + #h
        elseif c == "%" and s:match("^[01]", i + 1) and (#toks == 0 or toks[#toks].t == "op") then
            local b = s:match("^[01]+", i + 1)
            toks[#toks + 1] = { t = "num", v = tonumber(b, 2) }; i = i + 1 + #b
        elseif c:match("%d") then
            local h = s:match("^0[xX]%x+", i)
            if h then
                toks[#toks + 1] = { t = "num", v = tonumber(ssub(h, 3), 16) }; i = i + #h
            else
                local d = s:match("^%d+", i)
                local rest = ssub(s, i + #d, i + #d)
                if rest:match("[%a_]") then fail("bad number " .. d .. rest) end
                toks[#toks + 1] = { t = "num", v = tonumber(d) }; i = i + #d
            end
        elseif c == "'" then
            local ch, e = s:match("^'(\\?.)'()", i)
            if not ch then fail("bad character literal") end
            local v
            if #ch == 2 then v = ESC[ssub(ch, 2)]; if not v then fail("unknown escape") end
            else v = sbyte(ch) end
            toks[#toks + 1] = { t = "num", v = v }; i = e
        elseif c:match("[%a_]") or (c == "@" and s:match("^[%a_]", i + 1)) then
            local id = s:match("^@?[%a_][%w_]*", i)
            i = i + #id
            if ssub(id, 1, 1) == "@" then id = (scope or "") .. id end
            toks[#toks + 1] = { t = "id", v = id }
        else
            local two = ssub(s, i, i + 1)
            if two == "<<" or two == ">>" or two == "==" or two == "!=" or two == "<=" or two == ">="
               or two == "&&" or two == "||" then
                toks[#toks + 1] = { t = "op", v = two }; i = i + 2
            elseif c:match("[%+%-%*/%%&|~%^<>!%(%)]") then
                toks[#toks + 1] = { t = "op", v = c }; i = i + 1
            else
                fail("unexpected character '" .. c .. "'")
            end
        end
    end
    return toks
end

-- A tree: {"num", v} {"sym", name} {"pc"} {"un", op, a} {"bin", op, a, b}
local function parse_expr(text, scope)
    local toks = tokenize(text, scope)
    if #toks == 0 then fail("an expression is missing") end
    local pos = 1
    local parse
    local function primary()
        local t = toks[pos]
        if not t then fail("the expression ends too early") end
        pos = pos + 1
        if t.t == "num" then return { "num", t.v } end
        if t.t == "id" then return { "sym", t.v } end
        local v = t.v
        if v == "(" then
            local e = parse(0)
            local c = toks[pos]
            if not c or c.v ~= ")" then fail("a ) is missing") end
            pos = pos + 1
            return e
        elseif v == "*" then
            return { "pc" }
        elseif v == "-" or v == "+" or v == "!" or v == "~" then
            return { "un", v, parse(10) }
        elseif v == "<" or v == ">" or v == "^" then
            -- low byte, high byte, bank byte: they take the whole expression after them (#<label+1)
            return { "un", v, parse(4) }
        end
        fail("unexpected '" .. tostring(v) .. "' in the expression")
    end
    parse = function(minp)
        local left = primary()
        while true do
            local t = toks[pos]
            if not t or t.t ~= "op" then break end
            local p = BINPREC[t.v]
            if not p or p < minp then break end
            pos = pos + 1
            local right = parse(p + 1)
            left = { "bin", t.v, left, right }
        end
        return left
    end
    local tree = parse(0)
    if pos <= #toks then fail("unexpected '" .. tostring(toks[pos].v) .. "' after the expression") end
    return tree
end

-- ctx: { syms = table, pc = number, unk = name of the first unknown symbol or nil }
local ev
local function symval(name, ctx)
    local s = ctx.syms[name]
    if not s then
        if not ctx.unk then ctx.unk = name end
        return 0
    end
    if s.v ~= nil then return s.v end
    if s.busy then fail("the definition of " .. name .. " refers to itself") end
    s.busy = true
    local sub = { syms = ctx.syms, pc = s.pc }
    local ok, v = pcall(ev, s.ast, sub)
    s.busy = false
    if not ok then error(v, 0) end
    if sub.unk then
        if not ctx.unk then ctx.unk = sub.unk end
        return 0
    end
    s.v = v
    return v
end

ev = function(n, ctx)
    local k = n[1]
    if k == "num" then return n[2]
    elseif k == "sym" then return symval(n[2], ctx)
    elseif k == "pc" then return ctx.pc
    elseif k == "un" then
        local a = ev(n[3], ctx)
        local o = n[2]
        if o == "-" then return -a
        elseif o == "+" then return a
        elseif o == "~" then return ~a
        elseif o == "!" then return (a == 0) and 1 or 0
        elseif o == "<" then return a & 0xFF
        elseif o == ">" then return (a >> 8) & 0xFF
        else return (a >> 16) & 0xFF end
    else
        local o = n[2]
        local a = ev(n[3], ctx)
        if o == "&&" then if a == 0 then return 0 end; return (ev(n[4], ctx) ~= 0) and 1 or 0 end
        if o == "||" then if a ~= 0 then return 1 end; return (ev(n[4], ctx) ~= 0) and 1 or 0 end
        local b = ev(n[4], ctx)
        if o == "+" then return a + b
        elseif o == "-" then return a - b
        elseif o == "*" then return a * b
        elseif o == "/" then if b == 0 then fail("division by zero") end; return a // b
        elseif o == "%" then if b == 0 then fail("division by zero") end; return a % b
        elseif o == "<<" then return a << b
        elseif o == ">>" then return a >> b
        elseif o == "&" then return a & b
        elseif o == "|" then return a | b
        elseif o == "~" then return a ~ b
        elseif o == "==" then return (a == b) and 1 or 0
        elseif o == "!=" then return (a ~= b) and 1 or 0
        elseif o == "<" then return (a < b) and 1 or 0
        elseif o == ">" then return (a > b) and 1 or 0
        elseif o == "<=" then return (a <= b) and 1 or 0
        else return (a >= b) and 1 or 0 end
    end
end

-- ===================================================================================================
-- Operands
-- ===================================================================================================
local function parse_operand(txt)
    txt = trim(txt)
    if txt == "" then return { kind = "none" } end
    if slower(txt) == "a" then return { kind = "acc" } end
    if ssub(txt, 1, 1) == "#" then return { kind = "imm", e = trim(ssub(txt, 2)) } end
    local force
    local c, rest = txt:match("^([azfAZF]):%s*(.*)$")
    if c then force = slower(c); txt = rest end
    local first = ssub(txt, 1, 1)
    if first == "(" or first == "[" then
        local close = matching(txt)
        if close then
            local inner, after = trim(ssub(txt, 2, close - 1)), trim(ssub(txt, close + 1))
            local la = slower(after)
            if first == "[" then
                if after == "" then return { kind = "indl", e = inner, force = force } end
                if la:match("^,%s*y$") then return { kind = "indly", e = inner, force = force } end
            else
                local parts = splitargs(inner)
                if after == "" then
                    if #parts == 2 and slower(parts[2]) == "x" then
                        return { kind = "indx", e = parts[1], force = force }
                    elseif #parts == 1 then
                        return { kind = "ind", e = inner, force = force }
                    end
                elseif la:match("^,%s*y$") then
                    if #parts == 2 and slower(parts[2]) == "s" then
                        return { kind = "sry", e = parts[1], force = force }
                    elseif #parts == 1 then
                        return { kind = "indy", e = inner, force = force }
                    end
                end
            end
        end
    end
    local parts = splitargs(txt)
    if #parts == 1 then return { kind = "direct", e = parts[1], force = force } end
    if #parts == 2 then
        local r = slower(parts[2])
        if r == "x" or r == "y" or r == "s" then return { kind = r, e = parts[1], force = force } end
        return { kind = "blk", e = parts[1], e2 = parts[2] }
    end
    fail("the operand has too many parts")
end

-- ===================================================================================================
-- The assembler
-- ===================================================================================================
local function default_read(path)
    if not io or not io.open then return nil end
    local f = io.open(path, "rb")
    if not f then return nil end
    local d = f:read("a")
    f:close()
    return d
end

local function dirname(p)
    return p:match("^(.*[/\\])[^/\\]*$") or ""
end

-- opts: name (main file name), read(path) -> text or nil (for .include and .incbin), map ("lorom"|"hirom")
function M.assemble(source, opts)
    opts = opts or {}
    local read = opts.read or default_read
    local res = { ok = false, errors = {}, segments = {}, symbols = {}, symlist = {}, lines = {}, map = opts.map }
    local errors = res.errors
    local syms = {}                    -- name -> {v, ast, pc, kind, busy}
    local items = {}                   -- the output of pass 1
    local st = { pc = nil, m16 = false, x16 = false, scope = "", macros = {}, maccount = 0, cond = {} }
    local cur = { file = opts.name or "main.s", line = 0, macro = nil }
    local collect                      -- a .macro or .rept body being read

    local function add_error(msg, where)
        where = where or cur
        if #errors < 200 then
            errors[#errors + 1] = { file = where.file, line = where.line, msg = msg .. (where.macro and (" (in macro " .. where.macro .. ")") or "") }
        end
    end

    local function active()
        local c = st.cond[#st.cond]
        return c == nil or c.on
    end

    local function default_pc()
        return (st.map or res.map) == "hirom" and 0xC00000 or 0x008000
    end

    local function set_pc(a)
        st.pc = a
        items[#items + 1] = { k = "org", addr = a, w = { file = cur.file, line = cur.line, macro = cur.macro } }
    end

    local function ensure_pc()
        if not st.pc then set_pc(default_pc()) end
    end

    local function here() return { file = cur.file, line = cur.line, macro = cur.macro } end

    local function ctx1() return { syms = syms, pc = st.pc or default_pc() } end

    -- evaluate in pass 1: value, or nil when it is not known yet
    local function try_eval(ast)
        local c = ctx1()
        local v = ev(ast, c)
        if c.unk then return nil, c.unk end
        return v
    end

    local function need_eval(text, what)
        local ast = parse_expr(text, st.scope)
        local v, unk = try_eval(ast)
        if v == nil then fail((what or "the value") .. " must be known here (" .. unk .. " is not defined yet)") end
        return v
    end

    local function define(name, v, ast, kind)
        if syms[name] then fail("the symbol " .. name .. " is already defined") end
        syms[name] = { v = v, ast = ast, pc = st.pc or default_pc(), kind = kind }
    end

    -- ---- instruction (pass 1)
    local function do_instruction(mn0, rest)
        local mn = slower(mn0)
        local suffix
        local base, sfx = mn:match("^(%a+)%.([bwl])$")
        if base then mn = base; suffix = sfx end
        local al = ALIAS[mn]
        local acc_alias = ALIAS_ACC[mn]
        if al then mn = al end
        local tbl = T[mn]
        if not tbl then fail("unknown instruction or macro '" .. mn0 .. "'") end
        ensure_pc()
        local pc = st.pc
        local op = parse_operand(rest)
        local force = op.force
        if suffix then force = (suffix == "b") and "z" or (suffix == "w") and "a" or "f" end
        local it = { k = "ins", pc = pc, w = here(), mn = mn }
        local kind = op.kind
        if acc_alias then kind = "acc" end

        local function finish(mode, ast, len)
            it.mode, it.op, it.len, it.ast = mode, tbl[mode], len or LEN[mode], ast
            it.force = force ~= nil
            st.pc = pc + it.len
            items[#items + 1] = it
        end

        if kind == "none" or kind == "acc" then
            if tbl.imp then return finish("imp") end
            if tbl.acc then return finish("acc") end
            if tbl.sig and kind == "none" then return finish("sig") end
            fail(mn .. " needs an operand")
        end
        if tbl.imp then fail(mn .. " takes no operand") end

        if kind == "imm" then
            if mn == "pea" then kind = "direct"; force = "a"
            else
                local cls = IMMCLASS[mn]
                if tbl.sig then
                    local ast = parse_expr(op.e, st.scope)
                    return finish("sig", ast)
                end
                if not tbl.imm then fail(mn .. " has no immediate form") end
                local size = 1
                if cls == "m" then size = st.m16 and 2 or 1
                elseif cls == "x" then size = st.x16 and 2 or 1 end
                local ast = parse_expr(op.e, st.scope)
                if mn == "rep" or mn == "sep" then
                    local v, unk = try_eval(ast)
                    if v == nil then fail(mn .. " needs a constant here (" .. unk .. " is not defined yet)") end
                    if mn == "rep" then
                        if v & 0x20 ~= 0 then st.m16 = true end
                        if v & 0x10 ~= 0 then st.x16 = true end
                    else
                        if v & 0x20 ~= 0 then st.m16 = false end
                        if v & 0x10 ~= 0 then st.x16 = false end
                    end
                end
                it.isize = size
                return finish("imm", ast, 1 + size)
            end
        end

        if kind == "blk" then
            if not tbl.blk then fail(mn .. " takes one operand") end
            it.ast2 = parse_expr(op.e2, st.scope)
            return finish("blk", parse_expr(op.e, st.scope))
        end
        if tbl.blk then fail(mn .. " needs a source bank and a destination bank") end
        if tbl.sig then fail(mn .. " takes only a # signature byte") end

        -- relative branches
        if kind == "direct" and (tbl.rel or tbl.rell) then
            local ast = parse_expr(op.e, st.scope)
            if tbl.rel then return finish("rel", ast) end
            return finish("rell", ast)
        end

        local fam = FAM[kind]
        if not fam then fail("bad operand for " .. mn) end
        local avail = {}
        for _, m in ipairs(fam) do if tbl[m] then avail[#avail + 1] = m end end
        if #avail == 0 then fail(mn .. " has no such addressing mode") end
        local ast = parse_expr(op.e, st.scope)
        local pick
        if force then
            for _, m in ipairs(avail) do
                local cls = m:match("^abs") and "a" or m:match("^long") and "f" or "z"
                if cls == force then pick = m; break end
            end
            if not pick then
                fail(mn .. " has no " .. ((force == "z") and "direct page" or (force == "a") and "absolute" or "long") .. " form with this operand")
            end
        elseif #avail == 1 then
            pick = avail[1]
        else
            local v = try_eval(ast)
            if v == nil then
                for _, m in ipairs(avail) do if m:match("^abs") then pick = m end end
                pick = pick or avail[1]
            else
                if v > 0xFFFF and BANKREL[mn] and (v >> 16) == (pc >> 16) then v = v & 0xFFFF end
                for _, m in ipairs(avail) do
                    if v <= MAXV[m] then pick = m; break end
                end
                pick = pick or avail[#avail]
            end
        end
        it.bankrel = BANKREL[mn] and BANKREL[mn][pick]
        return finish(pick, ast)
    end

    -- ---- directives (pass 1)
    local function data_items(unit, rest)
        ensure_pc()
        local args = splitargs(rest)
        for _, a in ipairs(args) do
            if a == "" then fail("an empty value in the list") end
            if ssub(a, 1, 1) == '"' then
                if unit ~= 1 then fail("a string is for .byte only") end
                local bytes = unquote(a)
                local s = {}
                for i, b in ipairs(bytes) do s[i] = schar(b) end
                local str = table.concat(s)
                items[#items + 1] = { k = "raw", data = str, pc = st.pc }
                st.pc = st.pc + #str
            else
                items[#items + 1] = { k = "data", unit = unit, ast = parse_expr(a, st.scope), pc = st.pc, w = here() }
                st.pc = st.pc + unit
            end
        end
    end

    local handle_text       -- forward

    local function directive(d, rest)
        d = slower(d)
        if d == "org" then
            local v = need_eval(rest, ".org")
            if v < 0 or v > 0xFFFFFF then fail(".org is outside 24 bits") end
            set_pc(v)
        elseif d == "byte" or d == "db" then data_items(1, rest)
        elseif d == "word" or d == "dw" then data_items(2, rest)
        elseif d == "long" or d == "dl" then data_items(3, rest)
        elseif d == "dword" or d == "dd" then data_items(4, rest)
        elseif d == "res" or d == "ds" then
            local a = splitargs(rest)
            local n = need_eval(a[1], ".res size")
            if n < 0 then fail(".res size is negative") end
            ensure_pc()
            if a[2] then
                local f = need_eval(a[2], ".res fill")
                items[#items + 1] = { k = "raw", data = string.rep(schar(f & 0xFF), n), pc = st.pc }
                st.pc = st.pc + n
            else
                set_pc(st.pc + n)       -- the space stays empty: the ROM gets the padding byte there
            end
        elseif d == "align" then
            local a = splitargs(rest)
            local n = need_eval(a[1], ".align")
            if n <= 0 then fail(".align needs a positive number") end
            ensure_pc()
            local fill = a[2] and need_eval(a[2], ".align fill") or 0
            local pad = (n - (st.pc % n)) % n
            if pad > 0 then
                items[#items + 1] = { k = "raw", data = string.rep(schar(fill & 0xFF), pad), pc = st.pc }
                st.pc = st.pc + pad
            end
        elseif d == "a8" then st.m16 = false
        elseif d == "a16" then st.m16 = true
        elseif d == "i8" then st.x16 = false
        elseif d == "i16" then st.x16 = true
        elseif d == "lorom" then st.map = "lorom"; res.map = "lorom"
        elseif d == "hirom" then st.map = "hirom"; res.map = "hirom"
        elseif d == "include" then
            local a = trim(rest)
            if ssub(a, 1, 1) ~= '"' then fail('.include needs "file"') end
            local name = schar(table.unpack(unquote(a)))
            if (st.depth or 0) >= 16 then fail(".include is nested too deep") end
            local text = read(name) or read(dirname(cur.file) .. name)
            if not text then fail("cannot read " .. name) end
            local saved = { file = cur.file, line = cur.line, macro = cur.macro }
            st.depth = (st.depth or 0) + 1
            handle_text(text, name)
            st.depth = st.depth - 1
            cur.file, cur.line, cur.macro = saved.file, saved.line, saved.macro
        elseif d == "incbin" then
            local a = splitargs(rest)
            if ssub(a[1], 1, 1) ~= '"' then fail('.incbin needs "file"') end
            local name = schar(table.unpack(unquote(a[1])))
            local data = read(name) or read(dirname(cur.file) .. name)
            if not data then fail("cannot read " .. name) end
            local off = a[2] and need_eval(a[2], ".incbin offset") or 0
            local len = a[3] and need_eval(a[3], ".incbin length") or (#data - off)
            if off < 0 or len < 0 or off + len > #data then fail(".incbin range is outside the file") end
            ensure_pc()
            items[#items + 1] = { k = "raw", data = ssub(data, off + 1, off + len), pc = st.pc }
            st.pc = st.pc + len
        elseif d == "error" then
            fail(trim(rest):gsub('^"(.*)"$', "%1"))
        else
            fail("unknown directive ." .. d)
        end
    end

    -- ---- one source line
    local line_guard
    local function expand_macro(m, argtext)
        local args = (trim(argtext) == "") and {} or splitargs(argtext)
        if #args > #m.params then fail("macro " .. m.name .. " takes " .. #m.params .. " arguments") end
        if (st.mdepth or 0) >= 16 then fail("macros are nested too deep") end
        st.maccount = st.maccount + 1
        local uid = "_m" .. st.maccount
        local map = {}
        for i, p in ipairs(m.params) do map[p] = args[i] or "" end
        st.mdepth = (st.mdepth or 0) + 1
        local where = { file = cur.file, line = cur.line, macro = m.name }
        for _, bl in ipairs(m.body) do
            local t = bl:gsub("\\@", uid):gsub("[%a_][%w_]*", function(w) return map[w] end)
            local saved = { cur.file, cur.line, cur.macro }
            cur.file, cur.line, cur.macro = where.file, where.line, where.macro
            local ok, e = pcall(line_guard, t)
            cur.file, cur.line, cur.macro = saved[1], saved[2], saved[3]
            if not ok then error(e, 0) end
        end
        st.mdepth = st.mdepth - 1
    end

    local function handle_line(raw)
        local line = trim(strip_comment(raw))
        -- collecting a macro or a .rept body
        if collect then
            local d = line:match("^%.([%w_]+)")
            d = d and slower(d)
            if collect.kind == "macro" then
                if d == "endmacro" or d == "endm" then
                    if collect.active then st.macros[collect.name] = collect end
                    collect = nil
                else
                    collect.body[#collect.body + 1] = line
                end
            else
                if d == "rept" then collect.nest = collect.nest + 1; collect.body[#collect.body + 1] = line
                elseif d == "endr" then
                    if collect.nest > 0 then
                        collect.nest = collect.nest - 1; collect.body[#collect.body + 1] = line
                    else
                        local c = collect
                        collect = nil
                        if c.active then
                            for _ = 1, c.count do
                                for _, bl in ipairs(c.body) do line_guard(bl) end
                            end
                        end
                    end
                else
                    collect.body[#collect.body + 1] = line
                end
            end
            return
        end
        if line == "" then return end

        -- conditionals
        local d, drest = line:match("^%.([%w_]+)%s*(.*)$")
        local dl = d and slower(d)
        if dl == "if" or dl == "ifdef" or dl == "ifndef" then
            local parent = active()
            local val = false
            if parent then
                if dl == "if" then val = need_eval(drest, ".if") ~= 0
                else
                    local defined = syms[(trim(drest):gsub("^@", st.scope .. "@"))] ~= nil
                    val = (dl == "ifdef") == defined
                end
            end
            st.cond[#st.cond + 1] = { on = parent and val, parent = parent, taken = val }
            return
        elseif dl == "else" then
            local c = st.cond[#st.cond]
            if not c then fail(".else without .if") end
            c.on = c.parent and not c.taken
            c.taken = true
            return
        elseif dl == "endif" then
            if #st.cond == 0 then fail(".endif without .if") end
            st.cond[#st.cond] = nil
            return
        end
        if dl == "macro" then
            local name, params = drest:match("^([%a_][%w_]*)%s*(.*)$")
            if not name then fail(".macro needs a name") end
            local ps = {}
            if trim(params) ~= "" then ps = splitargs(params) end
            collect = { kind = "macro", name = name, params = ps, body = {}, active = active() }
            return
        elseif dl == "rept" then
            local n = active() and need_eval(drest, ".rept count") or 0
            collect = { kind = "rept", count = n, body = {}, nest = 0, active = active() }
            return
        end
        if not active() then return end

        -- labels
        while true do
            local name, rest2 = line:match("^(@?[%a_][%w_]*):%s*(.*)$")
            if not name then break end
            local full = name
            if ssub(name, 1, 1) == "@" then
                full = st.scope .. name
            else
                st.scope = name
            end
            ensure_pc()
            define(full, st.pc, nil, "label")
            line = rest2
        end
        if line == "" then return end

        -- an equate:  name = expr
        local ename, eexpr = line:match("^(@?[%a_][%w_]*)%s*=%s*(.+)$")
        if ename and ssub(eexpr, 1, 1) ~= "=" then
            local full = ename
            if ssub(ename, 1, 1) == "@" then full = st.scope .. ename end
            local ast = parse_expr(eexpr, st.scope)
            local v = try_eval(ast)
            define(full, v, ast, "const")
            return
        end

        local word, rest3 = line:match("^(%S+)%s*(.*)$")
        if ssub(word, 1, 1) == "." then
            return directive(ssub(word, 2), rest3)
        end
        local m = st.macros[word]
        if m then return expand_macro(m, rest3) end
        return do_instruction(word, rest3)
    end

    line_guard = function(raw)
        local ok, e = pcall(handle_line, raw)
        if not ok then
            if type(e) == "table" and e.msg then add_error(e.msg)
            else add_error("internal error: " .. tostring(e)) end
        end
    end

    handle_text = function(text, fname)
        cur.file = fname
        local ln = 0
        for raw in (text .. "\n"):gmatch("([^\n]*)\n") do
            ln = ln + 1
            cur.file, cur.line, cur.macro = fname, ln, nil
            line_guard((raw:gsub("\r$", "")))
        end
        if collect then
            add_error("a .macro or .rept is not closed")
            collect = nil
        end
    end

    -- ---- pass 1
    handle_text(source, cur.file)
    if #st.cond > 0 then add_error("an .if is not closed", { file = cur.file, line = cur.line }) end
    if st.map then res.map = st.map end

    -- ---- pass 2
    if #errors == 0 then
        local seg
        local function new_seg(addr)
            if seg and seg.size == 0 then seg.addr = addr; return end
            seg = { addr = addr, parts = {}, size = 0 }
            res.segments[#res.segments + 1] = seg
        end
        local function put(s)
            seg.parts[#seg.parts + 1] = s
            seg.size = seg.size + #s
        end
        local function value(ast, it)
            local c = { syms = syms, pc = it.pc }
            local ok, v = pcall(ev, ast, c)
            if not ok then error(v, 0) end
            if c.unk then fail("the symbol " .. c.unk .. " is not defined") end
            return v
        end
        local function le(v, n)
            local t = {}
            for i = 1, n do t[i] = schar(v & 0xFF); v = v >> 8 end
            return table.concat(t)
        end
        for _, it in ipairs(items) do
            if it.k == "ins" and it.w and not it.w.macro then      -- res.lines: where each instruction line sits (the Run tab sets breakpoints with it)
                res.lines[#res.lines + 1] = { file = it.w.file, line = it.w.line, pc = it.pc }
            end
            local ok, e = pcall(function()
                if it.k == "org" then new_seg(it.addr)
                elseif it.k == "raw" then put(it.data)
                elseif it.k == "data" then
                    local v = value(it.ast, it)
                    local lo, hi = -(1 << (8 * it.unit - 1)), (1 << (8 * it.unit)) - 1
                    if v < lo or v > hi then fail(sfmt("value %d does not fit in %d byte(s)", v, it.unit)) end
                    put(le(v, it.unit))
                else
                    local mode = it.mode
                    if mode == "imp" or mode == "acc" then put(schar(it.op))
                    elseif mode == "sig" then
                        local v = it.ast and value(it.ast, it) or 0
                        if v < -128 or v > 255 then fail("the signature byte is outside 0 to 255") end
                        put(schar(it.op, v & 0xFF))
                    elseif mode == "imm" then
                        local v = value(it.ast, it)
                        local lo, hi = (it.isize == 1) and -128 or -32768, (it.isize == 1) and 255 or 65535
                        if v < lo or v > hi then
                            fail(sfmt("the value %d does not fit in %d bits (use .a8/.a16/.i8/.i16 or REP/SEP)", v, it.isize * 8))
                        end
                        put(schar(it.op) .. le(v, it.isize))
                    elseif mode == "rel" or mode == "rell" then
                        local t = value(it.ast, it)
                        local from = it.pc + it.len
                        if (t >> 16) ~= (it.pc >> 16) then fail("the branch target is in another bank") end
                        local d = (t & 0xFFFF) - (from & 0xFFFF)
                        if mode == "rel" then
                            if d < -128 or d > 127 then fail("the branch is too far (" .. d .. " bytes)") end
                            put(schar(it.op, d & 0xFF))
                        else
                            if d < -32768 or d > 32767 then d = d % 65536 end
                            put(schar(it.op) .. le(d, 2))
                        end
                    elseif mode == "blk" then
                        local s, dd = value(it.ast, it), value(it.ast2, it)
                        if s < 0 or s > 255 or dd < 0 or dd > 255 then fail("a bank number is outside 0 to 255") end
                        put(schar(it.op, dd, s))
                    else
                        local v = value(it.ast, it)
                        if it.bankrel and v > 0xFFFF and (v >> 16) == (it.pc >> 16) then v = v & 0xFFFF end
                        if it.force and (mode:match("^abs")) and v > 0xFFFF then v = v & 0xFFFF end
                        if v < 0 or v > MAXV[mode] then
                            local what = (MAXV[mode] == 0xFF) and "direct page (0 to $FF)" or (MAXV[mode] == 0xFFFF) and "16 bits (use a long address or jml/jsl for another bank)" or "24 bits"
                            fail(sfmt("the value $%X does not fit in %s", v, what))
                        end
                        put(schar(it.op) .. le(v, LEN[mode] - 1))
                    end
                end
            end)
            if not ok then
                if type(e) == "table" and e.msg then add_error(e.msg, it.w or cur)
                else add_error("internal error: " .. tostring(e), it.w or cur) end
            end
        end
        for _, s in ipairs(res.segments) do s.data = table.concat(s.parts); s.parts = nil end
    end

    -- ---- symbols
    for name, s in pairs(syms) do
        local c = { syms = syms, pc = s.pc }
        local ok, v = pcall(ev, { "sym", name }, c)
        if ok and not c.unk then
            res.symbols[name] = v
            res.symlist[#res.symlist + 1] = { name = name, value = v, kind = s.kind }
        end
    end
    table.sort(res.symlist, function(a, b)
        if a.value ~= b.value then return a.value < b.value end
        return a.name < b.name
    end)
    res.ok = (#errors == 0)
    return res
end

-- "file:line: message" for each error, one per line.
function M.format_errors(res)
    local t = {}
    for _, e in ipairs(res.errors) do t[#t + 1] = sfmt("%s:%d: %s", e.file, e.line, e.msg) end
    return table.concat(t, "\n")
end

-- The symbol table as text: labels as "$BB:AAAA name", constants as "= $VALUE name".
function M.symbol_table(res)
    local t = {}
    for _, s in ipairs(res.symlist) do
        if s.kind == "label" then
            t[#t + 1] = sfmt("$%02X:%04X %s", (s.value >> 16) & 0xFF, s.value & 0xFFFF, s.name)
        else
            t[#t + 1] = sfmt("= $%X %s", s.value, s.name)
        end
    end
    return table.concat(t, "\n")
end

-- ===================================================================================================
-- The ROM builder
-- ===================================================================================================
-- The ROM offset of a CPU address, or nil and the reason.
local function rom_offset(addr, map)
    local bank, low = (addr >> 16) & 0xFF, addr & 0xFFFF
    if map == "hirom" then
        if bank <= 0x3F or (bank >= 0x80 and bank <= 0xBF) then
            if low < 0x8000 then return nil, "is not ROM in HiROM (banks $00-$3F and $80-$BF map ROM at $8000-$FFFF)" end
            return (bank & 0x3F) * 0x10000 + low
        elseif (bank >= 0x40 and bank <= 0x7D) or bank >= 0xC0 then
            return (bank & 0x3F) * 0x10000 + low
        end
        return nil, "is not ROM"
    end
    if bank == 0x7E or bank == 0x7F then return nil, "is WRAM, not ROM" end
    if low < 0x8000 then
        if bank >= 0x70 and bank <= 0x7D or bank >= 0xF0 then return nil, "is SRAM, not ROM" end
        return nil, "is not ROM in LoROM (ROM is at $8000-$FFFF of each bank)"
    end
    return (bank & 0x7F) * 0x8000 + (low - 0x8000)
end
M.rom_offset = rom_offset

local function lookup_sym(res, v)
    if type(v) == "number" then return v end
    local s = res.symbols
    return s[v] or s[slower(v)] or s[v:upper()]
end

-- h: title, map, fast, sram_kb, battery, region, dev_id, version, vectors, fill, min_kb, chipset
-- vectors: reset, nmi, irq, cop, brk, abort (a label name or a number); emu_nmi, emu_irq, emu_cop,
-- emu_abort override the emulation vectors. A missing vector is a label named like the key, else an RTI.
function M.build_rom(res, h)
    h = h or {}
    if not res.ok then return nil, "the program has errors" end
    local map = h.map or res.map or "lorom"
    if map ~= "lorom" and map ~= "hirom" then return nil, "the map mode is lorom or hirom" end
    local hdr = (map == "hirom") and 0xFFC0 or 0x7FC0      -- the file offset of the header
    local fill = h.fill or 0
    -- 1. the ranges, with the checks
    local ranges = {}
    local top = hdr + 0x40
    for _, s in ipairs(res.segments) do
        if #s.data > 0 then
            local off, why = rom_offset(s.addr, map)
            if not off then return nil, sfmt("the code at $%06X %s", s.addr, why) end
            -- the segment must stay in one bank
            local first, last = off, off + #s.data - 1
            local span = (map == "hirom") and 0x10000 or 0x8000
            if (first // span) ~= (last // span) then
                return nil, sfmt("the code at $%06X crosses a bank border; put a new .org at the start of the next bank", s.addr)
            end
            ranges[#ranges + 1] = { lo = first, hi = last, data = s.data, addr = s.addr }
            if last + 1 > top then top = last + 1 end
        end
    end
    table.sort(ranges, function(a, b) return a.lo < b.lo end)
    for i = 2, #ranges do
        if ranges[i].lo <= ranges[i - 1].hi then
            return nil, sfmt("the code at $%06X overlaps the code at $%06X", ranges[i].addr, ranges[i - 1].addr)
        end
    end
    for _, r in ipairs(ranges) do
        if r.lo < hdr + 0x40 and r.hi >= hdr then
            return nil, sfmt("the code at $%06X is in the header area ($FFC0-$FFFF of bank 0); the builder writes it", r.addr)
        end
    end
    -- 2. the size: a power of two
    local minkb = h.min_kb or ((map == "hirom") and 64 or 32)
    local size = minkb * 1024
    while size < top do size = size * 2 end
    if size > 0x800000 then return nil, "the ROM is larger than 8 MB" end
    local chunks = {}
    -- 3. the image: pieces and padding
    local pos = 0
    local padc = schar(fill & 0xFF)
    for _, r in ipairs(ranges) do
        if r.lo > pos then chunks[#chunks + 1] = string.rep(padc, r.lo - pos) end
        chunks[#chunks + 1] = r.data
        pos = r.hi + 1
    end
    if pos < size then chunks[#chunks + 1] = string.rep(padc, size - pos) end
    local rom = table.concat(chunks)
    -- 4. the header and the vectors
    local function put(off, str) rom = ssub(rom, 1, off) .. str .. ssub(rom, off + #str + 1) end
    local title = (h.title or "BM SNES PROGRAM"):upper():gsub("[^\32-\126]", "?")
    title = ssub(title .. string.rep(" ", 21), 1, 21)
    local mapbyte = ((map == "hirom") and 0x21 or 0x20) | (h.fast and 0x10 or 0)
    local sram = h.sram_kb or 0
    local sramcode = 0
    if sram > 0 then
        sramcode = 0
        while (1 << sramcode) < sram do sramcode = sramcode + 1 end
    end
    local chipset = h.chipset or ((sram > 0) and ((h.battery == false) and 0x01 or 0x02) or 0x00)
    local romcode = 0
    while (1 << romcode) < size // 1024 do romcode = romcode + 1 end
    put(hdr, title .. schar(mapbyte, chipset, romcode, sramcode, h.region or 1, h.dev_id or 0x00, h.version or 0))
    put(hdr + 0x1C, "\xFF\xFF\x00\x00")
    local stub = 0xFFE0                                  -- an RTI for the vectors that have no handler
    put(hdr + 0x20, "\x40")
    local function vec(name, emu)
        local v
        if emu and h.vectors and h.vectors["emu_" .. name] ~= nil then v = h.vectors["emu_" .. name]
        elseif h.vectors and h.vectors[name] ~= nil then v = h.vectors[name]
        else v = res.symbols[name] or res.symbols[name:upper()] end
        if v == nil then return stub end
        local n = lookup_sym(res, v)
        if n == nil then error({ msg = "the vector " .. name .. " names an unknown label " .. tostring(v) }, 0) end
        return n & 0xFFFF
    end
    local ok, e = pcall(function()
        local function w16(off, v) put(off, schar(v & 0xFF, (v >> 8) & 0xFF)) end
        local base = hdr + 0x24                           -- $FFE4: native COP BRK ABORT NMI (-) IRQ
        w16(base, vec("cop")); w16(base + 2, vec("brk")); w16(base + 4, vec("abort"))
        w16(base + 6, vec("nmi")); w16(base + 8, stub); w16(base + 10, vec("irq"))
        local eb = hdr + 0x34                             -- $FFF4: emulation COP (-) ABORT NMI RESET IRQ
        w16(eb, vec("cop", true)); w16(eb + 2, stub); w16(eb + 4, vec("abort", true))
        w16(eb + 6, vec("nmi", true))
        local r = res.symbols["reset"] or res.symbols["RESET"]
        if h.vectors and h.vectors.reset ~= nil then r = lookup_sym(res, h.vectors.reset) end
        if r == nil then error({ msg = "no reset vector: define a label named reset, or pass vectors.reset" }, 0) end
        w16(eb + 8, r & 0xFFFF)
        w16(eb + 10, vec("irq", true))
    end)
    if not ok then return nil, (type(e) == "table" and e.msg) or tostring(e) end
    -- 5. the checksum
    local sum = 0
    for i = 1, #rom, 4096 do
        local n = math.min(4096, #rom - i + 1)
        local t = { sbyte(rom, i, i + n - 1) }
        for j = 1, n do sum = sum + t[j] end
    end
    sum = sum & 0xFFFF
    put(hdr + 0x1C, schar((sum ~ 0xFFFF) & 0xFF, ((sum ~ 0xFFFF) >> 8) & 0xFF, sum & 0xFF, sum >> 8))
    return rom, { size = size, checksum = sum, map = map, header = hdr }
end

-- Assemble and build in one call: returns the ROM string and the assembler result, or nil and the errors.
function M.build(source, opts, header)
    local res = M.assemble(source, opts)
    if not res.ok then return nil, M.format_errors(res), res end
    local rom, info = M.build_rom(res, header)
    if not rom then return nil, info, res end
    return rom, res, info
end

return M
