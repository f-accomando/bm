-- bm PSX: the user interface of the PlayStation 1 emulator. The machine itself is in C (src/bm/psx_*.c); this
-- cartridge lists the games of /psx, starts one (the C side reads the BIOS and the disc from the card), passes the
-- pads to it every frame, draws its picture and has the menu. Original code.
--
-- /psx on the SD card:   the BIOS (a 512 KB file, like scph1001.bin) and the games: .cue + .bin, .iso, .bin, .exe

local AUTO = false            -- a test build opens the first game by itself (the bridge cannot press keys)
local show_fps = false
local jit_on = false          -- the dynamic recompiler (the Pi): off until the self test has passed (at the start)
local selftest_msg = ""

-- PlayStation buttons as bits (psx.h). The bm names follow the Nintendo layout on the RGB30: a right, b bottom,
-- x top, y left. Cross is the bottom button, Circle right, Triangle top, Square left.
local PAD = { select = 0x0001, start = 0x0008, up = 0x0010, right = 0x0020, down = 0x0040, left = 0x0080,
              l2 = 0x0100, r2 = 0x0200, l1 = 0x0400, r1 = 0x0800 }
local FACE_NINTENDO = { b = 0x4000, a = 0x2000, x = 0x1000, y = 0x8000 }       -- cross, circle, triangle, square
local FACE_OTHER    = { a = 0x4000, b = 0x2000, y = 0x1000, x = 0x8000 }       -- keyboard and Xbox pads: a is the bottom one
local face = FACE_OTHER
local function pad_layout()
  local c = controller and controller(1)
  face = (c and c.layout == "nintendo") and FACE_NINTENDO or FACE_OTHER
end

local frames = 0
local function auto_pad(f)                                    -- the test build presses Cross now and then to pass the screens
  if f > 600 and f % 90 < 8 then return 0x4000 end
  if f > 600 and f % 90 > 40 and f % 90 < 48 then return 0x0008 end
  return 0
end

local function readpad(p)
  if AUTO and p == 1 then return auto_pad(frames) end
  local v = 0
  for name, bit in pairs(PAD) do if btn(name, p) then v = v | bit end end
  for name, bit in pairs(face) do if btn(name, p) then v = v | bit end end
  if v & 0x0030 == 0x0030 then v = v & ~0x0030 end            -- left and right together: none
  if v & 0x0050 == 0x0050 then v = v & ~0x0050 end            -- up and down together: none
  return v
end

local state = "browse"           -- browse, run, menu
local games, sel, msg = {}, 1, nil
local have_bios = false
local scr
local hold, menusel = 0, 1
local draw_ms = { 0, 0 }
local dx, dy, dw, dh = 0, 0, 320, 240

local function list_games()
  games = {}
  msg = nil
  have_bios = false
  local l, why = psx_list()
  if not l then msg = why or "the player did not allow the files"; return end
  for _, d in ipairs(l) do
    if d.kind == "bios" then have_bios = true
    elseif d.kind == "exe" or d.kind == "cue" or d.kind == "disc" then games[#games + 1] = d end
  end
  -- a .bin that has a .cue next to it is listed once (by its .cue)
  local cues = {}
  for _, d in ipairs(games) do if d.kind == "cue" then cues[d.name:gsub("%.[^.]*$", ""):lower()] = true end end
  local out = {}
  for _, d in ipairs(games) do
    local base = d.name:gsub("%.[^.]*$", ""):lower()
    if d.kind ~= "disc" or not cues[base] then out[#out + 1] = d end
  end
  games = out
  table.sort(games, function(a, b) return a.name:lower() < b.name:lower() end)
  if sel > #games then sel = math.max(1, #games) end
end

local function start_game(d)
  local ok, why = psx_boot(d.name)
  if not ok then msg = why or "cannot start the game"; return end
  msg = nil
  state = "run"
  frames = 0
end

function _init()
  dh = SCREEN_H
  dw = (dh * 4) // 3
  if dw > SCREEN_W then dw = SCREEN_W; dh = (dw * 3) // 4 end
  dx, dy = (SCREEN_W - dw) // 2, (SCREEN_H - dh) // 2
  scr = target_create(dw, dh)            -- the machine scales its picture into it
  pad_layout()
  frameskip(2)
  list_games()
  do                                                     -- the recompiler is checked first (random programs run both ways) and used only if it agrees
    local _, avail = psx_jit()
    if avail then
      local bad, msg = psx_selftest(200)
      selftest_msg = string.format("selftest: %d of 200 differ %s", bad, msg or "")
      if bad == 0 then jit_on = true end
    else
      selftest_msg = "no recompiler on this device"
    end
  end
  psx_jit(jit_on)
  if AUTO and games[1] then start_game(games[1]) end
end

function _update()
  if state == "browse" then
    if btnp("up") then sel = math.max(1, sel - 1) end
    if btnp("down") then sel = math.min(#games, sel + 1) end
    if btnp("x") then list_games() end
    if btnp("y") then                                     -- the self test of the recompiler: random programs, compiled and interpreted
      local bad, msg = psx_selftest(300)
      selftest_msg = string.format("selftest: %d of 300 differ %s", bad, msg or "")
      if bad == 0 then jit_on = psx_jit(true) end
    end
    if btnp("a") and games[sel] then start_game(games[sel]) end
  elseif state == "run" then
    if btn("select") and btn("start") then hold = hold + 1 else hold = 0 end
    if hold > 45 then state = "menu"; menusel = 1; hold = 0; return end
    psx_frame(readpad(1), readpad(2), frames % 2 == 0)
    frames = frames + 1
    if frames % 120 == 0 then pad_layout() end
  elseif state == "menu" then
    if btnp("up") then menusel = math.max(1, menusel - 1) end
    if btnp("down") then menusel = math.min(5, menusel + 1) end
    if btnp("a") then
      if menusel == 1 then state = "run"
      elseif menusel == 2 then psx_reset(); state = "run"
      elseif menusel == 3 then show_fps = not show_fps
      elseif menusel == 4 then jit_on = psx_jit(not jit_on)
      else state = "browse"; list_games() end
    end
    if btnp("b") then state = "run" end
  end
end

function _draw()
  if state == "browse" then
    cls(0x101828)
    print("bm PSX", 16, 12, 0xFFD050, 2)
    print("Put the BIOS (scph1001.bin) and the games in the /psx folder of the SD card.", 16, 56, 0x8090A0)
    print(have_bios and "BIOS: found" or "BIOS: not found (a PS-EXE still runs without it)", 16, 72, have_bios and 0x60C060 or 0xFF8040)
    if #games == 0 then print("No game found (.cue .bin .iso .exe).", 16, 108, 0xFFFFFF) end
    local y = 100
    for i = 1, #games do
      local g = games[i]
      local c = (i == sel) and 0xFFFFFF or 0x90A0B0
      if i == sel then rectfill(10, y - 1, SCREEN_W - 20, 17, 0x304860) end
      print(g.name:sub(1, 44), 16, y, c)
      print(string.format("%d MB", g.size // 1048576), SCREEN_W - 120, y, c)
      y = y + 18
      if y > SCREEN_H - 50 then break end
    end
    if msg then print(msg:sub(1, 70), 16, SCREEN_H - 40, 0xFF6060) end
    if selftest_msg ~= "" then print(selftest_msg:sub(1, 90), 16, SCREEN_H - 76, 0xFFFF80); print(selftest_msg:sub(91, 190), 16, SCREEN_H - 60, 0xFFFF80) end
    print("A play   X refresh   Y recompiler test   in game: hold Select+Start for the menu", 16, SCREEN_H - 20, 0x8090A0)
    return
  end
  local t0 = time()
  psx_draw(scr, dw, dh)
  local t1 = time()
  if dx > 0 then
    rectfill(0, 0, dx, SCREEN_H, 0)
    rectfill(dx + dw, 0, SCREEN_W - dx - dw, SCREEN_H, 0)
  end
  if dy > 0 then
    rectfill(0, 0, SCREEN_W, dy, 0)
    rectfill(0, dy + dh, SCREEN_W, SCREEN_H - dy - dh, 0)
  end
  scr:draw(dx, dy)
  draw_ms = { (t1 - t0) * 1000, (time() - t1) * 1000 }
  if show_fps then
    local d = psx_debug()
    rectfill(0, 0, 640, 148, 0)
    print(string.format("%d fps  frame %.1f ms (cpu+gpu %.1f, gpu draw %.1f)  gpu cmds %d  dma %d", stat(2), d.us_frame / 1000, d.us_cpu / 1000, d.us_gpu / 1000, d.gpu_cmds, d.dma_runs), 4, 4, 0x00FF00)
    print(string.format("PC %08X  RA %08X  istat %03X imask %03X  unhandled %d (%08X)", d.pc, d.ra, d.istat, d.imask, d.unhandled, d.last_unhandled), 4, 20, 0x00FF00)
    print(string.format("CD cmds %d last %02X sectors %d fail %d  irq flag %d en %02X  queued %d", d.cd_cmds, d.cd_last_cmd, d.cd_sectors, d.cd_read_fail, d.cd_irq_flag, d.cd_irq_en, d.cd_events), 4, 36, 0x00FF00)
    print(string.format("copy %.1f blit %.1f ms  lua %.0f ms", draw_ms[1], draw_ms[2], stat(1)), 4, 52, 0x00FF00)
    print(d.audio, 4, 68, 0x00FF00)
    print((jit_on and "JIT on  " or "JIT off  ") .. selftest_msg:sub(1, 70), 4, 100, 0xFFFF80)
    print(string.format("a frame: blocks %d  jit instr %d  interp %d  exec %d  rslow %d  wslow %d", d.st_blocks, d.st_jit_instr, d.st_interp, d.st_exec, d.st_rslow, d.st_wslow), 4, 116, 0xFFFF80)
    print(d.stat_text, 4, 132, 0xFFFF80)
    print(string.format("lba %d loc %d  %s", d.cd_lba, d.cd_loc, d.cd_trace), 4, 84, 0x00FF00)
  end
  local tty = psx_tty()
  if #tty > 0 and frames < 600 then print(tty:sub(-60), 4, SCREEN_H - 16, 0xFFFF80) end
  if state == "menu" then
    rectfill(SCREEN_W // 2 - 110, SCREEN_H // 2 - 80, 220, 160, 0x101828)
    rect(SCREEN_W // 2 - 110, SCREEN_H // 2 - 80, 220, 160, 0xFFD050)
    local items = { "Resume", "Reset", show_fps and "FPS: on" or "FPS: off", jit_on and "JIT: on" or "JIT: off", "Back to the list" }
    for i = 1, 5 do
      print(items[i], SCREEN_W // 2 - 90, SCREEN_H // 2 - 68 + (i - 1) * 24, i == menusel and 0xFFFFFF or 0x8090A0)
    end
    print(psx_info().game:sub(1, 26), SCREEN_W // 2 - 90, SCREEN_H // 2 + 56, 0x8090A0)
  end
end
