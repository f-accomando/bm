-- Stages 3D, front of the .b16 edition (RGB30, 360x360): the view, the HUD, the screens. Draws only: it writes nothing in GAME and logs nothing.
local W, H = 360, 360
local PITCH = -0.45         -- negative looks down: the value checked on a shot for the .bm
local RADIUS_VISUAL = 0.4
local ball, fruit

local function centred(text, y, c)
    print(text, (W - #text * 8) // 2, y, c or 0xFFFFFF)
end

-- the game counts updates: the label is in 60 Hz seconds, as on the .bm
local function seconds(frames)
    return string.format("%.1f", frames / 60)
end

function FRONT.title_draw(g)
    cls(0x101828)
    centred("STAGES 3D", 90, 0xFFD050)
    centred("A to start", 150)
    centred(g.gpu and "3D: GPU" or "3D: ARM", 190, 0x80C0FF)
    if g.best then
        centred("best " .. g.best .. " frames", 230, 0xC0FFC0)
    end
end

function FRONT.play_draw(g)
    if not ball then
        ball = mesh_sphere(5, 6, 0xFFD050, 0xC09030)
        fruit = mesh_sphere(4, 5, 0xE03030, 0xA02020)
    end
    cls(0x6090C0)
    zclear()
    light3d(0.4, -1, 0.3, 0.45)
    camera3d(g.px, g.py + 3.5, g.pz - 5, 0, PITCH, 60)
    map3d_draw(g.world)
    draw3d(ball, g.px, g.py, g.pz, 0, 0, 0, RADIUS_VISUAL)
    for _, a in ipairs(g.objects.apples) do
        if not a.taken then
            draw3d(fruit, a.x, ground(a.x, a.z) + EYE, a.z, 0, 0, 0, 0.3)
        end
    end
    print("STAGE " .. g.stage .. " " .. g.stages[g.stage].name, 8, 8, 0xFFFFFF)
    print("APPLES " .. g.apples .. "/" .. g.apples_total, 8, 24, 0xFFFFFF)
    print("TIME " .. seconds(g.stage_frames) .. " s", 8, 40, 0xFFFFFF)
end

function FRONT.clear_draw(g)
    FRONT.play_draw(g)
    centred("STAGE " .. g.stage .. " CLEAR", 160)
end

function FRONT.final_draw(g)
    cls(0x182818)
    centred("ALL STAGES CLEAR", 90, 0xFFD050)
    centred("total time " .. seconds(g.total_frames) .. " s", 150)
    centred("A to continue", 210)
end

function FRONT.record_draw(g)
    cls(0x181828)
    centred("RECORD", 90, 0xFFD050)
    centred("best " .. tostring(g.best) .. " frames", 150, 0xC0FFC0)
    centred("this run " .. g.total_frames .. " frames", 180)
    centred("A to return", 230)
end
