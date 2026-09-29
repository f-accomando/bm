-- Progress on the SD card (save() / saved()): stars and best score of each
-- stage, how far the campaign is open, the recipes met, endless records and
-- statistics. Written after a stage, a run or an options change, never
-- during play (the SD card takes a few milliseconds).

Save.data = nil

local function defaults()
  return {
    v = 1,
    stars = {},             -- stage id -> 0..3
    best = {},              -- stage id -> best coins
    seen = {},              -- recipe id -> true
    endless = { time = 0, money = 0, served = 0 },
    stats = {},             -- counter name -> number
    music = true, sound = true,
  }
end

function Save.load()
  local d = saved()
  if type(d) ~= "table" or d.v ~= 1 then d = defaults() end
  local def = defaults()
  for k, v in pairs(def) do if d[k] == nil then d[k] = v end end
  Save.data = d
  Snd.music_on = d.music ~= false
  Snd.on = d.sound ~= false
end

function Save.write()
  if G.no_save then return end
  local ok, why = save(Save.data)
  if not ok then log("save failed: " .. tostring(why)) end
end

-- total stars, and whether a stage can be played
function Save.total_stars()
  local n = 0
  for _, s in pairs(Save.data.stars) do n = n + s end
  return n
end

-- a stage opens when the one before has at least one star; the first stage
-- of a world also needs some stars in total
Data.WORLD_NEEDS = { 0, 6, 14, 24, 34, 46 }
function Save.open(stage)
  if stage.n == 1 or G.all_open then return true end
  local prev = Data.STAGES[stage.n - 1]
  if (Save.data.stars[prev.id] or 0) < 1 then return false end
  return Save.total_stars() >= Data.WORLD_NEEDS[stage.world]
end

function G.stats_add(key, n)
  local s = Save.data and Save.data.stats
  if s then s[key] = (s[key] or 0) + n end
end

function G.recipe_seen(id)
  if Save.data then Save.data.seen[id] = true end
end
