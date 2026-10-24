-- Test fixture (not a game): a cartridge with 8 models, one skeleton
-- and a sheet, made from models.bm. The studio and resource tests open it.
function _init()
  for _, name in ipairs(models()) do model(name) end
end

function _draw()
  cls(0)
  print("Studio Village (test fixture)", 8, 8, 0xFFFFFF)
end
