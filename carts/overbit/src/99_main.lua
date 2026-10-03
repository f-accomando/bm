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
  Input.read()
  Dev.update()
  if Input.cmd.dev_p or Input.cmd.f1_p then G.dev = not G.dev end
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
  Modes.draw()
  if G.dev then Dev.draw() end
end
