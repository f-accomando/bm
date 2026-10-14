local pico, apple, crate, w
local lamps, apples = {}, {}
local px, pz, t = 7.5, 2, 0

function _init()
  pico, apple, crate = model("PICO"), model("APPLE"), model("CRATE")
  w = map3d_build()
  lamps, apples = map3d_objects(w, "LAMP"), map3d_objects(w, "APPLE")
  -- [shadow]
  -- [sky]
  -- [fog]
end

function _update()
  t = t + 1 / 60
  pz = pz + 3 / 60
  if pz > 19 then pz = 2 end
end

function _draw()
  local py = map3d_height(w, px, pz) or 0
  cls(0xA8D0C0)
  camera3d(px, py + 3.2, pz - 6, 0, -0.35, 60)
  light3d(-0.4, 0.8, -0.5, 0.45)
  zclear()
  map3d_draw(w)
  -- [lamp]
  animate(pico, "run", t)
  -- [shadow-draw]
  draw3d(pico, px - 0.75, py, pz, 0, 0, 0, 0.5)
  for _, o in ipairs(apples) do
    draw3d(apple, o.x, o.y + 0.4, o.z, 0, t * 2, 0, 0.5)
  end
end
