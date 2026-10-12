-- Sounds: the cartridge's bank (carts/overbit/art/sounds.py makes it, the
-- names here in its order). Played by name; a few at a time at most. The
-- effects are samples and soft synth voices; GAIN keeps them under the music of the console's menus.

Snd = { on = true }

local NAMES = { "cannon", "pistol", "rocket", "boost", "bump", "boom", "boom_big", "redline", "eject",
                "call", "land", "hit", "crit", "kill", "step", "ui", "ui_back", "field",
                "saber", "barrier", "shatter", "strike", "repeater", "limit", "dash",
                "rifle", "helix", "sprint", "heal", "visor",
                "open", "capture", "lost", "win", "lose", "tick" }
local index = {}
for i, n in ipairs(NAMES) do index[n] = i - 1 end
Snd.index = index

local UIQUIET = { ui = 0.6, ui_back = 0.6, tick = 0.6 }     -- the menus are the quietest
local GAIN = 0.7            -- everything a little under full scale
local last = {}

function Snd.play(name, vol)
  if not Snd.on then return end
  local n = index[name]
  if not n then return end
  -- the same sound at most every 2 frames (the cannons fire 7 times a second)
  if last[name] and G.frame - last[name] < 2 then return end
  last[name] = G.frame
  sfx(n, nil, 0, (vol or 1) * GAIN * (UIQUIET[name] or 1))
end

function Snd.hit(crit)
  Snd.play(crit and "crit" or "hit", 0.7)
end

function Snd.kill()
  Snd.play("kill")
end
