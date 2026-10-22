do
  function _init()
    H.world_init()
    H.heroes_init()
    H.options_sync()
    textures3d(true)
    log("herocart 1 start build " .. tostring(HEROCART_BUILD))
  end

  function _update()
    H.frame = H.frame + 1
    H.input()
    H.heroes_update()
    H.camera_update(H.frame * DT)
  end

  function _draw()
    H.stats_frame()
    cls(SKY)
    zclear()
    H.camera_apply()
    local flags = H.draw_flags()
    local opt = H.draw_opts()
    map3d_draw(H.world, flags)
    H.heroes_draw(H.frame * DT, flags | (opt.noz and 1 or 0), opt)
    H.panel_draw()
  end
end
