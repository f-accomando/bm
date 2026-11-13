do
  -- The 2D drawn over the 3D of the plaza, as a game draws its HUD, to monitor the 2D functions in the context of this cart:
  -- sprites (flips, texels with holes), text, map() layers, fade (dark_end) and light (light_end). The 2D over 3D row picks
  -- one and the 2D amount row says how many things. The heroes and the plaza are the 3D under it.
  local floor, min = math.floor, math.min
  local W, Hh = 640, 360

  H.OVER2D = { "off", "sprites", "text", "map layers", "fade", "light" }
  H.HMATS = { "flat colours", "textured", "texture, holes", "screen-door", "baked light" }
  H.GPUOPTS = { "wc", "tex16", "etc1", "fs2", "sort", "attr", "filter", "merge", "light", "2d" }

  local ready = false

  local function hash(i)
    i = i & 0xFFFFFFFF
    i = i ~ (i >> 16); i = (i * 0x7FEB352D) & 0xFFFFFFFF
    i = i ~ (i >> 15); i = (i * 0x846CA68B) & 0xFFFFFFFF
    i = i ~ (i >> 16)
    return i
  end

  -- two 64x64 pictures in the free part of the sheet (the plaza uses the first row of tiles): (0,64) opaque, (64,64) with
  -- holes (a checker of 8 pixel blocks is empty): the sprites are cut from them
  local function paint()
    for y = 0, 63 do
      for x = 0, 63 do
        local r, g, b = 60 + x * 3, 90 + y * 2, 200 - x - y
        sset(x, 64 + y, ((r & 255) << 16) | ((g & 255) << 8) | (b & 255))
        if ((x // 8) + (y // 8)) % 2 == 0 then
          sset(64 + x, 64 + y, nil)
        else
          sset(64 + x, 64 + y, ((255 - r) << 16) | (g << 8) | (b & 255))
        end
      end
    end
  end

  function H.scene_init()
    if ready then return end
    ready = true
    paint()
    local tables = {}
    local COL = { 0xF80000, 0x00FC00, 0x0000F8, 0xF8FC00 }
    for c = 1, 4 do
      local rgb, row = COL[c], {}
      row[1] = rgb
      for k = 0, 15 do
        row[#row + 1] = (((rgb >> 16) * (k + 1) // 16) << 16) | ((((rgb >> 8) & 255) * (k + 1) // 16) << 8) | ((rgb & 255) * (k + 1) // 16)
      end
      tables[c] = row
    end
    fades(tables)
  end

  -- the 2D drawn after the 3D: n things of the kind the 2D row says
  function H.overlay_2d(f)
    local kind, n = H.o2d, H.n2d
    if kind == 1 or n <= 0 then return end
    if kind == 2 then                          -- sprites of 16x16, half opaque, half with holes, some flipped
      for i = 0, n - 1 do
        local h = hash(i)
        sspr((i & 1) * 64 + ((h >> 3) & 3) * 16, 64 + ((h >> 5) & 3) * 16, 16, 16,
             ((h >> 6) % (W - 16) + f * (1 + (h >> 29))) % (W - 16), (h >> 15) % (Hh - 16),
             ((h >> 26) & 1) == 1, ((h >> 27) & 1) == 1)
      end
    elseif kind == 3 then                      -- lines of text
      font("6x12")
      for i = 0, n - 1 do
        local h = hash(i + 3)
        print("SCORE " .. (h % 100000), (h >> 8) % (W - 80), (h >> 18) % (Hh - 12), 0xFFFFFF - (h & 0x3F3F3F))
      end
    elseif kind == 4 then                      -- map() layers scrolling (the 3 layers of the plaza, drawn n times over)
      for l = 0, min(n, 8) - 1 do
        local sx, sy = (f * (l + 1) * 5) % 120, (f * (l + 1)) % 64
        map(sx // 8, sy // 8, -(sx % 8), -(sy % 8), W // 8 + 1, Hh // 8 + 1, (l % 3) + 1, 1)
      end
    elseif kind == 5 then                      -- lamps by levels (dark_end) over the whole screen
      dark_begin(3)
      for i = 0, n - 1 do
        local h = hash(i + 5)
        glow((h % W) + f, ((h >> 12) & 0x1FF) % Hh, 40 + ((h >> 24) & 63), 15, 0.5)
      end
      dark_end()
    else                                       -- soft lamps (light_end) over the whole screen
      light_begin(0x404040)
      for i = 0, n - 1 do
        local h = hash(i + 9)
        light((h % W) + f, ((h >> 12) & 0x1FF) % Hh, 40 + ((h >> 24) & 63), 0xFFE0A0 + ((h >> 8) & 0x1F) * 0x10100, 1.0)
      end
      light_end()
    end
  end
end
