-- Input: the pad (DS4 layout, as Overwatch on consoles) or the keyboard,
-- into one command per frame for the local player.
--
--            pad                 keyboard
-- move       left stick          W A S D
-- look       right stick         arrows (and I J K L... no: arrows only)
-- fire       R2                  J (or Z)
-- secondary  L2                  K (or X)
-- ability 1  L1                  Shift
-- ability 2  R1                  E
-- ultimate   triangle (Y)        Q
-- jump       cross (A)           Space
-- crouch     circle (B)          C (or Ctrl)
-- reload     square (X)          R
-- melee      R3                  V
-- menu       Options (Start)     Enter
-- dev kit    Share (Select)      Tab
-- hero       down (in spawn)     H
--
-- With a mouse (the Pi; the RGB30 has none): the left button fires, the right
-- one is the secondary fire, and moving it turns the view in the hero's first
-- person (with the arrows and the stick; mouse_look below). The pointer is
-- hidden while playing and shows in the pause menu (86_pause).
--
-- On the RGB30 (Nintendo layout: X top, A right, B bottom, Y left, and
-- the game gets the letters printed on the console): the four face buttons
-- sit where the DS4's do, so jump is B (bottom), crouch A (right), reload Y
-- (left) and the ultimate X (top). Any other pad keeps the DS4's bits.
--
-- The menus (title, hero select, lobby, benchmark) confirm and go back with
-- the system's buttons, btn("ok") and btn("back"): cross and circle on the
-- DS4, B and A on the RGB30 (confirm=a swaps them), never the A and B bits
-- of the game; on the keyboard Space and Backspace.

Input = {}

local K = {
  W = 0x1A, A = 0x04, S = 0x16, D = 0x07, J = 0x0D, Z = 0x1D, K = 0x0E, X = 0x1B, E = 0x08,
  Q = 0x14, R = 0x15, C = 0x06, V = 0x19, SPACE = 0x2C, LSHIFT = 0xE1, RSHIFT = 0xE5,
  LCTRL = 0xE0, RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52, ENTER = 0x28, TAB = 0x2B,
  F1 = 0x3A, F2 = 0x3B, F3 = 0x3C, F4 = 0x3D, F5 = 0x3E, P = 0x13, N1 = 0x1E, N2 = 0x1F, N3 = 0x20, N4 = 0x21,
  H = 0x0B, F6 = 0x3F, BKSP = 0x2A,
}
Input.K = K

-- The keys the player can change (the COMMANDS page: 87_keys). Only these keyboard keys: the pad,
-- the mouse, Enter (menu), Space and Backspace (the menus' yes and back), the arrows (look and
-- the menus) and F1-F6 stay as they are. Each action has a list of keys (a default can have two,
-- as fire: J and Z); the player's choice is one key. Saved in the game's slot 1 as
-- keys = { fire = { 0x0D } }: only the actions that differ from the default.
local ACTIONS = {
  { "fwd", "MOVE FORWARD", { K.W } },
  { "bwd", "MOVE BACK", { K.S } },
  { "lft", "MOVE LEFT", { K.A } },
  { "rgt", "MOVE RIGHT", { K.D } },
  { "fire", "FIRE", { K.J, K.Z } },
  { "fire2", "SECONDARY FIRE", { K.K, K.X } },
  { "ab1", "ABILITY 1", { K.LSHIFT, K.RSHIFT } },
  { "ab2", "ABILITY 2", { K.E } },
  { "ult", "ULTIMATE", { K.Q } },
  { "jump", "JUMP", { K.SPACE } },
  { "crouch", "CROUCH", { K.C, K.LCTRL } },
  { "reload", "RELOAD", { K.R } },
  { "melee", "MELEE", { K.V } },
  { "hero", "CHANGE HERO", { K.H } },
}
Input.ACTIONS = ACTIONS

local function copy(l)
  local r = {}
  for i, v in ipairs(l) do r[i] = v end
  return r
end

local binds = {}
Input.binds = binds
local function reset_binds()
  for _, a in ipairs(ACTIONS) do binds[a[1]] = copy(a[3]) end
end
reset_binds()

local function same(a, b)
  if #a ~= #b then return false end
  for i = 1, #a do if a[i] ~= b[i] then return false end end
  return true
end

-- a key the player can give to an action: letters, digits, space, the punctuation, Ctrl and Shift
function Input.bindable(u)
  return type(u) == "number" and ((u >= 0x04 and u <= 0x27) or (u >= 0x2C and u <= 0x38) or u == 0xE0 or
    u == 0xE1 or u == 0xE4 or u == 0xE5)
end

-- the name prompt() draws for a key (lower case: "w", "space", "shift")
local NAMES = { [0x2C] = "space", [0x2D] = "-", [0x2E] = "=", [0x2F] = "[", [0x30] = "]", [0x31] = "\\",
  [0x32] = "#", [0x33] = ";", [0x34] = "'", [0x35] = "`", [0x36] = ",", [0x37] = ".", [0x38] = "/",
  [0xE0] = "ctrl", [0xE4] = "ctrl", [0xE1] = "shift", [0xE5] = "shift", [0x27] = "0" }
for i = 0, 25 do NAMES[0x04 + i] = string.char(97 + i) end
for i = 1, 9 do NAMES[0x1D + i] = tostring(i) end
function Input.keyname(u) return NAMES[u] or "?" end

-- the names of the keys of an action, once each ("shift" for both shifts)
function Input.keynames(id)
  local r, seen = {}, {}
  for _, u in ipairs(binds[id]) do
    local n = Input.keyname(u)
    if not seen[n] then seen[n] = true r[#r + 1] = n end
  end
  return r
end

local function kdown(id)
  for _, u in ipairs(binds[id]) do
    if keydown(u) then return true end
  end
  return false
end

local function save_binds()
  local ok, t = pcall(saved)
  if not ok or type(t) ~= "table" then t = {} end
  local k = {}
  for _, a in ipairs(ACTIONS) do
    if not same(binds[a[1]], a[3]) then k[a[1]] = copy(binds[a[1]]) end
  end
  t.keys = next(k) and k or nil
  pcall(save, t)
end

local function load_binds()
  local ok, t = pcall(saved)
  local s = ok and type(t) == "table" and t.keys
  if type(s) ~= "table" then return end
  for _, a in ipairs(ACTIONS) do
    local l = s[a[1]]
    if type(l) == "table" then
      local good = {}
      for _, u in ipairs(l) do
        if Input.bindable(u) and #good < 4 then good[#good + 1] = u end
      end
      if #good > 0 then binds[a[1]] = good end
    end
  end
end

-- The key u for the action id. Another action that had it loses it; if that leaves it with none,
-- it takes the key set the first action had (a swap). Returns the label of that action, or nil.
function Input.bind(id, u)
  if not Input.bindable(u) then return nil end
  local old, moved = binds[id], nil
  for _, a in ipairs(ACTIONS) do
    local o = a[1]
    if o ~= id then
      local n, had = {}, false
      for _, v in ipairs(binds[o]) do
        if v == u then had = true else n[#n + 1] = v end
      end
      if had then
        binds[o] = #n > 0 and n or copy(old)
        moved = a[2]
      end
    end
  end
  binds[id] = { u }
  save_binds()
  return moved
end

-- every action back on its default key
function Input.reset_binds()
  reset_binds()
  save_binds()
end

-- pad() bits
local PB = { L = 1, R = 2, U = 4, D = 8, A = 16, B = 32, START = 64, SELECT = 128, X = 256, Y = 512,
  L1 = 1024, R1 = 2048, L2 = 4096, R2 = 8192, L3 = 16384, R3 = 32768 }
Input.PB = PB

local prev = {}
local cmd = { mx = 0, mz = 0, look_x = 0, look_y = 0 }
Input.cmd = cmd
Input.sens = 1.0                  -- look speed (dev menu)

local function held(name) return cmd[name] end

local nintendo = false            -- the RGB30's own buttons: the face buttons by position
Input.nintendo = false

local named = false               -- btn("ok") exists (a runtime from before 2026-10-04: frames.py --root)

function Input.init()
  rawkeys(true)                   -- the keyboard is read key by key (keydown)
  pcall(mouse, true, false)       -- the buttons; the arrow only in the pause menu (no mouse: nothing)
  named = pcall(btn, "ok")
  load_binds()
end

local function layout_check()
  local ok, c = pcall(controller, 1)
  nintendo = ok and type(c) == "table" and c.layout == "nintendo"
  Input.nintendo = nintendo
end

-- the console's arrow over the frame: on in the pause menu, off while playing
local arrow = false
function Input.arrow(on)
  if on == arrow then return end
  arrow = on
  pcall(mouse, true, on)
end

-- The mouse look (.bm): mouse() says where the pointer is, held inside the
-- screen (the runtime has no motion of its own), so the view turns by how far
-- the pointer moved since the last frame. At an edge it cannot go on: pushed
-- into one, it keeps turning the view at the stick's top speed while it stays
-- there (moving it back a little stops it). Only in the hero's own first
-- person. What it turns is cmd.look_x / look_y, as the stick: the hero (80_modes)
-- or, on the network, the view sent in the command (83_net), never the
-- simulation directly (lockstep).
Input.mouse_sens = 1.0            -- mouse look speed
local MOUSE_TURN = math.pi        -- radians for the pointer across the screen's width
local ml = { ex = 0, ey = 0 }     -- ml.x, ml.y: the pointer at the last frame (ml.w, ml.h: its screen)

-- the local hero's own first person: not the pause menu (the arrow), the menus,
-- the death camera, a hero's camera (Fuse's wheel: cam3p), the free camera, the
-- hero select, a spectator, the network's leave question
local function first_person()
  local a, m = G.local_actor, G.mode
  if arrow or not a or not a.alive or a.cam3p or Dev.cam.on then return false end
  if m ~= "range" and m ~= "match" and m ~= "explore" then return false end
  local mt = Modes.list.match
  if m == "match" and mt and (mt.spectate or (mt.state.select and mt.state.select.open)) then return false end
  if online and select(2, online()) then return false end
  return true
end

-- -1 / 1: the pointer pushed into the low / high edge (p: its pixel, d: its move
-- this frame, e: the last frame's answer, hi: the last pixel); 0 elsewhere
local function edge(p, d, e, hi)
  if p <= 0 then return (d < 0 or e < 0) and -1 or 0 end
  if p >= hi then return (d > 0 or e > 0) and 1 or 0 end
  return 0
end

-- the mouse's turn this frame (radians) from the pointer at px, py (nil: none)
local function mouse_look(px, py)
  if not px or not first_person() then
    ml.x = nil
    return 0, 0
  end
  if not ml.x or SW ~= ml.w or SH ~= ml.h then     -- the first frame (or a new screen, that moves it): from here
    ml.x, ml.y, ml.w, ml.h, ml.ex, ml.ey = px, py, SW, SH, 0, 0
    return 0, 0
  end
  local dx, dy = px - ml.x, py - ml.y
  ml.x, ml.y = px, py
  ml.ex = edge(px, dx, ml.ex, SW - 1)
  ml.ey = edge(py, dy, ml.ey, SH - 1)
  local k = MOUSE_TURN * Input.mouse_sens / SW            -- (square pixels: the width for both)
  return dx * k + ml.ex * 3.4 * Input.sens * DT, -dy * k - ml.ey * 2.4 * Input.sens * DT
end

-- one frame: fills cmd (and cmd.pressed_* for the edges)
function Input.read()
  local p = pad(1) | pad()
  local kd = keydown
  Input.frames = (Input.frames or 0) + 1
  if Input.frames % 30 == 1 then layout_check() end      -- a Bluetooth pad can come and go
  local pj, pc, pu, pr = PB.A, PB.B, PB.Y, PB.X          -- jump, crouch, ultimate, reload
  if nintendo then pj, pc, pu, pr = PB.B, PB.A, PB.X, PB.Y end
  local function b(bit) return p & bit ~= 0 end
  -- the mouse: buttons held (1 left, 2 right) and the pointer in the HUD's pixels
  local mb, mlx, mly = 0, 0, 0
  do
    local ok, px, py, bt = pcall(mouse)
    if not ok then px = nil end
    if px then
      mb = bt or 0
      cmd.ptr_x, cmd.ptr_y = px // UI, py // UI
    end
    mlx, mly = mouse_look(px, py)
  end
  -- movement: left stick, or WASD
  local sx, sy = stick(1)
  local kx = (kdown("rgt") and 1 or 0) - (kdown("lft") and 1 or 0)
  local kz = (kdown("fwd") and 1 or 0) - (kdown("bwd") and 1 or 0)
  if kx ~= 0 or kz ~= 0 then
    local l = sqrt(kx * kx + kz * kz)
    sx, sy = kx / l, -kz / l
  end
  cmd.mx, cmd.mz = sx, -sy
  -- looking: right stick (with a curve: fine aim near the centre), or arrows
  local rx, ry = stick(1, 1)
  local ax = (kd(K.RIGHT) and 1 or 0) - (kd(K.LEFT) and 1 or 0)
  local ay = (kd(K.DOWN) and 1 or 0) - (kd(K.UP) and 1 or 0)
  if ax ~= 0 or ay ~= 0 then rx, ry = ax * 0.75, ay * 0.75 end
  local function curve(v) return v * abs(v) end
  cmd.look_x = curve(rx) * 3.4 * Input.sens * DT + mlx      -- radians this frame (and the mouse's)
  cmd.look_y = -curve(ry) * 2.4 * Input.sens * DT + mly
  local now = {
    fire = b(PB.R2) or kdown("fire") or mb & 1 ~= 0,
    fire2 = b(PB.L2) or kdown("fire2") or mb & 2 ~= 0,
    ab1 = b(PB.L1) or kdown("ab1"),
    ab2 = b(PB.R1) or kdown("ab2"),
    ult = b(pu) or kdown("ult"),
    jump = b(pj) or kdown("jump"),
    crouch = b(pc) or kdown("crouch"),
    reload = b(pr) or kdown("reload"),
    melee = b(PB.R3) or kdown("melee"),
    menu = b(PB.START) or kd(K.ENTER),
    dev = b(PB.SELECT) or kd(K.TAB),
    up = b(PB.U) or kd(K.UP), down = b(PB.D) or kd(K.DOWN),
    left = b(PB.L) or kd(K.LEFT), right = b(PB.R) or kd(K.RIGHT),
    f1 = kd(K.F1), f2 = kd(K.F2), f3 = kd(K.F3), f4 = kd(K.F4), f5 = kd(K.F5), f6 = kd(K.F6),
    hero = kdown("hero"),
    click = mb & 1 ~= 0,                                                          -- (the pause menu)
    ok = (named and btn("ok")) or kd(K.SPACE) or (not named and b(PB.A)),          -- the menus' yes
    back = (named and btn("back")) or kd(K.BKSP) or (not named and b(PB.B)),       -- and back
  }
  for k, v in pairs(now) do
    cmd[k] = v
    cmd[k .. "_p"] = v and not prev[k]          -- pressed this frame
    prev[k] = v
  end
  cmd.pad = lastinput() ~= "keyboard"
end

-- an empty command (bots fill theirs; frozen players)
function Input.blank(c)
  c = c or {}
  c.mx, c.mz, c.look_x, c.look_y = 0, 0, 0, 0
  for _, k in ipairs({ "fire", "fire2", "ab1", "ab2", "ult", "jump", "crouch", "reload", "melee" }) do
    c[k] = false
    c[k .. "_p"] = false
  end
  return c
end
