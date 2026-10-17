-- movement, apples, goal, the record
function player_step(dx, dz)
    local nx, nz = GAME.px + dx * SPEED, GAME.pz + dz * SPEED
    if can_stand(nx, nz) then
        GAME.px, GAME.pz = nx, nz
    elseif can_stand(nx, GAME.pz) then
        GAME.px = nx
    elseif can_stand(GAME.px, nz) then
        GAME.pz = nz
    end
    GAME.py = ground(GAME.px, GAME.pz) + EYE
end

local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function collect()
    for _, a in ipairs(GAME.objects.apples) do
        if not a.taken and dist(GAME.px, GAME.pz, a.x, a.z) < APPLE_R then
            a.taken = true
            GAME.apples = GAME.apples + 1
        end
    end
end

function at_goal()
    local g = GAME.objects.goal
    return dist(GAME.px, GAME.pz, g.x, g.z) < GOAL_R
end

-- keeps the fewest frames; returns whether the save worked and what reads back
function record_save()
    local old = saved()
    local best = old and old.best
    if not best or GAME.total_frames < best then
        best = GAME.total_frames
    end
    local ok = save({ best = best })
    GAME.best = best
    return ok, (saved() or {}).best
end
