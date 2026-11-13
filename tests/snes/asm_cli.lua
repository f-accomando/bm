-- asm_cli.lua: assemble a 65C816 source file into a SNES ROM with core/src/script/snes_asm.lua.
--   build/host/luahost tests/snes/asm_cli.lua in.s out.sfc [--title T] [--map lorom|hirom] [--dev N] [--fast] [--sym out.sym]
-- Run it from the repository root. Prints the errors as file:line: message and exits 1 on an error.
package.path = "core/src/script/?.lua;" .. package.path
local A = require("snes_asm")
local inp, out = arg[1], arg[2]
if not inp or not out then io.stderr:write("usage: asm_cli.lua in.s out.sfc [--title T] [--map M] [--dev N] [--fast] [--sym f]\n"); os.exit(2) end
local h, symf = {}, nil
local i = 3
while arg[i] do
    local a = arg[i]
    if a == "--title" then h.title = arg[i + 1]; i = i + 1
    elseif a == "--map" then h.map = arg[i + 1]; i = i + 1
    elseif a == "--dev" then h.dev_id = tonumber(arg[i + 1]); i = i + 1
    elseif a == "--sym" then symf = arg[i + 1]; i = i + 1
    elseif a == "--fast" then h.fast = true end
    i = i + 1
end
local dir = inp:match("^(.*[/\\])") or ""
local function read(p)
    for _, name in ipairs({ p, dir .. p }) do
        local f = io.open(name, "rb")
        if f then local d = f:read("a"); f:close(); return d end
    end
end
local f = assert(io.open(inp, "rb")); local src = f:read("a"); f:close()
local rom, res = A.build(src, { name = inp, read = read }, h)
if not rom then io.stderr:write(tostring(res), "\n"); os.exit(1) end
local o = assert(io.open(out, "wb")); o:write(rom); o:close()
if symf then local s = assert(io.open(symf, "w")); s:write(A.symbol_table(res), "\n"); s:close() end
print("wrote " .. out .. " (" .. #rom .. " bytes)")
