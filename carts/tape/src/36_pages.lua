-- bm Tape pages (design 3, 5): ten pages in the order of the arrows. F1..F8 jump to the first page of a module;
-- Tab goes to the next page of the module. A preview page is a scene of phase 1 that makes no sound yet.
local PAGES = {
  { label = "SYNTH ENGINE", mod = 1, sc = engine_page }, { label = "SYNTH ENVELOPE", mod = 1, sc = env_page },
  { label = "SYNTH FILTER", mod = 1, sc = flt_page }, { label = "DRUM PAD", mod = 2, sc = drum_page },
  { label = "SEQ GRID", mod = 3, sc = grid_page }, { label = "TAPE", mod = 4, sc = tape_page, preview = true },
  { label = "MIX", mod = 5, sc = mix_page, preview = true }, { label = "FX ECHO", mod = 6, sc = fx_page, preview = true },
  { label = "DJ", mod = 7, sc = dj_page, preview = true }, { label = "TEMPO", mod = 8, sc = tempo_page },
}
local MODFIRST = { 1, 4, 5, 6, 7, 8, 9, 10 }

-- the page after cur in the same module (wraps)
local function tab_page(cur)
  local m = PAGES[cur].mod
  for k = 1, #PAGES do
    local i = (cur - 1 + k) % #PAGES + 1
    if PAGES[i].mod == m then return i end
  end
  return cur
end

-- the page for the module n: the drum module takes track 1, the synth module a synth track
local function module_page(n)
  if n == 2 then select_track(1) elseif n == 1 and S.trk == 1 then select_track(S.syn) end
  return MODFIRST[n]
end
