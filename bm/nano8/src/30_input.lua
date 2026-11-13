-- In: the six buttons of a cart (and pause), from the keyboards and the
-- controllers, with the mapping the player chose (Ui.controls). The
-- keyboard is read key by key (rawkeys), the controllers as bm's buttons.

-- bm's controller bits (pad())
local PAD = { left = 1, right = 2, up = 4, down = 8, a = 16, b = 32, start = 64, select = 128,
              x = 256, y = 512, l1 = 1024, r1 = 2048, l2 = 4096, r2 = 8192, l3 = 16384, r3 = 32768 }
In.PAD = PAD
In.PAD_NAMES = { [1] = "Left", [2] = "Right", [4] = "Up", [8] = "Down", [16] = "A", [32] = "B",
                 [64] = "Start", [128] = "Select", [256] = "X", [512] = "Y", [1024] = "L1", [2048] = "R1",
                 [4096] = "L2", [8192] = "R2", [16384] = "L3", [32768] = "R3" }

-- the cart's buttons: left right up down O X pause
In.BUTTONS = { "Left", "Right", "Up", "Down", "O", "X", "Pause" }

-- keyboard players 1 and 2: USB HID usages per button
function In.default_keys()
  return {
    { { 0x50 }, { 0x4F }, { 0x52 }, { 0x51 }, { 0x1D, 0x06, 0x11 }, { 0x1B, 0x19, 0x10 }, { 0x28, 0x13 } },
    { { 0x16 }, { 0x09 }, { 0x08 }, { 0x07 }, { 0xE1, 0x2B }, { 0x04, 0x14 }, {} },
  }
end

-- controllers: bm bits per button
function In.default_pad()
  return { PAD.left, PAD.right, PAD.up, PAD.down, PAD.a | PAD.x, PAD.b | PAD.y, PAD.start }
end

local KEYNAMES = {
  [0x28] = "Enter", [0x29] = "Esc", [0x2A] = "Backspace", [0x2B] = "Tab", [0x2C] = "Space",
  [0x2D] = "-", [0x2E] = "=", [0x2F] = "[", [0x30] = "]", [0x31] = "\\", [0x33] = ";", [0x34] = "'",
  [0x35] = "`", [0x36] = ",", [0x37] = ".", [0x38] = "/", [0x39] = "Caps",
  [0x49] = "Insert", [0x4A] = "Home", [0x4B] = "PgUp", [0x4C] = "Delete", [0x4D] = "End", [0x4E] = "PgDn",
  [0x4F] = "Right", [0x50] = "Left", [0x51] = "Down", [0x52] = "Up",
  [0xE0] = "LCtrl", [0xE1] = "LShift", [0xE2] = "LAlt", [0xE3] = "LGui",
  [0xE4] = "RCtrl", [0xE5] = "RShift", [0xE6] = "RAlt", [0xE7] = "RGui",
}

function In.keyname(u)
  if KEYNAMES[u] then return KEYNAMES[u] end
  if u >= 0x04 and u <= 0x1D then return char(0x41 + u - 0x04) end
  if u >= 0x1E and u <= 0x26 then return char(0x31 + u - 0x1E) end
  if u == 0x27 then return "0" end
  if u >= 0x3A and u <= 0x45 then return "F" .. (u - 0x39) end
  if u >= 0x59 and u <= 0x61 then return "Num" .. (u - 0x58) end
  if u == 0x62 then return "Num0" end
  return fmt("Key%02X", u)
end

function In.padname(bits)
  local names = {}
  for b = 0, 15 do
    if bits >> b & 1 == 1 then names[#names + 1] = In.PAD_NAMES[1 << b] end
  end
  return #names > 0 and concat(names, " ") or "-"
end

-- this frame's buttons, per cart player (1..8), as bits 0-6
local bits = { 0, 0, 0, 0, 0, 0, 0, 0 }
In.bits = bits

function In.read()
  for i = 1, 8 do bits[i] = 0 end
  local keys = Cfg.data.keys
  for pl = 1, 2 do
    local map = keys[pl]
    for b = 1, 7 do
      local list = map[b]
      for k = 1, #list do
        if keydown(list[k]) then
          bits[pl] = bits[pl] | (1 << (b - 1))
          break
        end
      end
    end
  end
  local padmap = Cfg.data.pad
  for p = 1, 4 do
    local raw = pad(p)
    if raw ~= 0 then
      for b = 1, 7 do
        if raw & padmap[b] ~= 0 then bits[p] = bits[p] | (1 << (b - 1)) end
      end
    end
  end
  return bits
end

-- ---------------------------------------------------------------- the console's own keys

-- what the menus use, whatever the mapping: arrows, Enter / Space / Z
-- (ok), Backspace / X (back), Tab / C (more); the controllers' cross, A,
-- B, X / Y, Start
local NAV = {
  up = { keys = { 0x52 }, pad = PAD.up }, down = { keys = { 0x51 }, pad = PAD.down },
  left = { keys = { 0x50 }, pad = PAD.left }, right = { keys = { 0x4F }, pad = PAD.right },
  ok = { keys = { 0x28, 0x2C, 0x1D, 0x58 }, pad = PAD.a },
  back = { keys = { 0x2A, 0x1B }, pad = PAD.b },
  more = { keys = { 0x2B, 0x06 }, pad = PAD.x | PAD.y },
  start = { keys = { 0x13 }, pad = PAD.start },
  l = { keys = { 0x4B, 0x14 }, pad = PAD.l1 }, r = { keys = { 0x4E, 0x08 }, pad = PAD.r1 },
}
local held, since, repeat_at = {}, {}, {}

-- nav() -> the menu buttons pressed this frame (directions repeat while held)
function In.nav()
  local all = pad()
  local out = {}
  local now = time()
  for name, d in pairs(NAV) do
    local on = all & d.pad ~= 0
    if not on then
      for i = 1, #d.keys do
        if keydown(d.keys[i]) then on = true; break end
      end
    end
    if on and not held[name] then
      out[name] = true
      since[name] = now
      repeat_at[name] = now + 0.4
    elseif on and (name == "up" or name == "down" or name == "left" or name == "right" or name == "l" or name == "r")
        and now >= repeat_at[name] then
      out[name] = true
      repeat_at[name] = now + 0.09
    end
    held[name] = on
  end
  return out
end

-- forget what is held (a new screen does not see the key that opened it)
function In.settle()
  local all = pad()
  for name, d in pairs(NAV) do
    local on = all & d.pad ~= 0
    for i = 1, #d.keys do
      if keydown(d.keys[i]) then on = true end
    end
    held[name] = on
  end
end

-- any key or button at all (to wait for release before listening)
function In.anything()
  return #keys() > 0 or pad() ~= 0
end
