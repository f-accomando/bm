-- The fixed chip "F12 keys" of the hint bars (U.hint_row in src/script/bmui.lua),
-- checked on the pages of one editor. Shared by the editors' host tests:
--   dofile(ROOT .. "/tests/studio/hintchip.lua")(o)
-- o = { name =, check =, texts = fn -> the texts drawn this frame {s, x, y},
--       hy = the hint bar's y, w = the screen's width, chip_w = fn(name) the width of a key's chip,
--       click = fn(x, y) a left click, shown = fn -> how many times showkeys() was called,
--       pages = { { name =, go = fn that shows the page }, ... } }
-- For each page: the chip and its label are drawn at the right end of the bar, on the
-- 8 px columns, and no other chip or label of the bar reaches them or leaves the
-- screen; a click on the chip opens the keys.
return function(o)
  local n = 0
  for _, pg in ipairs(o.pages) do
    pg.go()
    local row, chip, label = {}, nil, nil
    for _, t in ipairs(o.texts()) do
      if t[3] == o.hy then
        row[#row + 1] = t
        if t[1] == "[f12]" then chip = t end
      end
    end
    for _, t in ipairs(row) do if chip and t[1] == "keys" and t[2] > chip[2] then label = t end end
    local what = o.name .. ", " .. pg.name .. " (" .. o.w .. " px)"
    o.check(chip and label, what .. ": the fixed chip F12 keys is drawn")
    if chip and label then
      o.check(chip[2] % 8 == 0 and label[2] % 8 == 0, what .. ": the fixed chip is on the 8 px columns")
      o.check(label[2] + 32 <= o.w and label[2] + 32 >= o.w - 16, what .. ": the fixed chip is at the right end, inside the screen")
      local bad
      for _, t in ipairs(row) do
        if t ~= chip and t ~= label then
          local inner = t[1]:match("^%[(.+)%]$")
          local w = inner and o.chip_w(inner) or #t[1] * 8
          if t[2] + w > chip[2] - 8 then bad = t[1] end
        end
      end
      o.check(not bad, what .. ": the bar never reaches the fixed chip" .. (bad and (" (" .. bad .. ")") or ""))
      local before = o.shown()
      o.click(chip[2] + 4, chip[3] + 8)
      o.check(o.shown() == before + 1, what .. ": a click on the fixed chip opens the keys")
    end
    n = n + 1
  end
  return n
end
