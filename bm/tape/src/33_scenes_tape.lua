-- bm Tape TAPE scene (design 4.3): two reels above four track lanes. A lane shows the clips of its track (runs of equal
-- song slots) in a window of 16 slots that scrolls with the cursor; the loop markers and the play head are the song's.
-- The reels turn only while the transport runs. The clip of the current track is white (encoder 4), the others are grid.
local tape_page
do
local TW, TX, TY = 18, 26, 78                       -- slot width, left edge of the lanes, top of the first lane (units)
SONG.win = 0                                        -- the first slot of the window (0-based)

tape_page = { name = "TAPE", params = {
  P("SLOT", 1, NSONG, 1, 1, "%02d"), P("LOOP IN", 1, NSONG, 1, 1, "%02d"),
  P("LOOP OUT", 1, NSONG, 1, 4, "%02d"), P("TRACK", 1, 4, 1, 2, "%d") } }

function tape_page.sub()
  local p = S.pat[S.trk]
  return string.format("T%d PAT %s", S.trk, LETTERS:sub(p, p))
end
function tape_page.pull(sc)
  local pr = sc.params
  pr[1].value, pr[2].value, pr[3].value, pr[4].value = S.sc + 1, S.lin, S.lout, S.trk
  SONG.win = clamp(clamp(SONG.win, S.sc - 15, S.sc), 0, NSONG - 16)
end
function tape_page.push(sc, i)
  local v = sc.params[i].value
  if i == 1 then S.sc = v - 1
  elseif i == 2 then S.lin, S.lout = v, max(S.lout, v)
  elseif i == 3 then S.lin, S.lout = min(S.lin, v), v
  else select_track(v) end
end

function tape_page.draw(t, v)
  local a0 = tr.run and tick_at(audio_time()) * 0.03 or 0
  for _, x in ipairs({ 112, 208 }) do
    O(x, 44, 22, 22, C.e4); O(x, 44, 6, 6, C.e4)
    for k = 0, 2 do
      local a = a0 + k * 2.094
      L(x + cos(a) * 7, 44 + sin(a) * 7, x + cos(a) * 20, 44 + sin(a) * 20, C.grid)
    end
  end
  O(112, 44, 15, 15, C.grid); O(208, 44, 11, 11, C.grid)
  L(112, 66, 208, 66, C.e4); R(152, 62, 16, 7, C.hot)
  local w0 = SONG.win
  for i = 1, 4 do
    T("T" .. i, 8, TY + (i - 1) * 13 + 7, i == S.trk and C.e4 or C.mute)
  end
  clipu(24, 70, 296, 66)
  for j = 0, 16 do                                    -- the ruler: a longer tick at each fourth slot
    local x = TX + j * TW
    L(x, 71, x, (w0 + j) % 4 == 0 and 76 or 74, (w0 + j) % 4 == 0 and C.mute or C.dim)
  end
  for i = 1, 4 do
    local y, mine = TY + (i - 1) * 13, i == S.trk
    L(TX, y + 11, TX + 16 * TW, y + 11, C.dim)
    for _, c in ipairs(SONG.clips(i)) do
      local x0, x1 = TX + (c[1] - w0) * TW, TX + (c[2] + 1 - w0) * TW
      if x1 > TX and x0 < TX + 16 * TW then
        R(x0 + 1, y, x1 - x0 - 2, 9, mine and C.e4 or C.grid)
        T(LETTERS:sub(c[3], c[3]), x0 + 4, y + 7, mine and C.e4 or C.grid)
      end
    end
  end
  local lx, rx = TX + (v[2] - 1 - w0) * TW, TX + (v[3] - w0) * TW     -- loop in (pink) and out (amber)
  L(lx, 73, lx, 131, C.e2, 1.6); L(rx, 73, rx, 131, C.e3, 1.6); L(lx, 73, rx, 73, C.dim)
  R(TX + (v[1] - 1 - w0) * TW, 73, TW, 58, C.e1)                      -- the cursor slot (cyan)
  if tr.run and S.song then                                           -- the play head runs over the loop
    local len = S.len * TPS
    local pos = (tick_at(audio_time()) % ((S.lout - S.lin + 1) * len)) / len
    local x = TX + (S.lin - 1 + pos - w0) * TW
    L(x, 72, x, 132, C.hot, 1.6)
  end
  clip()
end
end
