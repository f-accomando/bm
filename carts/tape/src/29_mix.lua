-- bm Tape MIX and FX (design 3). The kernel has no master bus and no scope tap: the level of a track is the volume of
-- each note it plays; its pan and sends are keys of the note's tone; a meter rises by the velocity times the level of
-- each note when the note starts (the notes the sequencer queued, and the live ones) and falls with engine.meter_fall.
-- private helpers: the file is one block (a chunk holds 200 locals at most)
do
local MIXC = setmetatable({}, { __mode = "k" })    -- base tone -> { ver, tone }: the base with pan, sends and punch-in crush
MIX.ver, MIX.lv, MIX.pk, MIX.hits, MIX.t = 0, nil, nil, {}, 0

-- a change of pan, a send or a punch-in crush: the cached tones are made again
function MIX.touch() MIX.ver = MIX.ver + 1 end

-- the tone of a note of track t. when (audio_time) is the start of a queued note: the punch-in filter sweep cuts it.
function MIX.tone(base, t, when)
  local c = MIXC[base]
  if not c or c.ver ~= MIX.ver then
    local tn = {}
    for k, v in pairs(base) do tn[k] = v end
    tn.pan, tn.reverb, tn.echo, tn.chorus = S.pan[t], S.send[t][1], S.send[t][2], S.send[t][3]
    if PUNCH.crush then tn.crush, tn.coarse = PUNCH.CRUSH_BITS, PUNCH.CRUSH_COARSE end
    c = { ver = MIX.ver, tone = tn }
    MIXC[base] = c
  end
  if PUNCH.sweep and when then
    local tn = {}
    for k, v in pairs(c.tone) do tn[k] = v end
    local cut = PUNCH.cutoff(when)
    tn.cutoff, tn.filter = (tn.cutoff and tn.cutoff > 0) and min(tn.cutoff, cut) or cut, tn.filter or "lp"
    return tn
  end
  return c.tone
end

function MIX.init()
  if engine.mod then MIX.lv, MIX.pk = engine.buf(4), engine.buf(8) end
end

-- a note of track t with the volume vol (0..1) starts at when (audio_time; now if nil)
function MIX.hit(t, vol, when)
  if MIX.lv then MIX.hits[#MIX.hits + 1] = { when or 0, t, floor(clamp(vol, 0, 1) * 1000) } end
end

-- once per frame: the notes that have started raise their meters, then the meters fall by the time since the last frame
function MIX.step(now)
  if not MIX.lv then return end
  local at, keep = audio_time(), {}
  for _, h in ipairs(MIX.hits) do
    if h[1] <= at then
      aset(MIX.lv, h[2], max(aget(MIX.lv, h[2]), h[3]))
    else
      keep[#keep + 1] = h
    end
  end
  MIX.hits = keep
  engine.meter_fall(MIX.lv, MIX.pk, 4, clamp(floor((now - MIX.t) * 1000), 0, 100))
  MIX.t = now
end

-- the level and the peak (0..1000) of meter i
function MIX.meter(i)
  if not MIX.lv then return 0, 0 end
  return aget(MIX.lv, i), aget(MIX.pk, i)
end

-- ---------------------------------------------------------------- FX: the three effects of the kernel, the same for all voices
-- S.fx holds the settings; the kernel's values at the start are the defaults. The sends of a track are in S.send.
local function copy3(t) return { t[1], t[2], t[3] } end

function FX.init()
  local a, b, c = reverb()
  local d, e, f = echo()
  local g, h, i = chorus()
  FX.def = { room = { a, b, c }, echo = { d, e, f }, chorus = { g, h, i } }
  FX.reset()
end

-- the settings back to the defaults, and into the kernel
function FX.reset()
  S.fx = { room = copy3(FX.def.room), echo = copy3(FX.def.echo), chorus = copy3(FX.def.chorus) }
  FX.apply()
end

-- send the settings of one effect (name) or of all of them (nil) to the kernel
function FX.apply(name)
  local f = S.fx
  if not name or name == "room" then reverb(f.room[1], f.room[2], f.room[3]) end
  if not name or name == "echo" then echo(f.echo[1], f.echo[2], f.echo[3]) end
  if not name or name == "chorus" then chorus(f.chorus[1], f.chorus[2], f.chorus[3]) end
end
end
