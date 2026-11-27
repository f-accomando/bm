-- sdk_tpl_build.lua: builds the two start files of the Shell SDK (core/src/script/sdk_tpl_nes.s and
-- sdk_tpl_snes.s) with the default settings of the Project tab, and writes the ROMs for the core tests.
--   build/host/luahost tests/snes/sdk_tpl_build.lua out_dir        (make test-sdk-templates)
package.path = "core/src/script/?.lua;" .. package.path
local A = require("snes_asm")
local out = arg[1] or "build/host"

local function read(path)
    local f = assert(io.open(path, "rb"))
    local d = f:read("a")
    f:close()
    return d
end

local function write(path, data)
    local f = assert(io.open(path, "wb"))
    f:write(data)
    f:close()
end

local fails = 0
local function check(name, ok, extra)
    if not ok then fails = fails + 1; print("FAIL " .. name .. (extra and (": " .. tostring(extra)) or "")) end
end

-- NES: NROM, 32 KB PRG, 8 KB CHR (the header sdk_proj_header makes for the default project)
local src = read("core/src/script/sdk_tpl_nes.s")
local res = A.assemble(read(out .. "/sdk_tpl_nes_full.s"), { cpu = "6502" })    -- with the sound driver
check("nes template assembles", res.ok, A.format_errors(res))
if res.ok then
    local rom, info = A.build_nes(res, { prg_kb = 32, chr_kb = 8, mapper = 0 })
    check("nes template builds", rom ~= nil, info)
    if rom then write(out .. "/sdk_tpl.nes", rom) end
    for _, name in ipairs({ "reset", "nmi", "irq", "main", "update", "read_pad", "palette" }) do
        check("nes label " .. name, res.symbols[name] ~= nil)
    end
    for _, mark in ipairs({ ";@sprites", ";@music", ";@map" }) do
        check("nes marker " .. mark, src:find(mark .. "\n", 1, true) ~= nil)
    end
    check("nes: three ends", select(2, src:gsub(";@end\n", "")) == 3)
end

-- SNES: LoROM, 32 KB
src = read("core/src/script/sdk_tpl_snes.s")
res = A.assemble(src, { map = "lorom" })
check("snes template assembles", res.ok, A.format_errors(res))
if res.ok then
    local rom, info = A.build_rom(res, { title = "TEMPLATE", map = "lorom", min_kb = 32, chipset = 0 })
    check("snes template builds", rom ~= nil, info)
    if rom then write(out .. "/sdk_tpl.sfc", rom) end
    for _, name in ipairs({ "reset", "nmi", "irq", "main", "update", "read_pad" }) do
        check("snes label " .. name, res.symbols[name] ~= nil)
    end
    for _, mark in ipairs({ ";@sprites", ";@music", ";@map" }) do
        check("snes marker " .. mark, src:find(mark .. "\n", 1, true) ~= nil)
    end
    check("snes: three ends", select(2, src:gsub(";@end\n", "")) == 3)
end

print(fails == 0 and "sdk_tpl_build: ok" or ("sdk_tpl_build: " .. fails .. " FAILED"))
os.exit(fails == 0 and 0 or 1)
