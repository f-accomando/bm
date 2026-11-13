-- The cartridge's entry points.

function _init()
  math.randomseed(stat(3) + floor(time() * 1000))
  Save.load()
  if Save.data.res == "1280x720" and screen then
    screen(1280, 720)
  end
  update_screen_size()
  Food.init_dish_icons()
  Mesh.init()
  Snd.init()
  Scr.go("title")
end

function _update()
  update_screen_size()
  G.frame = G.frame + 1
  G.t = G.t + DT
  -- Select toggles the frame-time readout (to check the speed on the Pi)
  if btnp(9) then G.debug = not G.debug end
  Scr.update(DT)
  Snd.update(DT)
end

function _draw()
  Scr.draw()
  if G.debug then
    rectfill(W - 150, H - 20, 146, 16, 0x000000)
    print(fmt("%4.1fms %2dfps %4dt", stat(1), stat(2), stat(4)), W - 148, H - 20, 0x80FF80)
  end
end

-- for the host tests
KITCHEN = { G = G, Data = Data, Scr = Scr, Kit = Kit, Food = Food, Chef = Chef, Ord = Ord,
            Bot = Bot, End = End, Save = Save, Hud = Hud, Mesh = Mesh, Dis = Dis, Haz = Haz }
