-- The four chefs. Each one looks, moves and works differently; all four are
-- useful. speed/chop/cook multiply the base values; dash and throw say if
-- the B and Y buttons do anything.
Data.CHEFS = {
  { id = "basil", name = "BASIL", role = "The Pro", motto = "Determined professional",
    speed = 1.00, chop = 1.00, cook = 1.00, dash = true, throw = true,
    height = 1.35, accent = 0xE03A30, portrait = 0,
    trait = "does everything well" },
  { id = "bun", name = "BUN", role = "The Jolly One", motto = "Cheerful, a bit chaotic",
    speed = 0.75, chop = 1.25, cook = 1.00, dash = false, throw = true,
    height = 1.05, accent = 0xF0A020, portrait = 1,
    trait = "chops fast, cannot dash" },
  { id = "noodle", name = "NOODLE", role = "The Sprinter", motto = "Energetic perfectionist",
    speed = 1.25, chop = 1.00, cook = 1.00, dash = true, throw = true,
    height = 1.65, accent = 0x30B060, portrait = 2,
    trait = "the fastest runner" },
  { id = "pepper", name = "PEPPER", role = "The Tiny Boss", motto = "Serious little powerhouse",
    speed = 1.00, chop = 1.15, cook = 1.00, dash = true, throw = false,
    height = 0.92, accent = 0x3A70E0, portrait = 3,
    trait = "chops fast, cannot throw" },
}

Data.BASE_SPEED = 4.2       -- tiles per second
Data.DASH_SPEED = 13        -- during a dash
Data.DASH_TIME = 0.16
Data.DASH_COOL = 0.55
Data.THROW_RANGE = 4.2      -- tiles
