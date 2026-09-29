-- The robot's configurations and its moves.
--
-- Weight matters: the heavy armour takes more punishment and hits harder,
-- but walks, jumps and dashes less; only the light one can jump again in
-- the air. The sword cuts hard and costs energy; the guns shoot from afar
-- and heat up.

Data.ARMOR = {
  light = { name = "LIGHT", walk = 2.8, back = 2.2, jump = 10.6, air_jumps = 1, dash = 9.5, dash_t = 15,
            armor = 240, absorb = 0.64, regen = 0.4, dmg = 1.05, dash_cost = 16,
            stats = { speed = 5, jump = 5, armor = 2, power = 3 }, desc = "FAST, JUMPS TWICE" },
  heavy = { name = "HEAVY", walk = 1.9, back = 1.5, jump = 8.7, air_jumps = 0, dash = 7.2, dash_t = 12,
            armor = 400, absorb = 0.75, regen = 0.26, dmg = 1.1, dash_cost = 22,
            stats = { speed = 2, jump = 2, armor = 5, power = 5 }, desc = "SLOW, TOUGH, STRONG" },
}
Data.ARMORS = { "light", "heavy" }

Data.WEAPON = {
  sword = { name = "SWORD", desc = "BIG SLASH, 30 ENERGY", special = "slash", energy = 30, reach = 3, power = 5 },
  guns = { name = "GUNS", desc = "6 SHOTS, HEATS UP", special = "fire", heat = 9, reach = 5, power = 2 },
}
Data.WEAPONS = { "sword", "guns" }

-- Moves: anim, ticks of each frame, damage, hit stun, block stun, push,
-- level (mid: any guard; low: crouching guard; over: standing guard),
-- rank (a move cancels into a higher rank when it connects), and extras:
-- kd knocks down, launch sends up for a juggle.
Data.MOVE = {
  lp = { anim = "lp", ticks = { 3, 5, 5 }, dmg = 38, stun = 14, bstun = 10, push = 3, level = "mid", rank = 1 },
  hp = { anim = "hp", ticks = { 6, 3, 4, 12 }, dmg = 92, stun = 20, bstun = 14, push = 6, level = "mid", rank = 3 },
  lk = { anim = "lk", ticks = { 4, 6, 7 }, dmg = 50, stun = 15, bstun = 11, push = 4, level = "mid", rank = 2 },
  hk = { anim = "hk", ticks = { 7, 4, 4, 14 }, dmg = 110, stun = 22, bstun = 15, push = 8, level = "mid", rank = 4,
         kd = true },
  clp = { anim = "clp", ticks = { 3, 6 }, dmg = 34, stun = 13, bstun = 9, push = 3, level = "mid", rank = 1, crouch = true },
  chp = { anim = "chp", ticks = { 5, 5, 8 }, dmg = 88, stun = 24, bstun = 14, push = 3, level = "mid", rank = 3,
          crouch = true, launch = true },
  clk = { anim = "clk", ticks = { 4, 7 }, dmg = 40, stun = 14, bstun = 10, push = 3, level = "low", rank = 2, crouch = true },
  chk = { anim = "chk", ticks = { 6, 7, 16 }, dmg = 84, stun = 20, bstun = 14, push = 5, level = "low", rank = 4,
          crouch = true, kd = true },
  jhp = { anim = "jhp", ticks = { 4, 30 }, dmg = 70, stun = 18, bstun = 12, push = 4, level = "over", rank = 3, air = true },
  jhk = { anim = "jhk", ticks = { 4, 30 }, dmg = 80, stun = 19, bstun = 13, push = 5, level = "over", rank = 4, air = true },
  -- the weapons
  slash = { anim = "slash", ticks = { 7, 8, 4, 5, 16 }, dmg = 175, stun = 26, bstun = 18, push = 10, level = "mid",
            rank = 9, kd = true, special = true, armor_k = 1.5 },
  fire = { anim = "fire", ticks = { 5, 5, 5, 5, 5, 5, 5, 5 }, loop = true, shots = 6, special = true, rank = 9 },
}
-- the bullet of the guns
Data.BULLET = { speed = 14, dmg = 24, stun = 9, bstun = 6, push = 2, level = "mid" }
