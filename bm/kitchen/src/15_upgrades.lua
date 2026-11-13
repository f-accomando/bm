-- Endless mode: the upgrades the register offers. Data only; 36_endless.lua
-- applies them. Fields:
--   cat    EQUIPMENT, PANTRY, RECIPE, SPEED, CAPACITY, SAFETY, LOGISTICS
--   tier   1..4 (when it starts to be offered)       cost  price at tier
--   max    how many times it can be bought (default 1); each time costs 35% more
--   st     a station placed in the kitchen            n     how many
--   ing    an ingredient crate placed in the pantry
--   mult   multipliers added to run.mult               flag  a run flag set
--   icon   sheet icon (Hud.IC name or an ingredient)
-- Ingredient crates and new recipes are also generated from the recipe list
-- (see End.offers), so every recipe can be reached.

Data.UPGRADES = {
  -- equipment: placed next to the stations like it
  { id = "pot", cat = "EQUIPMENT", name = "Extra Stove", desc = "a pot for soups, rice, pasta", tier = 1, cost = 45, max = 3, st = "pot", icon = "pot" },
  { id = "pan", cat = "EQUIPMENT", name = "Frying Station", desc = "a pan to fry things", tier = 1, cost = 50, max = 3, st = "pan", icon = "pan" },
  { id = "board", cat = "EQUIPMENT", name = "Cutting Board", desc = "one more place to chop", tier = 1, cost = 30, max = 3, st = "board", icon = "knife" },
  { id = "counter", cat = "EQUIPMENT", name = "Two Counters", desc = "room to put things down", tier = 1, cost = 20, max = 4, st = "counter", n = 2, icon = "box" },
  { id = "oven", cat = "EQUIPMENT", name = "Oven", desc = "bakes pizza, toast, cakes", tier = 2, cost = 90, max = 2, st = "oven", icon = "oven" },
  { id = "blender", cat = "EQUIPMENT", name = "Blender", desc = "smoothies and batters", tier = 2, cost = 80, max = 2, st = "blender", icon = "blender" },
  { id = "serve", cat = "EQUIPMENT", name = "Second Window", desc = "serve from two places", tier = 3, cost = 140, max = 1, st = "serve", icon = "bell" },
  { id = "trash", cat = "EQUIPMENT", name = "Another Bin", desc = "a trash bin closer by", tier = 2, cost = 35, max = 1, st = "trash", icon = "burnt" },
  -- speed
  { id = "knives", cat = "SPEED", name = "Sharp Knives", desc = "chop 15% faster", tier = 1, cost = 40, max = 3, mult = { chop = 0.15 }, icon = "knife2" },
  { id = "flame", cat = "SPEED", name = "Hot Flames", desc = "cook 15% faster", tier = 1, cost = 50, max = 3, mult = { cook = 0.15 }, icon = "fire" },
  { id = "shoes", cat = "SPEED", name = "Running Shoes", desc = "everyone moves 8% faster", tier = 2, cost = 70, max = 2, mult = { move = 0.08 }, icon = "speed" },
  { id = "runner", cat = "SPEED", name = "Dish Runner", desc = "plates come back faster", tier = 1, cost = 35, max = 2, flag = "fast_plates", icon = "plate" },
  -- capacity
  { id = "plates", cat = "CAPACITY", name = "Plate Rack", desc = "two more clean plates", tier = 1, cost = 30, max = 4, plates = 2, icon = "plate" },
  { id = "queue", cat = "CAPACITY", name = "Bigger Dining Room", desc = "more orders, more money", tier = 3, cost = 120, max = 2, mult = { money = 0.2 }, orders = 1, icon = "coin" },
  -- safety
  { id = "simmer", cat = "SAFETY", name = "Slow Simmer", desc = "food takes 40% longer to burn", tier = 2, cost = 60, max = 2, mult = { burn = 0.4 }, icon = "clock" },
  { id = "sprinkler", cat = "SAFETY", name = "Sprinklers", desc = "fires put themselves out", tier = 2, cost = 90, max = 1, flag = "auto_ext", icon = "drop" },
  { id = "chairs", cat = "SAFETY", name = "Comfy Chairs", desc = "customers wait 15% longer", tier = 2, cost = 75, max = 2, patience = 0.15, icon = "heart2" },
  { id = "cat", cat = "SAFETY", name = "Kitchen Cat", desc = "no more rats or ducks", tier = 3, cost = 110, max = 1, flag = "no_pests", icon = "shield" },
  -- logistics
  { id = "arm", cat = "LOGISTICS", name = "Strong Arm", desc = "throw 25% further", tier = 1, cost = 30, max = 2, mult = { throw = 0.25 }, icon = "up" },
  { id = "tips", cat = "LOGISTICS", name = "Tip Jar", desc = "15% more money per dish", tier = 2, cost = 80, max = 3, mult = { money = 0.15 }, icon = "coin" },
  { id = "heart", cat = "LOGISTICS", name = "Good Review", desc = "one heart back", tier = 1, cost = 60, max = 99, heal = 1, icon = "heart" },
}

Data.UPGRADE = {}
for _, u in ipairs(Data.UPGRADES) do Data.UPGRADE[u.id] = u end

-- the tier an ingredient crate belongs to
Data.ING_TIER = {
  tomato = 1, onion = 1, mushroom = 1, lettuce = 1, cucumber = 1, potato = 1, carrot = 1,
  bread = 1, pasta = 1, egg = 1, herbs = 1, butter = 1,
  cheese = 2, rice = 2, sausage = 2, chicken = 2, flour = 2,
  beef = 3, fish = 3, shrimp = 3, milk = 3, fruit = 3,
  chocolate = 4,
}
-- prices by tier
Data.TIER_COST = { 25, 70, 130, 230 }

-- the kitchen endless mode starts with
Data.ENDLESS = {
  id = "endless", world = 1, name = "Endless Kitchen", time = 0, plates = 4,
  recipes = { "tomato_soup", "onion_soup" },
  layout = {
    "################",
    "#CtCoCm........#",
    "#..............#",
    "#..............#",
    "#.1....$.....2.W",
    "#..............#",
    "#.3..........4.#",
    "#..............#",
    "CBCPCDCT........",
  },
}
