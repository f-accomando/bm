-- bmweb: UTF-8 to code page 437, the code page of the bm fonts. One code
-- point gives exactly one byte, so the layout widths (chars * cell) stay true.
-- A code point with no glyph gives "?".
local M = {}

local MAP = {}
local function add(bytes, cps)
  local i = 0
  for _, cp in utf8.codes(cps) do
    i = i + 1
    MAP[cp] = string.char(bytes[i])
  end
end

add({ 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
      147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 159, 160, 161, 162, 163, 164, 165, 166,
      167, 168, 170, 171, 172, 173, 174, 175, 225, 230, 241, 248, 250, 253 },
    "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥ƒáíóúñÑªº¿¬½¼¡«»ßµ±°·²")

-- Letters with no own glyph: the base letter.
local function base(letter, list)
  for _, cp in utf8.codes(list) do MAP[cp] = letter end
end
base("A", "ÀÁÂÃ") base("a", "ã") base("E", "ÈÊË") base("I", "ÌÍÎÏ") base("O", "ÒÓÔÕØ") base("o", "õø")
base("U", "ÙÚÛ") base("Y", "Ý") base("y", "ý") base("D", "Ð") base("N", "Ñ")
MAP[0xA0] = " "
MAP[0x2018], MAP[0x2019], MAP[0x201A] = "'", "'", ","
MAP[0x201C], MAP[0x201D], MAP[0x201E] = '"', '"', '"'
MAP[0x2013], MAP[0x2014], MAP[0x2212] = "-", "-", "-"
MAP[0x2026] = "."
MAP[0x2022], MAP[0x25CF], MAP[0x00D7] = "\7", "\7", "x"
MAP[0x20AC] = "E"
MAP[0x2192] = "\26"
MAP[0x2190] = "\27"
MAP[0x2191] = "\24"
MAP[0x2193] = "\25"

local cache, ncache = {}, 0

function M.conv(s)
  local c = cache[s]
  if c then return c end
  if not s:find("[\128-\255]") then
    c = s
  else
    local out, n = {}, 0
    local ok = pcall(function()
      for _, cp in utf8.codes(s) do
        n = n + 1
        out[n] = (cp < 128 and string.char(cp)) or MAP[cp] or "?"
      end
    end)
    if not ok then c = (s:gsub("[\128-\255]", "?")) else c = table.concat(out) end
  end
  if ncache >= 2000 then cache, ncache = {}, 0 end
  cache[s] = c
  ncache = ncache + 1
  return c
end

return M
