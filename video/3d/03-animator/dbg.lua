
-- debug tail appended by probe.py: logs the state of bm Animator when it changes
local _u = _update
local last = ""
function _update()
  _u()
  local m = T.M()
  local r = m and m.rig
  local p = { "p=" .. tostring(S.page), "m=" .. tostring(S.msg), "rb=" .. rg.bone .. (rg.tail and "T" or "H") .. (rg.skin and "S" or ""),
              "ab=" .. an.bone, "ac=" .. an.clip, string.format("at=%.3f", an.t or 0), an.playing and "PLAY" or "" }
  if r then
    for i, b in ipairs(r.bones) do
      p[#p + 1] = string.format("[%d %s<%d (%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f)]", i, b.name, b.parent, b.head[1], b.head[2], b.head[3], b.tail[1], b.tail[2], b.tail[3])
    end
    for i, c in ipairs(r.clips) do
      p[#p + 1] = string.format("{%d %s L=%s len=%.3f mode=%d keys=%d}", i, c.name, tostring(c.loop), c.length, c.mode, #c.keys)
    end
  end
  local s = table.concat(p, " ")
  if s ~= last then log(S.frame .. " " .. s); last = s end
end
