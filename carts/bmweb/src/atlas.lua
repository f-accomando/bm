-- bmweb: slots on the sprite sheet for the pictures of one page. The sheet
-- is COLS x ROWS cells of SW x SH pixels; a slot is a cell. Pure Lua.
local M = {}

M.SW, M.SH, M.COLS, M.ROWS = 160, 128, 4, 4
M.W, M.H = M.SW * M.COLS, M.SH * M.ROWS    -- the sheet size: 640x512
M.IMG_W, M.IMG_H = 160, 120                 -- a picture is cut to this
M.MAX = M.COLS * M.ROWS

function M.new() return { n = 0 } end

function M.alloc(a)
  if a.n >= M.MAX then return nil end
  local i = a.n
  a.n = a.n + 1
  return (i % M.COLS) * M.SW, (i // M.COLS) * M.SH
end

function M.reset(a) a.n = 0 end

return M
