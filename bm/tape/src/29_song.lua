-- bm Tape song (design 3, TAPE): a row of NSONG slots for each track. A slot lasts one pattern length (S.len steps)
-- and holds a pattern number (0 = empty, 1..8 = A..H). A clip is a run of equal, non-empty slots.
local LETTERS = "ABCDEFGH"

function SONG.get(t, j) return aget(SL, (t - 1) * NSONG + j + 1) end
function SONG.set(t, j, p) aset(SL, (t - 1) * NSONG + j + 1, p) end

-- the clips of track t: { { first slot, last slot, pattern }, ... } (slots 0-based)
function SONG.clips(t)
  local out, j = {}, 0
  while j < NSONG do
    local p = SONG.get(t, j)
    if p > 0 then
      local e = j
      while e + 1 < NSONG and SONG.get(t, e + 1) == p do e = e + 1 end
      out[#out + 1] = { j, e, p }
      j = e
    end
    j = j + 1
  end
  return out
end

-- T on the TAPE page: the selected pattern of the current track goes into the slot at the cursor
function SONG.write() SONG.set(S.trk, S.sc, S.pat[S.trk]) end
-- Backspace: the slot at the cursor is empty; Shift+Backspace: the whole row of the track
function SONG.clear() SONG.set(S.trk, S.sc, 0) end
function SONG.clear_row() for j = 0, NSONG - 1 do SONG.set(S.trk, j, 0) end end

-- the previous or next pattern (A..H) of the current track
function SONG.pattern(d) S.pat[S.trk] = clamp(S.pat[S.trk] + d, 1, NPAT) end

-- P: PATTERN or SONG mode. While the transport plays it starts again from the loop start.
function SONG.mode()
  S.song = not S.song
  if tr.run then
    transport_stop()
    transport_start()
  end
end
