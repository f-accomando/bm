-- Kitchen furniture. Every station fills one cell and blocks walking.
--   hold      it can hold one item on top
--   give      a crate: gives its ingredient to an empty hand
--   work      "chop" (X at the cutting board)
--   box       a cooking vessel: its process, capacity, seconds to cook and,
--             after that, seconds until it burns (nil: never)
--   col/top   body and top colours of the 3D model
Kit.TOP = 0.75                      -- height of a counter top

Data.ST = {
  counter  = { name = "counter", hold = true, col = 0xB89870, top = 0xECE2CC },
  crate    = { name = "crate", hold = true, give = true, col = 0x9A6A3A, top = 0xC08A50 },
  board    = { name = "cutting board", hold = true, work = "chop", col = 0xB89870, top = 0xECE2CC },
  pot      = { name = "stove", box = "boil", cap = 3, cook = 7, burn = 9, col = 0x585E68, top = 0x30343A },
  pan      = { name = "stove", box = "fry", cap = 1, cook = 5, burn = 6, col = 0x585E68, top = 0x30343A },
  oven     = { name = "oven", box = "bake", cap = 4, cook = 9, burn = 10, col = 0x8A4A3A, top = 0x5A5048 },
  blender  = { name = "blender", box = "blend", cap = 3, cook = 4, col = 0xB89870, top = 0xECE2CC },
  plates   = { name = "plates", col = 0xB89870, top = 0xECE2CC },
  sink     = { name = "sink", col = 0x7890A8, top = 0xB8C8D8 },
  ret      = { name = "dirty plates", col = 0x8A7A68, top = 0xB0A490 },
  serve    = { name = "serving window", col = 0x3A7AC0, top = 0x58A0E8 },
  trash    = { name = "trash", col = 0x4E5A4E, top = 0x2A302A },
  register = { name = "register", col = 0xB08030, top = 0xE0B040 },
  belt     = { name = "conveyor", hold = true, col = 0x505860, top = 0x3A3E44 },
  valve    = { name = "valve", col = 0x6878A0, top = 0x8898C0 },
  wall     = { name = "wall", wall = true },
}

-- layout characters (see 14_stages.lua)
Data.CHAR_ST = {
  ["#"] = "wall", C = "counter", B = "board", P = "pot", F = "pan", O = "oven",
  M = "blender", D = "plates", S = "sink", R = "ret", W = "serve", T = "trash",
  ["$"] = "register", E = "counter", [">"] = "belt", ["<"] = "belt", U = "belt", V = "belt",
}
Data.BELT_DIR = { [">"] = { 1, 0 }, ["<"] = { -1, 0 }, U = { 0, 1 }, V = { 0, -1 } }

-- crates: lower case letter -> ingredient
Data.CRATE_CHAR = {
  t = "tomato", o = "onion", p = "potato", c = "carrot", m = "mushroom", l = "lettuce",
  u = "cucumber", h = "cheese", b = "bread", r = "rice", a = "pasta", e = "egg",
  f = "flour", k = "milk", y = "butter", i = "chicken", s = "beef", x = "fish",
  z = "shrimp", g = "sausage", n = "herbs", q = "fruit", j = "chocolate",
}

-- floor characters
--   .  floor      ~  ice        _  water / gap (not walkable)
--   ^  steam vent  +  door (opens and closes)   1-4  where the chefs start
Data.FLOOR_CHAR = { ["."] = "floor", ["~"] = "ice", ["_"] = "void", ["^"] = "vent", ["+"] = "door",
                    ["1"] = "floor", ["2"] = "floor", ["3"] = "floor", ["4"] = "floor" }
