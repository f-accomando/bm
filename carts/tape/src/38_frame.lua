-- bm Tape frame (design 4.1): status strip on top, value strip at the bottom.
local function fmt_param(p)
  local f = p.format
  if type(f) == "function" then return f(p.value) end
  return string.format(f, p.value)
end

local function frame(page, t)
  local sc = page.sc
  -- status strip: page, REC, PREVIEW, track, tempo, bar.beat.step, muted tracks, voice lights
  T(page.label, 6, 9, C.e4)
  if tr.rec then T("REC", 66, 9, C.hot) end
  if page.preview then T("PREVIEW", 82, 9, C.mute) end
  if engine.err then
    T("ENGINE " .. engine.err:sub(1, 33), 118, 9, C.hot)
  else
    T(string.format("T%d %s", S.trk, S.trk == 1 and "DRUM" or ENGINES[trk[S.trk].eng].name), 118, 9, C.mute)
    T(string.format("%.1f BPM", S.bpm), 162, 9, C.mute)
    local tk = now_tick()
    T(string.format("%02d.%d.%d", 1 + tk // 96, 1 + (tk // 24) % 4, 1 + (tk // 6) % 4), 214, 9, tr.run and C.e4 or C.mute)
    local m = ""
    for i = 1, 4 do
      if aget(Q.mutes, i) ~= 0 then m = m .. i end
    end
    if m ~= "" then T("M" .. m, 240, 9, C.mute) end
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
