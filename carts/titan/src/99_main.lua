-- The cartridge's entry points.

function _init()
  math.randomseed(stat(3) + floor(time() * 1000))
  Snd.init()
  Scr.go("title")
end

-- every 10 s of fight the frame time goes to the log (serial console)
local perf = { n = 0, sum = 0, max = 0 }

function _update()
  G.frame = G.frame + 1
  -- Select shows the frame time (to check the speed on the Pi)
  if btnp(BSELECT) then G.debug = not G.debug end
  Scr.cur.update()
  Snd.update()
  if Scr.name == "fight" then
    local ms = stat(1)
    perf.n, perf.sum, perf.max = perf.n + 1, perf.sum + ms, max(perf.max, ms)
    if perf.n >= 600 then
      log(fmt("titan fight: %.1f ms avg, %.1f max, %d fps, %d effects", perf.sum / perf.n, perf.max, stat(2),
              Fx.count()))
      perf.n, perf.sum, perf.max = 0, 0, 0
    end
  end
end

function _draw()
  Scr.cur.draw()
  if G.debug then
    rectfill(W - 190, H - 20, 186, 16, 0x000000)
    print(fmt("%4.1fms %2dfps %3dfx", stat(1), stat(2), Fx.count()), W - 188, H - 20, 0x80FF80)
    -- the controls as the game reads them: any controller, then each pad,
    -- then each fighter (its pad, numpad direction, state, position)
    local function dirs(p)
      return (btn(BL, p) and "L" or ".") .. (btn(BR, p) and "R" or ".") ..
             (btn(BU, p) and "U" or ".") .. (btn(BD, p) and "D" or ".")
    end
    local sx, sy = stick()
    local n, mask = players()
    rectfill(4, H - 76, 300, 56, 0x000000)
    print(fmt("any %s  p1 %s  p2 %s  stick %+.1f %+.1f", dirs(), dirs(1), dirs(2), sx, sy), 6, H - 76, 0xFFE060)
    print(fmt("players %d (mask %d)", n, mask), 6, H - 62, 0xFFE060)
    for i, f in ipairs(G.fighters or {}) do
      print(fmt("F%d pad %s dir %d %s x %d", i, tostring(f.pad), f.inp.dir or 5, f.state, floor(f.x)),
            6, H - 62 + i * 14, 0xFFE060)
    end
  end
end

-- for the host tests (tests/titan/sim.lua)
TITAN = { G = G, Data = Data, Scr = Scr, Fighter = Fighter, Fx = Fx, Stage = Stage, Hud = Hud, Cpu = Cpu,
          Snd = Snd }
