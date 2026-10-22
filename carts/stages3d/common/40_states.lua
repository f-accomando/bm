-- the states: title -> play (stage 1..3, each ended by a clear) -> final -> record -> title
local function enter_stage(k)
    GAME.stage = k
    GAME.objects = stage_objects(k)
    GAME.px, GAME.pz = GAME.objects.start.x, GAME.objects.start.z
    GAME.py = ground(GAME.px, GAME.pz) + EYE
    GAME.apples = 0
    GAME.apples_total = #GAME.objects.apples
    GAME.stage_frames = 0
    GAME.state = "play"
    log("stages3d: stage " .. k .. " start")
end

function update_state()
    local s = GAME.state
    if s == "title" then
        if btnp("a") then
            GAME.total_frames = 0
            enter_stage(1)
        end
    elseif s == "play" then
        local dx = (btn("right") and 1 or 0) - (btn("left") and 1 or 0)
        local dz = (btn("up") and 1 or 0) - (btn("down") and 1 or 0)
        player_step(dx, dz)
        collect()
        GAME.stage_frames = GAME.stage_frames + 1
        GAME.total_frames = GAME.total_frames + 1
        if at_goal() then
            GAME.state = "clear"
            GAME.timer = 0
            log("stages3d: stage " .. GAME.stage .. " clear frames=" .. GAME.stage_frames
                .. " apples=" .. GAME.apples .. "/" .. GAME.apples_total)
        end
    elseif s == "clear" then
        GAME.timer = GAME.timer + 1
        if GAME.timer >= CLEAR_FRAMES then
            if GAME.stage < #GAME.stages then
                enter_stage(GAME.stage + 1)
            else
                GAME.state = "final"
                log("stages3d: final frames=" .. GAME.total_frames)
            end
        end
    elseif s == "final" then
        if btnp("a") then
            GAME.state = "record"
            local ok, back = record_save()
            log("stages3d: record frames=" .. GAME.total_frames .. " saved=" .. tostring(ok)
                .. " readback=" .. tostring(back) .. " best=" .. tostring(GAME.best))
        end
    elseif s == "record" then
        if btnp("a") then
            GAME.state = "title"
            log("stages3d: title")
        end
    end
end
