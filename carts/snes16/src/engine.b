-- engine.b: Native ARMv6 SNES-16 game engine (compiled on-console by bmlc.c).
-- Runs hero physics, multi-band HDMA parallax, sky gradient, Mode 7 perspective,
-- and 16-bit OAM sprite placement directly in ARM without Lua or 65C816 overhead.

-- state[] indices (1-based):
--   1: px_q8      2: py_q8      3: vy_q8      4: facing (0=R, 1=L)
--   5: on_ground  6: cam_x      7: slime_x    8: slime_dir
--   9: score     10: coin_mask 11: prev_pad  12: m7_ang_q8
--  13: m7_z_q8   14: spark_t   15: spark_x   16: spark_y

function pack_attr(pal: i32, prio: i32, hflip: i32, big: i32): i32
  return ((pal & 7) << 1) | ((prio & 3) << 4) | ((hflip & 1) << 6) | ((big & 1) << 8)
end

function step_frame(pad: i32, tick: i32, flags: i32, st: i32[]): i32
  -- flags: bit 0 = Mode 7 floor (bottom half), bit 1 = HDMA parallax/grad, bit 2 = OAM sprites
  local px = st[1]
  local py = st[2]
  local vy = st[3]
  local facing = st[4]
  local grounded = st[5]
  local cam_x = st[6]
  local slime_x = st[7]
  local slime_dir = st[8]
  local prev_pad = st[11]
  local ang = st[12]
  local zpos = st[13]
  local spark_t = st[14]

  local newly = pad & (~prev_pad)
  st[11] = pad

  -- Horizontal movement (D-pad Left = 0x0200, Right = 0x0100)
  local moving = 0
  if (pad & 0x0200) ~= 0 then
    px = px - 384
    facing = 1
    moving = 1
  end
  if (pad & 0x0100) ~= 0 then
    px = px + 384
    facing = 0
    moving = 1
  end
  px = clamp(px, 2048, 128000)

  -- Jump (B = 0x8000)
  if (newly & 0x8000) ~= 0 and grounded ~= 0 then
    vy = -1024
    grounded = 0
    s16_sfx(0, 0, 5200, 100)
  end

  -- Crystal pulse / dash sound (A = 0x0080 or Y = 0x4000)
  if (newly & 0x4080) ~= 0 then
    spark_t = 18
    if facing == 0 then
      st[15] = (px >> 8) + 16
    else
      st[15] = (px >> 8) - 12
    end
    st[16] = (py >> 8) + 2
    s16_sfx(1, 1, 6800, 96)
  end

  -- Gravity and floor collision (ground at y = 160 px -> 40960 q8)
  local floor_y = 160 * 256
  if (flags & 1) ~= 0 then
    floor_y = 128 * 256
  end
  vy = vy + 64
  if vy > 1280 then vy = 1280 end
  py = py + vy
  if py >= floor_y then
    py = floor_y
    vy = 0
    grounded = 1
  end

  -- Smooth camera follow
  local target_cam = (px >> 8) - 116
  if target_cam < 0 then target_cam = 0 end
  if target_cam > 256 then target_cam = 256 end
  cam_x = cam_x + ((target_cam - cam_x) >> 2)

  -- Slime patrol
  slime_x = slime_x + slime_dir
  if slime_x < 140 then slime_dir = 1 end
  if slime_x > 340 then slime_dir = -1 end

  -- Select (0x2000): respawn coins
  if (newly & 0x2000) ~= 0 then
    st[10] = 7
    s16_sfx(2, 2, 6400, 90)
  end

  -- Mode 7 camera drift & L1/R1 (Q/E) rotation
  if (pad & 0x0800) ~= 0 then zpos = zpos + 6 end
  if (pad & 0x0400) ~= 0 then zpos = max(64, zpos - 6) end
  if (pad & 0x0020) ~= 0 then ang = (ang - 320) & 65535 end
  if (pad & 0x0010) ~= 0 then ang = (ang + 320) & 65535 end
  if (flags & 1) ~= 0 then
    if facing == 0 then
      ang = (ang + 192) & 65535
    else
      ang = (ang - 192) & 65535
    end
    zpos = zpos + 4
  end

  -- Sky HDMA gradient & multi-band parallax scroll
  if (flags & 2) ~= 0 then
    local pulse = (tick >> 2) & 7
    s16_hdma_grad(0, 136, (2 << 10) | (4 << 5) | (8 + (pulse >> 2)), (14 << 10) | (14 << 5) | 28)
    s16_hdma_grad(137, 223, (14 << 10) | (14 << 5) | 28, (6 << 10) | (6 << 5) | 12)
    s16_hdma_band(1, 0, 55, (cam_x >> 2) + (tick >> 2), 0)
    s16_hdma_band(1, 56, 111, cam_x >> 1, 0)
    s16_hdma_band(1, 112, 223, cam_x, 0)
  else
    s16_hdma_grad(0, 223, (6 << 10) | (2 << 5) | 2, (6 << 10) | (2 << 5) | 2)
  end

  s16_scroll(0, cam_x, 0)
  s16_scroll(2, 0, 0)

  if (flags & 1) ~= 0 then
    s16_hdma_m7(136, 223, 512, zpos, ang, 160, 128)
  end

  -- OAM sprites
  s16_oam_clear(0)
  if (flags & 4) ~= 0 then
    local hx = (px >> 8) - cam_x
    local hy = py >> 8
    local h_tile = 0
    if grounded == 0 then
      h_tile = 4
    elseif moving ~= 0 and ((tick >> 2) & 1) ~= 0 then
      h_tile = 2
    end
    -- Hero 16x16
    s16_oam(0, hx, hy - 16, h_tile, pack_attr(0, 2, facing, 1))

    -- Slime enemy 16x16
    local sx = slime_x - cam_x
    local sy = (floor_y >> 8) - 16 - (((tick >> 3) & 1) * 2)
    if sx > -16 and sx < 256 then
      local sflip = 0
      if slime_dir < 0 then sflip = 1 end
      s16_oam(1, sx, sy, 6, pack_attr(1, 2, sflip, 1))
    end

    -- 3 Floating coins (8x8 small) + Crystal (16x16, semi-transparent pal >= 4)
    local coin_mask = st[10]
    for c = 0, 2 do
      local bit = 1 << c
      if (coin_mask & bit) ~= 0 then
        local cx = 130 + c * 42 - cam_x
        local cy = (floor_y >> 8) - 48 + (((tick + c * 10) >> 3) & 3)
        if abs((px >> 8) + 8 - (130 + c * 42 + 4)) < 14 and abs((py >> 8) - 8 - cy) < 16 then
          coin_mask = coin_mask & (~bit)
          st[9] = st[9] + 100
          s16_sfx(2, 2, 8200, 110)
        elseif cx > -8 and cx < 256 then
          local ctile = 32 + ((tick >> 3) & 1)
          s16_oam(2 + c, cx, cy, ctile, pack_attr(2, 2, 0, 0))
        end
      end
    end
    st[10] = coin_mask

    -- Crystal on stone pedestal (palette 4 -> color math enabled!)
    local kx = 320 - cam_x
    local ky = (floor_y >> 8) - 28 + (((tick >> 4) & 1) * 2)
    if kx > -16 and kx < 256 then
      s16_oam(5, kx, ky, 8, pack_attr(4, 2, 0, 1))
    end

    -- Sparkle burst on A/Y press
    if spark_t > 0 then
      spark_t = spark_t - 1
      local spx = st[15] - cam_x
      local spy = st[16]
      s16_oam(6, spx, spy, 34, pack_attr(4, 3, (tick >> 1) & 1, 0))
    end
  end

  st[1] = px
  st[2] = py
  st[3] = vy
  st[4] = facing
  st[5] = grounded
  st[6] = cam_x
  st[7] = slime_x
  st[8] = slime_dir
  st[12] = ang
  st[13] = zpos
  st[14] = spark_t

  s16_render()
  return st[9]
end
