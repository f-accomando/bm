-- bm Tape frame (design 4.1): status strip on top, value strip at the bottom.
local function fmt_param(p)
  local f = p.format
  if type(f) == "function" then return f(p.value) end
  return string.format(f, p.value)
end

local function frame(page, t)
  local sc = page.sc
  -- status strip: page, PREVIEW (DJ), REC, punch-in effect, then track and pattern, tempo, PAT or SONG, bar.beat.step, muted
  -- tracks, voice lights. A message (SAVED, an error) takes the place of the middle part for 2 s.
  T(page.label, 6, 9, C.e4)
  if page.preview then T("PREVIEW", 6 + #page.label * 4 + 6, 9, C.mute) end
  if tr.rec then T("REC", 66, 9, C.hot) end
  local fx = PUNCH.label()
  if fx then T(fx, 82, 9, C.hot) end
  if SV.msg and t < SV.until_t then
    T(SV.msg, 114, 9, SV.err and C.hot or C.e4)
  elseif engine.err then
    T("ENGINE " .. engine.err:sub(1, 33), 114, 9, C.hot)
  else
    local p = S.pat[S.trk]
    T(string.format("T%d %s %s", S.trk, LETTERS:sub(p, p), S.trk == 1 and "DRUM" or ENGINES[trk[S.trk].eng].name), 114, 9, C.mute)
    T(string.format("%.1f", S.bpm), 166, 9, C.mute)
    T(S.song and "SONG" or "PAT", 190, 9, S.song and C.e4 or C.mute)
    local tk = now_tick()
    T(string.format("%02d.%d.%d", 1 + tk // 96, 1 + (tk // 24) % 4, 1 + (tk // 6) % 4), 210, 9, tr.run and C.e4 or C.mute)
    local m = ""
    for i = 1, 4 do
      if aget(Q.mutes, i) ~= 0 then m = m .. i end
    end
    if m ~= "" then T("M" .. m, 238, 9, C.mute) end
  end
  for i = 0, 7 do R(264 + i * 6, 4, 4, 5, playing(i) and C.hot or C.dim, true) end
  L(0, 13, 320, 13, C.dim)
  L(0, BOT + 2, 320, BOT + 2, C.dim)
  if sc.sub then T(sc.sub(), 6, 24, C.mute) end
  -- value strip: one cell per encoder
  for i, p in ipairs(sc.params) do
    T(p.label, 6 + (i - 1) * 79, 146, ECOL[i])
    T(fmt_param(p), 6 + (i - 1) * 79, 155, C.mute)
  end
end
