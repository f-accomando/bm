function _init()
  Snd.init()
end
function _update()
  G.frame = G.frame + 1
  Scr.update()
end
function _draw()
  cls(0x102030)
  Scr.draw()
end
