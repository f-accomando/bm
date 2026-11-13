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
    H.scene_init()
    local look = cart_config and tonumber(cart_config("hero_look") or "")     -- the Hero look row at the start (1 to 5)
    if look and ROWS[H.ROW.hmat] then
      ROWS[H.ROW.hmat].i = math.max(1, math.min(5, math.floor(look)))
      ROWS[H.ROW.hmat].step(0)
    end
    local want = cart_config and cart_config("hero_test") or nil
    if want and want ~= "" then                  -- the bridge asked for tests (config hero_test=..., hero_prof=...)
      H.suite_want, H.prof_want = want, cart_config("hero_prof") or ""
      cart_config("hero_test", "")
      cart_config("hero_prof", "")
    end
    log("hero 1 start build " .. tostring(HERO_BUILD))
  end

  function _update()
    H.frame = H.frame + 1
    if H.suite_want and H.frame == 60 then
      H.suite_start(H.suite_want, H.prof_want)
      H.suite_want = nil
    end
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
    H.overlay_2d(H.frame)
    H.panel_draw()
  end
end
