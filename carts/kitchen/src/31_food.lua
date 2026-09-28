-- Items and what the stations do with them.
-- An item in a hand, on a station or on the floor is one of:
--   { key = "tomato/c", chop = 0..1 }     food (chop: progress on the board)
--   { plate = true, parts = { keys } }    a clean plate, maybe with food
--   { dirty = n }                          a stack of n dirty plates
--   { key = "ext" }                        the fire extinguisher
--   { key = "burnt" }                      what is left of burnt food
-- Only what leads to a dish of this kitchen can go on a plate or in a pot:
-- mistakes are refused on the spot, not discovered at the window.

---------------------------------------------------------------- the pool

local function count(list, add)
  local c = {}
  for _, k in ipairs(list) do c[k] = (c[k] or 0) + 1 end
  if add then c[add] = (c[add] or 0) + 1 end
  return c
end

local function fits(small, big)
  for k, n in pairs(small) do
    if (big[k] or 0) < n then return false end
  end
  return true
end

local function collect_boxes(key, out)
  local k = Food.parse(key)
  if k.ing then return end
  local list = out[k.proc]
  if not list then list = {}; out[k.proc] = list end
  list[#list + 1] = count(k.kids)
  for _, s in ipairs(k.kids) do collect_boxes(s, out) end
end

-- The recipes that can be ordered in this run; rebuilt when they change.
function Food.set_pool(run, recipes)
  run.pool = recipes
  run.plate_sets, run.box_sets = {}, {}
  run.need_st, run.need_ing = {}, {}
  for _, r in ipairs(recipes) do
    run.plate_sets[#run.plate_sets + 1] = count(r.parts)
    for _, p in ipairs(r.parts) do collect_boxes(p, run.box_sets) end
    for s in pairs(r.st) do run.need_st[s] = true end
    for i in pairs(r.ing) do run.need_ing[i] = true end
  end
end

function Food.plate_ok(run, parts, add)
  local c = count(parts, add)
  for _, set in ipairs(run.plate_sets) do
    if fits(c, set) then return true end
  end
  return false
end

function Food.box_ok(run, proc, items, add)
  local sets = run.box_sets[proc]
  if not sets then return false end
  local c = count(items, add)
  for _, set in ipairs(sets) do
    if fits(c, set) then return true end
  end
  return false
end

-- 1 if the plate matches a recipe exactly (for the "ready" tick on it)
function Food.plate_sig(parts)
  local list = {}
  for i, k in ipairs(parts) do list[i] = k end
  sort(list)
  return concat(list, "|")
end

---------------------------------------------------------------- helpers

local function is_food(it) return it and it.key and it.key ~= "ext" and it.key ~= "burnt" end
Food.is_food = is_food

function Food.new_ing(id) return { key = id } end
function Food.new_plate() return { plate = true, parts = {} } end

-- what trash does to an item: nil (gone) or the item left (an empty plate)
function Food.trash(it)
  if not it then return nil end
  if it.plate then
    if #it.parts > 0 then it.parts = {} end
    return it
  end
  if it.dirty or it.key == "ext" then return it end
  return nil
end

-- a chopped / processed name for texts
function Food.label(key)
  local k = Food.parse(key)
  if k.ing then
    local n = Data.ING[k.ing].name
    return k.chopped and ("chopped " .. n) or n
  end
  return k.proc
end

---------------------------------------------------------------- cooking

-- chef stats and upgrades multiply these
local function speed(run, what)
  local u = run.mult
  return u and u[what] or 1
end
Food.speed = speed

local function box_add(run, st, key)
  local b = st.box
  local n = #b.items
  b.items[n + 1] = key
  -- a new ingredient cools the rest a little: the time done so far counts
  -- for the share of the pot that was already there
  b.t = b.done and st.def.cook * n / (n + 1) or b.t * n / (n + 1)
  b.done, b.t2, b.warn, b.out = false, 0, 0, nil
  st.bump = 0.25
end

function Food.box_accepts(run, st, key)
  local b = st.box
  if b.burnt or #b.items >= (st.cap or st.def.cap) then return false end
  if st.def.box == "fry" and #b.items >= 1 then return false end
  return Food.box_ok(run, st.def.box, b.items, key)
end

local function box_take(st)
  local b = st.box
  local out = b.burnt and "burnt" or b.out
  b.items, b.t, b.t2, b.done, b.burnt, b.out, b.warn = {}, 0, 0, false, false, nil, 0
  return out
end
Food.box_take = box_take

function Food.update(run, dt)
  local cook_k = speed(run, "cook")
  local burn_k = speed(run, "burn")
  for _, st in ipairs(run.stations) do
    local b = st.box
    if b and #b.items > 0 and not b.burnt and st.fire <= 0 then
      local def = st.def
      if not b.done then
        b.t = b.t + dt * cook_k
        if b.t >= def.cook then
          b.done = true
          b.t2 = 0
          b.out = Food.mix_key(def.box, b.items)
          Snd.ding()
          Fx.puff(st.x, Kit.TOP + 0.4, st.z, 0xFFFFFF, 3)
        end
      elseif def.burn and not run.no_burn then
        b.t2 = b.t2 + dt / burn_k
        local left = def.burn - b.t2
        if left < def.burn * 0.55 then
          b.warn = b.warn + dt
          if b.warn > (left < def.burn * 0.25 and 0.25 or 0.5) then
            b.warn = 0
            Snd.warn()
          end
        end
        if b.t2 >= def.burn then
          b.burnt = true
          b.out = "burnt"
          st.fire_in = run.no_fire and nil or 2.5
          Snd.burn()
          Fx.puff(st.x, Kit.TOP + 0.5, st.z, 0x303030, 6)
          G.stats_add("burnt", 1)
        end
      end
    end
    -- burnt food left on the stove catches fire
    if st.fire_in then
      st.fire_in = st.fire_in - dt
      if st.fire_in <= 0 then
        st.fire_in = nil
        if b and b.burnt then Food.ignite(run, st) end
      end
    end
    if st.fire > 0 then Food.update_fire(run, st, dt) end
  end
end

function Food.ignite(run, st)
  if st.fire > 0 then return end
  if run.auto_ext then
    Fx.puff(st.x, Kit.TOP + 0.4, st.z, 0xE0F0FF, 8)
    return
  end
  st.fire = 1
  st.spread = 5
  Snd.fire()
  Fx.text(st.x, st.z, "FIRE!", 0xFF6020)
end

function Food.update_fire(run, st, dt)
  st.spread = (st.spread or 5) - dt
  if st.item and is_food(st.item) then st.item = { key = "burnt" } end
  Fx.flame(st.x, st.z)
  if st.spread <= 0 then
    st.spread = 6
    local dirs = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
    local d = dirs[random(4)]
    local n = Kit.cell(run, st.cx + d[1], st.cz + d[2]).st
    if n and n.fire <= 0 and n.kind ~= "serve" and n.kind ~= "sink" then Food.ignite(run, n) end
  end
end

---------------------------------------------------------------- A: take, put, combine

-- Returns true if the action did something (the caller plays a sound on false).
function Food.interact(run, chef, st)
  local h = chef.hold
  local def = st.def
  if st.fire > 0 then return false, "fire" end

  -- serving window
  if st.kind == "serve" then
    if h and h.plate and #h.parts > 0 then
      chef.hold = nil
      Ord.serve(run, h, st, chef)
      return true
    end
    return false, h and h.plate and "empty plate" or (h and "needs a plate")
  end

  -- trash
  if st.kind == "trash" then
    if not h then return false end
    if h.dirty or h.key == "ext" then return false end
    local was_plate = h.plate
    chef.hold = Food.trash(h)
    Snd.trash()
    Fx.puff(st.x, Kit.TOP + 0.3, st.z, 0x808080, 3)
    if not was_plate then G.stats_add("wasted", 1) end
    return true
  end

  -- clean plates
  if st.kind == "plates" then
    if not h and st.n > 0 then
      st.n = st.n - 1
      chef.hold = Food.new_plate()
      return true
    elseif h and h.plate and #h.parts == 0 then
      st.n = st.n + 1
      chef.hold = nil
      return true
    end
    return false
  end

  -- dirty plates coming back
  if st.kind == "ret" then
    if not h and st.n > 0 then
      chef.hold = { dirty = st.n }
      st.n = 0
      return true
    elseif h and h.dirty then
      st.n = st.n + h.dirty
      chef.hold = nil
      return true
    end
    return false
  end

  -- sink
  if st.kind == "sink" then
    if h and h.dirty then
      st.dirty = st.dirty + h.dirty
      chef.hold = nil
      Snd.splash()
      return true
    elseif not h and st.clean > 0 then
      st.clean = st.clean - 1
      chef.hold = Food.new_plate()
      return true
    end
    return false
  end

  -- the register (endless mode)
  if st.kind == "register" then
    return End.open(chef, st)
  end

  -- cooking vessels
  if st.box then
    local b = st.box
    local ready = b.done or b.burnt
    if ready and not h then
      chef.hold = { key = box_take(st) }
      return true
    end
    if ready and h and h.plate and not b.burnt then
      if Food.plate_ok(run, h.parts, b.out) then
        h.parts[#h.parts + 1] = box_take(st)
        return true
      end
      return false, "not for this dish"
    end
    if h and is_food(h) and Food.box_accepts(run, st, h.key) then
      box_add(run, st, h.key)
      chef.hold = nil
      Snd.plop()
      return true
    end
    -- a plate with a single part poured in (e.g. a bowl kept on a plate)
    if h and h.plate and #h.parts == 1 and Food.box_accepts(run, st, h.parts[1]) then
      box_add(run, st, h.parts[1])
      h.parts = {}
      Snd.plop()
      return true
    end
    if h and is_food(h) then return false, "not in here" end
    return false
  end

  -- crates
  if st.kind == "crate" and not st.item then
    if not h then
      chef.hold = Food.new_ing(st.ing)
      st.bump = 0.25
      return true
    end
    if h.plate and Food.plate_ok(run, h.parts, st.ing) then
      h.parts[#h.parts + 1] = st.ing
      st.bump = 0.25
      return true
    end
    -- anything else is put down on the lid
    st.item = h
    chef.hold = nil
    return true
  end

  -- counters, boards, belts, crate lids
  if def.hold or st.kind == "crate" then
    local it = st.item
    if not h and it then
      chef.hold = it
      st.item = nil
      return true
    end
    if h and not it then
      st.item = h
      chef.hold = nil
      st.bump = 0.12
      return true
    end
    if h and it then
      if h.plate and is_food(it) then
        if Food.plate_ok(run, h.parts, it.key) then
          h.parts[#h.parts + 1] = it.key
          st.item = nil
          return true
        end
        return false, "not for this dish"
      end
      if it.plate and is_food(h) then
        if Food.plate_ok(run, it.parts, h.key) then
          it.parts[#it.parts + 1] = h.key
          chef.hold = nil
          return true
        end
        return false, "not for this dish"
      end
      if h.dirty and it.dirty then
        it.dirty = it.dirty + h.dirty
        chef.hold = nil
        return true
      end
      -- a plate on a plate: pour the one in hand onto the other if it fits
      if h.plate and it.plate and #h.parts > 0 then
        local all = true
        local parts = {}
        for _, k in ipairs(it.parts) do parts[#parts + 1] = k end
        for _, k in ipairs(h.parts) do
          if not Food.plate_ok(run, parts, k) then all = false break end
          parts[#parts + 1] = k
        end
        if all then
          it.parts = parts
          h.parts = {}
          return true
        end
      end
    end
    return false
  end
  return false
end

-- A thrown item lands on a station: true if the station took it.
function Food.catch(run, st, item)
  if st.fire > 0 then return false end
  if st.kind == "trash" then
    if item.dirty or item.key == "ext" then return false end
    Snd.trash()
    return true
  end
  if st.box then
    if is_food(item) and Food.box_accepts(run, st, item.key) then
      box_add(run, st, item.key)
      Snd.plop()
      return true
    end
    return false
  end
  if st.kind == "serve" then
    if item.plate and #item.parts > 0 then
      Ord.serve(run, item, st, nil)
      return true
    end
    return false
  end
  if st.kind == "sink" and item.dirty then
    st.dirty = st.dirty + item.dirty
    return true
  end
  if (st.def.hold or st.kind == "crate") then
    local it = st.item
    if not it then
      st.item = item
      st.bump = 0.2
      return true
    end
    if it.plate and is_food(item) and Food.plate_ok(run, it.parts, item.key) then
      it.parts[#it.parts + 1] = item.key
      st.bump = 0.2
      return true
    end
  end
  return false
end

---------------------------------------------------------------- X: chop, wash

-- What X does at this station; nil if nothing.
function Food.work_kind(run, chef, st)
  if st.fire > 0 then return nil end
  if st.kind == "board" and st.item and st.item.key then
    local k = Food.parse(st.item.key)
    if k.ing and not k.chopped and Data.ING[k.ing].chop and not chef.hold then return "chop" end
  end
  if st.kind == "sink" and st.dirty > 0 and not chef.hold then return "wash" end
  if st.kind == "valve" and st.leak then return "valve" end
  return nil
end

-- One frame of work; returns true while there is more to do.
function Food.work(run, chef, st, kind, dt)
  if kind == "chop" then
    local it = st.item
    if not it or not it.key then return false end
    local k = Food.parse(it.key)
    if k.chopped or not k.ing then return false end
    local need = Data.ING[k.ing].chop
    it.chop = (it.chop or 0) + dt * chef.def.chop * speed(run, "chop") / need
    if it.chop >= 1 then
      it.key = k.ing .. "/c"
      it.chop = nil
      st.bump = 0.2
      Snd.chopped()
      Fx.bits(st.x, Kit.TOP + 0.1, st.z, Data.ING[k.ing].c2)
      G.stats_add("chopped", 1)
      return false
    end
    return true
  elseif kind == "wash" then
    if st.dirty <= 0 then return false end
    st.prog = st.prog + dt * speed(run, "wash") / 1.4
    if st.prog >= 1 then
      st.prog = 0
      st.dirty = st.dirty - 1
      st.clean = st.clean + 1
      Snd.plate()
      Fx.puff(st.x, Kit.TOP + 0.2, st.z, 0xD0E8FF, 2)
      G.stats_add("washed", 1)
      return st.dirty > 0
    end
    return true
  elseif kind == "valve" then
    return Dis.fix_valve(run, chef, st, dt)
  end
  return false
end
