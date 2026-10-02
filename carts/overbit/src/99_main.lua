-- The frame: input, the mode's update, the quality governor; the mode draws.

function _init()
  Input.init()
  World.init()
  Quality.set(G.quality)
  Modes.start(OVERBIT_START or "menu")
  log("overbit build " .. (OVERBIT_BUILD or "dev"))
end

function _update()
  G.frame = G.frame + 1
  Input.read()
  if Input.cmd.dev_p or Input.cmd.f1_p then G.dev = not G.dev end
  if G.dev and Input.cmd.f2_p then G.qauto = false Quality.set((G.quality + 1) % 5) end
  if G.dev and Input.cmd.f3_p then G.qauto = not G.qauto end
  if G.dev and Input.cmd.f4_p and G.local_actor then G.local_actor.ult = 100 end      -- dev: a full ultimate
  G.t = G.t + DT
  Modes.update()
  G.update_ms = stat(8)
  Quality.update()
end

function _draw()
  Modes.draw()
  if G.dev then Dev.draw() end
end
