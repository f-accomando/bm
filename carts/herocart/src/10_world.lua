do
  -- The plaza (map3d: 32 x 32 cells of 1 unit, assets/map_*.csv) and the camera.
  local fx, fy, fz, fyaw, fpitch = CX, 3, CZ - 12, 0, -0.2     -- the free camera

  function H.world_init()
    H.world = map3d_build({ floor = "floor", height = "height", walls = "walls", cell = 1.0, step = 0.25 })
    light3d(-0.4, 0.8, -0.5, 0.45)
  end

  -- camera3d looks along (sin yaw, ., cos yaw); a positive pitch looks up
  local function look(x, y, z, tx, ty, tz)
    local dx, dy, dz = tx - x, ty - y, tz - z
    return math.atan(dx, dz), math.atan(dy, math.sqrt(dx * dx + dz * dz))
  end

  H.CAMS = { "overview", "orbit", "close", "free" }

  -- sets H.cx, H.cy, H.cz (the camera, for the level of detail) and the call
  function H.camera_update(t)
    local x, y, z, yaw, pitch
    if H.cam == 1 then
      x, y, z = CX, 15, CZ - 21
      yaw, pitch = look(x, y, z, CX, 0, CZ)
    elseif H.cam == 2 then
      local a = t * 2 * PI / 40
      x, y, z = CX + 15 * math.sin(a), 9, CZ + 15 * math.cos(a)
      yaw, pitch = look(x, y, z, CX, 1, CZ)
    elseif H.cam == 3 then
      -- behind the first hero, close: a few heroes fill the screen
      local hx, hz, hy = H.x[1], H.z[1], H.yaw[1]
      x, y, z = hx - 4.5 * math.sin(hy), 2.6, hz - 4.5 * math.cos(hy)
      yaw, pitch = look(x, y, z, hx, 1.1, hz)
    else
      local sx, sy = stick()
      local rx, ry = stick(nil, 1)
      fyaw = fyaw + rx * 0.03
      fpitch = math.max(-1.4, math.min(1.4, fpitch - ry * 0.03))
      fx = fx + (math.sin(fyaw) * -sy + math.cos(fyaw) * sx) * 0.12
      fz = fz + (math.cos(fyaw) * -sy - math.sin(fyaw) * sx) * 0.12
      x, y, z, yaw, pitch = fx, fy, fz, fyaw, fpitch
    end
    H.cx, H.cy, H.cz = x, y, z
    H.cyaw, H.cpitch = yaw, pitch
  end

  function H.camera_apply()
    camera3d(H.cx, H.cy, H.cz, H.cyaw, H.cpitch, 60)
  end
end
