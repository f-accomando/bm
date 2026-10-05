-- The frame: input, the mode's update, the quality governor; the mode draws.

function _init()
  if OVERBIT_SEED then math.randomseed(OVERBIT_SEED) grandom_seed(OVERBIT_SEED) end   -- the trainer's matches
  Input.init()
  World.init()
  Quality.set(G.quality)
  if OVERBIT_HERO and H[OVERBIT_HERO] then G.hero_id = OVERBIT_HERO end     -- (the reel takes a list too)
  if OVERBIT_QUALITY then G.qauto = false Quality.set(OVERBIT_QUALITY) end
  Modes.start(OVERBIT_START or "menu")
  log("overbit build " .. (OVERBIT_BUILD or "dev"))
end

function _update()
  G.frame = G.frame + 1
  G.gpu = stat(9) == 1          -- the GPU draws the 3D: some things cost less (40_actor)
  if SCREEN_W ~= SW or SCREEN_H ~= SH then screen_size() end      -- screen() took effect
  if G.frame == 2 then Modes.saved_res() end     -- (the GPU has started by now: stat(9))
  -- the ARM draws (3D in the menu, or the GPU stopped): not above 640x360,
  -- its pixels cost it (the choice saved stays for the GPU)
  if not G.gpu and SW * SH > 640 * 360 and screen(640, 360) then log("overbit resolution 640x360 (the ARM draws)") end
  Input.read()
  -- the dev kit is the system's overlay (F11): Select, Tab or F1 go round it
  -- too (simple, detailed, off); the dev keys work while it is shown
  if Input.cmd.dev_p or Input.cmd.f1_p then devkit((devkit() + 1) % 3) end
  G.dev = devkit() > 0
  Dev.update()
  if G.dev and Input.cmd.f2_p then G.qauto = false Quality.set((G.quality + 1) % 5) end
  if G.dev and Input.cmd.f3_p then G.qauto = not G.qauto end
  if G.dev and Input.cmd.f4_p and G.local_actor then G.local_actor.ult = 100 end      -- dev: a full ultimate
  if G.dev and Input.cmd.f5_p and G.local_actor then        -- dev: lose this life (a mech: the pilot ejects)
    local a = G.local_actor
    a.fx.invuln_t = 0
    Actors.damage(a, (Actors.total(a) + 1) / (1 - 0.3), nil, false, "dev")
  end
  if not Net.on then G.t = G.t + DT end    -- a network match moves it only when a frame runs
  Modes.update()
  G.update_ms = stat(8)
  Quality.update()
end

function _draw()
  if OVERBIT_HEADLESS then return end           -- the trainer's matches: nothing to see
  if SCREEN_W ~= SW or SCREEN_H ~= SH then screen_size() end      -- (the frame queue ran _update before)
  Modes.draw()
  if devkit() == 2 then Dev.info() end
end
