-- Titan Clash EX: Complete Screen & Game Modes Suite
-- Modes: Arcade Campaign (with Boss!), Tag Team 2v2, Versus 1v1, Hangar Diorama, Training.
-- Native 360x360 display layout for PowKiddy RGB30.

local S = {}
Scr.screens = S

Scr.setup = {
  mode = "arcade",           -- "arcade", "tag", "versus", "training", "cpu"
  stage = 1,                 -- 1..4
  level = "normal",
  team1 = {
    point = { chassis = "vanguard", armor = "medium", weapon = "sword" },
    partner = { chassis = "valkyrie", armor = "light", weapon = "guns" },
  },
  team2 = {
    point = { chassis = "behemoth", armor = "heavy", weapon = "cannon" },
    partner = { chassis = "vanguard", armor = "medium", weapon = "guns" },
  },
}
local setup = Scr.setup

function Scr.go(name, ...)
  Scr.name, Scr.cur = name, S[name]
  Scr.t = 0
  if Scr.cur and Scr.cur.enter then Scr.cur.enter(...) end
end

local function hit(b, p) return btnp(b, p) end
local function ok(p) return btnp(BA, p) or btnp(BB, p) or btnp(BSTART, p) end
local function back(p) return btnp(BX, p) or btnp(BY, p) or btnp(BSELECT, p) end

local function menu_draw(rows, sel, x, y, w)
  panel(x, y, w, #rows * 22 + 14, 0x080C14, 0x587498, true)
  for k, r in ipairs(rows) do
    local yy = y + 8 + (k - 1) * 22
    if k == sel then
      rectfill(x + 4, yy - 2, w - 8, 20, 0x223C5E)
      text(">", x + 8, yy, 0xFFD040)
    end
    text_c(r, x + w // 2, yy, k == sel and 0xFFFFFF or 0x8898B0)
  end
end

local function menu_move(sel, n, p)
  if hit(BU, p) then sel = (sel - 2) % n + 1; Snd.ui_move() end
  if hit(BD, p) then sel = sel % n + 1; Snd.ui_move() end
  return sel
end

---------------------------------------------------------------- TITLE SCREEN

local title_fighters = {}
S.title = {}

function S.title.enter()
  G.camx = 0
  G.stage = 1
  Stage.reset()
  Fx.clear()
  title_fighters[1] = Fighter.new(1, 1, "cpu", setup.team1.point, 120, 1)
  title_fighters[2] = Fighter.new(2, 1, "cpu", setup.team2.point, 240, -1)
  Snd.play_song("title")
end

function S.title.update()
  Scr.t = Scr.t + 1
  Stage.update()
  G.camx = 100 + sin(Scr.t / 500) * 80

  for _, f in ipairs(title_fighters) do
    f.t = f.t + 1
    f.ft = f.ft - 1
    if f.ft <= 0 then f.fi, f.ft = f.fi % #ANIM.idle + 1, 9 end
  end

  if ok() then
    Snd.ui_ok()
    Scr.go("mode")
  elseif Scr.t > 900 then
    -- Attract mode: CPU vs CPU demo fight
    setup.mode = "cpu"
    Snd.ui_ok()
    Scr.go("fight")
  end
end

function S.title.draw()
  Stage.draw_back(G.camx)
  for _, f in ipairs(title_fighters) do Fighter.draw(f) end
  Stage.draw_front(G.camx)

  -- Title Banner
  panel(24, 28, 312, 64, 0x060A12, 0x4A6890, true)
  sprite("t_titan", 180, 50)
  sprite("t_clash", 180, 72)
  text_c("EX - 16-BIT B16 EDITION", 180, 96, 0x58B4FF, 1)

  -- Press start prompt
  if (Scr.t // 30) % 2 == 0 then
    panel(80, 240, 200, 24, 0x0C121E, 0xFFD040)
    text_c("PRESS START BUTTON", 180, 245, 0xFFD040, 1)
  end

  -- Bottom info
  panel(20, 324, 320, 24, 0x080C14, 0x384860)
  text_c("POWKIDDY RGB30 NATIVE 360x360", 180, 328, 0x8898B0, 1)
end

---------------------------------------------------------------- MODE SELECT

S.mode = { sel = 1 }
local MODES = { "ARCADE CAMPAIGN", "TAG TEAM 2v2", "VERSUS 1v1", "HANGAR DIORAMA", "MOVE LIST" }

function S.mode.enter()
  S.mode.sel = 1
end

function S.mode.update()
  S.mode.sel = menu_move(S.mode.sel, #MODES)
  if ok() then
    Snd.ui_ok()
    local s = S.mode.sel
    if s == 1 then
      setup.mode = "arcade"
      setup.stage = 1
      Scr.go("hangar")
    elseif s == 2 then
      setup.mode = "tag"
      Scr.go("hangar")
    elseif s == 3 then
      setup.mode = "versus"
      Scr.go("hangar")
    elseif s == 4 then
      setup.mode = "hangar"
      Scr.go("hangar")
    else
      Scr.go("moves")
    end
  elseif back() then
    Snd.ui_back()
    Scr.go("title")
  end
end

function S.mode.draw()
  Stage.draw_back(0)
  Stage.draw_front(0)
  rectfill(0, 0, W, H, 0x080E18)

  panel(40, 24, 280, 32, 0x0C1422, 0x58B4FF, true)
  text_c("SELECT MISSION MODE", 180, 32, 0xFFD040, 1)

  menu_draw(MODES, S.mode.sel, 50, 72, 260)

  -- Mode descriptions
  local descs = {
    "Fight through 4 battle zones to defeat Dreadnought X!",
    "Full 2v2 tag team battle! Switch mechas in combat!",
    "Classic 1v1 Mecha duel against Player 2 or CPU.",
    "Customize chassis, modular armor and weapons in diorama.",
    "View full fighting command list and special techniques."
  }
  panel(30, 220, 300, 44, 0x0A0F18, 0x485E7E)
  text_c(descs[S.mode.sel], 180, 230, 0xE0EAF8, 1)

  text_c("A/B: CONFIRM    X/Y: BACK", 180, 330, 0x8898B0, 1)
end

---------------------------------------------------------------- HANGAR CUSTOMIZATION

S.hangar = { row = 1, col = 1, ready1 = false, ready2 = false }

function S.hangar.enter()
  S.hangar.row = 1
  S.hangar.col = 1
  S.hangar.ready1 = false
  S.hangar.ready2 = (setup.mode == "arcade" or setup.mode == "versus") -- CPU is auto ready
  Snd.play_song("hangar")
end

function S.hangar.update()
  local h = S.hangar
  local cfg = setup.team1.point

  if not h.ready1 then
    if hit(BU) then h.row = (h.row - 2) % 4 + 1; Snd.ui_move() end
    if hit(BD) then h.row = h.row % 4 + 1; Snd.ui_move() end

    if h.row == 1 then
      -- Chassis
      if hit(BL) or hit(BR) then
        local idx = 1
        for k, id in ipairs(Data.ROSTER) do if id == cfg.chassis then idx = k end end
        idx = (idx + (hit(BR) and 0 or -2)) % #Data.ROSTER + 1
        cfg.chassis = Data.ROSTER[idx]
        Snd.clank()
      end
    elseif h.row == 2 then
      -- Armor
      if hit(BL) or hit(BR) then
        local idx = 1
        for k, id in ipairs(Data.ARMORS) do if id == cfg.armor then idx = k end end
        idx = (idx + (hit(BR) and 0 or -2)) % #Data.ARMORS + 1
        cfg.armor = Data.ARMORS[idx]
        Snd.clank()
      end
    elseif h.row == 3 then
      -- Weapon
      if hit(BL) or hit(BR) then
        local idx = 1
        for k, id in ipairs(Data.WEAPONS) do if id == cfg.weapon then idx = k end end
        idx = (idx + (hit(BR) and 0 or -2)) % #Data.WEAPONS + 1
        cfg.weapon = Data.WEAPONS[idx]
        Snd.clank()
      end
    elseif h.row == 4 then
      if ok() then
        h.ready1 = true
        Snd.ui_ok()
      end
    end
  else
    if back() then
      h.ready1 = false
      Snd.ui_back()
    end
  end

  if h.ready1 and h.ready2 then
    if setup.mode == "hangar" then
      Scr.go("mode")
    else
      Scr.go("fight")
    end
  end
end

function S.hangar.draw()
  rectfill(0, 0, W, H, 0x080C14)
  -- Hangar diorama background
  sprite("bay", 20, 0)

  -- Title panel
  panel(20, 10, 320, 26, 0x0C121E, 0x4A6890, true)
  text_c("MECHA HANGAR CUSTOMIZATION", 180, 15, 0xFFD040, 1)

  local cfg = setup.team1.point
  local C = Data.CHASSIS[cfg.chassis or "vanguard"]
  local A = Data.ARMOR[cfg.armor or "medium"]
  local W_cfg = Data.WEAPON[cfg.weapon or "sword"]

  -- Configuration options
  local opts = {
    { "CHASSIS", C.name },
    { "ARMOR", A.name },
    { "WEAPON", W_cfg.name },
    { "STATUS", S.hangar.ready1 and "DEPLOY READY!" or "PRESS CONFIRM TO DEPLOY" }
  }

  panel(18, 42, 324, 94, 0x0A0F1A, 0x486082, true)
  for k, o in ipairs(opts) do
    local yy = 48 + (k - 1) * 21
    if k == S.hangar.row and not S.hangar.ready1 then
      rectfill(22, yy - 2, 316, 18, 0x1E3452)
      text(">", 26, yy, 0xFFD040)
    end
    text(o[1], 38, yy, 0x88A8D0, 1)
    text_r(o[2], 330, yy, k == S.hangar.row and 0xFFFFFF or 0xD0E0F0, 1)
  end

  -- Live mecha view on pad
  local dummy = Fighter.new(1, 1, "cpu", cfg, 180, 1)
  dummy.y = 0
  Fighter.draw(dummy)

  -- Mecha specs Pentagram / Gauges
  panel(18, 256, 324, 76, 0x080D16, 0x385070, true)
  text("SPECIFICATIONS", 26, 260, 0xFFD040, 1)

  local function stat_row(lbl, val, max_v, y)
    text(lbl, 26, y, 0x8898B0, 1)
    for b = 1, max_v do
      rectfill(110 + (b - 1) * 18, y + 2, 14, 6, b <= val and 0x38D848 or 0x182820)
    end
  end

  stat_row("ARMOR", C.stats.armor + (A.name == "HEAVY" and 1 or (A.name == "LIGHT" and -1 or 0)), 5, 274)
  stat_row("SPEED", C.stats.speed + (A.name == "LIGHT" and 1 or (A.name == "HEAVY" and -1 or 0)), 5, 286)
  stat_row("POWER", C.stats.power + W_cfg.power - 3, 5, 298)
  stat_row("REACTOR", C.stats.energy, 5, 310)

  text_c("D-PAD: SELECT    A/B: CONFIRM DEPLOY", 180, 342, 0x8898B0, 1)
end

---------------------------------------------------------------- MOVE LIST

S.moves = {}

function S.moves.update()
  if ok() or back() then
    Snd.ui_back()
    Scr.go("mode")
  end
end

function S.moves.draw()
  rectfill(0, 0, W, H, 0x080C14)
  panel(16, 12, 328, 30, 0x0C1422, 0x58B4FF, true)
  text_c("TITAN CLASH EX - COMMAND LIST", 180, 18, 0xFFD040, 1)

  local moves = {
    { "D-PAD", "MOVE / CROUCH / JUMP" },
    { "X  /  Y", "LIGHT PUNCH / HEAVY PUNCH" },
    { "A  /  B", "LIGHT KICK  / HEAVY KICK" },
    { "FWD, FWD", "BOOSTER DASH (AIR DASH)" },
    { "DOWN, UP", "SUPER JUMP (AERIAL LAUNCH)" },
    { "CROUCH + HP", "LAUNCHER (LAUNCHES FOE)" },
    { "CLOSE + HP", "MECHA GRAPPLE THROW" },
    { "D, DF, F + P", "SPECIAL WEAPON ATTACK" },
    { "F, D, DF + P", "RISING MECHA UPPERCUT" },
    { "L1 / X + A", "TAG SWITCH (2 CHARGES)" },
    { "D, DF, F + HP+HK", "HYPER SUPER (100% GAUGE)" },
  }

  panel(16, 48, 328, 260, 0x0A0F1A, 0x485E7E, true)
  for k, m in ipairs(moves) do
    local yy = 54 + (k - 1) * 22
    text(m[1], 24, yy, 0x58B4FF, 1)
    text_r(m[2], 336, yy, 0xE0EAF8, 1)
  end

  text_c("PRESS ANY BUTTON TO RETURN", 180, 326, 0xFFD040, 1)
end

---------------------------------------------------------------- COMBAT MATCH SCREEN

S.fight = {}
local fight_state = "intro" -- "intro", "fight", "ko", "timeover", "win"
local fight_timer = 99
local fight_subticks = 0
local round_num = 1
local wins1, wins2 = 0, 0
local active_fighters = {}

function S.fight.enter()
  round_num = 1
  wins1, wins2 = 0, 0
  G.stage = setup.stage or 1
  S.fight.start_round()
end

function S.fight.start_round()
  fight_state = "intro"
  fight_timer = 99
  fight_subticks = 0
  G.camx = (ARENA_W - W) // 2
  G.freeze = 0
  Stage.reset()
  Fx.clear()

  local p1_cfg = setup.team1.point
  local p2_cfg = setup.team2.point

  local p1 = Fighter.new(1, 1, setup.mode == "cpu" and "cpu" or 1, p1_cfg, G.camx + 80, 1)
  local p2 = Fighter.new(2, 1, setup.mode == "versus" and 2 or "cpu", p2_cfg, G.camx + W - 80, -1)

  -- Tag team setup
  if setup.mode == "tag" or setup.mode == "arcade" then
    local p1_partner = Fighter.new(1, 2, p1.pad, setup.team1.partner, G.camx - 100, 1)
    local p2_partner = Fighter.new(2, 2, p2.pad, setup.team2.partner, G.camx + W + 100, -1)
    p1.team = { active = p1, partner = p1_partner }
    p1_partner.team = p1.team
    p2.team = { active = p2, partner = p2_partner }
    p2_partner.team = p2.team
  end

  active_fighters = { p1, p2 }
  G.fighters = active_fighters
  Snd.play_song("fight")
  Snd.round()
end

function S.fight.update()
  Scr.t = Scr.t + 1

  if G.freeze > 0 then
    G.freeze = G.freeze - 1
    return
  end

  Stage.update()
  local f1, f2 = active_fighters[1], active_fighters[2]

  if fight_state == "intro" then
    Fighter.hands_off(f1)
    Fighter.hands_off(f2)
    if Scr.t > 80 then
      fight_state = "fight"
      Snd.fight()
    end
  elseif fight_state == "fight" then
    Fighter.read(f1)
    Fighter.read(f2)

    if f1.cpu then Cpu.update(f1, f2, setup.level) end
    if f2.cpu then Cpu.update(f2, f1, setup.level) end

    Fighter.update(f1, f2)
    Fighter.update(f2, f1)
    Fighter.separate(f1, f2)
    Fx.update(active_fighters)

    -- Match Timer (60 frames = 1 second)
    fight_subticks = fight_subticks + 1
    if fight_subticks >= 60 then
      fight_subticks = 0
      fight_timer = max(0, fight_timer - 1)
      if fight_timer == 0 then
        fight_state = "timeover"
        Snd.ko()
      end
    end

    -- K.O. check
    if f1.life <= 0 or f2.life <= 0 then
      fight_state = "ko"
      Fx.ko(f1.life <= 0 and f1 or f2)
      Snd.ko()
      Scr.t = 0
    end
  elseif fight_state == "ko" or fight_state == "timeover" then
    Fighter.hands_off(f1)
    Fighter.hands_off(f2)
    Fighter.update(f1, f2)
    Fighter.update(f2, f1)
    Fx.update(active_fighters)

    if Scr.t > 120 then
      -- Round resolution
      if f1.life > f2.life then
        wins1 = wins1 + 1
        Fighter.celebrate(f1)
      elseif f2.life > f1.life then
        wins2 = wins2 + 1
        Fighter.celebrate(f2)
      end

      if wins1 >= 2 or wins2 >= 2 then
        fight_state = "win"
        Snd.win()
        Scr.t = 0
      else
        round_num = round_num + 1
        S.fight.start_round()
      end
    end
  elseif fight_state == "win" then
    if ok() or Scr.t > 180 then
      if setup.mode == "arcade" and wins1 >= 2 then
        if setup.stage < 4 then
          setup.stage = setup.stage + 1
          S.fight.start_round()
        else
          Scr.go("victory")
        end
      else
        Scr.go("mode")
      end
    end
  end

  -- Camera follows fighters smoothly
  local mid_x = (f1.x + f2.x) / 2
  G.camx = clamp(floor(mid_x - W // 2), 0, ARENA_W - W)

  -- Screen shake decay
  if G.shake > 0 then
    G.sy = (G.shake % 2 == 0) and G.shake or -G.shake
    G.shake = G.shake - 1
  else
    G.sy = 0
  end
end

function S.fight.draw()
  Stage.draw_back(G.camx)
  Fx.draw_back()

  Fighter.draw(active_fighters[1])
  Fighter.draw(active_fighters[2])

  Fighter.draw_tag(active_fighters[1], active_fighters[1].cpu and "CPU" or "P1", Hud.P_COL[1])
  Fighter.draw_tag(active_fighters[2], active_fighters[2].cpu and "CPU" or "P2", Hud.P_COL[2])

  Fx.draw_front()
  Stage.draw_front(G.camx)

  -- HUD
  Hud.draw(active_fighters, fight_timer, wins1, wins2)

  -- Announcer Banners
  if fight_state == "intro" then
    if Scr.t < 40 then
      sprite("t_round", W // 2, 140)
      sprite("r" .. min(3, round_num), W // 2 + 110, 140)
    else
      sprite("t_fight", W // 2, 140)
    end
  elseif fight_state == "ko" then
    sprite("t_ko", W // 2, 140)
  elseif fight_state == "timeover" then
    sprite("t_time", W // 2, 140)
  elseif fight_state == "win" then
    local winner_name = wins1 >= 2 and (active_fighters[1].C.name .. " WINS!") or (active_fighters[2].C.name .. " WINS!")
    panel(40, 130, 280, 44, 0x080E18, 0xFFD040, true)
    text_c(winner_name, 180, 142, 0xFFD040, 2)
  end
end

---------------------------------------------------------------- VICTORY ENDING

S.victory = {}
function S.victory.enter()
  Snd.win()
end

function S.victory.update()
  if ok() then
    Snd.ui_ok()
    Scr.go("title")
  end
end

function S.victory.draw()
  rectfill(0, 0, W, H, 0x060A14)
  panel(24, 20, 312, 40, 0x0C182A, 0x58B4FF, true)
  text_c("OPERATION TITAN: COMPLETE!", 180, 30, 0xFFD040, 2)

  draw_pilot_face(1, 40, 80, false)
  panel(80, 75, 250, 60, 0x0A101C, 0x486082)
  text("PILOT KAI:", 88, 82, 0x58B4FF, 1)
  text("All enemy titans neutralised.", 88, 96, 0xFFFFFF, 1)
  text("Hong Kong harbor secured!", 88, 110, 0x88FF60, 1)

  panel(30, 160, 300, 120, 0x080C14, 0x384860)
  text_c("CONGRATULATIONS PILOT!", 180, 175, 0xFFD040, 2)
  text_c("You conquered all sectors and", 180, 205, 0xE0EAF8, 1)
  text_c("annihilated the Dreadnought X.", 180, 220, 0xE0EAF8, 1)
  text_c("THE EARTH SPHERE IS SAFE.", 180, 245, 0x58B4FF, 1)

  text_c("PRESS START BUTTON", 180, 320, 0xFFD040, 1)
end
