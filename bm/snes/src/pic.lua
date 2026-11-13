-- Which frames get a picture. A picture every `ev` frames (ev = 1: all of them). A game that flickers a sprite on every other frame (Zelda: A Link to the Past,
-- Link while he cannot be hurt; many games, to get round the limit of sprites on a line) shows it in the pictures of one parity of the frame count only if the
-- gap between the pictures is even: with `ev` 2 or 4 the sprite is in every picture or in none of them, and on a Pi that skips frames Link was gone for the whole
-- time he could not be hurt. For an even `ev` the gap is therefore `ev`, `ev`, `ev + 1` (a block of 3 * ev + 1 frames with three pictures): the parity of the pictures
-- changes every three pictures, so the sprite shows in half of them (the picture rate falls by 1 in 3 * ev + 1 frames).
function pic_due(f, ev)
  if ev % 2 == 1 then return f % ev == 0 end
  local o = f % (3 * ev + 1)
  return o == 0 or o == ev or o == 2 * ev
end
