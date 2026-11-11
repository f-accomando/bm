-- bm SNES: the user interface of the Super Nintendo emulator. The machine itself is in C (src/bm/snes_*.c); this
-- cartridge lists the ROMs of /sfc, loads one, passes the pads to it every frame, draws its picture, keeps the
-- battery RAM in /docs and has the menu. Original code.

local SW, SH = 256, 240
local AUTOPLAY = false        -- a build for the bridge (SNES_PLAY=1): opens the first ROM and plays by itself, with no test screen; the logger writes the speed
local AUTO = false            -- a test build opens the first ROM by itself (the bridge cannot press keys)

-- the SNES buttons as bits (snes.h): B Y Select Start Up Down Left Right A X L R
local BTNS = { { "b", 0x8000 }, { "y", 0x4000 }, { "select", 0x2000 }, { "start", 0x1000 }, { "up", 0x0800 },
               { "down", 0x0400 }, { "left", 0x0200 }, { "right", 0x0100 }, { "a", 0x0080 }, { "x", 0x0040 },
               { "l1", 0x0020 }, { "r1", 0x0010 } }
-- The SNES buttons sit as A right, B bottom, X top, Y left. On the RGB30 (and Nintendo pads) bm's names are the
-- same. On a keyboard or an Xbox pad bm's A is the bottom button and X the left one: swap them.
local SWAP = { a = "b", b = "a", x = "y", y = "x" }
local swap = false
local function pad_layout()
  local c = controller and controller(1)
  swap = not (c and c.layout == "nintendo")
end

local frames = 0

-- the test build plays by itself (the bridge cannot press keys): through the title and the file select, then to the right
local function auto_pad(f)
  if f < 300 then return 0 end
  local t = f - 300
  if t < 2400 then                                              -- taps of Start, A and B in turn: the title, the file, the messages
    if t % 40 < 6 then
      if t < 400 then return ({ 0x1000, 0x0080, 0x8000 })[(t // 40) % 3 + 1] end    -- Start only at the title (in a level it is the pause)
      return ({ 0x0080, 0x8000 })[(t // 40) % 2 + 1]
    end
    return 0
  end
  return 0x0100 | ((t % 60) < 25 and 0x8000 or 0) | 0x4000      -- right, running (Y), jumping (B) now and then
end

-- The keyboard is read with keydown (rawkeys: it is not a controller, so Esc is not Start). USB usages:
-- WASD and the arrows move, U I O P are Y X B A (the SNES layout: Y left, X top, B bottom, A right), Q and E are L and R, "," is Select, "." is Start.
-- Esc opens the emulator menu.
local KB = { { 0x1A, 0x0800 }, { 0x16, 0x0400 }, { 0x04, 0x0200 }, { 0x07, 0x0100 }, { 0x52, 0x0800 }, { 0x51, 0x0400 }, { 0x50, 0x0200 }, { 0x4F, 0x0100 },
             { 0x18, 0x4000 }, { 0x0C, 0x0040 }, { 0x12, 0x8000 }, { 0x13, 0x0080 }, { 0x14, 0x0020 }, { 0x08, 0x0010 }, { 0x36, 0x2000 }, { 0x37, 0x1000 } }
local kprev = {}
local function kedge(u)                        -- true once when the key goes down; kscan() runs at the end of every update
  return keydown(u) and not kprev[u]
end
local function kscan() for _, u in ipairs({ 0x1A, 0x16, 0x52, 0x51, 0x28, 0x2C, 0x29, 0x15 }) do kprev[u] = keydown(u) end end
local function nav_up() return btnp("up") or kedge(0x1A) or kedge(0x52) end
local function nav_down() return btnp("down") or kedge(0x16) or kedge(0x51) end
local function nav_ok() return btnp("a") or kedge(0x28) or kedge(0x2C) end

local function readpad(p)
  if (AUTO or AUTOPLAY) and p == 1 then return auto_pad(frames) end
  local v = 0
  if p == 1 then for i = 1, #KB do if keydown(KB[i][1]) then v = v | KB[i][2] end end end
  for i = 1, #BTNS do
    local e = BTNS[i]
    local name = swap and SWAP[e[1]] or e[1]
    if btn(name, p) then v = v | e[2] end
  end
  if v & 0x0300 == 0x0300 then v = v & ~0x0300 end          -- left and right together: none
  if v & 0x0C00 == 0x0C00 then v = v & ~0x0C00 end
  return v
end

-- Export the sprites the game shows now: each tile an OAM sprite uses, with the palette of that sprite, laid out as in the video RAM
-- (16 tiles in a row, the second name table below the first) into a 128 x 256 PNG in /docs; bm Pixel imports it (Sheet > Import a PNG).
local SPR_SIZES = { { 1, 2 }, { 1, 4 }, { 1, 8 }, { 2, 4 }, { 2, 8 }, { 4, 8 }, { 2, 4 }, { 2, 4 } }     -- tiles a side: small and large
local toast, export_n = nil, 0
function export_sprites()
  local d = snes_debug()
  local ob = tonumber(d.regs:match("obsel (%x+)"), 16) or 0
  local base = (ob & 7) << 14
  local gap = ((((ob >> 3) & 3) + 1) << 14)
  local sz = SPR_SIZES[((ob >> 5) & 7) + 1]
  local oam = d.oam
  for y = 0, 255 do for x = 0, 127 do sset(x, 256 + y, nil) end end
  local done, count = {}, 0
  for i = 0, 127 do
    local x, y, t, a = oam:byte(i * 4 + 1), oam:byte(i * 4 + 2), oam:byte(i * 4 + 3), oam:byte(i * 4 + 4)
    local hi = (oam:byte(513 + (i >> 2)) >> ((i & 3) * 2)) & 3
    local big = hi & 2 ~= 0
    local tw = big and sz[2] or sz[1]
    local x9 = x | ((hi & 1) << 8)
    if x9 >= 256 then x9 = x9 - 512 end
    if (y < 224 or y + tw * 8 > 256) and x9 > -tw * 8 and x9 < 256 then
      for ty = 0, tw - 1 do
        for tx = 0, tw - 1 do
          local n = ((((t >> 4) + ty) & 15) << 4) | ((t + tx) & 15)
          local table2 = a & 1
          local key = table2 * 256 + n
          if not done[key] then
            done[key] = true
            count = count + 1
            local addr = base + table2 * gap + n * 32
            snes_tiles(addr, 4, 1, 1, (n & 15) * 8, 256 + (table2 * 16 + (n >> 4)) * 8, 128, (a >> 1) & 7)
          end
        end
      end
    end
  end
  if count == 0 then toast = "no sprite on the screen"; return end
  export_n = export_n + 1
  local name = string.format("spr%d.png", export_n)
  local bytes, why = sheet_png(0, 256, 128, 256)
  if not bytes then toast = "export failed: " .. tostring(why); return end
  local ok, err = doc_write(name, bytes)
  toast = ok and (count .. " tile(s) in /docs/" .. name) or ("export failed: " .. tostring(err))
end

local state = "browse"           -- browse, run, menu
local files, sel, msg = {}, 1, nil
local rom_name = "-"                         -- the ROM that plays (the logger writes it in each line)
local top = 1                                -- the first ROM of the list that shows (the list scrolls with the selector)
local function rom_rows() return math.max(1, (SCREEN_H - 40 - 90) // 18 + 1) end
local function rom_scroll()
  local rows = rom_rows()
  if sel < top then top = sel end
  if sel > top + rows - 1 then top = sel - rows + 1 end
  top = math.max(1, math.min(top, math.max(1, #files - rows + 1)))
end
local scr
local holdmenu, menusel = 0, 1
local esc_prev = false                        -- Esc was down at the last update (the menu opens and closes on the press)
local show_fps = false
local draw_ms = { 0, 0 }
local acc = { n = 0, f = 0, p = 0, s = 0, n2 = 0, tf = 0, bg = 0, ob = 0, wi = 0, co = 0, mi = 0 }
local snap                                         -- the test build's kept lines

-- How often a picture is made: every `every` emulated frames (1 = 60 pictures a second). The machine runs 60 frames a second whatever the picture costs
-- (frameskip), so the cart makes more pictures when the console keeps up and fewer when it does not. A window of 90 updates is timed, and the draws
-- and the cost of an update and a draw are summed in it. Pi Zero W, 2026-11-11 (log of a play): at 1/1 the updates stayed at 60 a second but the loop
-- fell to 30 draws a second with gaps of 56 ms; at 1/2 it held 58 draws a second. So the loop counts as much as the updates:
-- - overloaded: under 57 updates a second, or under 50 draws a second. A picture less often, and no step back for 15 seconds (60 seconds
--   when the step back to 1/1 or to a level that just failed was the cause: `hold`);
-- - three good windows in a row (58.8 updates and 55 draws a second or more), and a step back is allowed only when the cost measured now (update
--   plus draw, ms a frame) leaves room for the finer level: under 11 ms for 1/1, under 13 ms for the others.
local every, win_n, win_t, win_last, cooldown, good = 3, 0, 0, nil, 0, 0
local win_dr, win_u, win_d, stepped = 0, 0, 0, false     -- draws, update ms and draw ms in the window; the last move was a step to a finer level
local MINI, last_ups, last_dps = false, 0, 0              -- a test build with a one-line speed readout
local function adapt()
  local t = time()
  if win_last then win_t = win_t + (t - win_last); win_n = win_n + 1 end
  win_last = t
  if cooldown > 0 then cooldown = cooldown - 1 end
  if win_n >= 90 then
    local ups = win_n / win_t                          -- _update a second
    local dps = win_dr / win_t                         -- draws a second (the loop)
    local cost = (win_u + win_d) / win_n               -- ms of an update plus a draw
    last_ups, last_dps = ups, dps
    if ups < 57 or dps < 50 then
      good = 0
      if every < 4 then every = every + 1; cooldown = stepped and 3600 or 900 end
      stepped = false
    elseif ups >= 58.8 and dps >= 55 then
      good = good + 1
      stepped = false                                  -- the last step held for a window: it was not the cause
      if good >= 3 and every > 1 and cooldown == 0 and cost < (every == 2 and 11 or 13) then every = every - 1; good = 0; stepped = true
      elseif good >= 3 then good = 3 end
    else
      good = 0; stepped = false
    end
    win_n, win_t, win_dr, win_u, win_d = 0, 0, 0, 0, 0
  end
end
local srm = ""

local function list_roms()
  files = {}
  msg = nil
  local l, why = doc_list("sfc")
  if not l then msg = why or "the player did not allow the files"; return end
  for _, d in ipairs(l) do
    local ext = d.name:match("%.([^.]+)$")
    ext = ext and ext:lower()
    if ext == "sfc" or ext == "smc" or ext == "swc" or ext == "fig" then files[#files + 1] = d end
  end
  table.sort(files, function(a, b) return a.name < b.name end)
  if sel > #files then sel = math.max(1, #files) end
end

local function save_sram()
  local s = snes_sram()
  if s and srm ~= "" then doc_write(srm, s) end
end

local function open_rom(d)
  local data, why = doc_read(d.name, "sfc")
  if not data then msg = why or "cannot read the file"; return end
  local ok, err = snes_load(data)
  if not ok then msg = err; return end
  local info = snes_info()
  local base = d.name:gsub("%.[^.]*$", ""):gsub("[^%w_%-]", ""):sub(1, 8)
  if base == "" then base = "rom" end
  srm = info.sram > 0 and (base .. ".srm") or ""
  if srm ~= "" then
    local s = doc_read(srm)
    if s then snes_sram(s) end
  end
  msg = nil
  rom_name = (d.name:gsub("[^%w%._%-]", "")):sub(1, 24)
  log("snes rom " .. rom_name)
  state = "run"
  frames = 0
end

local dx, dy, dw, dh = 0, 0, SW, SH            -- where and how big the picture is on the screen
-- "native": the screen is 256x256 (the console centres it in a 480x270 screen: 4x on a 1080p TV); "sharp": a whole-number scale (1x on a 360-line screen), all pixels the same size; "fit": 4:3 on the whole screen; "gpu2" and "gpu3": the screen is 960x540 (a 1080p TV
-- shows it at whole pixels) or 1280x720 (a 720p TV), and the GPU draws the picture 2 or 3 times bigger (snes_to_sheet and sspr)
local mode = "sharp"
local START_MODE = "native"                      -- the default scale; a test build starts in another mode
local cur_lines = 224                          -- lines of the picture now: 224, or 239 (PAL and overscan)

-- Where the picture goes for `lines` lines. A new size makes a new render target: the machine fills it whole.
local gmesh
local dbg, dbg_n = nil, 0                    -- the last debug table and a counter of draws
-- the menu is 228 pixels high: centred, but it never goes below the screen (the native screen is 256 high)
function menu_top() return math.max(4, math.min(SCREEN_H // 2 - 70, SCREEN_H - 232)) end
-- The logger: one line a second in the kernel's log (`log`), no text on the screen. Read it with `pi.py line log` or the monitor command `log`.
-- Fields: t seconds; upd = emulated frames; drw = pictures drawn; fps = the console's loop; pic = picture every N frames;
-- umax/uavg = the longest and the mean update (ms); dmax/davg = the same for the draw; gap = the longest time between two draws (ms);
-- late = draws more than 40 ms after the last; lua = stat(1); then the machine's own times (us frame, ppu, spc; the PPU parts).
local log_on = true                                -- the menu item "Log" switches it
local lg = { t0 = nil, f0 = 0, drw = 0, umax = 0, dmax = 0, gap = 0, late = 0, last = nil, usum = 0, dsum = 0 }
local function log_reset(now) lg.t0 = now; lg.f0 = frames; lg.drw = 0; lg.umax = 0; lg.dmax = 0; lg.gap = 0; lg.late = 0; lg.usum = 0; lg.dsum = 0 end
function log_draw(dm)
  local now = time()
  if not lg.t0 then log_reset(now) end
  if lg.last then
    local g = (now - lg.last) * 1000
    if g > lg.gap then lg.gap = g end
    if g > 40 then lg.late = lg.late + 1 end
  end
  lg.last = now
  lg.drw = lg.drw + 1; lg.dsum = lg.dsum + dm
  if dm > lg.dmax then lg.dmax = dm end
  if now - lg.t0 >= 1 then
    local n = math.max(1, frames - lg.f0)
    local d2 = dbg
    log(string.format("snes t=%.1f upd=%d drw=%d fps=%d pic=1/%d umax=%.1f uavg=%.1f dmax=%.1f davg=%.1f gap=%.0f late=%d lua=%.0f mode=%s rom=%s | %s | %s",
      now, frames - lg.f0, lg.drw, stat(2), every, lg.umax, lg.usum / n, lg.dmax, lg.dsum / math.max(1, lg.drw), lg.gap, lg.late, stat(1), mode, rom_name,
      d2 and d2.us or "-", d2 and d2.us_ppu or "-"))
    log_reset(now)
  end
end
local GPU_EMPTY = false                    -- a test: no picture, to time the empty 720p frame
local pic_new = true                       -- the machine made a picture since the last snes_to_sheet
local GPU3D = true                         -- call zclear() so that the GPU draws the 2D (needed at 1280x720)
local gpu_pic, gpu_zoom = false, 3             -- the picture goes by the sprite sheet and sspr (no render target)
local last_sw, last_sh = 0, 0
local function layout(lines)
  local w, h
  last_sw, last_sh = SCREEN_W, SCREEN_H
  if (mode == "gpu2" or mode == "gpu3") and SCREEN_H >= 448 then
    gpu_zoom = math.max(1, math.floor(math.min(SCREEN_H / lines, SCREEN_W / 256)))
    gpu_pic = true
    dw, dh = 256 * gpu_zoom, lines * gpu_zoom
    dx, dy = (SCREEN_W - dw) // 2, (SCREEN_H - dh) // 2
    return
  end
  gpu_pic = false
  if mode == "fit" then
    w, h = (SCREEN_H * 4) // 3, SCREEN_H
    if w > SCREEN_W then w = SCREEN_W; h = (SCREEN_W * 3) // 4 end
  else
    local s = math.max(1, math.floor(math.min(SCREEN_H / lines, SCREEN_W / 256)))   -- a whole number: every pixel the same size
    w, h = 256 * s, lines * s
  end
  if w ~= dw or h ~= dh or not scr then
    dw, dh = w, h
    scr = target_create(dw, dh)             -- the machine scales its picture into it: the kernel's own scaling is slow
  end
  dx, dy = (SCREEN_W - dw) // 2, (SCREEN_H - dh) // 2
end

local MODE_LABEL = { sharp = "Scale: sharp", native = "Scale: native 256x256", gpu2 = "Scale: GPU 2x (540p)", gpu3 = "Scale: GPU 3x (720p)", fit = "Scale: fit 4:3" }
function mode_label() return MODE_LABEL[mode] end

-- Changes the scale mode. The GPU modes ask for a bigger screen (screen() takes effect at the next frame); a console that cannot give it keeps the mode it had.
function set_mode(m)
  local ok = true
  if m == "native" then ok = screen(256, 256)   -- the console puts it in the middle of a 480x270 screen: 4x at whole pixels on a 1080p TV
  elseif m == "gpu2" then ok = screen(960, 540)
  elseif m == "gpu3" then ok = screen(1280, 720)
  elseif screen and SCREEN_W ~= 640 and (mode == "gpu2" or mode == "gpu3" or mode == "native") then screen(640, 360) end
  if not ok then return false end
  mode = m
  layout(cur_lines)
  return true
end

function next_mode()
  local order = { "sharp", "native", "gpu2", "gpu3", "fit" }
  local i = 1
  for k = 1, #order do if order[k] == mode then i = k end end
  for step = 1, #order do
    local m = order[(i + step - 1) % #order + 1]
    if set_mode(m) then return end
  end
end

function _init()
  layout(cur_lines)
  if START_MODE ~= mode then set_mode(START_MODE) end
  pad_layout()
  frameskip(4)
  if rawkeys then rawkeys(true) end
  list_roms()
  if (AUTO or AUTOPLAY) and files[1] then open_rom(files[1]) end
end

function _exit()
  save_sram()
  return true
end

function _update()
  if state == "browse" then
    if nav_up() then sel = math.max(1, sel - 1) end
    if nav_down() then sel = math.min(#files, sel + 1) end
    rom_scroll()
    if btnp("x") or kedge(0x15) then list_roms() end
    if nav_ok() and files[sel] then open_rom(files[sel]) end
  elseif state == "run" then
    -- the emulator menu: Esc on a keyboard, or L2 and R2 held for half a second on a pad (Q and E are L and R of the game)
    local esc_edge = kedge(0x29)
    if btn("l2") and btn("r2") then holdmenu = holdmenu + 1 else holdmenu = 0 end
    if holdmenu > 30 or esc_edge then state = "menu"; menusel = 1; holdmenu = 0; toast = nil; save_sram(); kscan(); return end
    local bench = AUTO and frames >= 600 and frames < 3200      -- the test build: every frame in this range is drawn, and the time of the parts is summed: the same frames in every run
    if not (AUTO and frames >= 3200) then                                -- the test build stops at frame 3200: the same picture in every run, to compare two kernels
      local tf = time()
      if not AUTO then adapt() end
      local render = bench or frames % (AUTO and 4 or every) == 0
      snes_frame(readpad(1), readpad(2), render)      -- a picture every `every` frames (the test build: every fourth)
      local u = (time() - tf) * 1000
      win_u = win_u + u
      if log_on then lg.usum = lg.usum + u; if u > lg.umax then lg.umax = u end end
      if render then pic_new = true end
      if bench then acc.tf = acc.tf + (time() - tf) end
    end
    if bench then
      local bg, ob, wi, co, mi = string.match(snes_debug().us_ppu, "bg (%d+) obj (%d+) win (%d+) comp (%d+) mix (%d+)")
      if bg then acc.n2 = acc.n2 + 1; acc.bg = acc.bg + bg; acc.ob = acc.ob + ob; acc.wi = acc.wi + wi; acc.co = acc.co + co; acc.mi = acc.mi + mi end
    end
    frames = frames + 1
    if frames % 120 == 0 then pad_layout() end
    if frames % 600 == 0 then save_sram() end
  elseif state == "menu" then
    if nav_up() then menusel = math.max(1, menusel - 1) end
    if nav_down() then menusel = math.min(7, menusel + 1) end
    if nav_ok() then
      if menusel == 1 then state = "run"
      elseif menusel == 2 then save_sram(); snes_reset(); state = "run"
      elseif menusel == 3 then show_fps = not show_fps
      elseif menusel == 4 then next_mode()
      elseif menusel == 5 then export_sprites()
      elseif menusel == 6 then log_on = not log_on; log("snes log " .. (log_on and "on" or "off"))
      else save_sram(); state = "browse"; list_roms() end
    end
    if btnp("b") or kedge(0x29) then state = "run"; holdmenu = 0 end
  end
  kscan()
end

function _draw()
  if state == "browse" then
    cls(0x101820)
    print("bm SNES", 16, 12, 0xFFD050, 2)
    print("Put .sfc or .smc files in the /sfc folder of the SD card.", 16, 56, 0x8090A0)
    if #files == 0 then print("No ROM found.", 16, 100, 0xFFFFFF) end
    local y = 90
    rom_scroll()
    local last = math.min(#files, top + rom_rows() - 1)
    for i = top, last do
      local f = files[i]
      local c = (i == sel) and 0xFFFFFF or 0x90A0B0
      if i == sel then rectfill(10, y - 1, SCREEN_W - 20, 17, 0x304860) end
      print(f.name:sub(1, 48), 16, y, c)
      print(string.format("%d KB", f.size // 1024), SCREEN_W - 120, y, c)
      y = y + 18
    end
    if top > 1 then print("^", SCREEN_W - 20, 72, 0x8090A0) end
    if last < #files then print("v " .. (#files - last) .. " more", SCREEN_W - 100, SCREEN_H - 40, 0x8090A0) end
    if msg then print(msg, 16, SCREEN_H - 56, 0xFF6060) end
    print("Enter or A play   R or X refresh   in game: Esc (keyboard) or hold L2+R2 (pad) for the menu", 16, SCREEN_H - 24, 0x8090A0)
    return
  end
  if SCREEN_W ~= last_sw or SCREEN_H ~= last_sh then layout(cur_lines) end   -- screen() took effect
  if gpu_pic then
    local lines = cur_lines
    if pic_new then                            -- the sheet is made again only for a new picture (the GPU keeps its texture)
      lines = snes_to_sheet(0, 0)
      pic_new = false
    end
    if lines ~= cur_lines then cur_lines = lines; layout(lines) end
    if GPU3D then                              -- a 3D call opens the GPU's job (a tiny triangle, far away): the 2D that follows is drawn by the GPU, in the job
      gmesh = gmesh or mesh({ -1, -1, 0, 1, -1, 0, 0, 1, 0 }, { 1, 2, 3, -1 }, { 0, 0, 1, 0, 0, 1 })
      zclear()
      camera3d(0, 0, -2000)
      light3d(0, 0, -1, 1)
      draw3d(gmesh, 0, 0, 0)
    end
    rectfill(0, 0, SCREEN_W, SCREEN_H, 0)
    if not GPU_EMPTY then sspr(0, 0, 256, lines, dx, dy, false, false, gpu_zoom) end
    if state == "run" then win_dr = win_dr + 1 end
    if log_on and state == "run" then log_draw(0) end
    if MINI then print(string.format("%.0f fps  %.1f upd/s  pic 1/%d  x%d  gpu %.0f  frame %.0f ms  job %.0f ms  wait %.0f ms", stat(2), last_ups, every, gpu_zoom, stat(9), stat(1), stat(16), stat(18)), 4, 4, 0x00FF00) end
    if state == "menu" then
      rectfill(SCREEN_W // 2 - 110, menu_top(), 220, 228, 0x101820)
      local items = { "Resume", "Reset", show_fps and "FPS: on" or "FPS: off", mode_label(), "Export sprites to /docs", log_on and "Log: on" or "Log: off", "Back to the list" }
      for i = 1, 7 do print(items[i], SCREEN_W // 2 - 90, menu_top() + 12 + (i - 1) * 24, i == menusel and 0xFFFFFF or 0x8090A0) end
      if toast then print(toast, SCREEN_W // 2 - 104, menu_top() + 200, 0xFFD050) end
    end
    return
  end
  local t0 = time()
  local lines = snes_draw(scr, dw, dh)
  local t1 = time()
  if lines ~= cur_lines then cur_lines = lines; layout(lines) end   -- the next picture has the new size
  if dx > 0 then                                   -- the screen has two buffers: the margins are cleared every frame
    rectfill(0, 0, dx, SCREEN_H, 0)
    rectfill(dx + dw, 0, SCREEN_W - dx - dw, SCREEN_H, 0)
  end
  if dy > 0 then
    rectfill(0, 0, SCREEN_W, dy, 0)
    rectfill(0, dy + dh, SCREEN_W, SCREEN_H - dy - dh, 0)
  end
  scr:draw(dx, dy)
  draw_ms = { (t1 - t0) * 1000, (time() - t1) * 1000 }
  if state == "run" then win_dr = win_dr + 1; win_d = win_d + draw_ms[1] + draw_ms[2] end
  if state ~= "run" then lg.last = nil; lg.t0 = nil elseif log_on then log_draw(draw_ms[1] + draw_ms[2]) end
  -- the machine's debug table is big (strings, the sprite table): the test build takes it at every picture, the cart twice a second, for the message of a screen that stays off
  dbg_n = dbg_n + 1
  if AUTO or dbg_n % 30 == 0 then dbg = snes_debug(AUTO and 0xCFC0 or nil) end
  local d = dbg
  if d and d.inidisp & 0x80 ~= 0 and frames > 180 and not AUTO and show_fps then      -- a game that keeps the screen off: say why (only with "FPS: on")
    local io = {}
    for i = 1, 6 do io[i] = string.format("%04X", d.io[i]) end
    print(string.format("screen off: PC %06X%s%s  nmi %02X  sound %s %04X", d.pc, d.wai and " WAI" or "",
      d.stp and " STP" or "", d.nmi, d.spc, d.spc_pc), 4, SCREEN_H - 32, 0xFF8040)
    print("reads: " .. table.concat(io, " ") .. "  " .. snes_info().mapping .. " " .. snes_info().title, 4,
      SCREEN_H - 16, 0xFF8040)
    print("cpu " .. d.cpu_code .. "  spc " .. d.spc_code, 4, SCREEN_H - 48, 0xFF8040)
    print("ports " .. d.ports, 4, SCREEN_H - 64, 0xFF8040)
  end
  if AUTO then                                           -- the test build: the mean cost of the parts over the same game frames (600 to 3200), to compare two kernels
    local f, p, sp = string.match(d.us, "us frame (%d+) ppu (%d+) spc (%d+)")
    if f and d.frames >= 600 and d.frames < 3300 then
      acc.n = acc.n + 1; acc.f = acc.f + f; acc.p = acc.p + p; acc.s = acc.s + sp
    end
    if acc.n > 0 then print(string.format("mean of %d draws, frames 600-%d: frame %d ppu %d spc %d us", acc.n, math.min(d.frames, 3300), acc.f // acc.n, acc.p // acc.n, acc.s // acc.n), 4, 90, 0xFFFF00) end
  end
  if AUTO and show_fps then                              -- the test build: the PPU registers, the first sprites, the other sprites, a tile and a palette; the overworld's values are kept
    local o, first, rest = d.oam, {}, {}
    for i = 0, 127 do
      local b = i * 4
      local y = o:byte(b + 2)
      if y < 224 then
        local hi = (o:byte(513 + (i >> 2)) >> ((i & 3) * 2)) & 3
        local t = string.format("%d:%d,%d t%02X a%02X s%d", i, o:byte(b + 1) + (hi & 1) * 256, y, o:byte(b + 3), o:byte(b + 4), hi >> 1)
        if #first < 4 then first[#first + 1] = t end
        if o:byte(b + 3) ~= 0x7E and #rest < 7 then rest[#rest + 1] = t end
      end
    end
    local hx, cg = {}, {}
    for i = 1, 16 do hx[i] = string.format("%02X", d.vram:byte(i)) end
    for i = 1, 8 do cg[i] = string.format("%02X%02X", d.cgram:byte(i * 2), d.cgram:byte(i * 2 - 1)) end
    local lines = { d.regs, "first: " .. table.concat(first, "  "), (acc.n2 > 0) and string.format("%d fr %.2f ms: bg %d obj %d win %d comp %d mix %d = %d us", acc.n2, acc.tf * 1000 / acc.n2, acc.bg // acc.n2, acc.ob // acc.n2, acc.wi // acc.n2, acc.co // acc.n2, acc.mi // acc.n2, (acc.bg + acc.ob + acc.wi + acc.co + acc.mi) // acc.n2) or ("other: " .. table.concat(rest, "  ")), "t7E " .. table.concat(hx, "", 1, 8) .. " vm " .. d.vmap:sub(193, 256) }
    if not snap and d.regs:find("hofs 1003") and #first > 0 and d.inidisp == 0x0F then snap = lines end       -- the overworld: keep it
    local out = snap or lines
    for i = 1, #out do print(out[i], 4, 282 + i * 16, snap and 0xFFC080 or 0x80FF80) end
  end
  if AUTO and show_fps then
    rectfill(0, 0, 400, 78, 0)                          -- a black bar under the lines, to read them
    print(string.format("%d fps  pic 1/%d  blank %02X  tm %02X  sound %s %04X", stat(2), every, d.inidisp, d.tm, d.spc, d.spc_pc), 4, 4, 0x00FF00)
    print(d.us_ppu, 4, 36, 0x00FF00)
    print(d.audio, 4, 52, 0x00FF00)
    print(string.format("PC %06X%s%s  nmi %02X  reads %04X %04X %04X  frames %d", d.pc, d.wai and " WAI" or "", d.stp and " STP" or "", d.nmi, d.io[1], d.io[3], d.io[6], d.frames), 4, 68, 0x00FF00)
    print(d.us .. string.format("  copy %.1f blit %.1f ms  lua %.0f ms", draw_ms[1], draw_ms[2], stat(1)), 4, 20, 0x00FF00)
  end
  if MINI or (show_fps and not AUTO) then print(string.format("%d fps  %.1f upd/s  pic 1/%d", stat(2), last_ups, every), 4, 4, 0x00FF00) end   -- one line: the loop, the machine (updates a second), the picture rate
  if state == "menu" then
    rectfill(SCREEN_W // 2 - 110, menu_top(), 220, 228, 0x101820)
    rect(SCREEN_W // 2 - 110, menu_top(), 220, 228, 0xFFD050)
    local items = { "Resume", "Reset", show_fps and "FPS: on" or "FPS: off",
                    mode_label(), "Export sprites to /docs", log_on and "Log: on" or "Log: off", "Back to the list" }
    for i = 1, 7 do
      print(items[i], SCREEN_W // 2 - 90, menu_top() + 12 + (i - 1) * 24, i == menusel and 0xFFFFFF or 0x8090A0)
    end
    print(snes_info().title, SCREEN_W // 2 - 90, menu_top() + 184, 0x8090A0)
    if toast then print(toast, SCREEN_W // 2 - 104, menu_top() + 200, 0xFFD050) end
  end
end
