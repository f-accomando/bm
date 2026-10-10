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

-- pad() bits
local PB = { L = 1, R = 2, U = 4, D = 8, A = 16, B = 32, START = 64, SELECT = 128, X = 256, Y = 512,
  L1 = 1024, R1 = 2048, L2 = 4096, R2 = 8192, L3 = 16384, R3 = 32768 }
Input.PB = PB

local prev = {}
local cmd = { mx = 0, mz = 0, look_x = 0, look_y = 0 }
Input.cmd = cmd
Input.sens = 1.0                  -- look speed (dev menu)

local function held(name) return cmd[name] end

local named = false               -- btn("ok") exists (a runtime from before 2026-10-04: frames.py --root)

function Input.init()
  rawkeys(true)                   -- the keyboard is read key by key (keydown)
  named = pcall(btn, "ok")
end

-- one frame: fills cmd (and cmd.pressed_* for the edges)
function Input.read()
  local p = pad(1) | pad()
  local kd = keydown
  local function b(bit) return p & bit ~= 0 end
  -- movement: left stick, or WASD
  local sx, sy = stick(1)
  local kx = (kd(K.D) and 1 or 0) - (kd(K.A) and 1 or 0)
  local kz = (kd(K.W) and 1 or 0) - (kd(K.S) and 1 or 0)
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
  cmd.look_x = curve(rx) * 3.4 * Input.sens * DT      -- radians this frame
  cmd.look_y = -curve(ry) * 2.4 * Input.sens * DT
  local now = {
    fire = b(PB.R2) or kd(K.J) or kd(K.Z),
    fire2 = b(PB.L2) or kd(K.K) or kd(K.X),
    ab1 = b(PB.L1) or kd(K.LSHIFT) or kd(K.RSHIFT),
    ab2 = b(PB.R1) or kd(K.E),
    ult = b(PB.Y) or kd(K.Q),
    jump = b(PB.A) or kd(K.SPACE),
    crouch = b(PB.B) or kd(K.C) or kd(K.LCTRL),
    reload = b(PB.X) or kd(K.R),
    melee = b(PB.R3) or kd(K.V),
    menu = b(PB.START) or kd(K.ENTER),
    dev = b(PB.SELECT) or kd(K.TAB),
    up = b(PB.U) or kd(K.UP), down = b(PB.D) or kd(K.DOWN),
    left = b(PB.L) or kd(K.LEFT), right = b(PB.R) or kd(K.RIGHT),
    f1 = kd(K.F1), f2 = kd(K.F2), f3 = kd(K.F3), f4 = kd(K.F4), f5 = kd(K.F5), f6 = kd(K.F6),
    hero = kd(K.H),
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
