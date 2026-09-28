-- Worlds and stages. A layout is a list of rows, the first one at the back
-- of the kitchen (top of the screen). Characters: see 12_stations.lua.
-- Stage fields:
--   time      seconds of service
--   recipes   the dishes that can be ordered
--   wash      served plates come back dirty (to R) and must be washed (S)
--   plates    clean plates at the start
--   plats     moving platforms { x, y, w, h (layout cells), dx, dy, period }
--   haz       hazards { kind = "vent"/"door"/"shelf"/"cart"/"leak"..., ... }
--   dis       disasters { kind, at (seconds), every (seconds, repeats) }
--   tip       one line shown before the stage

Data.WORLDS = {
  { name = "LITTLE DINER", tag = "Where it all begins",
    floor1 = 0xEADCC0, floor2 = 0xD6C29E, wall = 0xD86A50, wall2 = 0xF0E6D0, sky = 0x6AB8D8,
    base = 0x8A5A38, mech = "chop, cook, plate, serve", music = 1 },
  { name = "MARKET DISTRICT", tag = "Busy stalls, rolling carts",
    floor1 = 0xD8D0C8, floor2 = 0xB8ACA0, wall = 0x3E8A5A, wall2 = 0xE8E0C8, sky = 0x88C0E8,
    base = 0x6A5A4A, mech = "conveyor belts, market carts, rats", music = 2 },
  { name = "HARBOR", tag = "Kitchens afloat",
    floor1 = 0xC89A6A, floor2 = 0xAE8250, wall = 0x3A6AA0, wall2 = 0xE8F0F8, sky = 0x5AA8E0,
    base = 0x2A5A8A, water = 0x2E7AC0, mech = "moving decks, ducks", music = 3 },
  { name = "MOUNTAIN RESORT", tag = "Mind the ice",
    floor1 = 0xE8F0F8, floor2 = 0xC8D8E8, wall = 0x8A5A3A, wall2 = 0xF8F8F8, sky = 0x9AC8F0,
    base = 0x6A7A8A, water = 0xA8D8F0, mech = "ice, leaks, steam vents", music = 4 },
  { name = "OLD CASTLE", tag = "Something moves in the dark",
    floor1 = 0x8A8478, floor2 = 0x70695E, wall = 0x5A4E6A, wall2 = 0xB0A490, sky = 0x2A2440,
    base = 0x3A3446, water = 0x302850, mech = "doors, ghosts, haunted pans", music = 5 },
  { name = "CHAOS DISTRICT", tag = "Everything, all at once",
    floor1 = 0xE8C8E8, floor2 = 0xC8A0D0, wall = 0xE04890, wall2 = 0xF8E8A0, sky = 0x402060,
    base = 0x502860, water = 0x6040C0, mech = "all of it, and a tornado", music = 6 },
}

Data.STAGES = {
  ------------------------------------------------------------ world 1
  { id = "1-1", world = 1, name = "Opening Day", time = 150, plates = 4,
    recipes = { "tomato_soup" },
    tip = "Chop tomatoes (X), 3 in a pot, then plate the soup and serve it!",
    layout = {
      "##############",
      "#CCtCBCBCPCPC#",
      "#............#",
      "#.1..........W",
      "#.....CC...2.#",
      "#.3........4.#",
      "#............#",
      "CCCTCCDCCCCCCC",
    } },
  { id = "1-2", world = 1, name = "Soup of the Day", time = 180, plates = 4,
    recipes = { "tomato_soup", "onion_soup" },
    tip = "Two soups now. Keep an eye on the pots: they burn!",
    layout = {
      "##############",
      "#CtCoCBCB#PCP#",
      "#.........#..#",
      "#.1...2...#..W",
      "#............#",
      "#...3...4....#",
      "#.........#..#",
      "CCDCCTCCCC#CCC",
    } },
  { id = "1-3", world = 1, name = "Suds and Salads", time = 180, plates = 3, wash = true,
    recipes = { "green_salad", "sunny_salad", "tomato_soup" },
    tip = "Plates come back dirty: wash them in the sink (X).",
    layout = {
      "##############",
      "#CClCuCtCBCBC#",
      "#............#",
      "#.1.CCCPCP.2.W",
      "#............#",
      "#.3........4.#",
      "#............#",
      "CRSCCDCCCTCCCC",
    } },
  { id = "1-4", world = 1, name = "Sizzle School", time = 180, plates = 4, wash = true,
    recipes = { "scrambled", "sizzle_dog", "butter_pasta" },
    tip = "Pans fry one thing at a time. Take it off before it burns!",
    layout = {
      "##############",
      "#CeCgCnCbCyCa#",
      "#............#",
      "#.1..CBCB..2.W",
      "#............#",
      "#.3..FCFCP.4.#",
      "#............#",
      "CCRSCCCDCCTCCC",
    } },
  { id = "1-5", world = 1, name = "Over the Counter", time = 200, plates = 4, wash = true,
    recipes = { "tomato_soup", "sunny_salad", "sizzle_dog" },
    tip = "The counter splits the kitchen: throw (Y) or pass things across!",
    layout = {
      "##############",
      "#CtClCgCb#PCF#",
      "#......C.....#",
      "#.1....C...2.W",
      "#............#",
      "#.3....C...4.#",
      "#......C.....#",
      "CBCBRSCCCCDCTC",
    } },
}

Data.STAGE = {}
for i, s in ipairs(Data.STAGES) do
  s.n = i
  Data.STAGE[s.id] = s
end
