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
      H.y[i] = map3d_height(H.world, H.x[i], H.z[i]) or 0
      H.ry[i] = H.yaw[i] + PI
    end
    for lv = 0, 3 do
      H.hull[lv + 1] = model("hero" .. lv .. "_o")
    end
    -- the materials of the heroes (build/mkheroes.py): flat colours, a texture, a texture with holes, screen-door, baked light
    H.hero_sets = {}
    for k, suffix in ipairs({ "", "_t", "_a", "_s", "_b" }) do
      H.hero_sets[k] = {}
      for lv = 0, 3 do H.hero_sets[k][lv + 1] = model("hero" .. lv .. suffix) end
    end
    H.hero = H.hero_sets[1]
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
      local nx = x + math.sin(yaw) * H.spd[i] * DT
      local nz = z + math.cos(yaw) * H.spd[i] * DT
      H.x[i] = nx
      H.z[i] = nz
      H.y[i] = map3d_height(H.world, nx, nz) or 0
      H.ry[i] = yaw + PI
    end
  end

  local order, dist, lvl = {}, {}, {}
  local drawn = { 0, 0, 0, 0 }
  local buckets = {}
  for b = 1, 16 do buckets[b] = {} end
  local function farther(a, b) return dist[a] > dist[b] end

  -- draws the first H.n heroes that can be seen; counts them per level in H.drawn
  function H.heroes_draw(t, flags, opt)
    drawn[1], drawn[2], drawn[3], drawn[4] = 0, 0, 0, 0
    local m = 0
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
    for k = 1, m do
      local i = order[k]
      local lv
      if lock then lv = lock - 1
      else
        local d2 = dist[i]
        lv = d2 < 64 and 0 or d2 < 256 and 1 or d2 < 784 and 2 or 3
      end
      drawn[lv + 1] = drawn[lv + 1] + 1
      lvl[k] = lv
    end

    local anim_ms = 0
    if opt.shadow then
      for k = 1, m do
        local i, lv = order[k], lvl[k]
        local mesh = H.hero[lv + 1]
        local t0 = stat(8)
        animate(mesh, "walk", t + i * 0.37)
        anim_ms = anim_ms + (stat(8) - t0)
        local x, z = H.x[i], H.z[i]
        draw3d(mesh, x, H.y[i], z, 0, H.ry[i], 0, 1, 8)
      end
    end

    local f = opt.cel and (flags | 256) or flags
    if opt.flag512 then f = f | 512 end

    -- Group by pose (16 quantized poses across walk cycle) to deduplicate bone animation computation
    for b = 1, 16 do
      local bkt = buckets[b]
      for j = 1, #bkt do bkt[j] = nil end
    end

    for k = 1, m do
      local i = order[k]
      local ph = t + i * 0.37
      local pidx = math.floor((ph % 1.0) * 16) + 1
      local bkt = buckets[pidx]
      bkt[#bkt + 1] = k
    end

    for b = 1, 16 do
      local bkt = buckets[b]
      local count = #bkt
      if count > 0 then
        local pose_ph = (b - 0.5) / 16.0
        local last_mesh = nil
        for j = 1, count do
          local k = bkt[j]
          local i, lv = order[k], lvl[k]
          local mesh = H.hero[lv + 1]
          if mesh ~= last_mesh then
            local t0 = stat(8)
            animate(mesh, "walk", pose_ph)
            anim_ms = anim_ms + (stat(8) - t0)
            last_mesh = mesh
          end
          draw3d(mesh, H.x[i], H.y[i], H.z[i], 0, H.ry[i], 0, 1, f)
          if opt.hull then
            local hull = H.hull[lv + 1]
            local t0 = stat(8)
            animate(hull, "walk", pose_ph)
            anim_ms = anim_ms + (stat(8) - t0)
            draw3d(hull, H.x[i], H.y[i], H.z[i], 0, H.ry[i], 0, 1, 2)
          end
        end
      end
    end
    H.anim_ms = anim_ms
    H.drawn = drawn
  end
end
