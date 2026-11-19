-- Polis: neutral political-economic board game for bm (640x360)
-- 2-4 players (Hotseat & AI), 20-tile board, 16 Citizen Tokens, 4 Law Areas + Bank, 4 Victory Paths.

local C = {
  bg        = 0x10141C,
  panel     = 0x18202C,
  panel2    = 0x202A3A,
  border    = 0x384860,
  text      = 0xF0F4F8,
  muted     = 0x90A4B8,
  dim       = 0x58687C,
  gold      = 0xF0B840,
  cyan      = 0x40C8D8,
  green     = 0x48C878,
  red       = 0xE05858,
  purple    = 0xA878E8,
  orange    = 0xE88838,
  white     = 0xFFFFFF,
  black     = 0x080A10,
}

local PARTY_TEMPLATES = {
  { name = "Blu",   color = 0x4890E8, lean = "state"  },
  { name = "Ambra", color = 0xE8A838, lean = "market" },
  { name = "Verde", color = 0x48C078, lean = "state"  },
  { name = "Viola", color = 0xB070E0, lean = "market" },
}

local AI_PROFILES = { "Mercato", "Welfare", "Equilibrio", "Opportunista" }

-- 20-tile board clockwise starting at bottom-right (1 = Parlamento / Via)
local TILES = {
  { id=1,  name="Parlamento",  short="PARL.",   kind="start",   sector="ist",   col=C.gold,   price=0,   rent=0  },
  { id=2,  name="Tessile",     short="TESS.",   kind="prop",    sector="ind",   col=0xC87848, price=100, rent=25 },
  { id=3,  name="Logistica",   short="LOGI.",   kind="prop",    sector="serv",  col=0x5898D8, price=110, rent=28 },
  { id=4,  name="Mercato",     short="MERC.",   kind="event",   sector="mkt",   col=C.purple, price=0,   rent=0  },
  { id=5,  name="Acciaieria",  short="ACCIA",   kind="prop",    sector="ind",   col=0xC87848, price=140, rent=35 },
  { id=6,  name="Banca Centr", short="BANCA",   kind="bank",    sector="fin",   col=C.cyan,   price=0,   rent=0  },
  { id=7,  name="Ferrovia",    short="FERRO",   kind="prop",    sector="infra", col=0x48B888, price=160, rent=40 },
  { id=8,  name="Distr. Tech", short="TECH",    kind="prop",    sector="serv",  col=0x5898D8, price=180, rent=45 },
  { id=9,  name="Piazza",      short="PIAZZ",   kind="plaza",   sector="ist",   col=C.orange, price=0,   rent=0  },
  { id=10, name="Solare",      short="SOLAR",   kind="prop",    sector="ener",  col=0xE0C040, price=200, rent=50 },
  { id=11, name="Porto Merci", short="PORTO",   kind="prop",    sector="infra", col=0x48B888, price=210, rent=52 },
  { id=12, name="Demografia",  short="DEMO",    kind="demo",    sector="soc",   col=C.purple, price=0,   rent=0  },
  { id=13, name="Farmaceut.",  short="FARM.",   kind="prop",    sector="ind",   col=0xC87848, price=220, rent=55 },
  { id=14, name="Agricoltura", short="AGRI.",   kind="prop",    sector="prim",  col=0x88B848, price=150, rent=38 },
  { id=15, name="Referendum",  short="REFER",   kind="vote",    sector="ist",   col=C.gold,   price=0,   rent=0  },
  { id=16, name="Rete Elettr", short="RETE",    kind="prop",    sector="ener",  col=0xE0C040, price=240, rent=60 },
  { id=17, name="Edilizia",    short="EDIL.",   kind="prop",    sector="serv",  col=0x5898D8, price=250, rent=62 },
  { id=18, name="Bilancio",    short="FISCO",   kind="tax",     sector="ist",   col=C.red,    price=0,   rent=0  },
  { id=19, name="Telecom",     short="TELCO",   kind="prop",    sector="serv",  col=0x5898D8, price=280, rent=70 },
  { id=20, name="Borsa Valori",short="BORSA",   kind="prop",    sector="fin",   col=0xD86888, price=300, rent=75 },
}

-- Neutral policy definitions: A = Market/Deregulated, B = Mixed/Regulated, C = Public/Collective
local LAWS = {
  tax = {
    label = "Fisco",
    title = "Regime Fiscale & Welfare",
    opts = {
      { code="A", name="Aliquota Unica",    desc="Rendite +20%, imposta bassa, gettito ridotto." },
      { code="B", name="Imposta Progressiva",desc="Equilibrio tra gettito pubblico e rendite." },
      { code="C", name="Patrimoniale",       desc="Tassa patrimoni >$600, +pensioni e natalita'." },
    }
  },
  infra = {
    label = "Infrastr.",
    title = "Gestione Infrastrutture",
    opts = {
      { code="A", name="Concessioni Private",desc="Pedaggi +40% ai titolari, costo Stato $0." },
      { code="B", name="Modello Misto",       desc="Pedaggi standard, manutenzione pubblica $20." },
      { code="C", name="Servizio Pubblico",   desc="Pedaggi $0, +occupazione/stipendi, costo $55." },
    }
  },
  energy = {
    label = "Energia",
    title = "Politica Energetica",
    opts = {
      { code="A", name="Mercato Spot",        desc="Rendite energia +35%, +5% impiego, +2% inflaz." },
      { code="B", name="Mix Bilanciato",      desc="Rendite e stabilita' energetica neutrali." },
      { code="C", name="Piano Lungo Termine", desc="Costi +30% ora, -2% inflaz. e +stabilita' poi." },
    }
  },
  labor = {
    label = "Lavoro",
    title = "Mercato del Lavoro",
    opts = {
      { code="A", name="Flessibilita'",       desc="+10% impiego, cantieri -20%, stipendi -10." },
      { code="B", name="Contrattazione",      desc="Stipendi e occupazione bilanciati." },
      { code="C", name="Garanzia Impiego",    desc="Impiego min 75%, stipendi +12, costo imprese." },
    }
  },
}

local LAW_KEYS = { "tax", "infra", "energy", "labor" }

local BANK_OPTS = {
  { code="priv", name="Banca Privata",  desc="Tasso 20%, titoli speculativi, rischio pignoramento." },
  { code="pub",  name="Banca Pubblica", desc="Tasso 5%, emissione liquidita' (+inflazione)." },
}

-- Game state
local mode = "title" -- "title", "play", "modal", "gameover"
local title_cfg = {
  count = 3,
  kinds = { "Umano", "IA", "IA", "IA" },
  cursor = 1,
}

local g = {} -- initialized in new_game()

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function round(v)
  return math.floor(v + 0.5)
end

-- Map tile 1..20 to (x, y, w, h) on a 344x344 board at (8, 8)
local BOARD_X, BOARD_Y = 8, 8
local TILE_S = 57
local function tile_rect(idx)
  -- 1 is bottom-right (col 5, row 5), going clockwise:
  -- 1..6: bottom row right->left (cols 5..0, row 5)
  -- 7..11: left col bottom->top (col 0, rows 4..0)
  -- 12..16: top row left->right (cols 1..5, row 0)
  -- 17..20: right col top->bottom (col 5, rows 1..4)
  local col, row
  if idx <= 6 then
    col = 6 - idx
    row = 5
  elseif idx <= 11 then
    col = 0
    row = 11 - idx
  elseif idx <= 16 then
    col = idx - 11
    row = 0
  else
    col = 5
    row = idx - 16
  end
  return BOARD_X + col * TILE_S, BOARD_Y + row * TILE_S, TILE_S, TILE_S
end

local function add_log(msg)
  table.insert(g.logs, 1, msg)
  if #g.logs > 4 then table.remove(g.logs) end
end

local function active_citizen_count()
  local n = 0
  for i = 1, 16 do
    if g.citizens[i].active then n = n + 1 end
  end
  return n
end

local function count_votes()
  local mkt, ind, sta = 0, 0, 0
  for i = 1, 16 do
    local c = g.citizens[i]
    if c.active then
      if c.lean == "market" then mkt = mkt + 1
      elseif c.lean == "state" then sta = sta + 1
      else ind = ind + 1 end
    end
  end
  return mkt, ind, sta
end

local function player_votes(p)
  -- A party receives votes from citizens aligned with its leaning,
  -- split among active parties sharing that leaning (plus small bonus for assets/momentum).
  local mkt, _, sta = count_votes()
  local m_parties, s_parties = 0, 0
  for _, pl in ipairs(g.players) do
    if not pl.bankrupt then
      if pl.lean == "market" then m_parties = m_parties + 1
      else s_parties = s_parties + 1 end
    end
  end
  if p.lean == "market" then
    return m_parties > 0 and math.floor(mkt / m_parties + 0.5) or 0
  else
    return s_parties > 0 and math.floor(sta / s_parties + 0.5) or 0
  end
end

local function player_net_worth(p)
  local prop_val = 0
  for _, t in ipairs(g.tiles) do
    if t.owner == p.id then
      prop_val = prop_val + math.floor(t.price * (1 + (t.level - 1) * 0.6))
    end
  end
  return p.cash + prop_val + p.bonds - p.debt
end

local function real_wages()
  return clamp(round(g.country.wages - g.country.inflation * 0.75), 0, 100)
end

local function country_index()
  local act = active_citizen_count()
  return clamp(round(g.country.employment * 0.4 + real_wages() * 0.4 + act * 2.5 - g.country.inflation * 0.8), 0, 150)
end

local function new_game()
  g = {
    round = 1,
    max_rounds = 20,
    turn = 1,
    phase = "roll", -- "roll", "moving", "resolve", "action", "ai_wait"
    dice = { 1, 1 },
    move_steps = 0,
    move_timer = 0,
    ai_timer = 0,
    laws = {
      tax = 2,    -- B (Progressiva)
      infra = 2,  -- B (Misto)
      energy = 2, -- B (Mix)
      labor = 2,  -- B (Contrattazione)
    },
    bank_mode = "priv", -- "priv" or "pub"
    country = {
      employment = 68,
      wages = 58,
      treasury = 220,
      inflation = 4,
      demo_trend = 0,
    },
    citizens = {},
    tiles = {},
    players = {},
    logs = {},
    modal = nil,
    winner = nil,
    hegemony_streak = {},
  }

  for i = 1, 20 do
    local base = TILES[i]
    g.tiles[i] = {
      id = base.id, name = base.name, short = base.short,
      kind = base.kind, sector = base.sector, col = base.col,
      price = base.price, rent = base.rent,
      owner = 0, level = 1,
    }
  end

  -- 16 Citizen slots: 12 active initially, 4 vacant for immigration/births
  for i = 1, 16 do
    local lean = "ind"
    if i <= 4 then lean = "market"
    elseif i <= 8 then lean = "state" end
    local status = "emp"
    if i == 11 or i == 12 then status = "ret"
    elseif i == 10 then status = "unemp" end
    g.citizens[i] = {
      active = (i <= 12),
      lean = lean,
      status = status,
    }
  end

  for i = 1, title_cfg.count do
    local tpl = PARTY_TEMPLATES[i]
    g.players[i] = {
      id = i,
      name = tpl.name,
      color = tpl.color,
      lean = tpl.lean,
      is_ai = (title_cfg.kinds[i] == "IA"),
      ai_style = AI_PROFILES[i],
      pos = 1,
      cash = 500,
      debt = 0,
      bonds = 0,
      skip = 0,
      bankrupt = false,
      prestige = 0,
    }
    g.hegemony_streak[i] = 0
  end

  add_log("Partita iniziata. Turno di " .. g.players[1].name .. ".")
end

-- Economic simulation tick at the end of every full round
local function simulate_round()
  local act = active_citizen_count()
  local emp_target = 64
  local wage_target = 54
  local treasury_delta = 0
  local infl_delta = 0

  -- 1. Tax law
  if g.laws.tax == 1 then -- A: Flat
    treasury_delta = treasury_delta + act * 14
    wage_target = wage_target - 4
  elseif g.laws.tax == 2 then -- B: Progressive
    treasury_delta = treasury_delta + act * 22
  else -- C: Wealth & Transfers
    treasury_delta = treasury_delta + act * 30 - 60
    wage_target = wage_target + 8
  end

  -- 2. Infrastructure law
  if g.laws.infra == 1 then
    -- Private tolls, zero upkeep
  elseif g.laws.infra == 2 then
    treasury_delta = treasury_delta - 20
    emp_target = emp_target + 3
  else
    treasury_delta = treasury_delta - 55
    emp_target = emp_target + 8
    wage_target = wage_target + 5
  end

  -- 3. Energy law
  if g.laws.energy == 1 then
    emp_target = emp_target + 5
    infl_delta = infl_delta + 2
  elseif g.laws.energy == 3 then
    infl_delta = infl_delta - 2
    wage_target = wage_target + 3
  end

  -- 4. Labor law
  if g.laws.labor == 1 then
    emp_target = emp_target + 10
    wage_target = wage_target - 10
  elseif g.laws.labor == 3 then
    emp_target = math.max(emp_target, 76)
    wage_target = wage_target + 12
    treasury_delta = treasury_delta - 30
    for _, t in ipairs(g.tiles) do
      if t.owner > 0 then
        local pl = g.players[t.owner]
        if pl and not pl.bankrupt then
          pl.cash = pl.cash - 12
        end
      end
    end
  end

  -- Pension cost for retired citizens
  local ret_count = 0
  for i = 1, 16 do
    if g.citizens[i].active and g.citizens[i].status == "ret" then
      ret_count = ret_count + 1
    end
  end
  treasury_delta = treasury_delta - ret_count * 15

  -- Smoothly move country dials toward targets
  g.country.employment = clamp(round(g.country.employment * 0.5 + emp_target * 0.5), 20, 96)
  g.country.wages = clamp(round(g.country.wages * 0.5 + wage_target * 0.5), 15, 95)
  g.country.inflation = clamp(g.country.inflation + infl_delta, 0, 60)
  g.country.treasury = g.country.treasury + treasury_delta

  -- Player debt interest & private bonds
  for _, p in ipairs(g.players) do
    if not p.bankrupt then
      if p.debt > 0 then
        local rate = (g.bank_mode == "priv") and 0.20 or 0.05
        local interest = math.max(5, round(p.debt * rate))
        p.cash = p.cash - interest
      end
      if p.bonds > 0 and g.bank_mode == "priv" then
        if g.country.inflation < 25 then
          p.cash = p.cash + round(p.bonds * 0.15)
        else
          p.bonds = math.max(0, p.bonds - 30)
        end
      end
      -- Tax check on wealth
      if g.laws.tax == 3 and player_net_worth(p) > 600 then
        local wt = 35
        p.cash = p.cash - wt
        g.country.treasury = g.country.treasury + wt
      end
      -- Foreclosure in Private Bank if deeply negative
      if p.cash < -150 and g.bank_mode == "priv" then
        for _, t in ipairs(g.tiles) do
          if t.owner == p.id then
            t.owner = 0
            t.level = 1
            p.cash = p.cash + math.floor(t.price * 0.6)
            add_log(p.name .. " pignorato: ceduta " .. t.short)
            break
          end
        end
      end
    end
  end

  -- Update Citizen Tokens: status + demography + political shift
  local rw = real_wages()
  g.country.demo_trend = 0
  if rw >= 55 and g.country.employment >= 64 and act < 16 then
    for i = 1, 16 do
      if not g.citizens[i].active then
        g.citizens[i].active = true
        g.citizens[i].status = "emp"
        g.citizens[i].lean = "ind"
        g.country.demo_trend = 1
        add_log("Demografia: +1 Cittadino (nascite/immigr.)")
        break
      end
    end
  elseif (rw < 30 or g.country.employment < 40) and act > 8 then
    for i = 16, 1, -1 do
      if g.citizens[i].active then
        g.citizens[i].active = false
        g.country.demo_trend = -1
        add_log("Demografia: -1 Cittadino (emigrazione)")
        break
      end
    end
  end

  -- Reassign Employed / Unemployed / Retired
  act = active_citizen_count()
  local want_ret = clamp(math.floor(act / 5), 1, 3)
  local want_emp = clamp(round((act - want_ret) * (g.country.employment / 100)), 1, act - want_ret)
  local seen = 0
  for i = 1, 16 do
    local c = g.citizens[i]
    if c.active then
      seen = seen + 1
      if seen <= want_ret then
        c.status = "ret"
      elseif seen <= want_ret + want_emp then
        c.status = "emp"
      else
        c.status = "unemp"
      end
      -- Political reaction to economic reality
      if c.status == "unemp" or rw < 38 then
        if c.lean == "market" and math.random() < 0.45 then c.lean = "ind"
        elseif c.lean == "ind" and math.random() < 0.35 then c.lean = "state" end
      elseif g.laws.tax == 1 and g.country.employment >= 70 and rw >= 50 then
        if c.lean == "state" and math.random() < 0.35 then c.lean = "ind"
        elseif c.lean == "ind" and math.random() < 0.40 then c.lean = "market" end
      end
    end
  end
end

local function check_victory()
  local act = active_citizen_count()
  -- 4. Crisis / Default Victory
  if (g.country.treasury < -500 and g.bank_mode == "priv") or g.country.inflation >= 50 then
    local best, best_cash = nil, -999999
    for _, p in ipairs(g.players) do
      local liq = p.cash - p.debt
      if not p.bankrupt and liq > best_cash then
        best, best_cash = p, liq
      end
    end
    g.winner = {
      player = best,
      kind = "Gestione della Crisi",
      desc = "Il Paese e' andato in default/iperinflazione: vince il partito con piu' riserve liquide.",
    }
    mode = "gameover"
    return true
  end

  -- 1. Electoral Hegemony (>= 75% votes for 2 consecutive rounds)
  for _, p in ipairs(g.players) do
    if not p.bankrupt then
      local v = player_votes(p)
      if v >= math.ceil(act * 0.75) then
        g.hegemony_streak[p.id] = g.hegemony_streak[p.id] + 1
        if g.hegemony_streak[p.id] >= 2 then
          g.winner = {
            player = p,
            kind = "Mandato Elettorale",
            desc = "Maggioranza qualificata dei cittadini per 2 cicli consecutivi.",
          }
          mode = "gameover"
          return true
        end
      else
        g.hegemony_streak[p.id] = 0
      end

      -- 2. Economic Empire
      if player_net_worth(p) >= 2200 and g.country.inflation < 30 and g.country.treasury >= -200 then
        g.winner = {
          player = p,
          kind = "Capitale Consolidato",
          desc = "Superati $2200 di patrimonio netto in un contesto economico stabile.",
        }
        mode = "gameover"
        return true
      end
    end
  end

  -- 3. Prosperous State (round >= 12 & high Country Index, or max_rounds reached)
  local ci = country_index()
  if (g.round >= 12 and ci >= 115) or g.round >= g.max_rounds then
    local best, best_score = nil, -999999
    for _, p in ipairs(g.players) do
      if not p.bankrupt then
        local sc = player_votes(p) * 30 + p.prestige * 15 + math.floor(player_net_worth(p) / 40)
        if sc > best_score then
          best, best_score = p, sc
        end
      end
    end
    g.winner = {
      player = best,
      kind = (ci >= 115) and "Stato Prospero" or "Fine Mandato",
      desc = (ci >= 115)
        and "Indice Paese elevato raggiunto: vince il partito leader per consenso e prestigio."
        or "Termine dei 20 cicli: vince il partito con il miglior bilancio politico-economico.",
    }
    mode = "gameover"
    return true
  end

  return false
end

local function next_turn()
  local start = g.turn
  repeat
    g.turn = (g.turn % #g.players) + 1
    if g.turn == 1 then
      g.round = g.round + 1
      simulate_round()
      if check_victory() then return end
    end
  until (not g.players[g.turn].bankrupt) or g.turn == start

  local cur = g.players[g.turn]
  if cur.skip > 0 then
    cur.skip = cur.skip - 1
    add_log(cur.name .. " salta il turno (Piazza/Sciopero).")
    next_turn()
    return
  end

  g.phase = "roll"
  g.ai_timer = 30
end

local function effective_rent(tile)
  local r = tile.rent * tile.level
  if g.laws.tax == 1 then r = round(r * 1.2)
  elseif g.laws.tax == 3 then r = round(r * 0.85) end
  if tile.sector == "infra" then
    if g.laws.infra == 1 then r = round(r * 1.4)
    elseif g.laws.infra == 3 then return 0 end
  end
  if tile.sector == "ener" and g.laws.energy == 1 then
    r = round(r * 1.35)
  end
  return r
end

local function effective_upgrade_cost(tile)
  local c = math.floor(tile.price * 0.65)
  if g.laws.labor == 1 then c = round(c * 0.8) end
  if tile.sector == "ener" and g.laws.energy == 3 then c = round(c * 1.3) end
  c = round(c * (1 + g.country.inflation / 100))
  return c
end

-- Execute a parliament vote on a law key (1..4) or bank (5) -> target option
local function resolve_vote(proposer, target_idx, opt_idx)
  local mkt, ind, sta = count_votes()
  -- Option 1 favors market leaning, Option 3 favors state leaning, Option 2 gets undecided/mixed
  local yes_votes, no_votes = 0, 0
  if target_idx <= 4 then
    if opt_idx == 1 then
      yes_votes = mkt + math.floor(ind * 0.5)
      no_votes = sta + math.ceil(ind * 0.5)
    elseif opt_idx == 3 then
      yes_votes = sta + math.floor(ind * 0.5)
      no_votes = mkt + math.ceil(ind * 0.5)
    else
      yes_votes = ind + math.floor((mkt + sta) * 0.45)
      no_votes = math.ceil((mkt + sta) * 0.55)
    end
  else
    -- Bank mode: 1 = priv, 2 = pub
    if opt_idx == 1 then
      yes_votes = mkt + math.floor(ind * 0.5)
      no_votes = sta + math.ceil(ind * 0.5)
    else
      yes_votes = sta + math.floor(ind * 0.5)
      no_votes = mkt + math.ceil(ind * 0.5)
    end
  end

  -- Proposer gets tie-break + 1 institutional seat
  yes_votes = yes_votes + 1

  if yes_votes >= no_votes then
    if target_idx <= 4 then
      local key = LAW_KEYS[target_idx]
      g.laws[key] = opt_idx
      local opt = LAWS[key].opts[opt_idx]
      add_log("Approvata: " .. LAWS[key].label .. " -> " .. opt.name .. " (" .. yes_votes .. "-" .. no_votes .. ")")
    else
      g.bank_mode = (opt_idx == 1) and "priv" or "pub"
      add_log("Banca ora " .. (g.bank_mode == "priv" and "Privata" or "Pubblica") .. " (" .. yes_votes .. "-" .. no_votes .. ")")
    end
    proposer.prestige = proposer.prestige + 1
  else
    add_log("Respinta proposta di " .. proposer.name .. " (" .. yes_votes .. "-" .. no_votes .. ")")
  end
end

local function open_parliament_modal(p)
  if p.is_ai then
    -- AI picks a law based on style
    local t_idx = math.random(1, 5)
    local o_idx = 2
    if p.ai_style == "Mercato" then o_idx = 1
    elseif p.ai_style == "Welfare" then o_idx = (t_idx == 5) and 2 or 3
    elseif p.ai_style == "Opportunista" then
      o_idx = (g.country.treasury < 0 and t_idx == 5) and 2 or math.random(1, (t_idx == 5) and 2 or 3)
    end
    resolve_vote(p, t_idx, o_idx)
    g.phase = "action"
    return
  end

  g.modal = {
    kind = "parliament",
    cat = 1, -- 1..4 laws, 5 bank
    opt = 1,
    lobby_done = false,
  }
  mode = "modal"
end

local function open_bank_modal(p)
  g.modal = {
    kind = "bank",
    cursor = 1,
  }
  mode = "modal"
end

local function trigger_event(p, kind)
  local r = math.random(1, 4)
  if kind == "event" then
    if r == 1 then
      p.cash = p.cash + 80
      add_log("Mercato favorevole: " .. p.name .. " +$80.")
    elseif r == 2 then
      g.country.inflation = clamp(g.country.inflation + 4, 0, 60)
      add_log("Shock prezzi: Inflazione +4%.")
    elseif r == 3 then
      g.country.treasury = g.country.treasury + 70
      add_log("Avanzo commerciale: Cassa +$70.")
    else
      p.cash = p.cash - 60
      add_log("Sanzione regolatoria: " .. p.name .. " -$60.")
    end
  elseif kind == "demo" then
    if r <= 2 and active_citizen_count() < 16 then
      for i = 1, 16 do
        if not g.citizens[i].active then
          g.citizens[i].active = true
          g.citizens[i].status = "emp"
          g.citizens[i].lean = "ind"
          add_log("Flusso migratorio: +1 Cittadino attivo.")
          break
        end
      end
    else
      g.country.wages = clamp(g.country.wages + 5, 15, 95)
      add_log("Accordo sociale: Stipendi +5.")
    end
  end
end

local function resolve_tile(p)
  local tile = g.tiles[p.pos]
  if tile.kind == "start" then
    open_parliament_modal(p)
  elseif tile.kind == "prop" then
    if tile.owner == 0 then
      if p.is_ai then
        if p.cash >= tile.price + 60 then
          p.cash = p.cash - tile.price
          tile.owner = p.id
          add_log(p.name .. " acquista " .. tile.name .. " ($" .. tile.price .. ")")
        end
        g.phase = "action"
      else
        g.modal = { kind = "buy", tile = tile }
        mode = "modal"
      end
    elseif tile.owner == p.id then
      local uc = effective_upgrade_cost(tile)
      if tile.level < 3 then
        if p.is_ai then
          if p.cash >= uc + 80 then
            p.cash = p.cash - uc
            tile.level = tile.level + 1
            add_log(p.name .. " potenzia " .. tile.short .. " (Lv" .. tile.level .. ")")
          end
          g.phase = "action"
        else
          g.modal = { kind = "upgrade", tile = tile, cost = uc }
          mode = "modal"
        end
      else
        g.phase = "action"
      end
    else
      local owner = g.players[tile.owner]
      local rent = effective_rent(tile)
      if rent > 0 and owner and not owner.bankrupt then
        p.cash = p.cash - rent
        owner.cash = owner.cash + rent
        add_log(p.name .. " paga $" .. rent .. " a " .. owner.name .. " (" .. tile.short .. ")")
      elseif tile.sector == "infra" and g.laws.infra == 3 then
        g.country.treasury = g.country.treasury - 15
        owner.cash = owner.cash + 15
        add_log(tile.short .. " gratuita (Stato compensa $15).")
      end
      g.phase = "action"
    end
  elseif tile.kind == "bank" then
    if p.is_ai then
      if p.cash < 50 and p.debt < 300 then
        p.cash = p.cash + 150
        p.debt = p.debt + 150
        add_log(p.name .. " chiede prestito $150.")
      end
      g.phase = "action"
    else
      open_bank_modal(p)
    end
  elseif tile.kind == "plaza" then
    if g.country.employment < 55 or real_wages() < 40 then
      p.skip = 1
      add_log("Sciopero in Piazza! " .. p.name .. " fermo 1 turno.")
    else
      add_log(p.name .. " visita la Piazza (calma).")
    end
    g.phase = "action"
  elseif tile.kind == "tax" then
    local tax_amt = (g.laws.tax == 1) and 35 or ((g.laws.tax == 2) and 55 or 75)
    p.cash = p.cash - tax_amt
    g.country.treasury = g.country.treasury + tax_amt
    add_log(p.name .. " versa $" .. tax_amt .. " al Bilancio.")
    g.phase = "action"
  elseif tile.kind == "vote" then
    open_parliament_modal(p)
  elseif tile.kind == "event" or tile.kind == "demo" then
    trigger_event(p, tile.kind)
    g.phase = "action"
  end
end

local function start_roll(p)
  g.dice[1] = math.random(1, 6)
  g.dice[2] = math.random(1, 4) -- 10 max per step on 20-tile board keeps pace brisk
  g.move_steps = g.dice[1] + g.dice[2]
  g.move_timer = 8
  g.phase = "moving"
  add_log(p.name .. " tira " .. g.dice[1] .. "+" .. g.dice[2] .. " = " .. g.move_steps)
end

-- Lobbying action: spend $60 to sway 1 Indeciso citizen to player's leaning
local function do_lobby(p)
  if p.cash < 60 then
    add_log("Fondi insufficienti per Lobby ($60).")
    return false
  end
  for i = 1, 16 do
    local c = g.citizens[i]
    if c.active and c.lean == "ind" then
      c.lean = p.lean
      p.cash = p.cash - 60
      add_log(p.name .. " orienta 1 cittadino ($60).")
      return true
    end
  end
  -- Otherwise sway an opposing citizen to Indeciso for $80
  if p.cash >= 80 then
    for i = 1, 16 do
      local c = g.citizens[i]
      if c.active and c.lean ~= p.lean then
        c.lean = "ind"
        p.cash = p.cash - 80
        add_log(p.name .. " neutralizza 1 voto ($80).")
        return true
      end
    end
  end
  add_log("Nessun cittadino spostabile ora.")
  return false
end

function _init()
  math.randomseed(math.floor((time() or 1) * 1000))
end

local function update_title()
  if btnp("up") then
    title_cfg.cursor = (title_cfg.cursor - 2) % 6 + 1
  elseif btnp("down") then
    title_cfg.cursor = (title_cfg.cursor % 6) + 1
  elseif btnp("left") or btnp("right") then
    local dir = btnp("right") and 1 or -1
    if title_cfg.cursor == 1 then
      title_cfg.count = clamp(title_cfg.count + dir, 2, 4)
    elseif title_cfg.cursor >= 2 and title_cfg.cursor <= 5 then
      local idx = title_cfg.cursor - 1
      title_cfg.kinds[idx] = (title_cfg.kinds[idx] == "Umano") and "IA" or "Umano"
    end
  elseif btnp("ok") or btnp("start") then
    if title_cfg.cursor == 6 or btnp("start") then
      new_game()
      mode = "play"
    elseif title_cfg.cursor >= 2 and title_cfg.cursor <= 5 then
      local idx = title_cfg.cursor - 1
      title_cfg.kinds[idx] = (title_cfg.kinds[idx] == "Umano") and "IA" or "Umano"
    end
  end
end

local function update_modal()
  local m = g.modal
  local p = g.players[g.turn]
  if not m then mode = "play"; return end

  if m.kind == "buy" then
    if btnp("ok") then
      if p.cash >= m.tile.price then
        p.cash = p.cash - m.tile.price
        m.tile.owner = p.id
        add_log(p.name .. " acquista " .. m.tile.name)
      else
        add_log("Fondi insufficienti.")
      end
      g.modal = nil; mode = "play"; g.phase = "action"
    elseif btnp("back") then
      g.modal = nil; mode = "play"; g.phase = "action"
    end
  elseif m.kind == "upgrade" then
    if btnp("ok") then
      if p.cash >= m.cost then
        p.cash = p.cash - m.cost
        m.tile.level = m.tile.level + 1
        add_log(p.name .. " potenzia " .. m.tile.short .. " (Lv" .. m.tile.level .. ")")
      else
        add_log("Fondi insufficienti.")
      end
      g.modal = nil; mode = "play"; g.phase = "action"
    elseif btnp("back") then
      g.modal = nil; mode = "play"; g.phase = "action"
    end
  elseif m.kind == "parliament" then
    local max_opt = (m.cat <= 4) and 3 or 2
    if btnp("up") then
      m.cat = (m.cat - 2) % 5 + 1
      m.opt = clamp(m.opt, 1, (m.cat <= 4) and 3 or 2)
    elseif btnp("down") then
      m.cat = (m.cat % 5) + 1
      m.opt = clamp(m.opt, 1, (m.cat <= 4) and 3 or 2)
    elseif btnp("left") then
      m.opt = (m.opt - 2) % max_opt + 1
    elseif btnp("right") then
      m.opt = (m.opt % max_opt) + 1
    elseif btnp("x") and not m.lobby_done then
      if do_lobby(p) then m.lobby_done = true end
    elseif btnp("ok") then
      resolve_vote(p, m.cat, m.opt)
      g.modal = nil; mode = "play"; g.phase = "action"
    elseif btnp("back") then
      g.modal = nil; mode = "play"; g.phase = "action"
    end
  elseif m.kind == "bank" then
    if btnp("up") then m.cursor = (m.cursor - 2) % 4 + 1
    elseif btnp("down") then m.cursor = (m.cursor % 4) + 1
    elseif btnp("ok") then
      if m.cursor == 1 then
        if p.debt < 400 then
          p.cash = p.cash + 150
          p.debt = p.debt + 150
          add_log(p.name .. " riceve prestito $150.")
        else
          add_log("Limite debito ($400) raggiunto.")
        end
      elseif m.cursor == 2 then
        local pay = math.min(p.debt, 150)
        if pay > 0 and p.cash >= pay then
          p.cash = p.cash - pay
          p.debt = p.debt - pay
          add_log(p.name .. " rimborsa $" .. pay .. " di debito.")
        end
      elseif m.cursor == 3 then
        if g.bank_mode == "priv" then
          if p.cash >= 150 then
            p.cash = p.cash - 150
            p.bonds = p.bonds + 150
            add_log(p.name .. " compra Titoli Privati ($150).")
          end
        else
          g.country.treasury = g.country.treasury + 200
          g.country.inflation = clamp(g.country.inflation + 6, 0, 60)
          add_log("Banca Pubblica emette $200 (Inflazione +6%).")
        end
      else
        g.modal = nil; mode = "play"; g.phase = "action"
      end
    elseif btnp("back") then
      g.modal = nil; mode = "play"; g.phase = "action"
    end
  elseif m.kind == "info" then
    if btnp("ok") or btnp("back") or btnp("y") then
      g.modal = nil; mode = "play"
    end
  end
end

local function update_play()
  local p = g.players[g.turn]
  if g.phase == "moving" then
    g.move_timer = g.move_timer - 1
    if g.move_timer <= 0 then
      p.pos = (p.pos % 20) + 1
      if p.pos == 1 then
        local salary = round(100 * (0.6 + real_wages() / 120))
        p.cash = p.cash + salary
        add_log(p.name .. " passa Parlamento: +$" .. salary)
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
        start_roll(p)
      elseif g.phase == "action" then
        if p.cash >= 140 and math.random() < 0.35 then
          do_lobby(p)
        end
        next_turn()
      end
    end
    return
  end

  if g.phase == "roll" then
    if btnp("ok") then
      start_roll(p)
    elseif btnp("x") then
      open_bank_modal(p)
    elseif btnp("y") then
      g.modal = { kind = "info" }; mode = "modal"
    end
  elseif g.phase == "action" then
    if btnp("ok") then
      next_turn()
    elseif btnp("x") then
      open_bank_modal(p)
    elseif btnp("y") then
      do_lobby(p)
    end
  end
end

function _update()
  if mode == "title" then
    update_title()
  elseif mode == "play" then
    update_play()
  elseif mode == "modal" then
    update_modal()
  elseif mode == "gameover" then
    if btnp("ok") or btnp("start") then
      mode = "title"
    end
  end
end

-- Drawing helpers
local function box(x, y, w, h, bg, bd)
  rrectfill(x, y, w, h, 4, bg)
  if bd then rrect(x, y, w, h, 4, bd) end
end

local function bar(x, y, w, h, pct, col)
  rectfill(x, y, w, h, C.black)
  local fw = clamp(round(w * clamp(pct, 0, 1)), 0, w)
  if fw > 0 then rectfill(x, y, fw, h, col) end
  rect(x, y, w, h, C.border)
end

local function draw_title()
  cls(C.bg)
  box(40, 22, 560, 316, C.panel, C.border)
  font("8x16")
  print("POLIS", 296, 36, C.gold)
  font("6x12")
  print("Simulatore politico-economico da tavolo (neutrale)", 168, 58, C.muted)

  local rows = {
    "Numero Partiti:  < " .. title_cfg.count .. " >",
    "Partito 1 (Blu):   " .. title_cfg.kinds[1],
    "Partito 2 (Ambra): " .. title_cfg.kinds[2],
    "Partito 3 (Verde): " .. (title_cfg.count >= 3 and title_cfg.kinds[3] or "---"),
    "Partito 4 (Viola): " .. (title_cfg.count >= 4 and title_cfg.kinds[4] or "---"),
    "INIZIA PARTITA",
  }

  for i, txt in ipairs(rows) do
    local y = 96 + (i - 1) * 28
    local sel = (title_cfg.cursor == i)
    if sel then
      rrectfill(150, y - 4, 340, 20, 3, C.panel2)
      rrect(150, y - 4, 340, 20, 3, C.gold)
    end
    local col = sel and C.white or C.muted
    if i == 6 then col = sel and C.gold or C.green end
    print(txt, 175, y, col)
  end

  print("4 Vie di Vittoria: Mandato Elettorale | Capitale | Benessere | Crisi", 112, 282, C.dim)
  print("D-Pad: Seleziona/Cambia   OK/Start: Conferma", 184, 304, C.muted)
end

local function draw_board()
  -- Outer board frame
  box(4, 4, 352, 352, C.panel, C.border)

  for i = 1, 20 do
    local t = g.tiles[i]
    local x, y, w, h = tile_rect(i)
    local bg = C.panel2
    local bd = C.border
    if t.owner > 0 then
      bd = g.players[t.owner].color
    end
    rectfill(x + 1, y + 1, w - 2, h - 2, bg)
    rect(x + 1, y + 1, w - 2, h - 2, bd)
    -- Top sector stripe
    rectfill(x + 2, y + 2, w - 4, 7, t.col)

    font("6x12")
    print(t.short, x + 5, y + 12, C.text)

    if t.kind == "prop" then
      if t.owner == 0 then
        print("$" .. t.price, x + 5, y + 26, C.muted)
      else
        local own_col = g.players[t.owner].color
        rectfill(x + 5, y + 26, 8, 8, own_col)
        for lv = 1, t.level do
          rectfill(x + 16 + (lv - 1) * 6, y + 28, 4, 4, C.gold)
        end
      end
    end

    -- Player tokens on this tile
    local slot = 0
    for _, p in ipairs(g.players) do
      if not p.bankrupt and p.pos == i then
        local px = x + 8 + (slot % 2) * 14
        local py = y + 40 + math.floor(slot / 2) * 10
        circfill(px, py, 5, p.color)
        circ(px, py, 5, C.white)
        slot = slot + 1
      end
    end
  end

  -- Center area: Citizen Tokens (4x4 grid) + Round & Log
  local cx, cy = BOARD_X + TILE_S + 4, BOARD_Y + TILE_S + 4
  local cw, ch = TILE_S * 4 - 8, TILE_S * 4 - 8
  box(cx, cy, cw, ch, C.black, C.border)

  font("6x12")
  local mkt, ind, sta = count_votes()
  print("POPOLO (" .. active_citizen_count() .. "/16)", cx + 8, cy + 6, C.text)
  print("M:" .. mkt .. " I:" .. ind .. " S:" .. sta, cx + 114, cy + 6, C.muted)

  for i = 1, 16 do
    local c = g.citizens[i]
    local col_i = (i - 1) % 4
    local row_i = math.floor((i - 1) / 4)
    local tx = cx + 28 + col_i * 46
    local ty = cy + 34 + row_i * 26
    if not c.active then
      circ(tx, ty, 7, C.dim)
    else
      local fill = C.muted
      if c.lean == "market" then fill = C.gold
      elseif c.lean == "state" then fill = C.cyan end
      circfill(tx, ty, 8, fill)
      local rim = C.white
      if c.status == "unemp" then rim = C.red
      elseif c.status == "ret" then rim = C.purple end
      circ(tx, ty, 8, rim)
      local tag = (c.status == "emp" and "L") or (c.status == "unemp" and "D") or "P"
      print(tag, tx - 3, ty - 6, C.black)
    end
  end

  -- Legend & Logs
  print("L=Lav D=Dis P=Pen | Oro=Mkt Ciano=Stato", cx + 6, cy + 132, C.dim)
  line(cx + 6, cy + 146, cx + cw - 6, cy + 146, C.border)

  local cur = g.players[g.turn]
  print("Ciclo " .. g.round .. "/" .. g.max_rounds .. " | Turno: " .. cur.name, cx + 8, cy + 150, cur.color)
  for i = 1, math.min(3, #g.logs) do
    local msg = g.logs[i]
    if #msg > 34 then msg = string.sub(msg, 1, 34) end
    print(msg, cx + 8, cy + 166 + (i - 1) * 14, (i == 1) and C.text or C.muted)
  end
end

local function draw_hud()
  local hx = 362
  local hw = 274
  box(hx, 4, hw, 352, C.panel, C.border)

  font("6x12")
  -- 1. Party Cards
  print("PARTITI", hx + 8, 10, C.muted)
  for i, p in ipairs(g.players) do
    local py = 24 + (i - 1) * 26
    local is_cur = (i == g.turn)
    rrectfill(hx + 8, py, hw - 16, 22, 3, is_cur and C.panel2 or C.black)
    if is_cur then rrect(hx + 8, py, hw - 16, 22, 3, p.color) end
    rectfill(hx + 12, py + 5, 8, 12, p.color)
    local tag = p.is_ai and "IA" or "UM"
    print(p.name .. "(" .. tag .. ")", hx + 24, py + 5, C.text)
    print("$" .. p.cash, hx + 106, py + 5, (p.cash >= 0) and C.green or C.red)
    print("NW:" .. player_net_worth(p), hx + 158, py + 5, C.muted)
    print("V:" .. player_votes(p), hx + 224, py + 5, C.gold)
  end

  -- 2. Country Parameters
  local cy = 134
  print("PARAMETRI PAESE  (Indice: " .. country_index() .. ")", hx + 8, cy, C.muted)
  local rw = real_wages()
  local dials = {
    { "Occupazione", g.country.employment .. "%", g.country.employment / 100, C.green },
    { "Stip/Pens.",  rw .. "/100",                rw / 100,                  C.cyan },
    { "Demografia",  active_citizen_count() .. "/16", active_citizen_count() / 16, C.purple },
    { "Cassa Pubb.", "$" .. g.country.treasury,   (g.country.treasury + 500) / 1000, (g.country.treasury >= 0) and C.gold or C.red },
    { "Inflazione",  g.country.inflation .. "%",  g.country.inflation / 50,  (g.country.inflation < 25) and C.orange or C.red },
  }
  for i, d in ipairs(dials) do
    local dy = cy + 16 + (i - 1) * 20
    print(d[1], hx + 8, dy + 2, C.text)
    bar(hx + 94, dy + 4, 108, 10, d[3], d[4])
    print(d[2], hx + 210, dy + 2, d[4])
  end

  -- 3. Active Laws & Bank
  local ly = 254
  print("LEGGI ATTIVE & BANCA", hx + 8, ly, C.muted)
  local codes = { "A", "B", "C" }
  for i, key in ipairs(LAW_KEYS) do
    local lx = hx + 8 + ((i - 1) % 2) * 132
    local lyy = ly + 16 + math.floor((i - 1) / 2) * 20
    local opt_i = g.laws[key]
    rrectfill(lx, lyy, 126, 16, 2, C.black)
    print(LAWS[key].label .. ": [" .. codes[opt_i] .. "]", lx + 6, lyy + 2, C.text)
  end
  local b_txt = (g.bank_mode == "priv") and "Banca: Privata (20%)" or "Banca: Pubblica (5%)"
  rrectfill(hx + 8, ly + 56, hw - 16, 16, 2, C.black)
  print(b_txt, hx + 14, ly + 58, (g.bank_mode == "priv") and C.gold or C.cyan)

  -- 4. Controls Footer
  local fy = 334
  local cur = g.players[g.turn]
  if cur.is_ai then
    print("Turno IA in corso...", hx + 8, fy, C.muted)
  elseif g.phase == "roll" then
    print("OK: Tira Dado | X: Banca | Y: Regole", hx + 8, fy, C.gold)
  elseif g.phase == "action" then
    print("OK: Fine Turno | X: Banca | Y: Lobby($60)", hx + 8, fy, C.green)
  end
end

local function draw_modal()
  local m = g.modal
  if not m then return end
  local p = g.players[g.turn]
  box(90, 44, 460, 272, C.panel, C.gold)
  font("6x12")

  if m.kind == "buy" then
    print("ACQUISTO SETTORE: " .. m.tile.name, 110, 62, C.gold)
    print("Prezzo: $" .. m.tile.price .. "  |  Rendita base: $" .. m.tile.rent, 110, 92, C.text)
    print("Fondi disponibili: $" .. p.cash, 110, 114, C.muted)
    print("OK: Acquista   |   Indietro: Rifiuta", 110, 280, C.green)
  elseif m.kind == "upgrade" then
    print("POTENZIAMENTO: " .. m.tile.name, 110, 62, C.gold)
    print("Livello attuale: " .. m.tile.level .. " -> " .. (m.tile.level + 1), 110, 92, C.text)
    print("Costo adeguato a leggi/inflazione: $" .. m.cost, 110, 114, C.muted)
    print("OK: Potenzia   |   Indietro: Salta", 110, 280, C.green)
  elseif m.kind == "parliament" then
    print("PARLAMENTO - PROPOSTA DI LEGGE (" .. p.name .. ")", 110, 56, C.gold)
    for i = 1, 5 do
      local y = 84 + (i - 1) * 32
      local sel = (m.cat == i)
      if sel then rrectfill(106, y - 3, 428, 26, 3, C.panel2) end
      local title = (i <= 4) and LAWS[LAW_KEYS[i]].title or "Statuto Bancario"
      print(title, 114, y, sel and C.white or C.muted)
      if sel then
        local opts = (i <= 4) and LAWS[LAW_KEYS[i]].opts or BANK_OPTS
        local opt = opts[m.opt]
        print("< " .. opt.name .. " >", 330, y, C.cyan)
        print(opt.desc, 114, y + 12, C.gold)
      end
    end
    local ltxt = m.lobby_done and "(Lobby usata)" or "X: Lobby ($60)"
    print("OK: Vota Legge   |   " .. ltxt .. "   |   Indietro: Chiudi", 110, 288, C.green)
  elseif m.kind == "bank" then
    print("SPORTELLO BANCA (" .. (g.bank_mode == "priv" and "PRIVATA" or "PUBBLICA") .. ")", 110, 60, C.cyan)
    print("Fondi: $" .. p.cash .. "  |  Debito: $" .. p.debt .. "  |  Titoli: $" .. p.bonds, 110, 82, C.muted)
    local items = {
      "1. Chiedi Prestito (+$150)",
      "2. Rimborsa Debito (-$150)",
      (g.bank_mode == "priv") and "3. Compra Titoli Privati ($150)" or "3. Emetti Liquidita' Pubblica (+$200 Cassa, +6% Infl.)",
      "4. Chiudi Sportello",
    }
    for i, txt in ipairs(items) do
      local y = 118 + (i - 1) * 28
      local sel = (m.cursor == i)
      if sel then rrectfill(108, y - 4, 424, 20, 3, C.panel2) end
      print(txt, 116, y, sel and C.gold or C.text)
    end
  elseif m.kind == "info" then
    print("CONDIZIONI DI VITTORIA (4 VIE)", 110, 58, C.gold)
    print("1. Mandato Elettorale: >=75% voti per 2 cicli.", 110, 88, C.text)
    print("2. Capitale Consolidato: >=$2200 netti, Inflaz.<30%.", 110, 112, C.text)
    print("3. Stato Prospero: Indice Paese >=115 dal ciclo 12.", 110, 136, C.text)
    print("4. Gestione Crisi: Default/Iperinfl. -> vince +liquido.", 110, 160, C.text)
    print("Ogni legge bilancia crescita, gettito, occupazione e", 110, 200, C.muted)
    print("inflazione senza un modello unico vincente.", 110, 216, C.muted)
    print("OK / Indietro: Torna al gioco", 110, 284, C.green)
  end
end

local function draw_gameover()
  cls(C.bg)
  box(70, 50, 500, 260, C.panel, C.gold)
  font("8x16")
  print("ESITO PARTITA", 248, 70, C.gold)
  font("6x12")
  if g.winner and g.winner.player then
    local w = g.winner.player
    print("Vincitore: Partito " .. w.name, 100, 110, w.color)
    print("Condizione: " .. g.winner.kind, 100, 134, C.cyan)
    print(g.winner.desc, 100, 158, C.text)
  end
  print("Premi OK o Start per tornare al menu.", 196, 268, C.green)
end

function _draw()
  if mode == "title" then
    draw_title()
    return
  elseif mode == "gameover" then
    draw_gameover()
    return
  end

  cls(C.bg)
  draw_board()
  draw_hud()
  if mode == "modal" then
    draw_modal()
  end
end
