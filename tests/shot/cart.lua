-- Screenshots (docs/SCREENSHOTS.md), checked in bmhost with --sd: shot() planned
-- at frame 3 and 6 (two files, numbers 1 and 2), shot("ab!c") at frame 9 (a
-- prefix of its own), twice in one frame (one file). tests/shot/check.py reads
-- the PNGs in the SD's bm/shots.
local frame = 0

function _draw()
  frame = frame + 1
  cls(1)
  rectfill(10, 10, 60, 40, 8)
  rectfill(frame * 4, 50, frame * 4 + 20, 70, 11)
  if frame == 3 or frame == 6 then
    log("shot: " .. tostring(shot()))
  elseif frame == 9 then
    log("shot: " .. tostring(shot("ab!c")))
    log("shot: " .. tostring(shot("zz")))       -- the same frame: the same file
  end
end
