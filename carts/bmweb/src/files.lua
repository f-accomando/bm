-- bmweb: what a response is, and the name of a saved file. Pure Lua.
-- The kernel allows only a bare 8.3 name in bm/web, never .bm, .bme or .b16.
local M = {}

local EXT_BY_TYPE = {
  ["model/gltf-binary"] = "GLB", ["model/gltf+json"] = "GLT", ["application/zip"] = "ZIP",
  ["application/pdf"] = "PDF", ["application/json"] = "JSN", ["text/plain"] = "TXT",
  ["text/csv"] = "CSV", ["image/png"] = "PNG", ["image/jpeg"] = "JPG", ["image/webp"] = "WBP",
  ["image/gif"] = "GIF", ["image/svg+xml"] = "SVG", ["audio/mpeg"] = "MP3", ["audio/ogg"] = "OGG",
  ["audio/wav"] = "WAV", ["application/octet-stream"] = "BIN",
}
local FORBIDDEN = { BM = true, BME = true, B16 = true }

local function hash2(s)
  local h = 5381
  for i = 1, #s do h = (h * 33 + s:byte(i)) % 1296 end
  local d = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  return d:sub(h // 36 + 1, h // 36 + 1) .. d:sub(h % 36 + 1, h % 36 + 1)
end

local function mime(ctype)
  return ((ctype or ""):match("^%s*([^;%s]+)") or ""):lower()
end
M.mime = mime

-- What the bytes are, from the first bytes of a body.
function M.sniff(head)
  if head:sub(1, 8) == "\137PNG\r\n\26\n" then return "png" end
  if head:sub(1, 3) == "\255\216\255" then return "jpeg" end
  if head:sub(1, 4) == "RIFF" and head:sub(9, 12) == "WEBP" then return "webp" end
  if head:sub(1, 4) == "glTF" then return "glb" end
  if head:sub(1, 6) == "GIF87a" or head:sub(1, 6) == "GIF89a" then return "gif" end
  local t = head:gsub("^[%s\239\187\191]+", ""):sub(1, 16):lower()
  if t:sub(1, 1) == "<" and (t:sub(1, 5) == "<!doc" or t:sub(1, 5) == "<html" or t:find("^<%a")) then return "html" end
  return nil
end

-- "html", "text", "image" or "download", from the Content-Type; "sniff"
-- when the type says nothing and the first bytes must decide.
function M.kind(ctype)
  local m = mime(ctype)
  if m == "text/html" or m == "application/xhtml+xml" then return "html" end
  if m == "text/plain" then return "text" end
  if m == "image/png" or m == "image/jpeg" or m == "image/webp" then return "image" end
  if m == "" or m == "application/octet-stream" or m == "binary/octet-stream" then return "sniff" end
  return "download"
end

-- A safe 8.3 name for a saved file: STEM(6)+2 characters of a hash of the
-- URL, a dot and a 3-letter extension. Never a .BM, .BME or .B16.
function M.name(u, ctype)
  local path = u:gsub("[?#].*$", ""):match("([^/]*)$") or ""
  local stem, ext = path:match("^(.-)%.([%w]+)$")
  if not stem then stem = path end
  stem = stem:upper():gsub("[^A-Z0-9]", ""):sub(1, 6)
  if stem == "" then stem = "FILE" end
  ext = (ext or ""):upper():gsub("[^A-Z0-9]", ""):sub(1, 3)
  if ext == "" then ext = EXT_BY_TYPE[mime(ctype)] or "DAT" end
  if FORBIDDEN[ext] then ext = "DAT" end
  return stem .. hash2(u) .. "." .. ext
end

-- The note kept beside a saved file: where it came from.
function M.note(u, from, ctype, size)
  local t = { "Saved by bm Web.", "URL: " .. u }
  if from and from ~= "" then t[#t + 1] = "Found on: " .. from end
  if ctype and ctype ~= "" then t[#t + 1] = "Type: " .. ctype end
  t[#t + 1] = "Size: " .. tostring(size or "?") .. " bytes"
  t[#t + 1] = "Check the licence of the file on the page it came from before you use it."
  return table.concat(t, "\n") .. "\n"
end

function M.note_name(name)
  local stem, ext = name:match("^(.*)%.(%w+)$")
  return stem .. (ext == "TXT" and ".NTE" or ".TXT")
end

return M
