-- main.lua: BM-SNES16 Native ARM Showcase Cartridge (.bm)
-- Uses snes16.lua (slim library) + engine.b (compiled on-console to ARMv6 BMN1).

local ENGINE_SRC = [=====[
--@ENGINE_B@
]=====]

local mod, fn_step, state_buf
local tick = 0
local mode_m7 = false
local hdma_on = true
local oam_on = true
local scale_modes = { "fit", "sharp", "native", "gpu2x" }
local scale_i = 1
local menu_open = false
local menu_sel = 1
local l2r2_hold = 0
local music_on = true

local function build_tiles_and_palettes()
  -- CGRAM Palettes (RGB555):
  -- Bank 0: Sky / BG2 Clouds & Mountains
  S16.pal(0, {
    S16.rgb(2, 3, 7),    S16.rgb(4, 7, 13),  S16.rgb(7, 11, 18), S16.rgb(11, 16, 23),
    S16.rgb(18, 22, 27), S16.rgb(26, 28, 31),S16.rgb(3, 10, 8),  S16.rgb(6, 16, 11),
    S16.rgb(11, 22, 14), S16.rgb(20, 26, 18),S16.rgb(8, 7, 9),   S16.rgb(14, 13, 15),
    S16.rgb(20, 19, 21), S16.rgb(27, 25, 24),S16.rgb(30, 21, 9), S16.rgb(31, 28, 16),
  })
  -- Bank 1: BG1 Stone Ruins & Lush Grass
  S16.pal(1, {
    S16.rgb(0, 0, 0),    S16.rgb(3, 10, 4),  S16.rgb(6, 18, 6),  S16.rgb(12, 25, 8),
    S16.rgb(20, 29, 12), S16.rgb(6, 5, 6),   S16.rgb(11, 10, 11),S16.rgb(17, 16, 16),
    S16.rgb(23, 22, 20), S16.rgb(28, 26, 22),S16.rgb(9, 6, 4),   S16.rgb(15, 10, 6),
    S16.rgb(22, 15, 9),  S16.rgb(18, 24, 30),S16.rgb(25, 28, 31),S16.rgb(31, 24, 8),
  })
  -- Bank 8 (OBJ Pal 0): Hero Knight
  S16.pal(8, {
    S16.rgb(0, 0, 0),    S16.rgb(2, 2, 4),   S16.rgb(10, 12, 16),S16.rgb(18, 20, 25),
    S16.rgb(27, 28, 31), S16.rgb(24, 5, 4),  S16.rgb(31, 11, 6), S16.rgb(28, 19, 13),
    S16.rgb(22, 13, 8),  S16.rgb(4, 8, 18),  S16.rgb(8, 15, 26), S16.rgb(28, 22, 6),
    S16.rgb(16, 10, 5),  S16.rgb(31, 28, 18),S16.rgb(12, 4, 4),  S16.rgb(31, 31, 31),
  })
  -- Bank 9 (OBJ Pal 1): Green Slime
  S16.pal(9, {
    S16.rgb(0, 0, 0),    S16.rgb(2, 6, 2),   S16.rgb(4, 14, 4),  S16.rgb(9, 23, 6),
    S16.rgb(18, 30, 12), S16.rgb(28, 31, 22),S16.rgb(31, 31, 31),S16.rgb(1, 3, 1),
  })
  -- Bank 10 (OBJ Pal 2): Gold Coin
  S16.pal(10, {
    S16.rgb(0, 0, 0),    S16.rgb(10, 6, 1),  S16.rgb(21, 14, 2), S16.rgb(30, 23, 5),
    S16.rgb(31, 30, 16), S16.rgb(31, 31, 28),
  })
  -- Bank 12 (OBJ Pal 4 - Color Math): Glowing Crystal & Spark
  S16.pal(12, {
    S16.rgb(0, 0, 0),    S16.rgb(5, 10, 22), S16.rgb(9, 22, 30), S16.rgb(18, 29, 31),
    S16.rgb(28, 20, 31), S16.rgb(31, 30, 31),
  })

  -- Mode 7 palette entries (16..23)
  s16_cgram(16, S16.rgb(4, 6, 10))
  s16_cgram(17, S16.rgb(8, 12, 16))
  s16_cgram(18, S16.rgb(15, 18, 21))
  s16_cgram(19, S16.rgb(4, 15, 6))
  s16_cgram(20, S16.rgb(8, 22, 9))
  s16_cgram(21, S16.rgb(26, 21, 7))

  -- Disjoint VRAM layout so Mode 7 (0x0000..0x7FFF) never overlaps OBJ or BG1/BG2:
  -- 0x0000..0x7FFF: Mode 7 map+tiles, 0x8000: OBJ, 0xB000: BG2 map, 0xC000: BG2 chr, 0xD000: BG1 map, 0xE000: BG1 chr
  local BG1_CHR, BG2_CHR, OBJ_CHR = 0xE000, 0xC000, 0x8000

  -- BG1 Tile 0: empty, Tile 1: grass top, Tile 2: stone brick, Tile 3: pillar shaft, Tile 4: pillar cap
  S16.tile4(BG1_CHR, 0, { "........","........","........","........","........","........","........","........" })
  S16.tile4(BG1_CHR, 1, {
    "34334334","23223223","12112112","67765778",
    "78876887","55555555","77875787","66765676",
  })
  S16.tile4(BG1_CHR, 2, {
    "55555555","78875788","67765677","55555555",
    "78857887","67756776","55555555","66665666",
  })
  S16.tile4(BG1_CHR, 3, {
    ".578986.",".578986.",".567876.",".578986.",
    ".578986.",".567876.",".578986.",".578986.",
  })
  S16.tile4(BG1_CHR, 4, {
    "59999995","58888885","56777765",".578875.",
    ".578875.",".567765.",".578875.",".578875.",
  })

  -- BG2 (0x2000): Clouds (1,2) & Mountain (3,4,5)
  S16.tile4(BG2_CHR, 0, { "........","........","........","........","........","........","........","........" })
  S16.tile4(BG2_CHR, 1, {
    "....4555","..455555",".4555555","45555444",
    "44444333",".3333333","........","........",
  })
  S16.tile4(BG2_CHR, 2, {
    "5554....","555554..","5555554.","44455554",
    "33344444","3333333.","........","........",
  })
  S16.tile4(BG2_CHR, 3, {
    ".......4","......43",".....432","....4322",
    "...43221","..432211",".4322111","43221111",
  })
  S16.tile4(BG2_CHR, 4, {
    "4.......","34......","234.....","2234....",
    "12234...","112234..","1112234.","11112234",
  })
  S16.tile4(BG2_CHR, 5, {
    "22232211","22223211","12222111","11221111",
    "77677667","67766776","66766676","66666666",
  })

  -- OBJ (0x8000): Hero 16x16 idle (tiles 0,1 / 16,17) and walk (2,3 / 18,19), jump (4,5 / 20,21)
  local function upload_16x16(t0, tl, tr, bl, br)
    S16.tile4(OBJ_CHR, t0, tl)
    S16.tile4(OBJ_CHR, t0 + 1, tr)
    S16.tile4(OBJ_CHR, t0 + 16, bl)
    S16.tile4(OBJ_CHR, t0 + 17, br)
  end

  upload_16x16(0,
    { "..16661.",".1566651","..134431",".1344b41",".1322221","..17871.",".1344431","1449aa91" },
    { "........","........","1.......","41......","f1......","1.......","41......","91......" },
    { "13299a91","12199991",".1.1c11.","...1341.","...1231.","..1341..","..1111..","........" },
    { "91......","31......","1.......","41......","31......","41......","11......","........" })

  upload_16x16(2,
    { "..16661.",".1566651","..134431",".1344b41",".1322221","..17871.",".1344431","1449aa91" },
    { "........","........","1.......","41......","f1......","1.......","41......","91......" },
    { "13299a91","12199991",".1.1c11.","..1341..","..1231..",".1341...",".1111...","........" },
    { "91......","31......","1.......","41......","31......",".41.....",".11.....","........" })

  upload_16x16(4,
    { ".156661.","15666651","..134431",".1344b41",".1322221","..17871.","13444431","149aa941" },
    { "........","........","1.......","41......","f1......","1.......","41......","1......." },
    { "1299a91.",".199991.","..1cc1..",".1341341",".1231231","1341.141","111...11","........" },
    { "........","........","........","........","........","........","........","........" })

  -- Slime 16x16 at tile 6
  upload_16x16(6,
    { "........","........",".....111","...11343","..134554",".1345653",".1234432","12233332" },
    { "........","........","11......","3411....","44431...","346431..","2316121.","23333221" },
    { "12223332","12222222","11222222",".1111111","........","........","........","........" },
    { "23332221","22222221","22222211","1111111.","........","........","........","........" })

  -- Crystal 16x16 at tile 8 (Color Math Pal 4)
  upload_16x16(8,
    { ".......1","......14",".....145","....1355","...12345","..123454","..123443","..123342" },
    { "1.......","31......","431.....","5321....","54321...","433221..","332221..","222211.." },
    { "..123342","..123443","...12343","....1232",".....121","......11",".......1","........" },
    { "222211..","322211..","32211...","2211....","121.....","11......","1.......","........" })

  -- Coin 8x8 at tiles 32 & 33, Sparkle at 34
  S16.tile4(OBJ_CHR, 32, { "..1111..",".134431.","13455431","13453431","13453431","13444431",".133321.","..1111.." })
  S16.tile4(OBJ_CHR, 33, { "...11...","..1441..","..1451..","..1431..","..1431..","..1431..","..1321..","...11..." })
  S16.tile4(OBJ_CHR, 34, { "...5....","...3....","..353...","5355535.","..353...","...3....","...5....","........" })

  -- BG1 & BG2 tilemaps (64x32 = 2 screens wide, disjoint from Mode 7 0x0000..0x7FFF)
  s16_bg(0, 0xD000, BG1_CHR, 1) -- BG1: map 0xD000, chr 0xE000, 64x32
  s16_bg(1, 0xB000, BG2_CHR, 1) -- BG2: map 0xB000, chr 0xC000, 64x32
  s16_obj_cfg(0, OBJ_CHR, 0x2000) -- mode 0: small=8x8, big=16x16

  -- Clear maps
  S16.fill(0, 0, 0, 64, 32, 0, 1, 0)
  S16.fill(1, 0, 0, 64, 32, 0, 0, 0)

  -- BG2 Clouds (rows 2..5) & Mountains (rows 10..18)
  for x = 0, 60, 10 do
    s16_map(1, x, 3, 1, 0, 0)
    s16_map(1, x + 1, 3, 2, 0, 0)
    s16_map(1, x + 4, 5, 1, 0, 0)
    s16_map(1, x + 5, 5, 2, 0, 0)
  end
  for x = 0, 56, 8 do
    for h = 0, 3 do
      s16_map(1, x + h, 14 - h, 3, 0, 0)
      s16_map(1, x + 7 - h, 14 - h, 4, 0, 0)
      for f = 14 - h + 1, 27 do
        s16_map(1, x + h, f, 5, 0, 0)
        s16_map(1, x + 7 - h, f, 5, 0, 0)
      end
    end
  end

  -- BG1 Floor (row 20..27) + Ruin pillars
  S16.fill(0, 0, 20, 64, 1, 1, 1, 0)
  S16.fill(0, 0, 21, 64, 7, 2, 1, 0)
  for _, px in ipairs({ 6, 18, 38, 42 }) do
    s16_map(0, px, 16, 4, 1, 0)
    s16_map(0, px, 17, 3, 1, 0)
    s16_map(0, px, 18, 3, 1, 0)
    s16_map(0, px, 19, 3, 1, 0)
  end

  -- Mode 7 floor tile & map (tile 1: stone/grass checkerboard)
  S16.m7tile(1, {
    "cccccccc","cddddddc","cdeeeddc","cddeeddc",
    "cddddddc","cccccccc","ffffffff","ffffffff",
  }, { c = 16, d = 17, e = 18, f = 19 })
  S16.m7tile(2, {
    "gggggggg","ghhhhhhg","ghiiiihg","ghiiiihg",
    "ghhhhhhg","gggggggg","ffffffff","ffffffff",
  }, { g = 19, h = 20, i = 21, f = 17 })
  for my = 0, 127 do
    for mx = 0, 127 do
      s16_m7_map(mx, my, ((mx + my) % 2 == 0) and 1 or 2)
    end
  end
end

local function setup_audio()
  -- Slot 0: Jump chirp, Slot 1: Crystal shimmer, Slot 2: Coin ding, Slot 3: Bass pulse
  s16_dsp_pcm(0, 0x0400, S16.wave("tri", 256, 4, 14000), 0)
  s16_dsp_pcm(1, 0x0600, S16.wave("sine", 256, 6, 12000), 0)
  s16_dsp_pcm(2, 0x0800, S16.wave("square", 256, 8, 11000), 0)
  s16_dsp_pcm(3, 0x0A00, S16.wave("saw", 256, 2, 9000), 0)
end

local function apply_ppu_mode()
  s16_hdma_clear(0x0F)
  if mode_m7 then
    -- Top half (0..135): Mode 1 (BG2 mountains + OBJ); Bottom half (136..223): Mode 7 floor + OBJ
    s16_mode(1, 2 | 16, 0, 15)
    s16_hdma_mode(0, 135, 1, 2 | 16, 0)
    s16_hdma_mode(136, 223, 7, 1 | 16, 0)
  else
    -- Full Mode 1: BG1 + BG2 + OBJ on main screen; Color Math add fixed/sub on backdrop + sprite pal 4..7
    s16_mode(1, 1 | 2 | 16, 1 | 2, 15)
  end
  -- Color Math: add fixed-colour gradient onto backdrop (bit 5) and OBJ pal 4..7 (bit 4)
  s16_math(0x00, 0x30, S16.rgb(4, 6, 14))
end

function _init()
  S16.init(224)
  build_tiles_and_palettes()
  setup_audio()
  apply_ppu_mode()

  mod = S16.compile(ENGINE_SRC)
  fn_step = native_fn(mod, "step_frame")
  state_buf = vm_buf("i32", 16)
  vm_buf_fill(state_buf, {
    48 * 256, 160 * 256, 0, 0,
    1, 0, 210, 1,
    0, 7, 0, 0,
    160, 0, 0, 0
  })
end

local bass_notes = { 1200, 1200, 1500, 1350, 1200, 1600, 1800, 1500 }

function _update()
  if keyp("escape") then menu_open = not menu_open end
  if btn("l2") and btn("r2") then
    l2r2_hold = l2r2_hold + 1
    if l2r2_hold == 30 then menu_open = not menu_open end
  else
    l2r2_hold = 0
  end

  if menu_open then
    if keyp("up") or btnp("up") then menu_sel = (menu_sel - 2) % 5 + 1 end
    if keyp("down") or btnp("down") then menu_sel = menu_sel % 5 + 1 end
    if keyp("return") or keyp("z") or btnp("a") or btnp("b") then
      if menu_sel == 1 then
        mode_m7 = not mode_m7
        apply_ppu_mode()
      elseif menu_sel == 2 then
        hdma_on = not hdma_on
      elseif menu_sel == 3 then
        oam_on = not oam_on
      elseif menu_sel == 4 then
        for _ = 1, #scale_modes do
          scale_i = scale_i % #scale_modes + 1
          if S16.set_scale(scale_modes[scale_i]) then break end
        end
      elseif menu_sel == 5 then
        music_on = not music_on
      end
    end
    return
  end

  tick = tick + 1
  if music_on and (tick % 16) == 1 then
    local n = bass_notes[((tick // 16) % #bass_notes) + 1]
    s16_dsp_play(3, 3, n, 0xEF, 0xC8, 38, 38)
  end

  local flags = (mode_m7 and 1 or 0) | (hdma_on and 2 or 0) | (oam_on and 4 or 0)
  local pad = S16.read_pad(1)
  local t0 = time()
  native_run(mod, fn_step, pad, tick, flags, state_buf)
  local dt_us = math.floor((time() - t0) * 1000000 + 0.5)
  s16_dsp_step(534)
  s16_stats(dt_us)
end

function _draw()
  local dx, dy, sc = S16.present(scale_modes[scale_i], 224)
  local st = s16_stats()
  local score = vm_buf_get(state_buf, 9)
  local mode_lbl = mode_m7 and "M1+M7" or "MODE1"
  rectfill(dx + 4, dy + 4, 240, 14, 0x0810)
  print(string.format("SNES-16 ARM | %s | SCORE %05d", mode_lbl, score), dx + 6, dy + 4, 0xFFE0)
  local by = math.floor(dy + 224 * sc - 16)
  rectfill(dx + 4, by - 1, 290, 14, 0x0810)
  print(string.format("ARM %dus PPU %dus DSP %dus | %s", st.arm_us, st.ppu_us, st.dsp_us, scale_modes[scale_i]), dx + 6, by, 0x8FF7)

  if menu_open then
    local mx, my = math.floor(dx + 48 * sc), math.floor(dy + 40 * sc)
    rectfill(mx, my, 176, 112, 0x1018)
    rect(mx, my, 176, 112, 0xFFE0)
    local rows = {
      "Scene: " .. (mode_m7 and "Mode1+Mode7" or "Mode1"),
      "HDMA:  " .. (hdma_on and "ON" or "OFF"),
      "OAM:   " .. (oam_on and "ON" or "OFF"),
      "Scale: " .. scale_modes[scale_i],
      "Audio: " .. (music_on and "ON" or "OFF"),
    }
    print("SNES-16 MENU (ESC)", mx + 10, my + 8, 0xFFE0)
    for i = 1, #rows do
      local c = (i == menu_sel) and 0x07E0 or 0xFFFF
      print((i == menu_sel and "> " or "  ") .. rows[i], mx + 10, my + 24 + (i - 1) * 16, c)
    end
  end
end
