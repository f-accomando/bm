-- Stages 3D quality governor: frame time -> impostor threshold (shared by both editions; the budget is the front's FRONT.budget_ms).
local Q = { level = 0, over = 0, under = 0 }

function quality_step()
    local ms = stat(1)
    local budget = FRONT.budget_ms or 16.0
    if ms > budget then
        Q.over = Q.over + 1
        Q.under = 0
    elseif ms < budget * 0.7 then
        Q.under = Q.under + 1
        Q.over = 0
    else
        Q.over = 0
        Q.under = 0
    end
    if Q.level == 0 and Q.over >= 60 then
        Q.level = 1
        Q.over = 0
        gpu3d(nil, nil, nil, nil, 8)
        log("stages3d: quality 1 impostor=8 ms=" .. string.format("%.1f", ms))
    elseif Q.level == 1 and Q.under >= 180 then
        Q.level = 0
        Q.under = 0
        gpu3d(nil, nil, nil, nil, 0)
        log("stages3d: quality 0 impostor=0 ms=" .. string.format("%.1f", ms))
    end
end
