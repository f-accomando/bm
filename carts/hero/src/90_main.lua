do
  function _init()
    H.world_init()
    H.heroes_init()
    H.options_sync()
    gpu3d(true, nil, nil, 2, nil, true)
    H.options_sync()
    if stat(9) == 1 then
      H.n = 50
      H.lod = 1
      ROWS[2].i = 1
    end
    textures3d(true)
    frameskip(MAX_SKIP)     -- a slow frame is not drawn: the logic runs on at 60 a second
    log("hero 1 start build " .. tostring(HERO_BUILD))
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
