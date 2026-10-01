#!/usr/bin/env python3
"""Writes carts/nano8/roms/nanodemo.p8, the demo cart of nano8 (our own):
"Comet Catcher". Sprites and map are drawn here as text, the sound effects
and the music as lists of notes, so the cart can be made again without an
editor: python3 carts/nano8/mkdemo.py

The game shows what nano8 does: sprites and map, the symbols of the
buttons in the text, wide and tall letters, a fill pattern, palette
flashes, particles, sound effects and a looping tune, persistent data
(the best score) and an item of the pause menu.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- sprites
# one hex digit per pixel (the colour), "." = 0
SPRITES = {
    1: ["...cc...",
        "..c77c..",
        "..c77c..",
        ".cc77cc.",
        "c6cccc6c",
        "c66cc66c",
        ".8....8.",
        ".9....9."],
    2: ["...aa...",
        "...aa...",
        "aaaaaaaa",
        ".aa99aa.",
        "..aaaa..",
        ".aa..aa.",
        ".a....a.",
        "........"],
    3: ["..5555..",
        ".554445.",
        "5544d445",
        "54d44445",
        "5444d445",
        "5544445.",
        ".55545..",
        "...55..."],
    4: ["3b3b3b3b",
        "33333333",
        "43334333",
        "44444444",
        "44f44444",
        "44444444",
        "4444f444",
        "44444444"],
    5: ["44444444",
        "444f4444",
        "44444444",
        "4f444444",
        "44444444",
        "44444f44",
        "44444444",
        "44444444"],
    6: ["........",
        "...7....",
        "..777...",
        ".77777..",
        "..777...",
        "...7....",
        "........",
        "........"],
}


def gfx():
    px = [[0] * 128 for _ in range(128)]
    for n, rows in SPRITES.items():
        ox, oy = n % 16 * 8, n // 16 * 8
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                px[oy + y][ox + x] = 0 if ch == "." else int(ch, 16)
    return ["".join("%x" % v for v in row) for row in px]


def gff():
    flags = [0] * 256
    flags[4] = flags[5] = 1          # the ground (flag 0)
    return ["".join("%02x" % f for f in flags[i * 128:(i + 1) * 128]) for i in range(2)]


def map_rows():
    cells = [[0] * 128 for _ in range(32)]
    for x in range(16):
        cells[0][x] = 4
        cells[1][x] = 5
        cells[2][x] = 5
    return ["".join("%02x" % c for c in row) for row in cells]


# ---------------------------------------------------------------- sound
# a note: (pitch, waveform, volume, effect); None is a rest. Pitch 24 is
# middle C; waveforms: 0 triangle, 1 tilted saw, 2 saw, 3 square, 4 pulse,
# 5 organ, 6 noise, 7 phaser; effects: 1 slide, 2 vibrato, 3 drop, 4 fade
# in, 5 fade out, 6 and 7 arpeggios.

def sfx_line(notes, speed, loop=(0, 0)):
    notes = list(notes) + [None] * (32 - len(notes))
    out = "%02x%02x%02x%02x" % (0, speed, loop[0], loop[1])
    for n in notes[:32]:
        if n is None:
            out += "00000"
        else:
            p, w, v, e = n
            out += "%02x%x%x%x" % (p, w, v, e)
    return out


def tune():
    chords = [(36, 40, 43), (35, 38, 43), (33, 36, 40), (33, 36, 41)]   # C G Am F
    melody, bass, drums = [], [], []
    for (a, b, c), root in zip(chords, (24, 19, 21, 17)):
        for k, p in enumerate((a, b, c, b, a + 12, c, b, c)):
            melody.append((p, 4, 4 if k % 2 == 0 else 3, 2 if k == 4 else 0))
        for k in range(8):
            bass.append((root, 0, 5 if k % 4 == 0 else 3, 5 if k % 4 == 3 else 0))
    for k in range(32):
        if k % 8 == 0:
            drums.append((14, 6, 6, 3))          # kick: noise, dropping
        elif k % 8 == 4:
            drums.append((40, 6, 4, 5))          # snare
        elif k % 2 == 1:
            drums.append((60, 6, 2, 5))          # hat
        else:
            drums.append(None)
    return melody, bass, drums


def sfx_lines():
    lines = ["%02x%02x%02x%02x" % (0, 16, 0, 0) + "00000" * 32 for _ in range(64)]
    # 0 catch: a quick rising arpeggio
    lines[0] = sfx_line([(48, 3, 5, 0), (52, 3, 5, 0), (55, 3, 5, 0), (60, 3, 4, 5)], 3)
    # 1 hit: noise falling
    lines[1] = sfx_line([(34, 6, 7, 0), (30, 6, 6, 0), (26, 6, 5, 0), (22, 6, 4, 0), (18, 6, 3, 5)], 4)
    # 2 start: a jingle
    lines[2] = sfx_line([(36, 4, 5, 0), (40, 4, 5, 0), (43, 4, 5, 0), (48, 4, 5, 2), (48, 4, 5, 5)], 6)
    # 3 level up: an arpeggio effect on one long note
    lines[3] = sfx_line([(36, 5, 5, 6)] * 6 + [(36, 5, 4, 5)], 6)
    melody, bass, drums = tune()
    lines[8] = sfx_line(melody, 15)
    lines[9] = sfx_line(bass, 15)
    lines[10] = sfx_line(drums, 15)
    return lines


def music_lines():
    lines = ["00 41424344"] * 64
    lines[0] = "03 08090a44"             # loop start and loop back: plays forever
    return lines


# ---------------------------------------------------------------- code

CODE = r"""-- comet catcher
-- a nano8 demo, by bm
-- catch the stars, dodge the rocks

function _init()
 cartdata("bm_nano8_demo")
 best=dget(0)
 menuitem(1,"reset best",function()
  best=0 dset(0,0)
 end)
 stars={}
 for i=1,40 do
  add(stars,{x=rnd(128),y=rnd(128),s=0.2+rnd(1)})
 end
 sparks={}
 state="title"
 t,shake,flash=0,0,0
 music(0)
end

function start()
 state="play"
 score,lives,level=0,3,1
 px,vx=60,0
 items={}
 shake,flash,spawn=0,0,0
 sfx(2)
end

function _update60()
 t+=1
 for s in all(stars) do
  s.y+=s.s
  if (s.y>=128) s.y-=128 s.x=rnd(128)
 end
 if state=="title" then
  if (btnp(❎) or btnp(🅾️)) start()
 elseif state=="play" then
  play()
 elseif state=="over" then
  if (btnp(❎)) state="title"
 end
 for p in all(sparks) do
  p.x+=p.dx p.y+=p.dy p.dy+=0.05
  p.l-=1
  if (p.l<=0) del(sparks,p)
 end
 if (shake>0) shake-=1
 if (flash>0) flash-=1
end

function play()
 if (btn(⬅️)) vx-=0.3
 if (btn(➡️)) vx+=0.3
 if (btn(🅾️)) vx*=0.6
 vx*=0.88
 px=mid(0,px+vx,120)
 spawn-=1
 if spawn<=0 then
  add(items,{
   x=rnd(120),y=-8,
   dy=0.5+rnd(0.5)+level*0.08,
   rock=rnd(1)<0.22+level*0.03
  })
  spawn=24-min(level,14)
 end
 for it in all(items) do
  it.y+=it.dy
  if it.y>100 then
   del(items,it)
  elseif abs(it.x-px)<7 and abs(it.y-96)<6 then
   del(items,it)
   if it.rock then
    lives-=1
    shake,flash=12,6
    sfx(1)
    burst(it.x+4,it.y+4,9)
    if lives<=0 then
     state="over"
     if (score>best) best=score dset(0,best)
    end
   else
    score+=1
    sfx(0)
    burst(it.x+4,it.y+4,10)
    if score%10==0 then
     level+=1
     sfx(3)
    end
   end
  end
 end
end

function burst(x,y,c)
 for i=1,14 do
  local a=rnd(1)
  add(sparks,{
   x=x,y=y,
   dx=cos(a)*rnd(1.6),dy=sin(a)*rnd(1.6)-0.6,
   l=18+rnd(12),c=c
  })
 end
end

function _draw()
 if (flash>0) pal(1,2)
 cls(1)
 pal()
 if shake>0 then
  camera(rnd(4)-2,rnd(4)-2)
 end
 for s in all(stars) do
  pset(s.x,s.y,s.s>0.9 and 7 or 13)
 end
 map(0,0,0,104,16,3)
 if state!="title" then
  for it in all(items) do
   spr(it.rock and 3 or 2,it.x,it.y)
  end
  if (state=="play") spr(1,px,96)
 end
 for p in all(sparks) do
  pset(p.x,p.y,p.l>8 and p.c or 8)
 end
 camera()
 if state=="title" then
  title()
 else
  print("score "..score,2,2,7)
  for i=1,lives do
   print("♥",126-i*8,2,8)
  end
  print("lv"..level,2,9,13)
 end
 if state=="over" then
  rectfill(30,52,97,74,0)
  rect(30,52,97,74,8)
  print("game over",46,56,8)
  print("❎ again",48,66,7)
 end
end

function title()
 fillp(▒)
 rectfill(12,26,115,88,0x10)
 fillp()
 rect(12,26,115,88,12)
 print("\^w\^tcomet",34,34,10)
 print("catcher",50,48,9)
 spr(2,30,48) spr(2,90,48)
 print("best "..best,50,60,6)
 if (t\30%2==0) print("press ❎ to start",31,74,7)
 print("⬅️➡️ move   🅾️ brake",22,96,6)
end
"""


def main():
    out = ["pico-8 cartridge // http://www.pico-8.com", "version 41", "__lua__"]
    out += CODE.rstrip("\n").split("\n")
    out += ["__gfx__"] + gfx()
    out += ["__gff__"] + gff()
    out += ["__map__"] + map_rows()
    out += ["__sfx__"] + sfx_lines()
    out += ["__music__"] + music_lines()
    path = os.path.join(HERE, "roms", "nanodemo.p8")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(path)


if __name__ == "__main__":
    main()
