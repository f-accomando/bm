do
  -- 64 heroes walk along 12 waypoints: a ring of 8 and a cross of 4. The logic
  -- is fixed: no random numbers, no collisions. The speeds come from a table.
  local WX, WZ = {}, {}
  do
    local r, c = {}, {}
    for k = 0, 7 do r[k] = { CX + 9 * math.cos(k * PI / 4), CZ + 9 * math.sin(k * PI / 4) } end
    for j = 0, 3 do c[j] = { CX + 4.5 * math.cos(j * PI / 2), CZ + 4.5 * math.sin(j * PI / 2) } end
    local order = { r[0], c[0], r[1], r[2], c[1], r[3], r[4], c[2], r[5], r[6], c[3], r[7] }
    for i, p in ipairs(order) do WX[i], WZ[i] = p[1], p[2] end
  end

  function H.heroes_init()
    for i = 1, H.maxh do
      local w = (i * 5) % 12 + 1
      H.wp[i] = w % 12 + 1
      H.x[i] = WX[w] + ((i * 7) % 5 - 2) * 0.3
      H.z[i] = WZ[w] + ((i * 3) % 5 - 2) * 0.3
      H.yaw[i] = math.atan(WX[H.wp[i]] - H.x[i], WZ[H.wp[i]] - H.z[i])
      H.spd[i] = 0.9 + ((i * 37) % 7) / 7 * 0.6
    end
    for lv = 0, 3 do
      H.hero[lv + 1] = model("hero" .. lv)
      H.hull[lv + 1] = model("hero" .. lv .. "_o")
    end
  end

  -- all 64 run, drawn or not: the logic cost does not change with the count
  function H.heroes_update()
    for i = 1, H.maxh do
      local x, z = H.x[i], H.z[i]
      local tx, tz = WX[H.wp[i]], WZ[H.wp[i]]
      local dx, dz = tx - x, tz - z
      if dx * dx + dz * dz < 0.09 then
        H.wp[i] = H.wp[i] % 12 + 1
        tx, tz = WX[H.wp[i]], WZ[H.wp[i]]
        dx, dz = tx - x, tz - z
      end
      local d = math.atan(dx, dz) - H.yaw[i]
      d = (d + PI) % (2 * PI) - PI
      local turn = math.max(-0.14, math.min(0.14, d))     -- 8 degrees a frame
      local yaw = H.yaw[i] + turn
      H.yaw[i] = yaw
      H.x[i] = x + math.sin(yaw) * H.spd[i] * DT
      H.z[i] = z + math.cos(yaw) * H.spd[i] * DT
    end
  end

  local order, dist = {}, {}
  local function farther(a, b) return dist[a] > dist[b] end

  -- draws the first H.n heroes that can be seen; counts them per level in H.drawn
  function H.heroes_draw(t, flags, opt)
    local drawn = { 0, 0, 0, 0 }
    local m, cnt = 0, 0
    for i = 1, H.n do
      local x, z = H.x[i], H.z[i]
      if visible3d(x, 1, z, 1.3) then
        m = m + 1
        order[m] = i
        local dx, dz = x - H.cx, z - H.cz
        dist[i] = dx * dx + dz * dz
      end
    end
    for k = m + 1, #order do order[k] = nil end
    if opt.noz then table.sort(order, farther) end     -- no z test: far to near
    local lock = H.lod < 5 and H.lod or nil
    H.anim_ms = 0
    for k = 1, m do
      local i = order[k]
      local lv
      if lock then lv = lock - 1
      else
        local d2 = dist[i]
        lv = d2 < 64 and 0 or d2 < 256 and 1 or d2 < 784 and 2 or 3
      end
      drawn[lv + 1] = drawn[lv + 1] + 1
      local x, z = H.x[i], H.z[i]
      local y = map3d_height(H.world, x, z) or 0
      local ry = H.yaw[i] + PI                -- the model faces -z (checked with the close camera: from behind, no eyes)
      local ph = t + i * 0.37
      local mesh = H.hero[lv + 1]
      local a = stat(8)
      animate(mesh, "walk", ph)
      H.anim_ms = H.anim_ms + stat(8) - a
      draw3d(mesh, x, y, z, 0, ry, 0, 1, flags)
      if opt.shadow then draw3d(mesh, x, y, z, 0, ry, 0, 1, 8) end
      if opt.outline then
        local hull = H.hull[lv + 1]
        a = stat(8)
        animate(hull, "walk", ph)
        H.anim_ms = H.anim_ms + stat(8) - a
        draw3d(hull, x, y, z, 0, ry, 0, 1, 2)
      end
    end
    H.drawn = drawn
  end
end
