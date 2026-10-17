-- Stages 3D, shared logic (both editions): locals shared by every file, the GAME table the front reads
local FRONT = {}            -- the edition's front fills this in (title_draw, play_draw, clear_draw, final_draw, record_draw)
local GAME = {
    state = "title",        -- "title", "play", "clear", "final", "record"
    stage = 1, px = 0, py = 0, pz = 0,
    apples = 0, apples_total = 0,
    stage_frames = 0, total_frames = 0,
    best = nil,             -- the record: the fewest frames, or nil
    gpu = false, world = nil, stages = nil, timer = 0,
}

local SPEED = 0.08          -- cells per update
local RADIUS = 0.4
local EYE = 0.5             -- height of the ball's centre above the floor
local GOAL_R = 0.6
local APPLE_R = 0.7
local CLEAR_FRAMES = 60
local ZONE = 16             -- cells per stage in the map
