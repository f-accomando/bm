-- the 3D map, the floor under a point, what stands where
function world_build()
    GAME.world = map3d_build({ floor = "floor", height = "height", walls = "walls", cell = 1.0, step = 0.25 })
end

-- the objects of stage k: where the player starts, the goal, the apples
function stage_objects(k)
    local lo, hi = ZONE * (k - 1), ZONE * k
    local r = { start = { x = 0, z = 0 }, goal = { x = 0, z = 0 }, apples = {} }
    for _, o in ipairs(map3d_objects(GAME.world)) do
        if o.z >= lo and o.z < hi then
            if o.kind == "PLAYER_START" then
                r.start = { x = o.x, z = o.z }
            elseif o.kind == "GOAL" then
                r.goal = { x = o.x, z = o.z }
            elseif o.kind == "APPLE" then
                r.apples[#r.apples + 1] = { x = o.x, z = o.z, taken = false }
            end
        end
    end
    return r
end

-- the floor's height under (x, z), or nil
function ground(x, z)
    return map3d_height(GAME.world, x, z)
end

function can_stand(x, z)
    local g = ground(x, z)
    return g ~= nil and not map3d_blocked(GAME.world, x, g + EYE, z, RADIUS)
end
