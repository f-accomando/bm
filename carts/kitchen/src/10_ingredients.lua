-- Ingredients. `chop`: seconds to chop (nil: used whole). `icon`: cell of
-- the 16x16 icon in sheet.png (chopped icon = icon + 32; see mkassets.py).
-- `shape` and the colours make the 3D model (see 20_meshes.lua).

Data.ING_LIST = {
  { id = "tomato",    name = "tomato",    chop = 1.0, c1 = 0xE83A2A, c2 = 0x3CA040, shape = "ball",    size = 0.22 },
  { id = "onion",     name = "onion",     chop = 1.2, c1 = 0xC89060, c2 = 0xE8D8B0, shape = "ball",    size = 0.21 },
  { id = "potato",    name = "potato",    chop = 1.4, c1 = 0xB88A4A, c2 = 0xE8C880, shape = "egg",     size = 0.22 },
  { id = "carrot",    name = "carrot",    chop = 1.0, c1 = 0xF08020, c2 = 0x40A030, shape = "cone",    size = 0.26 },
  { id = "mushroom",  name = "mushroom",  chop = 0.8, c1 = 0xC8A078, c2 = 0xF0E8D8, shape = "mush",    size = 0.2 },
  { id = "lettuce",   name = "lettuce",   chop = 1.0, c1 = 0x60C040, c2 = 0xA8E070, shape = "ball",    size = 0.25 },
  { id = "cucumber",  name = "cucumber",  chop = 1.0, c1 = 0x2E8A3A, c2 = 0xB8E8A0, shape = "long",    size = 0.28 },
  { id = "cheese",    name = "cheese",    chop = 0.8, c1 = 0xF8D040, c2 = 0xF0E090, shape = "wedge",   size = 0.22 },
  { id = "bread",     name = "bread",     chop = 0.8, c1 = 0xC88838, c2 = 0xF0D8A0, shape = "loaf",    size = 0.26 },
  { id = "rice",      name = "rice",                  c1 = 0xF0F0E8, c2 = 0xC8B890, shape = "sack",    size = 0.24 },
  { id = "pasta",     name = "pasta",                 c1 = 0xF0D060, c2 = 0x9060C0, shape = "bundle",  size = 0.26 },
  { id = "egg",       name = "egg",                   c1 = 0xF8F4E8, c2 = 0xF8C030, shape = "egg",     size = 0.16 },
  { id = "flour",     name = "flour",     chop = 1.0, c1 = 0xF4F0E4, c2 = 0xB89868, shape = "sack",    size = 0.24 },
  { id = "milk",      name = "milk",                  c1 = 0xF8F8FF, c2 = 0x4080E0, shape = "bottle",  size = 0.26 },
  { id = "butter",    name = "butter",                c1 = 0xF8E070, c2 = 0xF8F0C0, shape = "block",   size = 0.18 },
  { id = "chicken",   name = "chicken",   chop = 1.4, c1 = 0xF0B8A0, c2 = 0xF8E8D8, shape = "drum",    size = 0.24 },
  { id = "beef",      name = "beef",      chop = 1.6, c1 = 0xC83040, c2 = 0xF0D0D0, shape = "steak",   size = 0.26 },
  { id = "fish",      name = "fish",      chop = 1.4, c1 = 0x6C9CC8, c2 = 0xE0E8F0, shape = "fish",    size = 0.3 },
  { id = "shrimp",    name = "shrimp",    chop = 0.8, c1 = 0xF88868, c2 = 0xF8C8B0, shape = "curl",    size = 0.18 },
  { id = "sausage",   name = "sausage",   chop = 0.8, c1 = 0xB84830, c2 = 0xE88868, shape = "long",    size = 0.24 },
  { id = "herbs",     name = "herbs",     chop = 0.6, c1 = 0x30B040, c2 = 0x80E060, shape = "sprig",   size = 0.2 },
  { id = "fruit",     name = "fruit",     chop = 1.0, c1 = 0xF86890, c2 = 0x60C040, shape = "ball",    size = 0.2 },
  { id = "chocolate", name = "chocolate", chop = 0.8, c1 = 0x6A3A20, c2 = 0x9A6040, shape = "block",   size = 0.2 },
}

Data.ING = {}
for i, g in ipairs(Data.ING_LIST) do
  g.icon = i - 1
  g.n = i
  Data.ING[g.id] = g
end

-- how a chopped piece looks (a few cubes or slices of the inner colour)
Data.CHOP_LOOK = {
  tomato = "slices", onion = "rings", potato = "cubes", carrot = "slices", mushroom = "slices",
  lettuce = "leaves", cucumber = "slices", cheese = "cubes", bread = "slices", flour = "dough",
  chicken = "cubes", beef = "patty", fish = "fillet", shrimp = "cubes", sausage = "slices",
  herbs = "leaves", fruit = "cubes", chocolate = "cubes",
}
