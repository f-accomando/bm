do
  -- The scenes of the 3D Bench (b3d.c, carts/bench3d) in Lua, to run on this cart's panel: the same technique on the ARM and on
  -- the GPU, a load n you change, the numbers on the screen. The scene row picks one; the Heroes row is its load.
  --   spheres  n spheres of 96 faces        tiny   n grids of 512 faces of about 4 pixels      draws  n cubes, a draw each
  --   fill     quads of 320x180, each nearer than the last, every pixel drawn: the Fill row picks the face:
  --            flat, smooth, tex, alpha (texels with holes), screen (screen-door), bilinear, texswap (three textures in turn)
  -- The 2D row draws over any scene (after the 3D, as a game draws its HUD): sprites, text, map layers, fade, light.
  local sin, cos, floor, ceil, sqrt, max, min = math.sin, math.cos, math.floor, math.ceil, math.sqrt, math.max, math.min
  local W, Hh = 640, 360

  H.SCENES = {
    { id = "plaza",   name = "plaza + heroes", start = 0,  max = 64 },
    { id = "spheres", name = "spheres",        start = 1,  max = 8000 },
    { id = "tiny",    name = "small faces",    start = 1,  max = 2000 },
    { id = "draws",   name = "draw calls",     start = 1,  max = 40000 },
    { id = "fill",    name = "fill: quads",    start = 1,  max = 4000 },
  }
  H.FILLS = { "flat", "smooth", "tex", "alpha", "screen", "bilinear", "texswap" }
  H.OVER2D = { "off", "sprites", "text", "map layers", "fade", "light" }
  H.GPUOPTS = { "wc", "tex16", "etc1", "fs2", "sort", "attr", "filter", "merge", "light", "2d" }

  local sphere, cube, grid
  local quad = {}                             -- the quad of each Fill value
  local ready = false

  local function hash(i)
    i = i & 0xFFFFFFFF
    i = i ~ (i >> 16); i = (i * 0x7FEB352D) & 0xFFFFFFFF
    i = i ~ (i >> 15); i = (i * 0x846CA68B) & 0xFFFFFFFF
    i = i ~ (i >> 16)
    return i
  end

  -- two textures of 64x64 in the free part of the sheet (the plaza uses the first row of tiles): (0,64) opaque, (64,64) with
  -- holes (a checker of 8 pixel blocks is empty); three of 32x32 at y 24 for the texswap
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
    local C = { 0xE04040, 0x40C040, 0x4060E0 }
    for k = 0, 2 do
      for y = 0, 31 do
        for x = 0, 31 do
          local c = C[k + 1]
          if ((x // 4) + (y // 4)) % 2 == 0 then c = c ~ 0x303030 end
          sset(k * 40 + x, 24 + y, c)
        end
      end
    end
  end

  -- a quad of 2 x 1.125 units (16:9) in the plane z = 0; face: a colour, or -1 and the uv of the texture (sheet pixels)
  local function make_quad(colour, u0, v0, size)
    local f, uv = { 1, 3, 2, colour, 1, 4, 3, colour }, nil
    if colour == -1 then
      uv = { u0, v0 + size, u0 + size, v0, u0 + size, v0 + size, u0, v0 + size, u0, v0, u0 + size, v0 }
    end
    return mesh({ -1, -0.5625, 0, 1, -0.5625, 0, 1, 0.5625, 0, -1, 0.5625, 0 }, f, uv)
  end

  local function make_grid(k, colour)
    local v, f = {}, {}
    for z = 0, k do
      for x = 0, k do
        v[#v + 1] = x / k - 0.5; v[#v + 1] = 0; v[#v + 1] = z / k - 0.5
      end
    end
    for z = 0, k - 1 do
      for x = 0, k - 1 do
        local a = z * (k + 1) + x
        local b, c = a + 1, a + k + 1
        local d = c + 1
        f[#f + 1] = a + 1; f[#f + 1] = c + 1; f[#f + 1] = b + 1; f[#f + 1] = colour
        f[#f + 1] = b + 1; f[#f + 1] = c + 1; f[#f + 1] = d + 1; f[#f + 1] = colour
      end
    end
    return mesh(v, f)
  end

  function H.scene_init()
    if ready then return end
    ready = true
    paint()
    sphere = mesh_sphere(6, 8, 0x4080FF, 0xFFC040)
    cube = mesh_cube(0x60C060)
    grid = make_grid(16, 0x60A0E0)
    quad.flat = make_quad(0x80C0FF)
    quad.tex = make_quad(-1, 0, 64, 64)
    quad.alpha = make_quad(-1, 64, 64, 64)
    quad.screen = make_quad(0x80C0FF | 0x10000000)
    quad.swap = { make_quad(-1, 0, 24, 32), make_quad(-1, 40, 24, 32), make_quad(-1, 80, 24, 32) }
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

  local function scene_light()
    light3d(-0.4, 0.7, -0.6, 0.3)
  end

  local FOCAL = (W * 0.5) / math.tan(math.rad(60) / 2)

  -- the 3D of a scene; flags: the Lighting row (2 none, 0 flat, 4 smooth). Returns the pixels filled by a fill scene.
  function H.scene_draw(f, flags)
    local id = H.SCENES[H.scene].id
    local n = H.ns
    cls(0x0A0A1E)
    zclear()
    if id == "spheres" then
      camera3d(0, 0, -8, 0, 0, 60)
      scene_light()
      local side = ceil(sqrt(n))
      local step = 9.0 / (side > 1 and side or 1)
      for i = 0, n - 1 do
        local x = -4.5 + step * ((i % side) + 0.5)
        local y = 2.6 - step * 0.56 * ((i // side) + 0.5)
        draw3d(sphere, x, y, i % 3, f * 0.03 + i, f * 0.05, 0, step * 0.45, flags)
      end
    elseif id == "tiny" then
      camera3d(0, 6, -6, 0, -0.7, 60)
      scene_light()
      local side = ceil(sqrt(n))
      for i = 0, n - 1 do
        local x = -5.0 + 10.0 * ((i % side) + 0.5) / side
        local z = -2.0 + 8.0 * ((i // side) + 0.5) / side
        draw3d(grid, x, 0, z, 0, f * 0.01, 0, 0.35, 0)
      end
    elseif id == "draws" then
      camera3d(0, 0, -8, 0, 0, 60)
      scene_light()
      local side = ceil(sqrt(n))
      for i = 0, n - 1 do
        local x = -5.0 + 10.0 * ((i % side) + 0.5) / side
        local y = 3.0 - 6.0 * ((i // side) + 0.5) / side
        draw3d(cube, x, y, 0, i, f * 0.05, 0, 2.5 / side, 0)
      end
    else
      -- fill: quads of 320x180 pixels, one per quadrant in turn, each nearer than the one before
      local mode = H.FILLS[H.fill]
      camera3d(0, 0, 0, 0, 0, 60)
      scene_light()
      local k = 160.0 / FOCAL
      local d = 40.0
      local step = (gpu3d()) and 0.9985 or 0.985
      local m = mode == "flat" and quad.flat or mode == "smooth" and quad.flat or mode == "tex" and quad.tex
        or mode == "alpha" and quad.alpha or mode == "screen" and quad.screen or mode == "bilinear" and quad.tex or nil
      local fl = mode == "smooth" and 4 or 0
      for i = 0, n - 1 do
        local x = (i & 1) ~= 0 and k * d or -k * d
        local y = (i & 2) ~= 0 and -0.5625 * k * d or 0.5625 * k * d
        local q = m or quad.swap[i % 3 + 1]
        draw3d(q, x, y, d, 0, 0, 0, k * d, fl)
        d = d * step
      end
    end
  end

  -- the 2D drawn after the 3D (the HUD of a game): n things of the kind the 2D row says
  function H.overlay_2d(f)
    local kind, n = H.o2d, H.n2d
    if kind == 1 or n <= 0 then return end
    if kind == 2 then                          -- sprites of 16x16, half opaque, half with holes, some flipped
      for i = 0, n - 1 do
        local h = hash(i)
        sspr((i & 1) * 16, 64 + ((h >> 3) & 3) * 16, 16, 16, ((h >> 6) % (W - 16) + f * (1 + (h >> 29))) % (W - 16),
             (h >> 15) % (Hh - 16), ((h >> 26) & 1) == 1, ((h >> 27) & 1) == 1)
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

  -- the load steps of the sweep of a scene: 8 steps from its start to its max, by powers
  function H.scene_counts()
    local s = H.SCENES[H.scene]
    if H.scene == 1 then return { 0, 4, 8, 12, 16, 20, 28, 36, 48, 64 } end
    local out, v = {}, s.start
    local ratio = (s.max / s.start) ^ (1 / 9)
    for i = 1, 10 do
      out[i] = max(1, floor(v + 0.5))
      v = v * ratio
    end
    return out
  end
end
