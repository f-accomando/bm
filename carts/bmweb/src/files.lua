-- bmweb: what a response is, and the name of a saved file. Pure Lua.
-- The kernel allows only a bare 8.3 name in bm/web, never .bm, .bme or .b16.
local M = {}

local EXT_BY_TYPE = {
  ["model/gltf-binary"] = "glb", ["model/gltf+json"] = "glt", ["application/zip"] = "zip",
  ["application/pdf"] = "pdf", ["application/json"] = "jsn", ["text/plain"] = "txt",
  ["text/csv"] = "csv", ["image/png"] = "png", ["image/jpeg"] = "jpg", ["image/webp"] = "wbp",
  ["image/gif"] = "gif", ["image/svg+xml"] = "svg", ["audio/mpeg"] = "mp3", ["audio/ogg"] = "ogg",
  ["audio/wav"] = "wav", ["application/octet-stream"] = "bin",
}
local FORBIDDEN = { bm = true, bme = true, b16 = true }

local function hash2(s)
  local h = 5381
  for i = 1, #s do h = (h * 33 + s:byte(i)) % 1296 end
  local d = "0123456789abcdefghijklmnopqrstuvwxyz"
  return d:sub(h // 36 + 1, h // 36 + 1) .. d:sub(h % 36 + 1, h % 36 + 1)
end

local function mime(ctype)
  return ((ctype or ""):match("^%s*([^;%s]+)") or ""):lower()
end
M.mime = mime

-- A BMRES model file: the magic "BMRES" and three zero bytes, then kind 1 (a u16 at offset 12).
-- The kernel does the real check when file_write gets the bytes (src/bm/runtime.c file_write_model).
function M.is_bmm(head)
  return #head >= 14 and head:sub(1, 8) == "BMRES\0\0\0" and head:byte(13) == 1 and head:byte(14) == 0
end

-- What the bytes are, from the first bytes of a body.
function M.sniff(head)
  if M.is_bmm(head) then return "bmm" end
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

-- A safe 8.3 name for a saved file: stem(6)+2 characters of a hash of the
-- URL, a dot and a 3-letter extension. Never a .bm, .bme or .b16. All in lowercase.
function M.name(u, ctype)
  local path = u:gsub("[?#].*$", ""):match("([^/]*)$") or ""
  local stem, ext = path:match("^(.-)%.([%w]+)$")
  if not stem then stem = path end
  stem = stem:lower():gsub("[^a-z0-9]", ""):sub(1, 6)
  if stem == "" then stem = "file" end
  ext = (ext or ""):lower():gsub("[^a-z0-9]", ""):sub(1, 3)
  if ext == "" then ext = EXT_BY_TYPE[mime(ctype)] or "dat" end
  if FORBIDDEN[ext] then ext = "dat" end
  return stem .. hash2(u) .. "." .. ext
end

-- The name of a model (a .bmm, saved in /bmm by the kernel): 1 to 8 of a-z 0-9 _ - and ".bmm". It comes from
-- the last part of the URL in lower case, with every other character taken out. Second value: false when
-- the URL's own name was not a valid one and this name was made from it (the status says so).
function M.model_name(u)
  local path = u:gsub("[?#].*$", ""):match("([^/]*)$") or ""
  local stem = path:gsub("%.[%w]*$", "")
  local s = stem:lower():gsub("[^a-z0-9_%-]", ""):sub(1, 8)
  local same = s ~= "" and s == stem and path:lower():match("%.bmm$") ~= nil
  if s == "" then s = "model" end
  return s .. ".bmm", same
end

-- Another valid name when the first is taken: 6 characters of it and 2 of a hash of the URL.
function M.model_alt(name, u)
  return name:gsub("%.bmm$", ""):sub(1, 6) .. hash2(u) .. ".bmm"
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
  return stem .. (ext:lower() == "txt" and ".nte" or ".txt")
end

return M
