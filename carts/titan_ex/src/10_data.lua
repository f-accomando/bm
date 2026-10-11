-- Titan Clash EX: Mecha Frame Archetypes, Armor, Weapons, Moves and Combat Data.

-- Playable Mecha Frame Chassis
Data.CHASSIS = {
  vanguard = {
    id = "vanguard", pilot_id = 1,
    name = "TC-01 VANGUARD",
    archetype = "BALANCED TACTICAL STRIKER",
    desc = "All-rounder mecha with vibro-blade and high-velocity booster.",
    palette = 1,
    walk = 2.8, back = 2.2, jump = 10.6, air_jumps = 1, dash = 9.5, dash_t = 15,
    armor_base = 360, absorb = 0.68, energy_regen = 0.38, dmg_mult = 1.05,
    super_name = "V-BURST GIGA SABER",
    stats = { speed = 3, armor = 3, power = 3, energy = 4, range = 3 },
  },
  valkyrie = {
    id = "valkyrie", pilot_id = 2,
    name = "TC-02 VALKYRIE",
    archetype = "AERIAL HIGH-MOBILITY ACE",
    desc = "Light aero frame with wing thrusters, twin sabers and air dashes.",
    palette = 2,
    walk = 3.4, back = 2.7, jump = 11.8, air_jumps = 2, dash = 11.2, dash_t = 18,
    armor_base = 260, absorb = 0.60, energy_regen = 0.52, dmg_mult = 0.98,
    super_name = "METEOR WING SALVO",
    stats = { speed = 5, armor = 2, power = 3, energy = 5, range = 4 },
  },
  behemoth = {
    id = "behemoth", pilot_id = 3,
    name = "TC-03 BEHEMOTH",
    archetype = "HEAVY SIEGE FORTRESS",
    desc = "Colossal fortress titan with crushing hydraulic fists and siege cannon.",
    palette = 3,
    walk = 1.9, back = 1.5, jump = 8.8, air_jumps = 0, dash = 7.4, dash_t = 12,
    armor_base = 500, absorb = 0.80, energy_regen = 0.28, dmg_mult = 1.20,
    super_name = "TITAN QUAKE OVERDRIVE",
    stats = { speed = 2, armor = 5, power = 5, energy = 2, range = 5 },
  },
  boss = {
    id = "boss", pilot_id = 4,
    name = "DREADNOUGHT X",
    archetype = "APOCALYPSE WAR TITAN",
    desc = "Ancient forbidden battle machine commanded by General Vex.",
    palette = 4,
    walk = 2.4, back = 1.8, jump = 10.0, air_jumps = 1, dash = 9.0, dash_t = 14,
    armor_base = 700, absorb = 0.82, energy_regen = 0.45, dmg_mult = 1.30,
    super_name = "APOCALYPSE ERADICATOR",
    stats = { speed = 4, armor = 5, power = 5, energy = 5, range = 5 },
  },
}
Data.ROSTER = { "vanguard", "valkyrie", "behemoth" }

-- Armor Configurations
Data.ARMOR = {
  light = {
    name = "LIGHT", desc = "HIGH SPEED, DOUBLE AIR JUMP",
    armor_mod = 0.75, absorb_mod = -0.06, speed_mod = 1.20, air_jump_mod = 1,
    cost = 14, stats = { speed = 5, armor = 2 }
  },
  medium = {
    name = "TACTICAL", desc = "OPTIMIZED DEFENSE & THRUST",
    armor_mod = 1.00, absorb_mod = 0.00, speed_mod = 1.00, air_jump_mod = 0,
    cost = 18, stats = { speed = 3, armor = 4 }
  },
  heavy = {
    name = "HEAVY", desc = "MAX ABSORPTION, SUPER ARMOR POISE",
    armor_mod = 1.35, absorb_mod = 0.10, speed_mod = 0.85, air_jump_mod = -1,
    cost = 24, stats = { speed = 2, armor = 5 }
  },
}
Data.ARMORS = { "light", "medium", "heavy" }

-- Weapon Configurations
Data.WEAPON = {
  sword = {
    name = "V-BLADE", desc = "HIGH-FREQUENCY BLADE (ARMOR BREAKER)",
    special = "slash", energy = 28, reach = 3.5, power = 5,
    armor_break_mult = 1.5,
  },
  guns = {
    name = "GATLING", desc = "TWIN VULCANS (6 SHOTS, HEATS UP)",
    special = "fire", heat = 8, reach = 6.0, power = 3,
    shots = 6,
  },
  cannon = {
    name = "CANNON", desc = "SIEGE PLASMA MORTAR (HIGH EXPLOSIVE)",
    special = "cannon", heat = 22, energy = 20, reach = 7.0, power = 5,
  },
}
Data.WEAPONS = { "sword", "guns", "cannon" }

-- Combat Normal, Special and Super Moves:
-- Fields:
-- anim: animation sequence key
-- ticks: duration (in frames) of each animation frame
-- dmg: base damage
-- stun: hitstun frames on opponent
-- bstun: blockstun frames on opponent
-- push: pushback velocity
-- level: "mid" (block high/low), "low" (must block low), "over" (overhead, must block high)
-- rank: combo cancel rank (1: light -> 2: med -> 3: heavy -> 4: launch -> 5: special -> 9: super)
-- launch: true if launches opponent into high aerial juggle
-- kd: true if knocks down opponent
Data.MOVE = {
  -- Light attacks (rank 1)
  lp = { anim = "lp", ticks = { 3, 4, 4 }, dmg = 36, stun = 13, bstun = 9, push = 3, level = "mid", rank = 1 },
  clp = { anim = "clp", ticks = { 3, 4 }, dmg = 32, stun = 12, bstun = 8, push = 3, level = "mid", rank = 1, crouch = true },
  jlp = { anim = "jhp", ticks = { 3, 20 }, dmg = 38, stun = 14, bstun = 9, push = 3, level = "over", rank = 1, air = true },

  -- Medium / Light Kicks (rank 2)
  lk = { anim = "lk", ticks = { 4, 5, 5 }, dmg = 48, stun = 15, bstun = 11, push = 4, level = "mid", rank = 2 },
  clk = { anim = "clk", ticks = { 3, 5 }, dmg = 42, stun = 14, bstun = 10, push = 3, level = "low", rank = 2, crouch = true },
  jlk = { anim = "jhk", ticks = { 3, 22 }, dmg = 50, stun = 16, bstun = 11, push = 4, level = "over", rank = 2, air = true },

  -- Heavy attacks (rank 3)
  hp = { anim = "hp", ticks = { 5, 3, 4, 10 }, dmg = 96, stun = 21, bstun = 14, push = 7, level = "mid", rank = 3 },
  jhp = { anim = "jhp", ticks = { 4, 28 }, dmg = 78, stun = 18, bstun = 12, push = 5, level = "over", rank = 3, air = true },

  -- Heavy Kicks & Launchers (rank 4)
  hk = { anim = "hk", ticks = { 6, 3, 4, 12 }, dmg = 112, stun = 24, bstun = 15, push = 9, level = "mid", rank = 4, kd = true },
  chp = { anim = "chp", ticks = { 5, 4, 7 }, dmg = 88, stun = 28, bstun = 14, push = 3, level = "mid", rank = 4,
          crouch = true, launch = true }, -- LAUNCHER!
  chk = { anim = "chk", ticks = { 5, 6, 14 }, dmg = 84, stun = 22, bstun = 14, push = 6, level = "low", rank = 4,
          crouch = true, kd = true }, -- SWEEP!
  jhk = { anim = "jhk", ticks = { 4, 28 }, dmg = 88, stun = 20, bstun = 14, push = 6, level = "over", rank = 4, air = true, kd = true },

  -- Grapple / Throw (rank 5)
  throw = { anim = "hp", ticks = { 4, 6, 12, 10 }, dmg = 140, stun = 30, bstun = 0, push = 12, level = "unblockable",
            rank = 5, kd = true, throw = true },

  -- Special Weapon Moves (rank 5)
  slash = { anim = "slash", ticks = { 6, 7, 4, 5, 14 }, dmg = 175, stun = 26, bstun = 18, push = 11, level = "mid",
            rank = 5, kd = true, special = true, armor_k = 1.5 },
  fire = { anim = "fire", ticks = { 4, 4, 4, 4, 4, 4, 4, 4 }, loop = true, shots = 6, special = true, rank = 5 },
  cannon = { anim = "fire", ticks = { 6, 8, 8, 8 }, special = true, rank = 5, kd = true, dmg = 135 },
  uppercut = { anim = "hp", ticks = { 4, 4, 8, 12 }, dmg = 130, stun = 26, bstun = 16, push = 5, level = "mid",
               rank = 5, special = true, launch = true, kd = true },

  -- Hyper Super Moves (rank 9, costs 100 Super gauge)
  super_vanguard = {
    name = "V-BURST GIGA SABER", anim = "slash", ticks = { 5, 5, 6, 6, 18 },
    dmg = 330, stun = 35, bstun = 24, push = 16, level = "mid", rank = 9,
    kd = true, special = true, super = true, hits = 4, armor_k = 2.0
  },
  super_valkyrie = {
    name = "METEOR WING SALVO", anim = "fire", ticks = { 4, 4, 4, 4, 4, 4, 4, 4, 12 },
    dmg = 350, stun = 36, bstun = 22, push = 14, level = "mid", rank = 9,
    kd = true, special = true, super = true, hits = 10
  },
  super_behemoth = {
    name = "TITAN QUAKE OVERDRIVE", anim = "hp", ticks = { 6, 4, 6, 8, 20 },
    dmg = 380, stun = 40, bstun = 26, push = 20, level = "mid", rank = 9,
    kd = true, special = true, super = true, hits = 5
  },
}

-- Projectiles
Data.BULLET = { speed = 14, dmg = 24, stun = 10, bstun = 6, push = 2, level = "mid" }
Data.CANNON_SHELL = { speed = 10, dmg = 110, stun = 22, bstun = 14, push = 10, level = "mid", kd = true }
Data.PLASMA_ORB = { speed = 8, dmg = 45, stun = 14, bstun = 10, push = 4, level = "mid" }

-- CPU Difficulty AI Personalities
Data.CPU = {
  easy = { name = "EASY", think = 20, guard = 0.25, attack = 0.30, chain = 0.25, special = 0.20, super = 0.15, tag = 0.20 },
  normal = { name = "NORMAL", think = 10, guard = 0.55, attack = 0.50, chain = 0.65, special = 0.45, super = 0.40, tag = 0.45 },
  hard = { name = "HARD", think = 5, guard = 0.82, attack = 0.70, chain = 0.88, special = 0.65, super = 0.70, tag = 0.75 },
  boss = { name = "APOCALYPSE BOSS", think = 2, guard = 0.92, attack = 0.85, chain = 0.95, special = 0.80, super = 0.85, tag = 0.85 },
}
Data.CPUS = { "easy", "normal", "hard", "boss" }
