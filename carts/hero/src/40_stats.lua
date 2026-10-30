do
  -- The numbers. stat(16), stat(17) and stat(18) are read only to be shown and
  -- printed: nothing in the logic of the cart depends on them (no quality
  -- governor). The sweep is driven by frame counts, not by stat().
  local W = { n = 0, ms = 0, max = 0, gpu = 0, wait = 0, anim = 0, tri = 0, vtx = 0, lua = 0, skip = 0 }
  local t0 = 0
  local S = nil                 -- the sweep in progress
  local COUNTS2D = { 10, 20, 50, 100, 200, 400, 800, 1200, 1600, 2000 }
  local Q = nil                 -- the suite in progress (a list of tests, each on each profile: the bridge's tests)
  local COUNTS = { 0, 4, 8, 12, 16, 20, 28, 36, 48, 64 }
  -- the load the sweep changes: the heroes of the plaza, or the amount of the 2D over the 3D
  local function getload() return H.var2d and H.n2d or H.n end
  local function setload(v)
    if H.var2d then H.n2d = v else H.n = v end
  end
  local WARM, MEASURE = 30, 120

  H.last = { ms = 0, max = 0, fps = 0, gpu = 0, wait = 0, anim = 0, tri = 0, vtx = 0, lua = 0, skip = 0 }
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
    if S.i > #S.counts then
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
      local top = S.counts[#S.counts]
      add_line(string.format("sweep load60=%s load30=%s", l60 and fmt(l60, 1) or (">" .. top), l30 and fmt(l30, 1) or (">" .. top)))
      setload(S.n)
      H.lod = S.lod
      H.sweep_note = "sweep done: 60fps " .. (l60 and fmt(l60, 0) or (">" .. top)) .. ", 30fps " .. (l30 and fmt(l30, 0) or (">" .. top))
      local done = S
      S = nil
      if Q then H.suite_result(done, l60, l30, top) end
      return
    end
    setload(S.counts[S.i])
    S.phase, S.f = "warm", 0
    S.ms, S.gpu, S.tri, S.max = {}, 0, 0, 0
  end

  function H.sweep_start()
    if S then return end
    S = { i = 0, n = getload(), lod = H.lod, table = {}, counts = H.var2d and COUNTS2D or COUNTS }
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
    W.skip = W.skip + math.max(0, stat(15) - 1)   -- the frames not drawn since the last _draw
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
          local row = { n = getload(), ms = median(S.ms) }
          S.table[#S.table + 1] = row
          add_line(string.format("sweep n=%d ms=%s max=%s gpu=%s tri=%d", row.n, fmt(row.ms), fmt(S.max),
            fmt(S.gpu / MEASURE), math.floor(S.tri / MEASURE)))
          if Q then
            add_line(string.format("hero 1 point %s %s n=%d ms=%s max=%s gpu=%s tri=%d", Q.tests[Q.ti], Q.profs[Q.pi], row.n,
              fmt(row.ms), fmt(S.max), fmt(S.gpu / MEASURE), math.floor(S.tri / MEASURE)))
          end
          if row.ms > 70 then S.i = #S.counts end        -- far under 30 fps: the heavier loads are not run (they take minutes)
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
      L.skip = W.skip
      if H.n == 0 then H.map_tris = L.tri end
      H.seconds = H.seconds + 1
      local d = H.drawn or { 0, 0, 0, 0 }
      -- the frame count and the cursor row: a test that sends keys waits on them (a serial key is held 10 frames)
      log(string.format("hkeys f=%d row=%d", H.frame, H.cur))
      add_line(string.format(
        "hero 1 t=%d n=%d lod=%s res=%dx%d opt=%s | ms=%s max=%s fps=%d gpu=%s wait=%s anim=%s tri=%d vtx=%d lua=%d kb=%d skip=%d",
        math.floor(now), H.n, H.lod_name(), SCREEN_W, SCREEN_H, H.opt_string(), fmt(L.ms), fmt(L.max), math.floor(L.fps),
        fmt(L.gpu), fmt(L.wait), fmt(L.anim), math.floor(L.tri), math.floor(L.vtx), math.floor(L.lua / 1000),
        math.floor(L.kb), L.skip))
      W = { n = 0, ms = 0, max = 0, gpu = 0, wait = 0, anim = 0, tri = 0, vtx = 0, lua = 0, skip = 0 }
      t0 = now
    end
  end

  -- The suite: the tests the bridge asks for, each on each profile, one sweep each. The key `hero_test` (set with the bridge:
  -- `pi.py config hero_test=light-flat.fog hero_prof=arm.gpu`, then a restart) holds the ids apart by dots, or `all`;
  -- `hero_prof` the profiles (arm, gpu; default both). The keys are cleared when read. The result lines are
  -- `hero 1 result ID PROF load60=.. load30=..` and the report (Start, or the end of the suite) sends them.
  -- Each test is the plaza with its heroes and one technique of the 3D Bench on (the same functions, in the context of this
  -- cart, with its own models): the sweep changes the heroes (the 2D tests: the amount of the 2D over 20 heroes).
  -- set: { row, index of its value }; the others are the defaults.
  local DEFAULTS = { hmat = 1, tex = 1, light = 3, zt = 1, dither = 1, fog = 1, sky = 1, lamp = 1, shadow = 1, cel = 1, outline = 1,
                     imp = 1, aa = 1, vs = 1, queue = 1, later = 2 }
  local SPECS = {
    plaza = {}, ["hero-tex"] = { hmat = 2 }, ["hero-holes"] = { hmat = 3 }, ["hero-screen"] = { hmat = 4 },
    ["hero-baked"] = { hmat = 5 },
    ["light-none"] = { light = 1 }, ["light-flat"] = { light = 2 }, ["tex-off"] = { tex = 2 }, ["z-off"] = { zt = 2 },
    ["dither-off"] = { dither = 2 }, fog = { fog = 2 }, sky = { sky = 2 }, lamps2 = { lamp = 2 }, lamps4 = { lamp = 3 },
    shadows = { shadow = 2 }, cel4 = { cel = 4 }, ["outline-hull"] = { outline = 2 }, ["outline-512"] = { outline = 3 },
    impostor16 = { imp = 3 }, bilinear = { hmat = 2, bilinear = true }, aa = { aa = 2 }, vs1 = { vs = 2 }, vs2 = { vs = 3 },
    queue1 = { queue = 2 }, queue2 = { queue = 3 }, nolater = { queue = 3, later = 1 },
  }
  H.TESTS = { "plaza", "hero-tex", "hero-holes", "hero-screen", "hero-baked", "light-none", "light-flat", "tex-off", "z-off", "dither-off", "fog", "sky", "lamps2", "lamps4", "shadows",
              "cel4", "outline-hull", "outline-512", "impostor16", "bilinear", "aa", "vs2", "queue2", "nolater",
              "2d-sprites", "2d-text", "2d-map", "2d-fade", "2d-light" }

  local function rowset(key, i)
    local r = ROWS[H.ROW[key]]
    r.i = i
    r.step(0)                                  -- applies the row's value (a step of 0 turns it round to itself)
  end
  local function index_of(list, name)
    for i, v in ipairs(list) do if v == name then return i end end
    return 1
  end

  local function apply_test(id, prof)
    H.var2d = false
    rowset("o2d", 1)
    rowset("backend", prof == "gpu" and 2 or 1)
    for key, v in pairs(DEFAULTS) do rowset(key, v) end
    gpu3d_opt("filter", false)
    H.n = 20
    if id:sub(1, 3) == "2d-" then
      local kinds = { sprites = "sprites", text = "text", map = "map layers", fade = "fade", light = "light" }
      H.n2d = 10
      rowset("o2d", index_of(H.OVER2D, kinds[id:sub(4)]))
      H.var2d = true
    else
      local spec = SPECS[id] or {}
      for key, v in pairs(spec) do
        if key == "bilinear" then gpu3d_opt("filter", true) else rowset(key, v) end
      end
    end
  end

  local function suite_next()
    if not Q then return end
    Q.pi = Q.pi + 1
    if Q.pi > #Q.profs then Q.pi, Q.ti = 1, Q.ti + 1 end
    if Q.ti > #Q.tests then
      add_line("hero 1 suite done tests=" .. #Q.tests .. " res=" .. SCREEN_W .. "x" .. SCREEN_H)
      Q = nil
      H.var2d = false
      H.save()
      return
    end
    apply_test(Q.tests[Q.ti], Q.profs[Q.pi])
    add_line("hero 1 test " .. Q.tests[Q.ti] .. " " .. Q.profs[Q.pi])
    H.sweep_start()
  end

  H.results = {}                               -- the result lines of the suite: the report keeps them all (the lines are the last 120)
  function H.suite_result(done, l60, l30, top)
    local line = string.format("hero 1 result %s %s load60=%s load30=%s top=%d", Q.tests[Q.ti], Q.profs[Q.pi],
      l60 and fmt(l60, 1) or (">" .. top), l30 and fmt(l30, 1) or (">" .. top), top)
    add_line(line)
    H.results[#H.results + 1] = line
    suite_next()
  end

  function H.suite_start(spec, profs)
    if Q or S then return end
    local tests = {}
    if spec == "all" then
      for _, t in ipairs(H.TESTS) do tests[#tests + 1] = t end
    else
      for w in spec:gmatch("[^.]+") do tests[#tests + 1] = w end
    end
    local pl = {}
    for w in (profs ~= "" and profs or "arm.gpu"):gmatch("[^.]+") do pl[#pl + 1] = w end
    Q = { tests = tests, profs = pl, ti = 1, pi = 0 }
    add_line("hero 1 suite start tests=" .. #tests .. " profiles=" .. #pl)
    suite_next()
  end

  function H.suiting() return Q ~= nil end

  -- Start saves the last 120 lines in /docs (the player copies hero.txt) and as a report
  function H.save()
    local head = string.format("hero 1 saved t=%d n=%d lod=%s res=%dx%d map=%s\n", math.floor(time()), H.n,
      H.lod_name(), SCREEN_W, SCREEN_H, H.map_tris and tostring(math.floor(H.map_tris)) or "-")
    local text = head .. (#H.results > 0 and (table.concat(H.results, "\n") .. "\n") or "") .. table.concat(H.lines, "\n") .. "\n"
    local ok = doc_write("hero.txt", text)
    report("hero", text)
    H.save_note = ok and "saved hero.txt" or "doc_write failed"
    H.save_t = time()
  end
end
