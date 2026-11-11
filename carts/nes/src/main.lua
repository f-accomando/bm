-- bm NES: the user interface of the Famicom / NES emulator. The machine itself is in C (src/bm/nes.c); this
-- cartridge lists the ROMs of /nes, loads one, passes the pads to it every frame, draws its picture, keeps the
-- battery RAM in /docs and has the menu. Original code.

local SW, SH = 256, 240
local AUTOPLAY = false        -- a build for the bridge (NES_PLAY=1): opens the first ROM and taps Start now and then; the logger writes the speed

-- the NES buttons as bits (nes.h): A B Select Start Up Down Left Right
local PAD_A, PAD_B, PAD_SELECT, PAD_START, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT = 1, 2, 4, 8, 16, 32, 64, 128
-- The NES has two buttons: B on the left, A on the right. On a pad the bottom and the right buttons are A, the left and the top
-- buttons are B (both work, so any hand fits). bm's names: on the RGB30 (and Nintendo pads) bottom is "b", right "a", left "y", top "x";
-- on a keyboard or an Xbox pad bottom is "a", right "b", left "x", top "y".
-- The keyboard (read with keydown) plays player 1, or player 2 when a pad is player 1: one person on the keyboard and one on a pad can play together.
local nintendo = false
local kbp = 1                 -- the player the keyboard keys play
local function pad_layout()
  local c = controller and controller(1)
  nintendo = c and c.layout == "nintendo"
  kbp = (c and c.kind ~= "keyboard" and c.kind ~= "none") and 2 or 1
end

local frames = 0
local esc_grace = 0           -- ignores kernel's Esc->Start fallback while Esc opens/closes the .bm menu

-- the bridge build plays by itself: Start now and then, to pass a title screen
local function auto_pad(f)
  if f < 120 then return 0 end
  local t = f - 120
  if t % 90 < 6 then return PAD_START end
  if t % 90 > 60 then return PAD_RIGHT | ((t % 30) < 15 and PAD_A or 0) end
  return 0
end

-- The keyboard is read with keydown (rawkeys: it is not a controller, so Esc is only the .bm menu). USB usages:
-- WASD and the arrows move, O is B, P is A, "," is Select, "." is Start. Esc opens/closes the emulator menu.
local KB = { { 0x1A, PAD_UP }, { 0x16, PAD_DOWN }, { 0x04, PAD_LEFT }, { 0x07, PAD_RIGHT }, { 0x52, PAD_UP }, { 0x51, PAD_DOWN }, { 0x50, PAD_LEFT }, { 0x4F, PAD_RIGHT },
             { 0x12, PAD_B }, { 0x13, PAD_A }, { 0x36, PAD_SELECT }, { 0x37, PAD_START } }
local kprev = {}
local function kedge(u)                        -- true once when the key goes down; kscan() runs at the end of every update
  return keydown(u) and not kprev[u]
end
local function kscan() for _, u in ipairs({ 0x1A, 0x16, 0x52, 0x51, 0x28, 0x2C, 0x29, 0x15, 0x12, 0x13 }) do kprev[u] = keydown(u) end end
local function nav_up() return btnp("up") or kedge(0x1A) or kedge(0x52) end
local function nav_down() return btnp("down") or kedge(0x16) or kedge(0x51) end
local function nav_ok() return btnp("a") or kedge(0x28) or kedge(0x2C) or kedge(0x13) end
-- The key or button that started the cart (Enter, A) can still be down in the first frames: the list must not take it for a choice.
-- It plays a ROM only after every OK key and button has been up once.
local ok_armed = false
local function ok_held() return btn("a") or keydown(0x28) or keydown(0x2C) or keydown(0x13) end
local function ok_arm() if not ok_armed and not ok_held() then ok_armed = true end end

local function readpad(p)
  if AUTOPLAY and p == 1 then return auto_pad(frames) end
  local v = 0
  if p == kbp then for i = 1, #KB do if keydown(KB[i][1]) then v = v | KB[i][2] end end end
  if btn("up", p) then v = v | PAD_UP end
  if btn("down", p) then v = v | PAD_DOWN end
  if btn("left", p) then v = v | PAD_LEFT end
  if btn("right", p) then v = v | PAD_RIGHT end
  if btn("start", p) and not (p == 1 and esc_grace > 0) then v = v | PAD_START end
  if btn("select", p) then v = v | PAD_SELECT end
  local bottom, right, left, top
  if nintendo then bottom, right, left, top = "b", "a", "y", "x" else bottom, right, left, top = "a", "b", "x", "y" end
  if btn(bottom, p) or btn(right, p) then v = v | PAD_A end
  if btn(left, p) or btn(top, p) then v = v | PAD_B end
  if v & 0x0C0 == 0x0C0 then v = v & ~0x0C0 end         -- left and right together: none
  if v & 0x030 == 0x030 then v = v & ~0x030 end
  return v
end

local state = "browse"           -- browse, run, menu
local files, sel, msg = {}, 1, nil
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
local show_fps = false
local draw_ms = { 0, 0 }
local toast = nil

-- How often a picture is made: every `every` emulated frames (1 = 60 pictures a second). The machine runs 60 frames a second whatever the picture costs
-- (frameskip), so the cart makes more pictures when the console keeps up and fewer when it does not. A window of 90 updates is timed. Same rules as bm SNES:
-- - overloaded: under 57 updates a second, or under 50 draws a second: a picture less often, and no step back for 15 seconds;
-- - three good windows in a row (58.8 updates and 55 draws a second or more), and the step back is allowed only when the cost measured now leaves room.
local every, win_n, win_t, win_last, cooldown, good = 1, 0, 0, nil, 0, 0
local win_dr, win_u, win_d = 0, 0, 0
local last_ups = 0
local function adapt()
  local t = time()
  if win_last then win_t = win_t + (t - win_last); win_n = win_n + 1 end
  win_last = t
  if cooldown > 0 then cooldown = cooldown - 1 end
  if win_n >= 90 then
    local ups = win_n / win_t
    local dps = win_dr / win_t
    local cost = (win_u + win_d) / win_n
    last_ups = ups
    if ups < 57 or dps < 50 then
      good = 0
      if every < 4 then every = every + 1; cooldown = 900 end
    elseif ups >= 58.8 and dps >= 55 then
      good = good + 1
      if good >= 3 and every > 1 and cooldown == 0 and cost < (every == 2 and 11 or 13) then every = every - 1; good = 0
      elseif good >= 3 then good = 3 end
    else
      good = 0
    end
    win_n, win_t, win_dr, win_u, win_d = 0, 0, 0, 0, 0
  end
end
local srm = ""
local mode = "sharp"
local START_MODE = "native"                      -- the default scale

-- The logger: one line a second in the kernel's log (`log`), no text on the screen. Read it with `pi.py line log` or the monitor command `log`.
-- Fields: t seconds; upd = emulated frames; drw = pictures drawn; fps = the console's loop; pic = picture every N frames;
-- umax/uavg = the longest and the mean update (ms); dmax/davg = the same for the draw; gap = the longest time between two draws (ms);
-- late = draws more than 40 ms after the last; lua = stat(1); then the machine's own times (us frame, ppu, cpu) and audio stats.
local dbg, dbg_n = nil, 0
local log_on = true
local lg = { t0 = nil, f0 = 0, drw = 0, umax = 0, dmax = 0, gap = 0, late = 0, last = nil, usum = 0, dsum = 0 }
local function log_reset(now) lg.t0 = now; lg.f0 = frames; lg.drw = 0; lg.umax = 0; lg.dmax = 0; lg.gap = 0; lg.late = 0; lg.usum = 0; lg.dsum = 0 end
local function log_draw(dm)
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
    log(string.format("nes t=%.1f upd=%d drw=%d fps=%d pic=1/%d umax=%.1f uavg=%.1f dmax=%.1f davg=%.1f gap=%.0f late=%d lua=%.0f mode=%s | %s | %s",
      now, frames - lg.f0, lg.drw, stat(2), every, lg.umax, lg.usum / n, lg.dmax, lg.dsum / math.max(1, lg.drw), lg.gap, lg.late, stat(1), mode,
      d2 and d2.us or "-", d2 and d2.audio or "-"))
    log_reset(now)
  end
end

local function list_roms()
  files = {}
  msg = nil
  local l, why = doc_list("nes")
  if not l then msg = why or "the player did not allow the files"; return end
  for _, d in ipairs(l) do
    local ext = d.name:match("%.([^.]+)$")
    ext = ext and ext:lower()
    if ext == "nes" or ext == "unf" then files[#files + 1] = d end
  end
  table.sort(files, function(a, b) return a.name < b.name end)
  if sel > #files then sel = math.max(1, #files) end
end

local function save_sram()
  local s = nes_sram()
  if s and srm ~= "" then doc_write(srm, s) end
end

local function open_rom(d)
  local data, why = doc_read(d.name, "nes")
  if not data then msg = why or "cannot read the file"; return end
  local ok, err = nes_load(data)
  if not ok then msg = err; return end
  local info = nes_info()
  local base = d.name:gsub("%.[^.]*$", ""):gsub("[^%w_%-]", ""):sub(1, 8)
  if base == "" then base = "rom" end
  srm = info.sram > 0 and (base .. ".nsv") or ""
  if srm ~= "" then
    local s = doc_read(srm)
    if s then nes_sram(s) end
  end
  msg = nil
  state = "run"
  frames = 0
  dbg = nil
  lg.last = nil; lg.t0 = nil
end

local dx, dy, dw, dh = 0, 0, SW, SH            -- where and how big the picture is on the screen
-- "native": the screen is 256x256 (the console centres it in a 480x270 screen: 4x at whole pixels on a 1080p TV); "sharp": a whole-number scale (1x on a 360-line screen);
-- "fit": 4:3 on the whole screen; "gpu2" and "gpu3": the screen is 960x540 (a 1080p TV shows it at whole pixels) or 1280x720 (a 720p TV), and the GPU draws the
-- picture 2 or 3 times bigger (nes_to_sheet and sspr)
local gmesh
local pic_new = true                       -- the machine made a picture since the last nes_to_sheet
local gpu_pic, gpu_zoom = false, 3             -- the picture goes by the sprite sheet and sspr (no render target)
local last_sw, last_sh = 0, 0
local function layout()
  local w, h
  last_sw, last_sh = SCREEN_W, SCREEN_H
  if (mode == "gpu2" or mode == "gpu3") and SCREEN_H >= 480 then
    gpu_zoom = math.max(1, math.floor(math.min(SCREEN_H / SH, SCREEN_W / SW)))
    gpu_pic = true
    dw, dh = SW * gpu_zoom, SH * gpu_zoom
    dx, dy = (SCREEN_W - dw) // 2, (SCREEN_H - dh) // 2
    return
  end
  gpu_pic = false
  if mode == "fit" then
    w, h = (SCREEN_H * 4) // 3, SCREEN_H
    if w > SCREEN_W then w = SCREEN_W; h = (SCREEN_W * 3) // 4 end
  else
    local s = math.max(1, math.floor(math.min(SCREEN_H / SH, SCREEN_W / SW)))   -- a whole number: every pixel the same size
    w, h = SW * s, SH * s
  end
  if w ~= dw or h ~= dh or not scr then
    dw, dh = w, h
    scr = target_create(dw, dh)             -- the machine scales its picture into it: the kernel's own scaling is slow
  end
  dx, dy = (SCREEN_W - dw) // 2, (SCREEN_H - dh) // 2
end

local MODE_LABEL = { sharp = "Scale: sharp", native = "Scale: native 256x256", gpu2 = "Scale: GPU 2x (540p)", gpu3 = "Scale: GPU 3x (720p)", fit = "Scale: fit 4:3" }
function mode_label() return MODE_LABEL[mode] end

-- the menu is 204 pixels high: centred, but it never goes below the screen (the native screen is 256 high)
function menu_top() return math.max(4, math.min(SCREEN_H // 2 - 70, SCREEN_H - 208)) end

-- Changes the scale mode. The GPU modes ask for a bigger screen (screen() takes effect at the next frame); a console that cannot give it keeps the mode it had.
function set_mode(m)
  local ok = true
  if m == "native" then ok = screen(256, 256)   -- the console puts it in the middle of a 480x270 screen: 4x at whole pixels on a 1080p TV
  elseif m == "gpu2" then ok = screen(960, 540)
  elseif m == "gpu3" then ok = screen(1280, 720)
  elseif screen and SCREEN_W ~= 640 and (mode == "gpu2" or mode == "gpu3" or mode == "native") then screen(640, 360) end
  if not ok then return false end
  mode = m
  layout()
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
  layout()
  if START_MODE ~= mode then set_mode(START_MODE) end
  pad_layout()
  frameskip(4)
  if rawkeys then rawkeys(true) end
  list_roms()
  if AUTOPLAY and files[1] then open_rom(files[1]) end
end

function _exit()
  save_sram()
  return true
end

function _update()
  if keydown(0x29) then esc_grace = 10 elseif esc_grace > 0 then esc_grace = esc_grace - 1 end
  if state == "browse" then
    if nav_up() then sel = math.max(1, sel - 1) end
    if nav_down() then sel = math.min(#files, sel + 1) end
    rom_scroll()
    if btnp("x") or kedge(0x15) then list_roms() end
    ok_arm()
    if ok_armed and nav_ok() and files[sel] then open_rom(files[sel]) end
  elseif state == "run" then
    -- the emulator menu: Esc on a keyboard, or L2 and R2 held for half a second on a pad
    local esc_edge = kedge(0x29)
    if btn("l2") and btn("r2") then holdmenu = holdmenu + 1 else holdmenu = 0 end
    if holdmenu > 30 or esc_edge then state = "menu"; menusel = 1; holdmenu = 0; toast = nil; save_sram(); kscan(); return end
    local tf = time()
    adapt()
    local render = frames % every == 0
    nes_frame(readpad(1), readpad(2), render)           -- a picture every `every` frames
    local u = (time() - tf) * 1000
    win_u = win_u + u
    if log_on then lg.usum = lg.usum + u; if u > lg.umax then lg.umax = u end end
    if render then pic_new = true end
    frames = frames + 1
    if frames % 120 == 0 then pad_layout() end
    if frames % 600 == 0 then save_sram() end
  elseif state == "menu" then
    if nav_up() then menusel = math.max(1, menusel - 1) end
    if nav_down() then menusel = math.min(6, menusel + 1) end
    if nav_ok() then
      if menusel == 1 then state = "run"
      elseif menusel == 2 then save_sram(); nes_reset(); frames = 0; lg.last = nil; lg.t0 = nil; state = "run"
      elseif menusel == 3 then show_fps = not show_fps
      elseif menusel == 4 then next_mode()
      elseif menusel == 5 then log_on = not log_on; log("nes log " .. (log_on and "on" or "off"))
      else save_sram(); state = "browse"; list_roms() end
    end
    if btnp("b") or kedge(0x29) or kedge(0x12) then state = "run"; holdmenu = 0 end
  end
  kscan()
end

function _draw()
  if state == "browse" then
    cls(0x201014)
    print("bm NES", 16, 12, 0xFF6060, 2)
    print("Put .nes files in the /nes folder of the SD card.", 16, 56, 0x9090A0)
    if #files == 0 then print("No ROM found.", 16, 100, 0xFFFFFF) end
    local y = 90
    rom_scroll()
    local last = math.min(#files, top + rom_rows() - 1)
    for i = top, last do
      local f = files[i]
      local c = (i == sel) and 0xFFFFFF or 0xA0A0B0
      if i == sel then rectfill(10, y - 1, SCREEN_W - 20, 17, 0x603040) end
      print(f.name:sub(1, 48), 16, y, c)
      print(string.format("%d KB", f.size // 1024), SCREEN_W - 120, y, c)
      y = y + 18
    end
    if top > 1 then print("^", SCREEN_W - 20, 72, 0x9090A0) end
    if last < #files then print("v " .. (#files - last) .. " more", SCREEN_W - 100, SCREEN_H - 40, 0x9090A0) end
    if msg then print(msg, 16, SCREEN_H - 56, 0xFF6060) end
    print("Enter/P or A play  R/X refresh  keys: WASD  O=B P=A  ,=Sel .=Start  Esc=menu", 16, SCREEN_H - 24, 0x9090A0)
    return
  end
  if SCREEN_W ~= last_sw or SCREEN_H ~= last_sh then layout() end   -- screen() took effect
  local t0 = time()
  if gpu_pic then
    if pic_new then                            -- the sheet is made again only for a new picture (the GPU keeps its texture)
      nes_to_sheet(0, 0)
      pic_new = false
    end
    gmesh = gmesh or mesh({ -1, -1, 0, 1, -1, 0, 0, 1, 0 }, { 1, 2, 3, -1 }, { 0, 0, 1, 0, 0, 1 })
    zclear()                                   -- a 3D call opens the GPU's job (a tiny triangle, far away): the 2D that follows is drawn by the GPU, in the job
    camera3d(0, 0, -2000)
    light3d(0, 0, -1, 1)
    draw3d(gmesh, 0, 0, 0)
    rectfill(0, 0, SCREEN_W, SCREEN_H, 0)
    sspr(0, 0, 256, SH, dx, dy, false, false, gpu_zoom)
    if state == "run" then win_dr = win_dr + 1 end
    if state ~= "run" then lg.last = nil; lg.t0 = nil elseif log_on then log_draw(0) end
  else
    nes_draw(scr, dw, dh)
    local t1 = time()
    if dx > 0 then                             -- the screen has two buffers: the margins are cleared every frame
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
  end
  dbg_n = dbg_n + 1
  if dbg_n % 30 == 0 then dbg = nes_debug() end
  if show_fps then print(string.format("%d fps  %.1f upd/s  pic 1/%d", stat(2), last_ups, every), 4, 4, 0x00FF00) end
  if state == "menu" then
    rectfill(SCREEN_W // 2 - 110, menu_top(), 220, 204, 0x201014)
    rect(SCREEN_W // 2 - 110, menu_top(), 220, 204, 0xFF6060)
    local items = { "Resume", "Reset", show_fps and "FPS: on" or "FPS: off",
                    mode_label(), log_on and "Log: on" or "Log: off", "Back to the list" }
    for i = 1, 6 do
      print(items[i], SCREEN_W // 2 - 90, menu_top() + 12 + (i - 1) * 24, i == menusel and 0xFFFFFF or 0x9090A0)
    end
    local info = nes_info()
    print(string.format("mapper %d  %d KB", info.mapper, info.prg // 1024), SCREEN_W // 2 - 90, menu_top() + 164, 0x9090A0)
    if toast then print(toast, SCREEN_W // 2 - 104, menu_top() + 184, 0xFFD050) end
  end
end
