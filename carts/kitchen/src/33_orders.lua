-- Orders: they arrive over time, lose patience, and are served at the
-- window. Serving in a row builds a combo (x1..x4) that multiplies tips.

local MAX_ORDERS = { 4, 4, 5, 5 }
local RATE = { 1.35, 1.0, 0.82, 0.72 }       -- seconds between orders, by players
local PATIENCE = { 1.35, 1.1, 1.0, 0.92 }

function Ord.init(run, opts)
  run.orders = {}
  run.coins, run.coins_shown, run.combo, run.best_combo = 0, 0, 0, 0
  run.served, run.failed, run.wrong = 0, 0, 0
  run.order_t = 1.0
  run.order_every = opts.every or 17
  run.patience_k = opts.patience or 1
  run.next_id = 0
  run.last_recs = {}
  run.sigs = {}
end

local function players(run) return clamp(#run.chefs, 1, 4) end

local function pick_recipe(run)
  local pool = run.pool
  if #pool == 0 then return nil end
  -- avoid the same dish three times in a row; favour dishes not ordered lately
  local best, bs = nil, -1
  for _ = 1, 4 do
    local r = pool[random(#pool)]
    local s = random()
    local n = 0
    for _, l in ipairs(run.last_recs) do if l == r then n = n + 1 end end
    s = s - n * 0.4
    if s > bs then best, bs = r, s end
  end
  insert(run.last_recs, 1, best)
  run.last_recs[4] = nil
  return best
end

function Ord.spawn(run)
  local r = pick_recipe(run)
  if not r then return end
  local T = r.patience * run.patience_k * PATIENCE[players(run)]
  run.next_id = run.next_id + 1
  run.orders[#run.orders + 1] = { rec = r, t = T, T = T, id = run.next_id, enter = 1 }
  run.sigs[r.sig] = true
  Snd.order()
end

local function rebuild_sigs(run)
  run.sigs = {}
  for _, o in ipairs(run.orders) do run.sigs[o.rec.sig] = true end
end

function Ord.update(run, dt)
  local n = players(run)
  -- the next order: sooner when there are few
  run.order_t = run.order_t - dt
  local count = #run.orders
  if count == 0 and run.order_t > 2 then run.order_t = 2 end
  if run.order_t <= 0 and count < (run.max_orders or MAX_ORDERS[n]) then
    Ord.spawn(run)
    run.order_t = run.order_every * RATE[n] * (0.8 + random() * 0.4)
  end
  for i = #run.orders, 1, -1 do
    local o = run.orders[i]
    if o.enter > 0 then o.enter = max(0, o.enter - dt * 4) end
    if o.flash then o.flash = o.flash - dt end
    if not run.no_patience then o.t = o.t - dt end
    if o.t <= 0 then
      remove(run.orders, i)
      Ord.expire(run, o)
    end
  end
  run.coins_shown = approach(run.coins_shown, run.coins, max(1, abs(run.coins - run.coins_shown)) * dt * 6)
  if run.combo_flash then run.combo_flash = run.combo_flash - dt; if run.combo_flash <= 0 then run.combo_flash = nil end end
end

function Ord.expire(run, o)
  run.failed = run.failed + 1
  run.combo = 0
  local penalty = floor(o.rec.value * 0.25)
  if not run.endless then run.coins = max(0, run.coins - penalty) end
  Snd.expire()
  Fx.text(run.w / 2, run.h - 1, "ORDER LOST", 0xFF6040, true)
  Ren.shake(0.3)
  for _, c in ipairs(run.chefs) do Chef.set_act(c, "sad", 0.8) end
  rebuild_sigs(run)
  if run.on_expire then run.on_expire(run, o) end
  G.stats_add("lost", 1)
end

-- A plate reaches the serving window (from a chef, a throw or a belt).
function Ord.serve(run, plate, st, chef)
  local sig = Food.plate_sig(plate.parts)
  local found
  for i, o in ipairs(run.orders) do
    if o.rec.sig == sig then found = i break end
  end
  Kit.plate_back(run, 5)
  if not found then
    run.wrong = run.wrong + 1
    run.combo = 0
    Snd.wrong()
    Fx.text(st.x, st.z, "NOBODY ORDERED THAT", 0xFF8060)
    G.stats_add("wrong", 1)
    return false
  end
  local o = remove(run.orders, found)
  rebuild_sigs(run)
  run.combo = run.combo + 1
  run.best_combo = max(run.best_combo, run.combo)
  local mult = min(4, run.combo)
  local tip = floor(o.rec.value * 0.5 * clamp(o.t / o.T, 0, 1) + 0.5)
  local gain = floor((o.rec.value + tip * mult) * (run.money_k or 1))
  run.coins = run.coins + gain
  run.served = run.served + 1
  if run.combo > 1 then run.combo_flash = 1 end
  Snd.serve(mult)
  Fx.text(st.x, st.z, "+" .. gain, 0xFFE060, true)
  Fx.sparkle(st.x, Kit.TOP + 0.5, st.z)
  if mult >= 3 then Ren.shake(0.15) end
  for _, c in ipairs(run.chefs) do
    if dist2(c.x, c.z, st.x, st.z) < 16 or c == chef then Chef.set_act(c, "cheer", 0.7) end
  end
  G.stats_add("served", 1)
  G.stats_add("r:" .. o.rec.id, 1)
  G.recipe_seen(o.rec.id)
  if run.on_serve then run.on_serve(run, o, gain) end
  return true
end
