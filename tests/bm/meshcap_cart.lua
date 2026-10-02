-- A cartridge for test_meshcap: meshes in a table, in an array and in a
-- local of a function; then it overwrites what the name search uses and
-- stops with an error. The meshes made before the error come back, with
-- their names, and the error with its line.

local parts = {}
parts.wheel = mesh({ 0, 0, 0, 1, 0, 0, 0, 1, 0 }, { 1, 2, 3, 0xFF0000 })
local cars = { { body = mesh({ 0, 0, 0, 2, 0, 0, 0, 2, 0 }, { 1, 3, 2, 0x00FF00 }) } }
local gem
function _init()
  gem = mesh({ 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1 }, { 1, 2, 3, -1, 1, 3, 4, 0x0000FF }, { 0, 0, 8, 0, 8, 8, 0, 0, 0, 0, 0, 0 })
  local w, h = players()
  local sx, sy = project3d(0, 0, 0)
  assert(w and sx and sy < SCREEN_H, "the stand-ins give numbers")
end
function _draw()
  draw3d(parts.wheel, 0, 0, 0)
  draw3d(gem, 0, 0, 0)
  draw3d(cars[1].body, 0, 0, 0)
  type, next, _G = nil, nil, nil
  error("stop here")
end
