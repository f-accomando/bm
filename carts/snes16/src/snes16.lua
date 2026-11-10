-- snes16.lua: slim library for native ARM SNES-16 cartridges.
-- Abstracts PPU registers, 4bpp/Mode7 tile encoding, CGRAM RGB555, BRR waveforms,
-- native ARMv6 BML compilation, and Pi Zero display scaling (sharp / native / GPU 2x).

local S16 = {}

function S16.rgb(r, g, b)
  return (r & 31) | ((g & 31) << 5) | ((b & 31) << 10)
end

function S16.rgb8(r, g, b)
  return S16.rgb(r >> 3, g >> 3, b >> 3)
end

function S16.init(lines)
  s16_init(lines or 224)
end

-- Upload a 16-colour (or N-colour) RGB555 palette into CGRAM bank (0..15; sprites use 8..15).
function S16.pal(bank, colors)
  local base = (bank & 15) * 16
  for i = 1, #colors do
    s16_cgram(base + i - 1, colors[i])
  end
end

-- Decode 8 rows of 8 hex/symbol characters into a flat 64-byte index table.
local function parse_8x8(rows, cmap)
  local px = {}
  local k = 1
  for y = 1, 8 do
    local row = rows[y] or "........"
    for x = 1, 8 do
      local ch = row:sub(x, x)
      local v = cmap and cmap[ch]
      if v == nil then
        if ch == "." or ch == " " then v = 0
        else v = tonumber(ch, 16) or 0 end
      end
      px[k] = v
      k = k + 1
    end
  end
  return px
end

-- Upload a 4bpp tile (32 bytes per tile) at chr_base + tile_id * 32.
function S16.tile4(chr_base, tile_id, rows, cmap)
  s16_tile((chr_base + tile_id * 32) & 0xFFFF, 4, parse_8x8(rows, cmap))
end

-- Upload a 2bpp tile (16 bytes per tile) at chr_base + tile_id * 16.
function S16.tile2(chr_base, tile_id, rows, cmap)
  s16_tile((chr_base + tile_id * 16) & 0xFFFF, 2, parse_8x8(rows, cmap))
end

-- Upload a Mode 7 8-bit chunky tile.
function S16.m7tile(tile_id, rows, cmap)
  s16_m7_tile(tile_id, parse_8x8(rows, cmap))
end

-- Fill a tilemap rectangle on BG layer (0..3).
function S16.fill(layer, x0, y0, w, h, tile, pal, prio, hf, vf)
  for y = y0, y0 + h - 1 do
    for x = x0, x0 + w - 1 do
      s16_map(layer, x, y, tile, pal or 0, prio or 0, hf or false, vf or false)
    end
  end
end

-- Pack OAM attributes matching s16_oam(id, x, y, tile, attr) in engine.b.
function S16.oam_attr(pal, prio, hflip, vflip, big, tab)
  return (tab and 1 or 0)
    | ((pal & 7) << 1)
    | ((prio & 3) << 4)
    | (hflip and 0x40 or 0)
    | (vflip and 0x80 or 0)
    | (big and 0x100 or 0)
end

-- Compile .b source to native ARMv6 BMN1 module in RAM.
function S16.compile(src)
  local blob, err = native_compile(src)
  if not blob then error("snes16 compile: " .. tostring(err)) end
  local mod, lerr = native_load(blob)
  if not mod then error("snes16 load: " .. tostring(lerr)) end
  return mod
end

-- Generate a simple 16-bit PCM waveform table for s16_dsp_pcm().
function S16.wave(kind, nsamples, cycles, amp)
  local t = {}
  local a = amp or 14000
  local c = cycles or 1
  local seed = 0x5A3C
  for i = 1, nsamples do
    local ph = ((i - 1) * c / nsamples) % 1.0
    local v = 0
    if kind == "square" then
      v = ph < 0.5 and a or -a
    elseif kind == "pulse25" then
      v = ph < 0.25 and a or -a
    elseif kind == "saw" then
      v = math.floor((1.0 - 2.0 * ph) * a)
    elseif kind == "tri" then
      v = math.floor((ph < 0.5 and (4.0 * ph - 1.0) or (3.0 - 4.0 * ph)) * a)
    elseif kind == "sine" then
      v = math.floor(math.sin(ph * 6.2831853) * a)
    elseif kind == "noise" then
      seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
      local env = 1.0 - (i - 1) / nsamples
      v = math.floor((((seed >> 16) % 2 == 0) and a or -a) * env)
    end
    t[i] = v
  end
  return t
end

-- Read pad bitmask matching SNES_PAD_* bits in snes.h.
local BTNS = {
  { "b", 0x8000 }, { "y", 0x4000 }, { "select", 0x2000 }, { "start", 0x1000 },
  { "up", 0x0800 }, { "down", 0x0400 }, { "left", 0x0200 }, { "right", 0x0100 },
  { "a", 0x0080 }, { "x", 0x0040 }, { "l1", 0x0020 }, { "r1", 0x0010 },
}
function S16.read_pad(player)
  local v = 0
  local p = player or 1
  for i = 1, #BTNS do
    if btn(BTNS[i][1], p) then v = v | BTNS[i][2] end
  end
  return v
end

-- Present the 256x224 PPU frame on the current screen mode ("fit", "sharp", "native", "gpu2x").
local rt_buf, rt_w, rt_h = nil, 0, 0
local g_mesh = nil

function S16.set_scale(mode)
  if mode == "native" then
    return screen(256, 256) ~= false
  elseif mode == "gpu2x" then
    return screen(960, 540) ~= false
  elseif SCREEN_W ~= 640 or SCREEN_H ~= 360 then
    screen(640, 360)
  end
  return true
end

function S16.present(mode, lines)
  local sh = lines or 224
  if mode == "gpu2x" and SCREEN_H >= 448 then
    g_mesh = g_mesh or mesh({ -1, -1, 0, 1, -1, 0, 0, 1, 0 }, { 1, 2, 3, -1 }, { 0, 0, 1, 0, 0, 1 })
    zclear()
    camera3d(0, 0, -2000)
    light3d(0, 0, -1, 1)
    draw3d(g_mesh, 0, 0, 0)
    rectfill(0, 0, SCREEN_W, SCREEN_H, 0)
    local h = s16_to_sheet(0, 0)
    local sc = math.max(1, math.min(SCREEN_W // 256, SCREEN_H // h))
    local dx = (SCREEN_W - 256 * sc) // 2
    local dy = (SCREEN_H - h * sc) // 2
    sspr(0, 0, 256, h, dx, dy, false, false, sc)
    return dx, dy, sc
  end

  local dw, dh
  if mode == "fit" then
    dh = SCREEN_H
    dw = math.min(SCREEN_W, (SCREEN_H * 4) // 3)
  else
    local sc = math.max(1, math.min(SCREEN_W // 256, SCREEN_H // sh))
    dw, dh = 256 * sc, sh * sc
  end
  if not rt_buf or rt_w ~= dw or rt_h ~= dh then
    rt_w, rt_h = dw, dh
    rt_buf = target_create(dw, dh)
  end
  cls(0)
  s16_draw(rt_buf, dw, dh)
  local dx, dy = (SCREEN_W - dw) // 2, (SCREEN_H - dh) // 2
  target_blit(rt_buf, dx, dy)
  return dx, dy, dh / sh
end
