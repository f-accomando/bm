Scr = { name = "fight" }
function Scr.update()
  -- simple direct fight for now
  if not Scr.f1 then
    Scr.f1 = Fighter.new(1, 0, {armor="light", weapon="sword"}, 200, 1)
    Scr.f2 = Fighter.new(2, 1, {armor="heavy", weapon="guns"}, 560, -1)
  end
  Fighter.update(Scr.f1, Scr.f2)
  Fighter.update(Scr.f2, Scr.f1)
  -- camera
  local mid = (Scr.f1.x + Scr.f2.x) / 2
  G.camx = clamp(mid - W/2, 0, ARENA_W - W)
end
function Scr.draw()
  Stage.draw(G.camx)
  Fighter.draw(Scr.f1)
  Fighter.draw(Scr.f2)
  Hud.draw(Scr.f1, Scr.f2)
end
