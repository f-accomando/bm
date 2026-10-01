-- Cfg: what nano8 keeps on the SD card (bm's save()): the key mapping,
-- the screen size, and each cart's persistent data (cartdata: 256 bytes
-- per id).

Cfg.data = nil
local dirty, saved_at = false, -10

local function valid_keys(k)
  if type(k) ~= "table" or #k ~= 2 then return false end
  for pl = 1, 2 do
    if type(k[pl]) ~= "table" or #k[pl] ~= 7 then return false end
    for b = 1, 7 do
      local list = k[pl][b]
      if type(list) ~= "table" then return false end
      for i = 1, #list do
        if math.type(list[i]) ~= "integer" then return false end
      end
    end
  end
  return true
end

local function valid_pad(p)
  if type(p) ~= "table" or #p ~= 7 then return false end
  for b = 1, 7 do
    if math.type(p[b]) ~= "integer" then return false end
  end
  return true
end

function Cfg.load()
  local d = { keys = In.default_keys(), pad = In.default_pad(), scale = "crisp", cart = {}, last = nil }
  local s = saved()
  if type(s) == "table" then
    if valid_keys(s.keys) then d.keys = s.keys end
    if valid_pad(s.pad) then d.pad = s.pad end
    if s.scale == "fill" or s.scale == "crisp" then d.scale = s.scale end
    if type(s.cart) == "table" then
      for id, v in pairs(s.cart) do
        if type(id) == "string" and type(v) == "string" and #v == 256 then d.cart[id] = v end
      end
    end
    if type(s.last) == "string" then d.last = s.last end
  end
  Cfg.data = d
end

function Cfg.cartdata(id)
  return Cfg.data.cart[id]
end

function Cfg.set_cartdata(id, bytes)
  Cfg.data.cart[id] = bytes
  dirty = true
end

function Cfg.changed()
  dirty = true
end

-- writes the settings if something changed (not more than every 2 s
-- unless forced)
function Cfg.flush(force)
  if not dirty then return end
  if not force and time() - saved_at < 2 then return end
  local ok, err = save(Cfg.data)
  saved_at = time()
  dirty = false
  if not ok then log("nano8: cannot save: " .. tostring(err)) end
end
