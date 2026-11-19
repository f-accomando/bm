-- Polis v0.2: Neutral Political-Economic Board Game for bm (640x360)
-- Pure political representation: no personal buying/selling. Parties propose laws,
-- run campaigns around what the people need, and vote by majority seats.

local C = {
  bg_top    = 0x0E1525,
  bg_bot    = 0x162238,
  card_top  = 0x1B2740,
  card_bot  = 0x131C30,
  surface   = 0x22304D,
  border    = 0x3E547A,
  border_hi = 0x7094C8,
  text      = 0xF2F6FC,
  muted     = 0xA0B4D0,
  dim       = 0x5C7090,
  gold      = 0xF4C048,
  cyan      = 0x48D0E0,
  green     = 0x4CD484,
  red       = 0xE86060,
  purple    = 0xB080F0,
  orange    = 0xF09040,
  white     = 0xFFFFFF,
  black     = 0x0A0E18,
}

local PARTIES = {
  { name = "Blu",   color = 0x4C98F0, dark = 0x244C80, lean = "state"  },
  { name = "Ambra", color = 0xF0AC3C, dark = 0x80581C, lean = "market" },
  { name = "Verde", color = 0x48C880, dark = 0x226440, lean = "state"  },
  { name = "Viola", color = 0xB878E8, dark = 0x5C3878, lean = "market" },
}

local AI_STYLES = { "Mercato", "Welfare", "Equilibrio", "Opportunista" }

local NEEDS = {
  { id = "LAV",  label = "Lavoro",     col = 0x4CD484 },
  { id = "STIP", label = "Stipendi",   col = 0x48D0E0 },
  { id = "PREZ", label = "Prezzi",     col = 0xF09040 },
  { id = "SERV", label = "Servizi",    col = 0xB080F0 },
}

-- 16 spacious tiles around a 5x5 perimeter (68x68 px each)
local TILES = {
  { id=1,  name="Parlamento",   short="PARLAM.", kind="parl",   law=nil,      col=0xF4C048, icon="dome"  },
  { id=2,  name="Fisco",        short="FISCO",   kind="law",    law="tax",    col=0x4C98F0, icon="scale" },
  { id=3,  name="Campagna",     short="CAMP.",   kind="camp",   law=nil,      col=0xB080F0, icon="mega"  },
  { id=4,  name="Lavoro",       short="LAVORO",  kind="law",    law="labor",  col=0x4CD484, icon="gear"  },
  { id=5,  name="Banca Centr.", short="BANCA",   kind="bank",   law="bank",   col=0x48D0E0, icon="bank"  },
  { id=6,  name="Energia",      short="ENERGIA", kind="law",    law="energy", col=0xF0C040, icon="bolt"  },
  { id=7,  name="Sondaggio",    short="SONDAG.", kind="poll",   law=nil,      col=0x48C880, icon="vote"  },
  { id=8,  name="Infrastrutt.", short="INFRA.",  kind="law",    law="infra",  col=0x58A8E8, icon="road"  },
  { id=9,  name="Piazza",       short="PIAZZA",  kind="plaza",  law=nil,      col=0xF09040, icon="crowd" },
  { id=10, name="Bilancio",     short="BILANC.", kind="budget", law=nil,      col=0xE86060, icon="coin"  },
  { id=11, name="Demografia",   short="DEMOG.",  kind="demo",   law=nil,      col=0xB080F0, icon="folk"  },
  { id=12, name="Referendum",   short="REFER.",  kind="parl",   law=nil,      col=0xF4C048, icon="vote"  },
  { id=13, name="Congiuntura",  short="EVENTO",  kind="event",  law=nil,      col=0xE87898, icon="bolt"  },
  { id=14, name="Tribuna",      short="TRIB.",   kind="camp",   law=nil,      col=0xB080F0, icon="mega"  },
  { id=15, name="Elezioni",     short="VOTO",    kind="poll",   law=nil,      col=0x4CD484, icon="vote"  },
  { id=16, name="Mercato/Rete", short="RETI",    kind="law",    law="infra",  col=0x58A8E8, icon="road"  },
}

-- Strictly neutral law options: A = Deregulated/Market, B = Mixed/Balanced, C = Public/Collective
local LAWS = {
  tax = {
    label = "Fisco",
    title = "Regime Fiscale e Welfare",
    opts = {
      { code="A", name="Aliquota Unica",    need="LAV",  desc="+Investimenti e impiego, gettito statale ridotto." },
      { code="B", name="Imposta Progress.", need="PREZ", desc="Equilibrio tra crescita privata e cassa pubblica." },
      { code="C", name="Patrimoniale",      need="SERV", desc="+Cassa, pensioni e servizi; frena investimenti." },
    }
  },
  infra = {
    label = "Infrastr.",
    title = "Gestione Infrastrutture",
    opts = {
      { code="A", name="Concessioni",       need="PREZ", desc="Zero spesa pubblica, tariffe e reti ai privati." },
      { code="B", name="Partenariato",      need="SERV", desc="Costo pubblico moderato ($20), servizi stabili." },
      { code="C", name="Servizio Pubblico", need="SERV", desc="+Servizi e impiego, spesa pubblica alta ($55)." },
    }
  },
  energy = {
    label = "Energia",
    title = "Politica Energetica",
    opts = {
      { code="A", name="Mercato Spot",      need="LAV",  desc="Boom produttivo rapido, ma +2% inflazione/ciclo." },
      { code="B", name="Mix Bilanciato",    need="PREZ", desc="Prezzi e stabilita' energetica neutrali." },
      { code="C", name="Piano Lungo",       need="PREZ", desc="Costoso all'avvio, -2% inflazione nei cicli." },
    }
  },
  labor = {
    label = "Lavoro",
    title = "Mercato del Lavoro",
    opts = {
      { code="A", name="Flessibilita'",     need="LAV",  desc="+10% occupazione rapida, stipendi -10." },
      { code="B", name="Contrattazione",    need="STIP", desc="Equilibrio tra occupazione e stipendi." },
      { code="C", name="Garanzia Impiego",  need="STIP", desc="Impiego min 75% e stipendi +12, pesa su Cassa." },
    }
  },
}

local LAW_KEYS = { "tax", "infra", "energy", "labor" }

local BANK_OPTS = {
  { code="priv", name="Banca Privata",  need="PREZ", desc="Credito selettivo, inflazione contenuta, rischio crisi." },
  { code="pub",  name="Banca Pubblica", need="SERV", desc="Salva la Cassa emettendo moneta (+6% inflazione)." },
}

local mode = "title" -- "title", "play", "modal", "gameover"
local title_cfg = { count = 3, kinds = { "Umano", "IA", "IA", "IA" }, cursor = 1 }
local g = {}
local anim_t = 0

-- Procedural Hi-Fi Audio Feedback
local function sfx_play(kind)
  if not note then return end
  if kind == "move" then
    envelope(0, 1, 20, 0, 15)
    note(0, "C5", 45, SINE, 70)
  elseif kind == "roll" then
    envelope(1, 0, 25, 0, 20)
    note(1, 320, 50, PLUCK, 110)
    envelope(2, 0, 30, 0, 20)
    note(2, 1800, 35, NOISE, 55)
  elseif kind == "step" then
    envelope(0, 1, 35, 0, 20)
    note(0, 440 + (g.move_steps or 0) * 35, 55, TRIANGLE, 85)
  elseif kind == "pass" then
    envelope(3, 5, 120, 80, 140)
    note(3, "C4", 360, FM, 105)
    arp(3, "major", 65)
  elseif kind == "fail" then
    envelope(3, 5, 140, 60, 160)
    note(3, "A2", 340, ORGAN, 100)
    arp(3, "dim", 90)
  elseif kind == "campaign" then
    envelope(4, 2, 80, 40, 100)
    note(4, "G4", 220, PLUCK, 100)
    arp(4, "power", 60)
  elseif kind == "win" then
    envelope(5, 8, 180, 120, 220)
    note(5, "C4", 650, FM, 115)
    arp(5, "maj7", 85)
  end
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function round(v)
  return math.floor(v + 0.5)
end

-- 16-tile 5x5 board geometry (left side: x=8..348, y=6..346, tile=68x68)
local BX, BY, TS = 8, 6, 68
local function tile_rect(idx)
  local col, row
  if idx <= 5 then
    col = 5 - idx; row = 4
  elseif idx <= 9 then
    col = 0; row = 9 - idx
  elseif idx <= 13 then
    col = idx - 9; row = 0
  else
    col = 4; row = idx - 13
  end
  return BX + col * TS, BY + row * TS, TS, TS
end

local function set_banner(msg)
  g.banner = msg
end

local function active_citizens()
  local n = 0
  for i = 1, 16 do if g.citizens[i].active then n = n + 1 end end
  return n
end

local function party_seats(pid)
  local n = 0
  for i = 1, 16 do
    local c = g.citizens[i]
    if c.active and c.party == pid then n = n + 1 end
  end
  return n
end

local function real_wages()
  return clamp(round(g.country.wages - g.country.inflation * 0.75), 0, 100)
end

local function country_index()
  local act = active_citizens()
  return clamp(round(g.country.employment * 0.45 + real_wages() * 0.45 + act * 2.2 - g.country.inflation * 0.7), 0, 150)
end

-- Determine top 2 urgent needs of the people right now
local function top_people_needs()
  local counts = { LAV = 0, STIP = 0, PREZ = 0, SERV = 0 }
  for i = 1, 16 do
    local c = g.citizens[i]
    if c.active then counts[c.need] = (counts[c.need] or 0) + 1 end
  end
  local list = {}
  for _, nd in ipairs(NEEDS) do
    table.insert(list, { id = nd.id, label = nd.label, col = nd.col, n = counts[nd.id] })
  end
  table.sort(list, function(a, b) return a.n > b.n end)
  return list[1], list[2]
end

local function new_game()
  g = {
    round = 1,
    max_rounds = 18,
    turn = 1,
    phase = "roll", -- "roll", "moving", "action"
    dice = 1,
    move_steps = 0,
    move_timer = 0,
    ai_timer = 0,
    laws = { tax = 2, infra = 2, energy = 2, labor = 2 },
    bank_mode = "priv",
    country = { employment = 66, wages = 58, treasury = 220, inflation = 4 },
    citizens = {},
    players = {},
    banner = "Nuova legislatura! Tira per agire.",
    modal = nil,
    winner = nil,
    hegemony_streak = {},
  }

  -- 12 active citizens initially (+4 slots for growth)
  local need_ids = { "LAV", "STIP", "PREZ", "SERV" }
  for i = 1, 16 do
    local p_id = 0 -- 0 = Indeciso
    if i <= title_cfg.count * 2 then
      p_id = ((i - 1) % title_cfg.count) + 1
    end
    local st = "emp"
    if i == 11 or i == 12 then st = "ret"
    elseif i == 10 then st = "unemp" end
    g.citizens[i] = {
      active = (i <= 12),
      party = p_id,
      status = st,
      need = need_ids[((i - 1) % 4) + 1],
    }
  end

  for i = 1, title_cfg.count do
    local tpl = PARTIES[i]
    g.players[i] = {
      id = i,
      name = tpl.name,
      color = tpl.color,
      dark = tpl.dark,
      lean = tpl.lean,
      is_ai = (title_cfg.kinds[i] == "IA"),
      ai_style = AI_STYLES[i],
      pos = 1,
      influence = 4, -- political capital for proposals/campaigns
      prestige = 0,  -- earned when laws pass and help citizens
      skip = 0,
    }
    g.hegemony_streak[i] = 0
  end
end

-- Shift N citizens with a matching need (or undecided) toward party pid
local function sway_citizens(pid, target_need, count)
  local moved = 0
  -- First try undecided citizens matching the need
  for i = 1, 16 do
    if moved >= count then break end
    local c = g.citizens[i]
    if c.active and c.party == 0 and (not target_need or c.need == target_need) then
      c.party = pid
      moved = moved + 1
    end
  end
  -- Next try any undecided
  for i = 1, 16 do
    if moved >= count then break end
    local c = g.citizens[i]
    if c.active and c.party == 0 then
      c.party = pid
      moved = moved + 1
    end
  end
  -- Finally sway an opposing citizen whose need matches
  for i = 1, 16 do
    if moved >= count then break end
    local c = g.citizens[i]
    if c.active and c.party ~= pid and target_need and c.need == target_need then
      c.party = pid
      moved = moved + 1
    end
  end
  return moved
end

local function simulate_round()
  local act = active_citizens()
  local emp_t, wage_t = 64, 54
  local treas_d, infl_d = 0, 0

  if g.laws.tax == 1 then treas_d = treas_d + act * 14; emp_t = emp_t + 6; wage_t = wage_t - 4
  elseif g.laws.tax == 2 then treas_d = treas_d + act * 22
  else treas_d = treas_d + act * 30 - 55; wage_t = wage_t + 8 end

  if g.laws.infra == 2 then treas_d = treas_d - 20; emp_t = emp_t + 3
  elseif g.laws.infra == 3 then treas_d = treas_d - 55; emp_t = emp_t + 8; wage_t = wage_t + 5 end

  if g.laws.energy == 1 then emp_t = emp_t + 5; infl_d = infl_d + 2
  elseif g.laws.energy == 3 then infl_d = infl_d - 2; wage_t = wage_t + 3 end

  if g.laws.labor == 1 then emp_t = emp_t + 10; wage_t = wage_t - 10
  elseif g.laws.labor == 3 then emp_t = math.max(emp_t, 76); wage_t = wage_t + 12; treas_d = treas_d - 35 end

  local ret_n = 0
  for i = 1, 16 do if g.citizens[i].active and g.citizens[i].status == "ret" then ret_n = ret_n + 1 end end
  treas_d = treas_d - ret_n * 14

  g.country.employment = clamp(round(g.country.employment * 0.5 + emp_t * 0.5), 20, 96)
  g.country.wages = clamp(round(g.country.wages * 0.5 + wage_t * 0.5), 15, 95)
  g.country.inflation = clamp(g.country.inflation + infl_d, 0, 60)
  g.country.treasury = g.country.treasury + treas_d

  local rw = real_wages()
  if rw >= 55 and g.country.employment >= 64 and act < 16 then
    for i = 1, 16 do
      if not g.citizens[i].active then
        g.citizens[i].active = true; g.citizens[i].party = 0; g.citizens[i].status = "emp"
        set_banner("Demografia: +1 nuovo cittadino!")
        break
      end
    end
  elseif (rw < 30 or g.country.employment < 40) and act > 8 then
    for i = 16, 1, -1 do
      if g.citizens[i].active then
        g.citizens[i].active = false
        set_banner("Demografia: -1 cittadino (emigrazione).")
        break
      end
    end
  end

  -- Update citizen needs dynamically based on country state!
  act = active_citizens()
  local want_ret = clamp(math.floor(act / 5), 1, 3)
  local want_emp = clamp(round((act - want_ret) * (g.country.employment / 100)), 1, act - want_ret)
  local seen = 0
  for i = 1, 16 do
    local c = g.citizens[i]
    if c.active then
      seen = seen + 1
      c.status = (seen <= want_ret and "ret") or (seen <= want_ret + want_emp and "emp") or "unemp"
      if c.status == "unemp" or g.country.employment < 58 then
        c.need = "LAV"
      elseif g.country.inflation >= 18 then
        c.need = "PREZ"
      elseif rw < 48 then
        c.need = "STIP"
      elseif g.laws.infra == 1 then
        c.need = "SERV"
      end
    end
  end

  -- Every party gains +1 base influence per round
  for _, p in ipairs(g.players) do
    p.influence = clamp(p.influence + 1, 0, 10)
  end
end

local function check_victory()
  local act = active_citizens()
  -- 4. Crisis / Default Victory
  if (g.country.treasury < -450 and g.bank_mode == "priv") or g.country.inflation >= 50 then
    local best, best_sc = g.players[1], -999
    for _, p in ipairs(g.players) do
      local sc = party_seats(p.id) * 10 + p.influence * 5
      if sc > best_sc then best, best_sc = p, sc end
    end
    g.winner = {
      player = best,
      kind = "Gestione della Crisi",
      desc = "Default o iperinflazione: guida il Paese chi ha piu' seggi e influenza.",
    }
    sfx_play("win")
    mode = "gameover"
    return true
  end

  -- 1. Electoral Majority (>= 65% seats for 2 consecutive rounds)
  for _, p in ipairs(g.players) do
    local s = party_seats(p.id)
    if s >= math.ceil(act * 0.65) then
      g.hegemony_streak[p.id] = g.hegemony_streak[p.id] + 1
      if g.hegemony_streak[p.id] >= 2 then
        g.winner = {
          player = p,
          kind = "Maggioranza Solida",
          desc = "Controllo di almeno il 65% del Parlamento per 2 cicli.",
        }
        sfx_play("win")
        mode = "gameover"
        return true
      end
    else
      g.hegemony_streak[p.id] = 0
    end

    -- 2. Legislative Prestige (12 points)
    if p.prestige >= 12 and g.country.inflation < 30 then
      g.winner = {
        player = p,
        kind = "Leadership Riformatrice",
        desc = "12 punti prestigio ottenuti rispondendo ai bisogni reali del Paese.",
      }
      sfx_play("win")
      mode = "gameover"
      return true
    end
  end

  -- 3. Prosperous State or Term Limit
  local ci = country_index()
  if (g.round >= 10 and ci >= 112) or g.round >= g.max_rounds then
    local best, best_sc = g.players[1], -999
    for _, p in ipairs(g.players) do
      local sc = party_seats(p.id) * 15 + p.prestige * 12 + p.influence * 3
      if sc > best_sc then best, best_sc = p, sc end
    end
    g.winner = {
      player = best,
      kind = (ci >= 112) and "Stato Prospero" or "Fine Legislatura",
      desc = (ci >= 112)
        and "Benessere nazionale elevato: vince il partito con piu' consenso e prestigio."
        or "Termine dei 18 cicli: vince il partito leader in Parlamento.",
    }
    sfx_play("win")
    mode = "gameover"
    return true
  end
  return false
end

local function next_turn()
  g.turn = (g.turn % #g.players) + 1
  if g.turn == 1 then
    g.round = g.round + 1
    simulate_round()
    if check_victory() then return end
  end
  local cur = g.players[g.turn]
  if cur.skip > 0 then
    cur.skip = cur.skip - 1
    set_banner(cur.name .. " bloccato dalle proteste in Piazza.")
    next_turn()
    return
  end
  g.phase = "roll"
  g.ai_timer = 28
end

-- Forecast & execute parliamentary vote by actual Party Seats + Lobby boost
local function compute_vote(proposer, cat_idx, opt_idx, boost_inf)
  local yes_s, no_s = 0, 0
  local n1, n2 = top_people_needs()
  local opt = (cat_idx <= 4) and LAWS[LAW_KEYS[cat_idx]].opts[opt_idx] or BANK_OPTS[opt_idx]

  for _, pl in ipairs(g.players) do
    local seats = math.max(1, party_seats(pl.id))
    if pl.id == proposer.id then
      yes_s = yes_s + seats
    else
      -- Other parties vote based on whether option aligns with their profile or top citizen need
      local aligns = (opt.need == n1.id or opt.need == n2.id)
      local likes = false
      if pl.lean == "market" and opt_idx == 1 then likes = true
      elseif pl.lean == "state" and ((cat_idx <= 4 and opt_idx == 3) or (cat_idx == 5 and opt_idx == 2)) then likes = true
      elseif opt_idx == 2 and aligns then likes = true end
      if likes then yes_s = yes_s + seats else no_s = no_s + seats end
    end
  end
  yes_s = yes_s + (boost_inf or 0)
  return yes_s, no_s, opt
end

local function execute_vote(proposer, cat_idx, opt_idx, boost_inf)
  if boost_inf > 0 then proposer.influence = proposer.influence - boost_inf end
  local yes_s, no_s, opt = compute_vote(proposer, cat_idx, opt_idx, boost_inf)
  if yes_s > no_s then
    if cat_idx <= 4 then
      g.laws[LAW_KEYS[cat_idx]] = opt_idx
    else
      g.bank_mode = (opt_idx == 1) and "priv" or "pub"
    end
    local gained = sway_citizens(proposer.id, opt.need, 2)
    proposer.prestige = proposer.prestige + 2
    sfx_play("pass")
    set_banner("APPROVATA: " .. opt.name .. " (" .. yes_s .. "-" .. no_s .. ") +" .. gained .. " seggi!")
  else
    sfx_play("fail")
    set_banner("RESPINTA: " .. opt.name .. " (" .. yes_s .. "-" .. no_s .. ").")
  end
end

local function open_law_modal(p, fixed_cat)
  if p.is_ai then
    local cat = fixed_cat or math.random(1, 5)
    local max_o = (cat <= 4) and 3 or 2
    local opt = (p.ai_style == "Mercato" and 1) or (p.ai_style == "Welfare" and max_o) or math.random(1, max_o)
    local boost = (p.influence >= 2 and math.random() < 0.5) and 2 or 0
    execute_vote(p, cat, opt, boost)
    g.phase = "action"
    return
  end
  g.modal = { kind = "law", cat = fixed_cat or 1, opt = 1, boost = 0 }
  mode = "modal"
end

local function open_camp_modal(p)
  if p.is_ai then
    local n1 = top_people_needs()
    if p.influence >= 2 then
      p.influence = p.influence - 2
      local sw = sway_citizens(p.id, n1.id, 2)
      sfx_play("campaign")
      set_banner(p.name .. " fa campagna su " .. n1.label .. " (+" .. sw .. " seggi).")
    end
    g.phase = "action"
    return
  end
  g.modal = { kind = "camp", cursor = 1 }
  mode = "modal"
end

local function resolve_tile(p)
  local t = TILES[p.pos]
  if t.kind == "parl" then
    p.influence = clamp(p.influence + 2, 0, 10)
    open_law_modal(p, nil)
  elseif t.kind == "law" then
    local cat_map = { tax = 1, infra = 2, energy = 3, labor = 4 }
    open_law_modal(p, cat_map[t.law])
  elseif t.kind == "bank" then
    if p.is_ai then
      if g.bank_mode == "pub" and g.country.treasury < 0 then
        g.country.treasury = g.country.treasury + 200
        g.country.inflation = clamp(g.country.inflation + 6, 0, 60)
        set_banner(p.name .. ": Banca Pubblica emette $200 (+6% infl.).")
      else
        open_law_modal(p, 5)
      end
      g.phase = "action"
    else
      g.modal = { kind = "bank", cursor = 1 }
      mode = "modal"
    end
  elseif t.kind == "camp" then
    open_camp_modal(p)
  elseif t.kind == "poll" then
    local n1 = top_people_needs()
    local sw = sway_citizens(p.id, n1.id, 1)
    sfx_play("campaign")
    set_banner("Sondaggio: focus su " .. n1.label .. " (+" .. sw .. " seggio a " .. p.name .. ").")
    g.phase = "action"
  elseif t.kind == "plaza" then
    if g.country.employment < 55 or real_wages() < 40 then
      p.skip = 1
      sfx_play("fail")
      set_banner("Proteste in Piazza! " .. p.name .. " fermo 1 turno.")
    else
      p.influence = clamp(p.influence + 1, 0, 10)
      set_banner("Piazza serena: " .. p.name .. " +1 Influenza.")
    end
    g.phase = "action"
  elseif t.kind == "budget" then
    if g.country.treasury >= 0 then
      p.prestige = p.prestige + 1
      set_banner("Bilancio in ordine ($" .. g.country.treasury .. "): +1 Prestigio.")
    else
      g.country.inflation = clamp(g.country.inflation + 3, 0, 60)
      sfx_play("fail")
      set_banner("Deficit pubblico! Inflazione +3%.")
    end
    g.phase = "action"
  elseif t.kind == "demo" then
    if real_wages() >= 50 and active_citizens() < 16 then
      for i = 1, 16 do
        if not g.citizens[i].active then
          g.citizens[i].active = true; g.citizens[i].party = 0; g.citizens[i].status = "emp"
          set_banner("Demografia: +1 nuovo cittadino attivo!")
          break
        end
      end
    else
      set_banner("Demografia stabile (" .. active_citizens() .. "/16 cittadini).")
    end
    g.phase = "action"
  elseif t.kind == "event" then
    local r = math.random(1, 3)
    if r == 1 then
      g.country.treasury = g.country.treasury + 80
      set_banner("Congiuntura: avanzo export (+$80 Cassa).")
    elseif r == 2 then
      g.country.inflation = clamp(g.country.inflation + 4, 0, 60)
      sfx_play("fail")
      set_banner("Congiuntura: rincaro materie (+4% Inflaz.).")
    else
      p.influence = clamp(p.influence + 2, 0, 10)
      set_banner("Congiuntura: " .. p.name .. " +2 Influenza.")
    end
    g.phase = "action"
  end
end

function _init()
  math.randomseed(math.floor((time() or 1) * 1000))
end

local function update_title()
  if btnp("up") then
    title_cfg.cursor = (title_cfg.cursor - 2) % 6 + 1; sfx_play("move")
  elseif btnp("down") then
    title_cfg.cursor = (title_cfg.cursor % 6) + 1; sfx_play("move")
  elseif btnp("left") or btnp("right") then
    sfx_play("move")
    local dir = btnp("right") and 1 or -1
    if title_cfg.cursor == 1 then
      title_cfg.count = clamp(title_cfg.count + dir, 2, 4)
    elseif title_cfg.cursor >= 2 and title_cfg.cursor <= 5 then
      local idx = title_cfg.cursor - 1
      title_cfg.kinds[idx] = (title_cfg.kinds[idx] == "Umano") and "IA" or "Umano"
    end
  elseif btnp("ok") or btnp("start") then
    if title_cfg.cursor == 6 or btnp("start") then
      sfx_play("pass")
      new_game()
      mode = "play"
    elseif title_cfg.cursor >= 2 and title_cfg.cursor <= 5 then
      local idx = title_cfg.cursor - 1
      title_cfg.kinds[idx] = (title_cfg.kinds[idx] == "Umano") and "IA" or "Umano"
      sfx_play("move")
    end
  end
end

local function update_modal()
  local m = g.modal
  local p = g.players[g.turn]
  if not m then mode = "play"; return end

  if m.kind == "law" then
    local max_opt = (m.cat <= 4) and 3 or 2
    if btnp("up") then
      m.cat = (m.cat - 2) % 5 + 1
      m.opt = clamp(m.opt, 1, (m.cat <= 4) and 3 or 2)
      sfx_play("move")
    elseif btnp("down") then
      m.cat = (m.cat % 5) + 1
      m.opt = clamp(m.opt, 1, (m.cat <= 4) and 3 or 2)
      sfx_play("move")
    elseif btnp("left") then
      m.opt = (m.opt - 2) % max_opt + 1; sfx_play("move")
    elseif btnp("right") then
      m.opt = (m.opt % max_opt) + 1; sfx_play("move")
    elseif btnp("x") then
      if m.boost < math.min(3, p.influence) then m.boost = m.boost + 1 else m.boost = 0 end
      sfx_play("move")
    elseif btnp("ok") then
      execute_vote(p, m.cat, m.opt, m.boost)
      g.modal = nil; mode = "play"; g.phase = "action"
    elseif btnp("back") then
      g.modal = nil; mode = "play"; g.phase = "action"
    end
  elseif m.kind == "camp" then
    if btnp("up") then m.cursor = (m.cursor - 2) % 4 + 1; sfx_play("move")
    elseif btnp("down") then m.cursor = (m.cursor % 4) + 1; sfx_play("move")
    elseif btnp("ok") then
      if p.influence >= 2 then
        p.influence = p.influence - 2
        local nd = NEEDS[m.cursor]
        local sw = sway_citizens(p.id, nd.id, 2)
        sfx_play("campaign")
        set_banner("Campagna su " .. nd.label .. ": +" .. sw .. " seggi!")
      else
        sfx_play("fail")
        set_banner("Servono 2 Influenza per fare campagna.")
      end
      g.modal = nil; mode = "play"; g.phase = "action"
    elseif btnp("back") then
      g.modal = nil; mode = "play"; g.phase = "action"
    end
  elseif m.kind == "bank" then
    if btnp("up") then m.cursor = (m.cursor - 2) % 3 + 1; sfx_play("move")
    elseif btnp("down") then m.cursor = (m.cursor % 3) + 1; sfx_play("move")
    elseif btnp("ok") then
      if m.cursor == 1 then
        g.modal = { kind = "law", cat = 5, opt = (g.bank_mode == "priv") and 2 or 1, boost = 0 }
        sfx_play("move")
        return
      elseif m.cursor == 2 and g.bank_mode == "pub" then
        g.country.treasury = g.country.treasury + 200
        g.country.inflation = clamp(g.country.inflation + 6, 0, 60)
        sfx_play("campaign")
        set_banner("Banca Pubblica emette $200 (+6% Inflazione).")
      end
      g.modal = nil; mode = "play"; g.phase = "action"
    elseif btnp("back") then
      g.modal = nil; mode = "play"; g.phase = "action"
    end
  elseif m.kind == "info" then
    if btnp("ok") or btnp("back") or btnp("y") then
      g.modal = nil; mode = "play"; sfx_play("move")
    end
  end
end

local function update_play()
  local p = g.players[g.turn]
  if g.phase == "moving" then
    g.move_timer = g.move_timer - 1
    if g.move_timer <= 0 then
      p.pos = (p.pos % 16) + 1
      sfx_play("step")
      if p.pos == 1 then
        p.influence = clamp(p.influence + 1, 0, 10)
      end
      g.move_steps = g.move_steps - 1
      if g.move_steps <= 0 then
        resolve_tile(p)
      else
        g.move_timer = 7
      end
    end
    return
  end

  if p.is_ai then
    g.ai_timer = g.ai_timer - 1
    if g.ai_timer <= 0 then
      if g.phase == "roll" then
        g.dice = math.random(1, 5)
        g.move_steps = g.dice
        g.move_timer = 7
        g.phase = "moving"
        sfx_play("roll")
        set_banner(p.name .. " avanza di " .. g.dice .. " caselle.")
      elseif g.phase == "action" then
        next_turn()
      end
    end
    return
  end

  if g.phase == "roll" then
    if btnp("ok") then
      g.dice = math.random(1, 5)
      g.move_steps = g.dice
      g.move_timer = 7
      g.phase = "moving"
      sfx_play("roll")
      set_banner(p.name .. " avanza di " .. g.dice .. " caselle.")
    elseif btnp("x") then
      open_law_modal(p, nil); sfx_play("move")
    elseif btnp("y") then
      g.modal = { kind = "info" }; mode = "modal"; sfx_play("move")
    end
  elseif g.phase == "action" then
    if btnp("ok") then
      sfx_play("move")
      next_turn()
    elseif btnp("x") then
      open_law_modal(p, nil); sfx_play("move")
    elseif btnp("y") then
      open_camp_modal(p); sfx_play("move")
    end
  end
end

function _update()
  anim_t = anim_t + 1
  if mode == "title" then update_title()
  elseif mode == "play" then update_play()
  elseif mode == "modal" then update_modal()
  elseif mode == "gameover" then
    if btnp("ok") or btnp("start") then mode = "title"; sfx_play("move") end
  end
end

-- Modern Vector / Gradient Drawing Helpers
local function card(x, y, w, h, bd)
  blend("alpha", 90)
  rrectfill(x + 2, y + 3, w, h, 6, C.black)
  blend("none")
  rectgrad(x, y, w, h, C.card_top, C.card_bot)
  rrect(x, y, w, h, 6, bd or C.border)
end

local function bar(x, y, w, h, pct, col)
  rrectfill(x, y, w, h, 3, C.black)
  local fw = clamp(round(w * clamp(pct, 0, 1)), 0, w)
  if fw > 2 then rrectfill(x + 1, y + 1, fw - 2, h - 2, 2, col) end
  rrect(x, y, w, h, 3, C.border)
end

local function draw_icon(kind, cx, cy, col)
  if kind == "dome" then
    circfill(cx, cy + 1, 7, col)
    rectfill(cx - 8, cy + 2, 16, 6, C.card_top)
    line(cx - 8, cy + 2, cx + 8, cy + 2, col)
    rectfill(cx - 6, cy + 3, 2, 5, col)
    rectfill(cx - 1, cy + 3, 2, 5, col)
    rectfill(cx + 4, cy + 3, 2, 5, col)
  elseif kind == "scale" then
    line(cx, cy - 6, cx, cy + 6, col)
    line(cx - 6, cy - 3, cx + 6, cy - 3, col)
    tri(cx - 6, cy - 2, cx - 9, cy + 3, cx - 3, cy + 3, col)
    tri(cx + 6, cy - 2, cx + 3, cy + 3, cx + 9, cy + 3, col)
  elseif kind == "bolt" then
    tri(cx + 2, cy - 7, cx - 4, cy + 1, cx + 1, cy + 1, col)
    tri(cx - 1, cy - 1, cx + 4, cy - 1, cx - 2, cy + 7, col)
  elseif kind == "bank" then
    tri(cx, cy - 7, cx - 8, cy - 2, cx + 8, cy - 2, col)
    rectfill(cx - 6, cy - 1, 2, 6, col)
    rectfill(cx - 1, cy - 1, 2, 6, col)
    rectfill(cx + 4, cy - 1, 2, 6, col)
    rectfill(cx - 8, cy + 5, 16, 2, col)
  elseif kind == "mega" then
    tri(cx - 6, cy, cx + 5, cy - 6, cx + 5, cy + 6, col)
    rectfill(cx - 7, cy - 2, 3, 4, col)
  elseif kind == "vote" then
    rrect(cx - 6, cy - 4, 12, 10, 2, col)
    line(cx - 3, cy - 2, cx + 3, cy - 2, C.gold)
    rectfill(cx - 2, cy - 7, 4, 5, C.white)
  elseif kind == "gear" or kind == "folk" or kind == "crowd" then
    circfill(cx - 4, cy - 2, 3, col)
    circfill(cx + 4, cy - 2, 3, col)
    circfill(cx, cy - 4, 3, C.white)
    rrectfill(cx - 6, cy + 2, 12, 4, 2, col)
  else -- road / coin
    circ(cx, cy, 6, col)
    circfill(cx, cy, 2, col)
  end
end

local function draw_title()
  rectgrad(0, 0, 640, 360, C.bg_top, C.bg_bot)
  radgrad(320, 120, 190, 0x22365A, C.bg_top)
  card(70, 24, 500, 312, C.border_hi)

  font("8x16")
  print("P O L I S", 284, 42, C.gold)
  font("6x12")
  print("Rappresentanza Politica, Leggi Neutrali & Consenso", 168, 66, C.muted)

  local rows = {
    "Numero Partiti:    < " .. title_cfg.count .. " >",
    "Partito Blu:       " .. title_cfg.kinds[1],
    "Partito Ambra:     " .. title_cfg.kinds[2],
    "Partito Verde:     " .. (title_cfg.count >= 3 and title_cfg.kinds[3] or "---"),
    "Partito Viola:     " .. (title_cfg.count >= 4 and title_cfg.kinds[4] or "---"),
    "AVVIA LEGISLATURA",
  }

  for i, txt in ipairs(rows) do
    local y = 104 + (i - 1) * 28
    local sel = (title_cfg.cursor == i)
    if sel then
      rrectfill(150, y - 4, 340, 22, 4, C.surface)
      rrect(150, y - 4, 340, 22, 4, C.gold)
    end
    local col = sel and C.white or C.muted
    if i == 6 then col = sel and C.gold or C.green end
    print(txt, 178, y, col)
  end

  print("OK / Start: Conferma   |   D-Pad: Seleziona", 190, 298, C.dim)
end

local function draw_board()
  card(4, 4, 348, 352, C.border)

  for i = 1, 16 do
    local t = TILES[i]
    local x, y, w, h = tile_rect(i)
    local is_cur = (g.players[g.turn].pos == i)
    rectgrad(x + 2, y + 2, w - 4, h - 4, C.surface, C.card_bot)
    rrect(x + 2, y + 2, w - 4, h - 4, 4, is_cur and C.white or C.border)
    -- Colored top ribbon
    rrectfill(x + 4, y + 4, w - 8, 5, 2, t.col)
    -- Crisp vector icon
    draw_icon(t.icon, x + 34, y + 27, t.col)
    font("6x12")
    print(t.short, x + 8, y + 42, C.text)

    -- Party pawns
    local slot = 0
    for _, p in ipairs(g.players) do
      if p.pos == i then
        local px = x + 14 + slot * 14
        local py = y + 58
        local bob = (p.id == g.turn) and math.floor(math.sin(anim_t * 0.15) * 1.5) or 0
        circfill(px + 1, py + 1, 5, C.black)
        circfill(px, py + bob, 5, p.color)
        circ(px, py + bob, 5, C.white)
        slot = slot + 1
      end
    end
  end

  -- Center: People's Hemicycle + Urgent Needs
  local cx, cy = BX + TS + 4, BY + TS + 4
  local cw, ch = TS * 3 - 8, TS * 3 - 8
  rrectfill(cx, cy, cw, ch, 8, C.black)
  rrect(cx, cy, cw, ch, 8, C.border)

  font("6x12")
  local n1, n2 = top_people_needs()
  rrectfill(cx + 6, cy + 6, cw - 12, 22, 4, C.card_top)
  print("CHIEDE:", cx + 12, cy + 11, C.muted)
  print(n1.label, cx + 60, cy + 11, n1.col)
  print("+", cx + 112, cy + 11, C.dim)
  print(n2.label, cx + 122, cy + 11, n2.col)

  -- 16 stylized Citizen figures (4x4)
  for i = 1, 16 do
    local c = g.citizens[i]
    local col_i = (i - 1) % 4
    local row_i = math.floor((i - 1) / 4)
    local tx = cx + 26 + col_i * 46
    local ty = cy + 48 + row_i * 30
    if not c.active then
      circ(tx, ty, 5, C.dim)
    else
      local pcol = (c.party > 0) and PARTIES[c.party].color or C.muted
      -- Head + shoulders silhouette
      circfill(tx, ty - 5, 4, pcol)
      rrectfill(tx - 6, ty, 12, 8, 3, pcol)
      -- Status ring if unemployed
      if c.status == "unemp" then
        rrect(tx - 7, ty - 1, 14, 10, 3, C.red)
      end
      -- Small dot showing citizen's urgent need
      local ncol = C.white
      for _, nd in ipairs(NEEDS) do if nd.id == c.need then ncol = nd.col end end
      circfill(tx + 7, ty - 6, 2, ncol)
    end
  end

  -- Bottom banner inside center
  rrectfill(cx + 6, cy + ch - 30, cw - 12, 24, 4, C.card_top)
  local msg = g.banner or ""
  if #msg > 29 then msg = string.sub(msg, 1, 29) end
  print(msg, cx + 10, cy + ch - 23, C.text)
end

local function draw_hud()
  local hx, hw = 358, 278
  card(hx, 4, hw, 352, C.border)
  font("6x12")

  -- 1. Parties & Seats
  print("PARLAMENTO (Ciclo " .. g.round .. "/" .. g.max_rounds .. ")", hx + 10, 12, C.muted)
  for i, p in ipairs(g.players) do
    local py = 28 + (i - 1) * 26
    local is_cur = (i == g.turn)
    rrectfill(hx + 10, py, hw - 20, 22, 4, is_cur and C.surface or C.black)
    rrect(hx + 10, py, hw - 20, 22, 4, is_cur and p.color or C.border)
    rrectfill(hx + 14, py + 4, 8, 14, 2, p.color)
    local tag = p.is_ai and "IA" or "UM"
    print(p.name .. " (" .. tag .. ")", hx + 28, py + 5, C.text)
    print("Seggi:" .. party_seats(p.id), hx + 116, py + 5, C.gold)
    print("Inf:" .. p.influence, hx + 180, py + 5, C.cyan)
    print("P:" .. p.prestige, hx + 228, py + 5, C.green)
  end

  -- 2. Country Parameters (4 clean, spacious bars)
  local cy = 140
  print("PAESE  (Indice " .. country_index() .. ")", hx + 10, cy, C.muted)
  local rw = real_wages()
  local dials = {
    { "Occupazione", g.country.employment .. "%", g.country.employment / 100, C.green },
    { "Stip./Pens.", rw .. "/100",                rw / 100,                  C.cyan },
    { "Cassa Pubb.", "$" .. g.country.treasury,   (g.country.treasury + 450) / 900, (g.country.treasury >= 0) and C.gold or C.red },
    { "Inflazione",  g.country.inflation .. "%",  g.country.inflation / 50,  (g.country.inflation < 22) and C.orange or C.red },
  }
  for i, d in ipairs(dials) do
    local dy = cy + 18 + (i - 1) * 22
    print(d[1], hx + 10, dy + 2, C.text)
    bar(hx + 94, dy + 3, 114, 12, d[3], d[4])
    print(d[2], hx + 214, dy + 2, d[4])
  end

  -- 3. Active Laws & Bank
  local ly = 236
  print("LEGGI ATTIVE", hx + 10, ly, C.muted)
  local codes = { "A", "B", "C" }
  for i, key in ipairs(LAW_KEYS) do
    local lx = hx + 10 + ((i - 1) % 2) * 130
    local lyy = ly + 16 + math.floor((i - 1) / 2) * 22
    rrectfill(lx, lyy, 124, 18, 3, C.black)
    rrect(lx, lyy, 124, 18, 3, C.border)
    print(LAWS[key].label .. ": [" .. codes[g.laws[key]] .. "]", lx + 8, lyy + 3, C.text)
  end
  local btxt = (g.bank_mode == "priv") and "Banca: Privata" or "Banca: Pubblica"
  rrectfill(hx + 10, ly + 60, hw - 20, 18, 3, C.black)
  rrect(hx + 10, ly + 60, hw - 20, 18, 3, (g.bank_mode == "priv") and C.gold or C.cyan)
  print(btxt, hx + 18, ly + 63, (g.bank_mode == "priv") and C.gold or C.cyan)

  -- 4. Clear Action Prompt
  local fy = 324
  local cur = g.players[g.turn]
  rrectfill(hx + 10, fy, hw - 20, 22, 4, C.surface)
  if cur.is_ai then
    print("Turno IA: " .. cur.name .. "...", hx + 18, fy + 5, C.muted)
  elseif g.phase == "roll" then
    print("OK: Tira  |  X: Legge  |  Y: Info", hx + 18, fy + 5, C.gold)
  else
    print("OK: Fine  |  X: Legge  |  Y: Campagna", hx + 14, fy + 5, C.green)
  end
end

local function draw_modal()
  local m = g.modal
  if not m then return end
  local p = g.players[g.turn]
  blend("alpha", 160)
  rectfill(0, 0, 640, 360, C.black)
  blend("none")

  card(76, 34, 488, 292, C.gold)
  font("6x12")

  if m.kind == "law" then
    font("8x16")
    print("PARLAMENTO - PROPOSTA DI LEGGE", 96, 48, C.gold)
    font("6x12")
    print("Proponente: " .. p.name .. "   |   Influenza disp.: " .. p.influence, 96, 70, C.muted)

    for i = 1, 5 do
      local y = 92 + (i - 1) * 34
      local sel = (m.cat == i)
      if sel then
        rrectfill(92, y - 3, 456, 30, 4, C.surface)
        rrect(92, y - 3, 456, 30, 4, C.border_hi)
      end
      local title = (i <= 4) and LAWS[LAW_KEYS[i]].title or "Statuto Bancario"
      print(title, 102, y, sel and C.white or C.muted)
      if sel then
        local opts = (i <= 4) and LAWS[LAW_KEYS[i]].opts or BANK_OPTS
        local opt = opts[m.opt]
        print("< " .. opt.name .. " >", 336, y, C.cyan)
        print(opt.desc, 102, y + 14, C.gold)
      end
    end

    local yes_s, no_s = compute_vote(p, m.cat, m.opt, m.boost)
    local est_col = (yes_s > no_s) and C.green or C.red
    print("Stima Voto: " .. yes_s .. " Favorevoli vs " .. no_s .. " Contrari", 96, 268, est_col)
    print("OK: Vota   |   X: Spinta (+" .. m.boost .. " Inf)   |   Indietro: Chiudi", 96, 296, C.green)
  elseif m.kind == "camp" then
    font("8x16")
    print("CAMPAGNA SUL POPOLO", 96, 48, C.purple)
    font("6x12")
    print("Scegli su quale bisogno convincere gli elettori (Costo: 2 Influenza)", 96, 74, C.muted)
    for i, nd in ipairs(NEEDS) do
      local y = 108 + (i - 1) * 34
      local sel = (m.cursor == i)
      if sel then
        rrectfill(96, y - 4, 448, 24, 4, C.surface)
        rrect(96, y - 4, 448, 24, 4, nd.col)
      end
      print("Campagna su: " .. nd.label, 112, y + 2, sel and nd.col or C.text)
    end
    print("OK: Avvia Campagna (2 Inf)   |   Indietro: Annulla", 96, 292, C.green)
  elseif m.kind == "bank" then
    font("8x16")
    print("BANCA CENTRALE (" .. (g.bank_mode == "priv" and "PRIVATA" or "PUBBLICA") .. ")", 96, 48, C.cyan)
    font("6x12")
    local items = {
      "1. Proponi cambio Statuto Bancario (Privata / Pubblica)",
      (g.bank_mode == "pub") and "2. Emetti Liquidita' di Stato (+$200 Cassa, +6% Inflazione)" or "2. (Emissione disponibile solo con Banca Pubblica)",
      "3. Chiudi",
    }
    for i, txt in ipairs(items) do
      local y = 108 + (i - 1) * 36
      local sel = (m.cursor == i)
      if sel then
        rrectfill(96, y - 4, 448, 24, 4, C.surface)
        rrect(96, y - 4, 448, 24, 4, C.cyan)
      end
      print(txt, 110, y + 2, sel and C.gold or C.text)
    end
  elseif m.kind == "info" then
    font("8x16")
    print("REGOLE & VITTORIA (4 VIE)", 96, 48, C.gold)
    font("6x12")
    print("1. Maggioranza Solida: >=65% dei seggi per 2 cicli.", 96, 88, C.text)
    print("2. Leadership Riformatrice: 12 Prestigio con Inflazione <30%.", 96, 112, C.text)
    print("3. Stato Prospero: Indice Paese >=112 dal ciclo 10.", 96, 136, C.text)
    print("4. Gestione Crisi: Default/Iperinfl. -> vince chi ha piu' seggi.", 96, 160, C.text)
    print("Ogni cittadino ha un bisogno reale (Lavoro, Stipendi, Prezzi, Servizi).", 96, 200, C.muted)
    print("Le leggi che rispondono ai bisogni del popolo portano voti al partito.", 96, 218, C.muted)
    print("OK / Indietro: Torna al gioco", 96, 292, C.green)
  end
end

local function draw_gameover()
  rectgrad(0, 0, 640, 360, C.bg_top, C.bg_bot)
  card(70, 50, 500, 260, C.gold)
  font("8x16")
  print("ESITO LEGISLATURA", 232, 72, C.gold)
  font("6x12")
  if g.winner and g.winner.player then
    local w = g.winner.player
    print("Vincitore: Partito " .. w.name, 100, 116, w.color)
    print("Via di vittoria: " .. g.winner.kind, 100, 140, C.cyan)
    print(g.winner.desc, 100, 166, C.text)
  end
  print("Premi OK o Start per tornare al menu.", 196, 268, C.green)
end

function _draw()
  if mode == "title" then draw_title(); return
  elseif mode == "gameover" then draw_gameover(); return end

  rectgrad(0, 0, 640, 360, C.bg_top, C.bg_bot)
  draw_board()
  draw_hud()
  if mode == "modal" then draw_modal() end
end
