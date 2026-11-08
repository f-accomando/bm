-- Runs ../run.py's main.lua with the two functions of hot.pln instead of the C ones (the names *_c).
--   plua wrapper.lua HOT.lua CASES REPS show|- c MAIN.lua
local pm = require "hot"
cam_blocked_c, near_pairs_c = pm.cam_blocked, pm.near_pairs
dofile(arg[6])
