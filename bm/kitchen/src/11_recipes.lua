-- Food keys. Every item that can be carried, cooked or plated has a key:
--   "tomato"                 a raw ingredient
--   "tomato/c"               chopped
--   "boil(a,b,c)"            what comes out of a pot with a, b and c in it
--   "fry(x)" "bake(...)" "blend(...)"   pan, oven, blender
-- Keys nest ("fry(blend(egg,flour/c,milk))"); the parts inside a process are
-- always sorted, so a key names one dish whatever the order of the steps.
-- A recipe is the list of parts that must be on the plate.

-- Parses a key into { ing = id, chopped = bool } or { proc = p, kids = {...} }.
local cache = {}
local function parse(key)
  local c = cache[key]
  if c then return c end
  local p, inner = key:match("^(%a+)%((.*)%)$")
  if p then
    local kids, depth, start = {}, 0, 1
    for i = 1, #inner do
      local ch = sub(inner, i, i)
      if ch == "(" then depth = depth + 1
      elseif ch == ")" then depth = depth - 1
      elseif ch == "," and depth == 0 then
        kids[#kids + 1] = sub(inner, start, i - 1)
        start = i + 1
      end
    end
    kids[#kids + 1] = sub(inner, start)
    c = { proc = p, kids = kids }
  else
    local id, cut = key:match("^(%a+)(/?c?)$")
    assert(id and Data.ING[id], "unknown food key " .. key)
    c = { ing = id, chopped = cut == "/c" }
  end
  c.key = key
  cache[key] = c
  return c
end
Food.parse = parse

-- The canonical form of a key: processes with their parts sorted.
local function canon(key)
  local k = parse(key)
  if not k.proc then return key end
  local kids = {}
  for i, s in ipairs(k.kids) do kids[i] = canon(s) end
  sort(kids)
  return k.proc .. "(" .. concat(kids, ",") .. ")"
end
Food.canon = canon

function Food.mix_key(proc, items)
  local kids = {}
  for i, s in ipairs(items) do kids[i] = s end
  sort(kids)
  return proc .. "(" .. concat(kids, ",") .. ")"
end

-- how much work a key takes (sets price and patience)
local function work(key)
  local k = parse(key)
  if k.ing then return k.chopped and 2 or 1 end
  local n = 2
  for _, s in ipairs(k.kids) do n = n + work(s) end
  return n
end
Food.work = work

-- the ingredient that best represents a key (icons, colour)
function Food.main_ing(key)
  local k = parse(key)
  if k.ing then return k.ing end
  local best, bw = nil, -1
  for _, s in ipairs(k.kids) do
    local w = work(s)
    if w > bw then best, bw = Food.main_ing(s), w end
  end
  return best
end

-- maps a food key to its 16x16 icon index on the sheet
function Food.icon_num(key)
  if not key then return nil end
  if key == "burnt" then return Hud.IC.burnt or 72 end
  if key == "ext" then return Hud.IC.ext or 73 end
  local k = Food.parse(key)
  if k.ing then
    local g = Data.ING[k.ing]
    if not g then return nil end
    return k.chopped and (g.icon + 32) or g.icon
  end
  if k.proc and #k.kids == 1 and Food.parse(k.kids[1]).ing then
    local kk = Food.parse(k.kids[1])
    local g = Data.ING[kk.ing]
    if g then return g.icon + 32 end
  end
  if k.proc == "boil" then return Hud.IC.dish_soup or Hud.IC.bowl or 71
  elseif k.proc == "fry" then return Hud.IC.dish_fry or Hud.IC.pan or 66
  elseif k.proc == "bake" then return Hud.IC.dish_bake or Hud.IC.oven or 67
  elseif k.proc == "blend" then return Hud.IC.dish_smoothie or Hud.IC.blender or 68 end
  local main = Food.main_ing(key)
  if main and Data.ING[main] then
    return Data.ING[main].icon + 32
  end
  return Hud.IC.plate or 69
end

-- maps a finished recipe to its dedicated plated dish sprite
function Food.dish_icon_for_recipe(r)
  if not r then return nil end
  local id = r.id
  if id:find("soup") then return Hud.IC.dish_soup or 108
  elseif id:find("salad") then return Hud.IC.dish_salad or 109
  elseif id:find("burger") or id:find("sand") or id:find("bun") or id:find("sizzle") or id:find("toast") then
    return Hud.IC.dish_burger or 110
  elseif id:find("pasta") then return Hud.IC.dish_pasta or 111
  elseif id:find("bake") or id:find("pot") or id:find("pizza") or id:find("pie") or id:find("tart") then
    return Hud.IC.dish_bake or 112
  elseif id:find("smoothie") or id:find("blend") or id:find("shake") then
    return Hud.IC.dish_smoothie or 113
  elseif id:find("rice") or id:find("curry") then
    return Hud.IC.dish_rice or 115
  else
    return Hud.IC.dish_fry or 114
  end
end

local sig_to_dish = {}
function Food.init_dish_icons()
  for _, r in ipairs(Data.RECIPES) do
    local sig = Food.plate_sig(r.parts)
    sig_to_dish[sig] = Food.dish_icon_for_recipe(r)
  end
end

function Food.dish_icon(sig)
  return sig_to_dish[sig]
end

---------------------------------------------------------------- recipes

-- tier: 1 easy .. 4 very hard; world: where it first appears
Data.RECIPES = {
  -- tier 1
  { id = "tomato_soup",  name = "Tomato Soup",     tier = 1, world = 1, parts = { "boil(tomato/c,tomato/c,tomato/c)" } },
  { id = "onion_soup",   name = "Onion Soup",      tier = 1, world = 1, parts = { "boil(onion/c,onion/c,onion/c)" } },
  { id = "mush_soup",    name = "Mushroom Soup",   tier = 1, world = 1, parts = { "boil(mushroom/c,mushroom/c,onion/c)" } },
  { id = "garden_soup",  name = "Garden Soup",     tier = 1, world = 2, parts = { "boil(carrot/c,onion/c,potato/c)" } },
  { id = "green_salad",  name = "Green Salad",     tier = 1, world = 1, parts = { "lettuce/c", "cucumber/c" } },
  { id = "sunny_salad",  name = "Sunny Salad",     tier = 1, world = 1, parts = { "lettuce/c", "tomato/c" } },
  { id = "fruit_salad",  name = "Fruit Salad",     tier = 1, world = 4, parts = { "fruit/c", "fruit/c" } },
  { id = "scrambled",    name = "Scrambled Eggs",  tier = 1, world = 1, parts = { "fry(egg)", "herbs/c" } },
  { id = "sizzle_dog",   name = "Sizzle Sausage",  tier = 1, world = 1, parts = { "fry(sausage)", "bread/c" } },
  { id = "baked_potato", name = "Baked Potato",    tier = 1, world = 2, parts = { "bake(potato)", "butter" } },
  { id = "cheese_toast", name = "Cheese Toast",    tier = 1, world = 2, parts = { "bake(bread/c,cheese/c)" } },
  { id = "mush_toast",   name = "Mushroom Toast",  tier = 1, world = 2, parts = { "bake(bread/c,mushroom/c)" } },
  { id = "butter_pasta", name = "Butter Pasta",    tier = 1, world = 1, parts = { "boil(pasta)", "butter" } },
  { id = "herb_rice",    name = "Herb Rice",       tier = 1, world = 3, parts = { "boil(rice)", "herbs/c" } },
  -- tier 2
  { id = "mush_pasta",   name = "Mushroom Pasta",  tier = 2, world = 2, parts = { "boil(pasta)", "fry(mushroom/c)" } },
  { id = "red_pasta",    name = "Red Pasta",       tier = 2, world = 2, parts = { "boil(pasta,tomato/c)", "cheese/c" } },
  { id = "chick_sand",   name = "Chicken Sandwich", tier = 2, world = 2, parts = { "bread/c", "fry(chicken/c)", "lettuce/c" } },
  { id = "fish_bun",     name = "Fish Bun",        tier = 2, world = 3, parts = { "bread/c", "fry(fish/c)", "lettuce/c" } },
  { id = "cheeseburger", name = "Cheese Burger",   tier = 2, world = 2, parts = { "bread/c", "fry(beef/c)", "cheese/c" } },
  { id = "crispy_burger", name = "Crispy Burger",  tier = 2, world = 2, parts = { "bread/c", "fry(chicken/c)", "tomato/c" } },
  { id = "veg_curry",    name = "Veggie Curry",    tier = 2, world = 3, parts = { "boil(rice)", "boil(carrot/c,onion/c,potato/c)" } },
  { id = "fried_rice",   name = "Fried Rice",      tier = 2, world = 3, parts = { "boil(rice)", "fry(egg)", "carrot/c" } },
  { id = "shrimp_rice",  name = "Shrimp Rice",     tier = 2, world = 3, parts = { "boil(rice)", "fry(shrimp/c)" } },
  { id = "stuffed_pot",  name = "Stuffed Potato",  tier = 2, world = 2, parts = { "bake(cheese/c,mushroom/c,potato)" } },
  { id = "garden_pizza", name = "Garden Pizza",    tier = 2, world = 2, parts = { "bake(cheese/c,flour/c,onion/c,tomato/c)" } },
  { id = "mush_pizza",   name = "Mushroom Pizza",  tier = 2, world = 2, parts = { "bake(cheese/c,flour/c,mushroom/c,tomato/c)" } },
  { id = "sunny_pizza",  name = "Sunny Pizza",     tier = 2, world = 2, parts = { "bake(cheese/c,flour/c,tomato/c)", "herbs/c" } },
  { id = "smoothie",     name = "Berry Smoothie",  tier = 2, world = 4, parts = { "blend(fruit/c,fruit/c,milk)" } },
  { id = "choco_shake",  name = "Choco Shake",     tier = 2, world = 4, parts = { "blend(chocolate/c,milk)" } },
  { id = "omelette",     name = "Cheese Omelette", tier = 2, world = 4, parts = { "fry(blend(egg,milk))", "cheese/c" } },
  -- tier 3
  { id = "harbor_pasta", name = "Harbor Pasta",    tier = 3, world = 3, parts = { "boil(pasta)", "fry(shrimp/c)", "fry(fish/c)" } },
  { id = "tower_burger", name = "Tower Burger",    tier = 3, world = 3, parts = { "bread/c", "fry(beef/c)", "cheese/c", "lettuce/c", "tomato/c" } },
  { id = "deluxe_pizza", name = "Deluxe Pizza",    tier = 3, world = 5, parts = { "bake(cheese/c,flour/c,sausage/c,tomato/c)", "fry(mushroom/c)" } },
  { id = "chick_curry",  name = "Chicken Curry",   tier = 3, world = 3, parts = { "boil(rice)", "boil(chicken/c,onion/c,potato/c)" } },
  { id = "shrimp_curry", name = "Shrimp Curry",    tier = 3, world = 3, parts = { "boil(rice)", "boil(onion/c,shrimp/c,tomato/c)" } },
  { id = "stuffed_fish", name = "Stuffed Fish",    tier = 3, world = 3, parts = { "bake(butter,fish,herbs/c)", "lettuce/c" } },
  { id = "harbor_roll",  name = "Harbor Roll",     tier = 3, world = 3, parts = { "boil(rice)", "fish/c", "cucumber/c" } },
  { id = "grill_plate",  name = "Grill Platter",   tier = 3, world = 5, parts = { "fry(beef/c)", "fry(chicken/c)", "fry(sausage)" } },
  { id = "sea_plate",    name = "Sea Platter",     tier = 3, world = 3, parts = { "fry(fish/c)", "fry(shrimp/c)", "lettuce/c", "herbs/c" } },
  { id = "rice_bowl",    name = "Big Rice Bowl",   tier = 3, world = 5, parts = { "boil(rice)", "fry(beef/c)", "fry(egg)", "carrot/c" } },
  { id = "pancakes",     name = "Pancake Stack",   tier = 3, world = 4, parts = { "fry(blend(egg,flour/c,milk))", "fruit/c" } },
  { id = "fondue",       name = "Mountain Fondue", tier = 3, world = 4, parts = { "boil(cheese/c,cheese/c,milk)", "bread/c" } },
  { id = "hot_cocoa",    name = "Hot Cocoa",       tier = 3, world = 4, parts = { "boil(chocolate/c,milk)", "fruit/c" } },
  -- tier 4
  { id = "choco_cake",   name = "Chocolate Cake",  tier = 4, world = 5, parts = { "bake(blend(chocolate/c,egg,flour/c))", "fruit/c" } },
  { id = "fruit_tart",   name = "Fruit Tart",      tier = 4, world = 5, parts = { "bake(blend(butter,egg,flour/c),fruit/c)" } },
  { id = "royal_burger", name = "Royal Burger",    tier = 4, world = 6, parts = { "bread/c", "fry(beef/c)", "cheese/c", "fry(egg)", "lettuce/c", "tomato/c" } },
  { id = "paella",       name = "Harbor Paella",   tier = 4, world = 6, parts = { "boil(rice,shrimp/c,tomato/c)", "fry(fish/c)", "herbs/c" } },
  { id = "castle_stew",  name = "Castle Stew",     tier = 4, world = 5, parts = { "boil(beef/c,carrot/c,potato/c)", "bread/c", "herbs/c" } },
  { id = "carbonara",    name = "Chaos Carbonara", tier = 4, world = 6, parts = { "boil(pasta)", "fry(blend(cheese/c,egg))", "fry(sausage)" } },
  { id = "lasagna",      name = "Lasagna",         tier = 4, world = 6, parts = { "bake(beef/c,boil(pasta),cheese/c,tomato/c)" } },
  { id = "chaos_feast",  name = "Chaos Feast",     tier = 4, world = 6, parts = { "bake(cheese/c,flour/c,mushroom/c,tomato/c)", "fry(chicken/c)", "blend(fruit/c,milk)" } },
}

Data.RECIPE = {}

-- What a recipe needs: ingredients, stations, and its signature (the
-- sorted parts, compared with a plate at the serving window).
local function needs(key, ing, st)
  local k = parse(key)
  if k.ing then
    ing[k.ing] = true
    if k.chopped then st.chop = true end
    return
  end
  st[k.proc] = true
  for _, s in ipairs(k.kids) do needs(s, ing, st) end
end

local function steps_of(key, out)
  local k = parse(key)
  if k.ing then
    if k.chopped then out.prep = out.prep + 1 end
    return
  end
  out.cook = out.cook + 1
  for _, s in ipairs(k.kids) do steps_of(s, out) end
end

for i, r in ipairs(Data.RECIPES) do
  r.n = i
  local parts = {}
  for j, p in ipairs(r.parts) do parts[j] = canon(p) end
  sort(parts)
  r.parts = parts
  r.sig = concat(parts, "|")
  r.ing, r.st = {}, {}
  local steps = { prep = 0, cook = 0 }
  local w = 1
  for _, p in ipairs(parts) do
    needs(p, r.ing, r.st)
    steps_of(p, steps)
    w = w + work(p)
  end
  r.work = w
  -- the numbers the brief asks each recipe to have
  r.preparation_steps, r.cooking_steps, r.assembly_steps = steps.prep, steps.cook, #parts
  r.value = r.value or (floor((10 + w * 5) / 5 + 0.5) * 5)
  r.patience = r.patience or (50 + w * 6)
  local list = {}
  for id in pairs(r.ing) do list[#list + 1] = id end
  sort(list, function(a, b) return Data.ING[a].n < Data.ING[b].n end)
  r.ingredients = list
  Data.RECIPE[r.id] = r
end
