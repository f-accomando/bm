-- bm Tape pages (design 3, 5): thirteen pages in the order of the arrows. F1..F8 jump to the first page of a module;
-- Tab goes to the next page of the module. A preview page is a scene of phase 1 that makes no sound yet (DJ).
local PAGES = {
  { label = "SYNTH ENGINE", mod = 1, sc = engine_page }, { label = "SYNTH ENVELOPE", mod = 1, sc = env_page },
  { label = "SYNTH FILTER", mod = 1, sc = flt_page }, { label = "DRUM PAD", mod = 2, sc = drum_page },
  { label = "SEQ GRID", mod = 3, sc = grid_page }, { label = "TAPE", mod = 4, sc = tape_page },
  { label = "MIX LEVEL", mod = 5, sc = mixl_page }, { label = "MIX PAN", mod = 5, sc = mixp_page },
  { label = "FX ECHO", mod = 6, sc = fxe_page }, { label = "FX ROOM", mod = 6, sc = fxr_page },
  { label = "FX CHORUS", mod = 6, sc = fxc_page },
  { label = "DJ", mod = 7, sc = dj_page, preview = true }, { label = "TEMPO", mod = 8, sc = tempo_page },
}
local MODFIRST = { 1, 4, 5, 6, 7, 9, 12, 13 }

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
