Snd = {}
function Snd.init()
  envelope(0, 0, 20, 0, 10)
  envelope(1, 0, 30, 20, 15)
end
function Snd.hit() play(0, 800, 80, 2, 120) end
function Snd.swing() play(1, 600, 60, 1, 90) end
function Snd.jump() play(1, 200, 100, 0, 70) end
-- NeoGeo style punchy SFX
