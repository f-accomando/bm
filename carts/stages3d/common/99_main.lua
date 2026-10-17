-- the entry points: only dispatching
function _init()
    world_build()
    GAME.best = (saved() or {}).best
    GAME.gpu = gpu3d() and true or false
    log("stages3d: title gpu3d=" .. (GAME.gpu and "on" or "off"))
end

function _update()
    update_state()
end

function _draw()
    FRONT[GAME.state .. "_draw"](GAME)
end
