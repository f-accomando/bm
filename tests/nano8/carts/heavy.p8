pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- heavy init
-- more than the console allows in one frame: nano8 spreads it
function _init()
 local x=0
 -- 9 million turns (one loop cannot count that far: 16.16 numbers)
 for i=1,300 do
  for j=1,30000 do x+=1 end
 end
 printh("heavy: done "..x\1000)
end
function _draw()
 cls(12)
 print("done",4,4,7)
end
