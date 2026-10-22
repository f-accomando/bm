do
  -- The numbers. stat(16), stat(17) and stat(18) are read only to be shown and
  -- printed: nothing in the logic of the cart depends on them (no quality
  -- governor). The sweep is driven by frame counts, not by stat().
  local W = { n = 0, ms = 0, max = 0, gpu = 0, wait = 0, anim = 0, tri = 0, vtx = 0, lua = 0 }
  local t0 = 0
  local S = nil                 -- the sweep in progress
  local COUNTS = { 0, 4, 8, 12, 16, 20, 28, 36, 48, 64 }
  local WARM, MEASURE = 30, 120

  H.last = { ms = 0, max = 0, fps = 0, gpu = 0, wait = 0, anim = 0, tri = 0, vtx = 0, lua = 0 }
  H.seconds = 0

  local function add_line(s)
    log(s)
    local L = H.lines
    L[#L + 1] = s
    if #L > 120 then table.remove(L, 1) end
  end

  local function median(t)
    local c = { table.unpack(t) }
    table.sort(c)
    return c[(#c + 1) // 2] or 0
  end

  local function sweep_next()
    S.i = S.i + 1
    if S.i > #COUNTS then
      -- the load at 60 and 30 fps: the heroes where the median passes 16.7 and 33.3 ms
      local function load_at(limit)
        local prev
        for _, p in ipairs(S.table) do
          if p.ms > limit then
            if not prev then return 0 end
            return prev.n + (limit - prev.ms) / (p.ms - prev.ms) * (p.n - prev.n)
          end
          prev = p
        end
        return nil
      end
      local l60, l30 = load_at(1000 / 60), load_at(1000 / 30)
      add_line(string.format("sweep load60=%s load30=%s", l60 and fmt(l60, 1) or ">64", l30 and fmt(l30, 1) or ">64"))
      H.n, H.lod = S.n, S.lod
      H.sweep_note = "sweep done: 60fps " .. (l60 and fmt(l60, 0) or ">64") .. ", 30fps " .. (l30 and fmt(l30, 0) or ">64")
      S = nil
      return
    end
    H.n = COUNTS[S.i]
    S.phase, S.f = "warm", 0
    S.ms, S.gpu, S.tri, S.max = {}, 0, 0, 0
  end

  function H.sweep_start()
    if S then return end
    S = { i = 0, n = H.n, lod = H.lod, table = {} }
    if H.lod == 5 then H.lod = 1 end        -- the sweep uses one level, not auto
    H.sweep_note = nil
    add_line("sweep start lod=" .. H.lod_name())
    sweep_next()
  end

  function H.sweeping() return S ~= nil end

  -- once a frame, at the start of _draw: stat() holds the frame that ended
  function H.stats_frame()
    local ms, gpu, wait = stat(1), stat(16), stat(18)
    local tri, vtx, lua = stat(4), stat(7), stat(10)
    W.n = W.n + 1
    W.ms, W.gpu, W.wait = W.ms + ms, W.gpu + gpu, W.wait + wait
    W.tri, W.vtx, W.lua, W.anim = W.tri + tri, W.vtx + vtx, W.lua + lua, W.anim + H.anim_ms
    if ms > W.max then W.max = ms end
    if S then
      S.f = S.f + 1
      if S.phase == "warm" then
        if S.f >= WARM then S.phase, S.f = "measure", 0 end
      else
        S.ms[S.f], S.gpu, S.tri = ms, S.gpu + gpu, S.tri + tri
        if ms > S.max then S.max = ms end
        if S.f >= MEASURE then
          local row = { n = H.n, ms = median(S.ms) }
          S.table[#S.table + 1] = row
          add_line(string.format("sweep n=%d ms=%s max=%s gpu=%s tri=%d", H.n, fmt(row.ms), fmt(S.max),
            fmt(S.gpu / MEASURE), math.floor(S.tri / MEASURE)))
          sweep_next()
        end
      end
    end
    local now = time()
    if now - t0 >= 1 then
      local n = math.max(1, W.n)
      local L = H.last
      L.ms, L.max, L.gpu, L.wait, L.anim = W.ms / n, W.max, W.gpu / n, W.wait / n, W.anim / n
      L.tri, L.vtx, L.lua, L.fps = W.tri / n, W.vtx / n, W.lua / n, stat(2)
      L.kb = stat(0)
      if H.n == 0 then H.map_tris = L.tri end
      H.seconds = H.seconds + 1
      local d = H.drawn or { 0, 0, 0, 0 }
      add_line(string.format(
        "herocart 1 t=%d n=%d lod=%s res=%dx%d opt=%s | ms=%s max=%s fps=%d gpu=%s wait=%s anim=%s tri=%d vtx=%d lua=%d kb=%d",
        math.floor(now), H.n, H.lod_name(), SCREEN_W, SCREEN_H, H.opt_string(), fmt(L.ms), fmt(L.max), math.floor(L.fps),
        fmt(L.gpu), fmt(L.wait), fmt(L.anim), math.floor(L.tri), math.floor(L.vtx), math.floor(L.lua / 1000),
        math.floor(L.kb)))
      W = { n = 0, ms = 0, max = 0, gpu = 0, wait = 0, anim = 0, tri = 0, vtx = 0, lua = 0 }
      t0 = now
    end
  end

  -- Start saves the last 120 lines in /docs (the player copies HEROCART.TXT) and as a report
  function H.save()
    local head = string.format("herocart 1 saved t=%d n=%d lod=%s res=%dx%d map=%s\n", math.floor(time()), H.n,
      H.lod_name(), SCREEN_W, SCREEN_H, H.map_tris and tostring(math.floor(H.map_tris)) or "-")
    local text = head .. table.concat(H.lines, "\n") .. "\n"
    local ok = doc_write("HEROCART.TXT", text)
    report("herocart", text)
    H.save_note = ok and "saved HEROCART.TXT" or "doc_write failed"
    H.save_t = time()
  end
end
