pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- yharnam 8
-- by bm

-- the hunt of yharnam, to the measure of a pico-8 cart: written by
-- carts/yharnam/pico8/mk.py (the data in $... comes from there)

-- the night's palette: the darker colours of the second set
scr=split"129,130,131,4,5,6,7,136,9,135,139,12,13,132,15"
-- one step darker, colour by colour (the light's levels)
dk=split"0,1,1,2,1,13,6,2,4,9,3,13,5,2,4"

-- a table from a string: "k1,k2|v1,v2|..." -> {{k1=v1,k2=v2},...}
function rows(s)
 local l,t=split(s,"|"),{}
 local k=split(deli(l,1))
 for r in all(l) do
  local o,v={},split(r)
  for i=1,#k do o[k[i]]=v[i] end
  add(t,o)
 end
 return t
end

-- the creatures: 1 townsman, 2 rifleman, 3 scourge beast, 4 mad hunter,
-- 5 celestial child; the bosses 6-9. s sprite, z size (0 small, 1 tall,
-- 2 big), sp speed, r reach, w wind-up, c recovery, k distance kept,
-- o circling, n blows in a row, e dodging, ec echoes, pm poise, l lunge
ty=rows"s,z,hp,sp,r,dmg,w,c,k,o,n,e,ec,pm,l,mv,nm|70,1,6,.5,14,3,22,24,16,0,1,0,20,99,1.2,m,townsman|70,1,5,.4,90,3,40,30,60,0,1,0,25,99,0,g,rifleman|72,1,9,.8,14,3,14,20,20,.6,3,0,40,99,1.6,m,scourge beast|68,1,10,.75,15,3,18,16,28,.4,2,.5,50,99,2,m,mad hunter|74,0,4,.7,10,2,12,16,10,0,1,0,30,99,1.5,m,celestial child|96,2,110,.55,20,4,22,22,18,0,1,0,600,25,1.4,m,the butcher|100,2,110,.9,22,4,18,20,26,.5,1,0,700,22,2,m,the great hound|104,2,130,.75,18,4,18,18,22,.4,1,.2,800,24,1.8,m,father graves|108,2,150,.5,28,4,24,24,30,.3,1,0,900,28,0,m,the watcher"
-- the bosses' moves, a phase to a list (m<n>: n blows in a row)
bmv={"m1,m1,ring|m1,m2,ring,fire|m3,ring,fire,leap","m1,leap|m2,leap,ring|m3,leap,ring,leap","m1,gun|m2,leap,gun|m3,fire,ring,leap","beam,m1|beam,ring,fire|beam,m3,fire,ring"}
-- the saw cleaver, folded and open: wind-up, blow, recovery, reach,
-- damage, stamina
fm=rows"w,a,c,r,d,s|3,3,7,13,2,12|5,3,10,17,3.2,16"
-- the paths of the lamp
pth=split"feral affinity,moonlit breath,quicksilver rite,serrated oath,hunter's path"
pdesc=split"flesh grown thick,the moon's slow breath,the gun bites deep,the open saw bites,the folded saw sings"

function _init()
 lut={}
 for d=1,3 do
  lut[d]={}
  for b=0,255 do
   local l,h=b%16,b\16
   for i=1,d do l,h=dk[l] or 0,dk[h] or 0 end
   lut[d][b]=l+h*16
  end
 end
 -- half widths of the rings of light, row by row (odd rows a little wider:
 -- a dither on the edges)
 hw={}
 for k,r in ipairs(split"12,21,30,39") do
  hw[k]={}
  for d=0,39 do hw[k][d]=d<r and flr(sqrt(r*r-d*d)+d%2*.7) or 0 end
 end
 -- the map: what needs ground under it, the lights, the fires, the lamps
 under,lights,fires,shr={},{},{},{}
 local n=split"-1,0,1,0,0,-1,0,1,-2,0,2,0,0,2"
 for y=0,63 do
  for x=0,127 do
   local t=mget(x,y)
   if fget(t,3) then
    for i=1,#n,2 do
     local u=mget(x+n[i],y+n[i+1])
     if u>0 and u<13 and not under[y*128+x] then under[y*128+x]=u end
    end
    under[y*128+x]=under[y*128+x] or 1
   end
   if fget(t,1) then add(lights,{x*8+4,y*8+3}) end
   if t==38 or t==39 then add(fires,{x*8+(t==39 and 8 or 4),y*8+(t==39 and 4 or 1),t==39}) end
   if t==52 then add(shr,{x=x*8+4,y=y*8+8}) end
  end
 end
 sp=split"0,4,16,6,55,20,1,14,16,1,17,15,1,22,8,2,24,6,1,28,13,1,33,19,2,36,9,1,31,22,1,41,15,1,43,24,2,45,7,1,20,24,7,118,25,3,74,15,3,84,9,1,83,20,3,90,5,4,94,12,3,97,26,2,102,5,3,106,13,4,90,25,3,111,18,1,78,4,8,72,48,4,112,44,3,116,49,5,94,41,4,98,53,5,104,56,1,84,40,2,108,40,4,86,53,5,92,57,3,121,47,9,8,51,5,50,47,5,52,48,4,46,42,5,36,43,5,38,51,1,33,53,4,28,47,5,25,41,5,27,53,2,40,54,1,48,50"
 gt=split"63,14,63,17,1,112,31,115,31,2,64,46,64,49,3"
 st,t0="title",0
 new_hunt()
end

-- a new hunt: no echoes, no lamps lit, the start
function new_hunt()
 G={ec=0,won={},path={},t=0,kills=0,deaths=0}
 for s in all(shr) do s.lit=false end
 P={x=sp[2]*8+4,y=sp[3]*8+6,hx=sp[2]*8+4,hy=sp[3]*8+6,fx=0,fy=1,d=0,r=3,form=0,cmb=0,ang=.75,wk=0,inv=0,stw=0,ral=0,ho=0,hx2=0}
 paths()
 respawn()
 cx,cy=cam()
end

-- back at the last lamp lit (the start, if none), whole; the town fills again
function respawn()
 P.x,P.y,P.hp,P.st,P.act,P.ral,P.lock,P.q=P.hx,P.hy,20,100,nil,0,nil,nil
 F,S,R={},{},{}
 for i=4,#sp,3 do
  local k=sp[i]
  if k<6 or not G.won[k-5] then
   local T=ty[k]
   add(F,{T=T,x=sp[i+1]*8+4,y=sp[i+2]*8+6,hp=T.hp,st="idle",tm=0,cd=0,fx=-1,wk=0,fl=0,po=0,ph=1,
    boss=k>5 and k-5,side=rnd()<.5 and 1 or -1,rad=T.z==2 and 7 or 4,r=T.z==2 and 6 or 3})
  end
 end
 music(-1)
 boss=nil
end

-- the paths taken: the first counts the most, again less each time, a
-- mix less than one path followed
function paths()
 local v,n,dc={0,0,0,0,0},{0,0,0,0,0},0
 for p in all(G.path) do
  if n[p]==0 then dc+=1 end
  n[p]+=1
  v[p]+=(p==G.path[1] and 1 or .4)*.65^(n[p]-1)
 end
 for p=1,5 do v[p]*=1-.12*max(dc-1,0) end
 P.m={def=1-.18*v[1],stc=1-.2*v[2],reg=1+.4*v[2],gun=1+.7*v[3],par=flr(4*v[3]),stg=1+.4*v[3],
  open=1+.3*v[4],fold=1+.25*v[5],fst=1-.2*v[5]}
end

---------------------------------------------------------------- the world
function solid(x,y)
 if fget(mget(x\8,y\8),0) then return true end
 for i=1,#gt,5 do
  if not G.won[gt[i+4]] and x\8>=gt[i] and x\8<=gt[i+2] and y\8>=gt[i+1] and y\8<=gt[i+3] then return true end
 end
end

function blocked(x,y,r)
 return solid(x-r,y) or solid(x+r,y) or solid(x-r,y-3) or solid(x+r,y-3)
end

function move(o,dx,dy)
 if not blocked(o.x+dx,o.y,o.r) then o.x+=dx end
 if not blocked(o.x,o.y+dy,o.r) then o.y+=dy end
end

-- distance, safe from the 16.16 overflow of pico-8
function dist(a,b)
 local dx,dy=(b.x-a.x)/16,(b.y-a.y)/16
 return sqrt(dx*dx+dy*dy)*16,b.x-a.x,b.y-a.y
end

-- the region (1 town, 2 forest, 3 cathedral ward, 4 the old quarter)
function region(x,y)
 return y<256 and (x<512 and 1 or 2) or (x<512 and 4 or 3)
end

function cam()
 local g=region(P.x,P.y)
 local rx,ry=(g==1 or g==4) and 0 or 512,g<3 and 0 or 256
 return mid(rx,P.x-64,rx+384),mid(ry,P.y-72,ry+128)
end

---------------------------------------------------------------- input
-- two buttons: o tap a quick blow (in a row: a combo), o held a charged
-- blow; x tap a dodge, x held a run (standing: a blood vial); o and x
-- together the pistol (a parry, in a creature's wind-up), or after a
-- blow the trick: the saw changes, the combo goes on
function input()
 local o,x=btn(4),btn(5)
 if o and x and not used and P.ho<8 and P.hx2<8 then used=true want("pair") end
 if not o and P.ho>0 and not used then want(P.ho<8 and "light" or "heavy") end
 if not x and P.hx2>0 and not used and P.hx2<8 then want("dodge") end
 P.ho,P.hx2=o and P.ho+1 or 0,x and P.hx2+1 or 0
 if not (o or x) then used=false end
end

function want(a)
 P.q,P.qt=a,8
end

---------------------------------------------------------------- the hunter
function hunter()
 local m=P.m
 if P.inv>0 then P.inv-=1 end
 if P.ral>0 then P.ral=max(P.ral-.02,0) end
 -- the soft lock: the nearest creature awake, close
 P.lock=nil
 local best=99
 for f in all(F) do
  if f.st~="idle" and f.st~="dead" then
   local d=dist(P,f)
   if d<(f.boss and 90 or 48) and d<best then P.lock,best=f,d end
  end
 end
 local dx,dy=0,0
 if btn(0) then dx-=1 end
 if btn(1) then dx+=1 end
 if btn(2) then dy-=1 end
 if btn(3) then dy+=1 end
 if dx~=0 and dy~=0 then dx*=.7 dy*=.7 end
 P.mx,P.my=dx,dy
 local a=P.act
 if a=="dead" then
  P.at+=1
  if P.at==90 then
   G.deaths+=1
   if G.ec>=100 then G.ec-=100 respawn() else st,t0="lost",0 end
  end
  return
 end
 if P.q then
  P.qt-=1
  if P.qt<=0 then P.q=nil end
 end
 -- what was asked, if it can be done now
 local q=P.q
 if q then
  local free=not a or a=="run"
  local combo=a=="atk" and P.at>=P.sw.w+P.sw.a
  if q=="pair" and (free or combo) then
   if combo then P.cmb=(P.cmb+1)%3 attack(3) else shoot() end
  elseif q=="light" and (free or combo) then
   local f=P.lock
   local sh=near_shrine()
   if free and sh and not P.lock then rest(sh)
   elseif f and f.st=="stag" and dist(P,f)<18 then visceral(f)
   else
    P.cmb=combo and (P.cmb+1)%3 or 0
    attack(0)
   end
  elseif q=="heavy" and free then attack(P.ho2>=20 and 2 or 1)
  elseif q=="dodge" and (free or combo) then dodge()
  end
  if P.act~=a or P.act=="atk" and P.at==0 then P.q=nil end
 end
 a=P.act
 -- the stamina comes back, after a while
 if P.stw>0 then P.stw-=1 elseif P.st<100 then P.st=min(100,P.st+(P.form==0 and 2.2 or 1.8)*m.reg) end
 if not a or a=="run" then
  P.act=nil
  -- held o: charging; held x: running, or the vial
  local sp=1
  if P.hx2>=8 and not used then
   if dx==0 and dy==0 then
    if P.hx2==24 and G.ec>=100 and P.hp<20 then used=true heal() return end
   elseif P.st>0 then sp=1.6 P.st-=.6*m.stc P.stw=12 P.act="run" end
  end
  P.ho2=P.ho
  if P.ho>=8 and not used then sp=.4 end
  move(P,dx*sp,dy*sp)
  if dx~=0 or dy~=0 then
   P.wk+=sp
   if not P.lock then P.fx,P.fy=dx,dy end
  else P.wk=0 end
  face()
  -- the saw at rest, hanging down
  P.ang+=((P.ho>=8 and not used and atan2(P.fx,P.fy)+.3 or .75+(P.fl and .08 or -.08))-P.ang)/3
  return
 end
 P.at+=1
 if a=="atk" then
  local s=P.sw
  local w,b=s.w,s.w+s.a
  P.ang=P.at<w and s.a0 or P.at<b and s.a0+(s.a1-s.a0)*(P.at-w)/s.a or s.a1
  if P.at<w then move(P,P.fx*.3,P.fy*.3) end
  if P.at==w then strike() end
  if P.at>=b+s.c then P.act=nil end
 elseif a=="dodge" then
  local k=P.dk
  move(P,P.vx,P.vy)
  P.vx*=k P.vy*=k
  if P.at>=P.dt then P.act=nil end
 elseif a=="shoot" then
  if P.at==3 then fire_gun() end
  if P.at>=10 then P.act=nil end
 elseif a=="vis" then
  if P.at==9 then
   local f=P.vf
   sfx(7)
   hurt_foe(f,f.boss and 14 or 20)
   shake=6
  end
  if P.at>=22 then P.act=nil end
 elseif a=="heal" then
  if P.at==12 then P.hp=min(20,P.hp+8) sfx(12) end
  if P.at>=22 then P.act=nil end
 elseif a=="hurt" then
  move(P,P.vx,P.vy)
  P.vx*=.7 P.vy*=.7
  if P.at>=10 then P.act=nil end
 end
end

-- which way the hunter looks (towards the lock, if any)
function face()
 local f=P.lock
 if f then
  local d,dx,dy=dist(P,f)
  if d>0 then P.fx,P.fy=dx/d,dy/d end
 end
 local x,y=P.fx,P.fy
 if abs(x)>abs(y)*.8 then P.d,P.fl=2,x<0 else P.d=y<0 and 1 or 0 end
end

-- a blow: 0 quick, 1 heavy, 2 charged, 3 the trick (the saw changes)
function attack(k)
 local m=P.m
 if P.st<=0 then return end
 if k==3 then P.form=1-P.form sfx(11) end
 local f=P.form
 local s=fm[f+1]
 local fo=f==0
 local sp=fo and m.fold or 1
 local w,c,d,c2=flr((s.w+(k==1 and 4 or k==3 and 2 or 0))/sp),flr((s.c+(k>0 and k<3 and 3 or 0))/sp),
  s.d*(fo and 1 or m.open),s.s*m.stc*(fo and m.fst or 1)
 d*=({1,1.1,1.4})[P.cmb+1]*(k==1 and 1.6 or k==2 and (fo and 2.2 or 2.9) or k==3 and 1.3 or 1)
 if k>0 then c2+=k==2 and 10 or 6 end
 face()
 local fa=atan2(P.fx,P.fy)
 local sd=P.cmb%2==0 and 1 or -1
 local wide=k==2 and .35 or .22
 sid=(sid or 0)+1
 P.sw={w=w,a=s.a,c=c,r=s.r,d=d,id=sid,chg=k==2,a0=fa+wide*sd,a1=fa-wide*sd}
 P.act,P.at,P.st,P.stw="atk",0,P.st-c2,20
 sfx(k>0 and 1 or 0)
 if P.lock and P.lock.T.e>0 and rnd()<P.lock.T.e and P.lock.st=="move" then P.lock.st,P.lock.tm="hop",6 end
end

-- the blow lands: every creature in reach, in front
function strike()
 local s=P.sw
 for f in all(F) do
  local d,dx,dy=dist(P,f)
  if f.st~="dead" and f.st~="hop" and f.hit~=s.id and d<s.r+f.rad and dx*P.fx+dy*P.fy>-3 then
   f.hit=s.id
   -- a charged blow from behind: it reels
   hurt_foe(f,s.d,s.chg and (P.x-f.x)*f.fx<0)
  end
 end
end

function dodge()
 local m=P.m
 if P.st<=0 then return end
 local dx,dy=P.mx,P.my
 sfx(5)
 P.act,P.at,P.stw="dodge",0,20
 if dx==0 and dy==0 then
  -- standing: a step back
  P.vx,P.vy,P.dt,P.dk,P.inv,P.roll=-P.fx*2.6,-P.fy*2.6,7,.8,5,false
  P.st-=12*m.stc
 elseif P.lock then
  -- by a creature: a quick step
  P.vx,P.vy,P.dt,P.dk,P.inv,P.roll=dx*3.4,dy*3.4,6,.8,5,false
  P.st-=14*m.stc
 else
  P.vx,P.vy,P.dt,P.dk,P.inv,P.roll=dx*2.6,dy*2.6,12,.93,9,true
  P.fx,P.fy=dx,dy
  P.st-=20*m.stc
 end
end

function shoot()
 face()
 P.act,P.at="shoot",0
end

-- the pistol: the first creature on the line; in its wind-up, a parry
function fire_gun()
 sfx(3)
 shake=2
 local x,y=P.x,P.y-7
 for i=1,40 do
  x+=P.fx*2 y+=P.fy*2
  if solid(x,y) then break end
  for f in all(F) do
   if f.st~="dead" and abs(f.x-x)<f.rad+1 and abs(f.y-f.rad-2-y)<f.rad+3 then
    local parry=f.st=="wind" and f.tm<=10+P.m.par and f.mv~="beam"
    hurt_foe(f,1*P.m.gun,parry)
    gx,gy,gt2=x,y,4
    return
   end
  end
 end
 gx,gy,gt2=x,y,3
end

function visceral(f)
 P.act,P.at,P.vf,P.inv="vis",0,f,24
 local d,dx,dy=dist(P,f)
 P.fx,P.fy=dx/d,dy/d
 face()
end

function heal()
 G.ec-=100
 P.act,P.at="heal",0
end

-- the hunter struck
function hurt_me(d,x,y)
 if P.inv>0 or P.act=="dead" or P.act=="vis" then return end
 d*=P.m.def
 P.hp-=d
 P.ral=min(P.ral+d*.8,20-max(P.hp,0))
 shake=4
 sfx(4)
 if P.hp<=0 then
  P.hp,P.act,P.at=0,"dead",0
  music(-1)
  banner("you died",8)
  return
 end
 local dd,dx,dy=dist({x=x,y=y},P)
 dd=max(dd,1)
 P.act,P.at,P.vx,P.vy,P.inv="hurt",0,dx/dd*2,dy/dd*2,20
end

---------------------------------------------------------------- creatures
function hurt_foe(f,d,stag)
 if f.st=="dead" then return end
 f.hp-=d
 f.fl=4
 sfx(2)
 -- the rally: blood back for blood drawn
 local g=min(P.ral,d*.6)
 P.hp+=g P.ral-=g
 if f.hp<=0 then
  f.st,f.tm="dead",40
  G.kills+=1
  local e=f.T.ec*(1+(region(f.x,f.y)-1)*.5)
  G.ec+=e
  sfx(8)
  if f.boss then
   G.won[f.boss]=true
   boss=nil
   music(-1)
   sfx(15)
   banner("prey slaughtered",9)
   if f.boss==4 then G.done=true end
  end
  return
 end
 f.po+=d
 if stag or f.po>=f.T.pm then
  f.st,f.tm,f.po="stag",flr(50*P.m.stg),0
  sfx(6)
 elseif not f.boss and (f.T.n<3 or f.st~="wind") then
  f.st,f.tm="hurt",8
 end
end

function foe(f)
 local T=f.T
 if f.fl>0 then f.fl-=1 end
 if f.st=="dead" then
  f.tm-=1
  if f.tm<=0 then del(F,f) end
  return
 end
 if abs(P.x-f.x)>180 or abs(P.y-f.y)>150 then return end
 local d,dx,dy=dist(f,P)
 d=max(d,1)
 local ux,uy=dx/d,dy/d
 local ph=f.boss and f.ph or 1
 local sp=T.sp*(.8+ph*.2)
 if f.st=="idle" then
  if d<64 and P.act~="dead" then
   f.st,f.cd="move",20
   if f.boss then boss=f music(0) banner(T.nm,7) end
  end
  return
 end
 if P.act=="dead" then f.st="move" f.cd=30 return end
 f.tm-=1
 local s=f.st
 if s=="move" then
  f.cd-=1
  f.fx=dx<0 and -1 or 1
  local mx,my=0,0
  if d>T.k+4 then mx,my=ux,uy elseif d<T.k-6 then mx,my=-ux,-uy end
  mx+=-uy*T.o*f.side my+=ux*T.o*f.side
  if rnd()<.01 then f.side=-f.side end
  move(f,mx*sp,my*sp)
  f.wk+=sp
  -- a phase passed: a roar, a moment to strike
  if f.boss then
   local p=f.hp>T.hp*.66 and 1 or f.hp>T.hp*.33 and 2 or 3
   if p>f.ph then f.ph,f.st,f.tm=p,"roar",40 sfx(9) return end
  end
  if f.cd<=0 then
   local mv=T.mv
   if f.boss then mv=rnd(split(split(bmv[f.boss],"|")[f.ph])) end
   local melee=sub(mv,1,1)=="m"
   if (melee and d<T.r+10) or (not melee and d<100) then
    f.mv,f.ch=mv,melee and (tonum(sub(mv,2)) or T.n) or 1
    f.st,f.tm="wind",flr((melee and T.w or ({g=T.w,gun=26,ring=22,fire=18,beam=30,leap=16})[mv])*(1.1-ph*.1))
    f.tx,f.ty=P.x,P.y
   end
  end
 elseif s=="wind" then
  -- the lunge at the end of a wind-up
  if sub(f.mv,1,1)=="m" and f.tm<6 then move(f,ux*T.l,uy*T.l) end
  if f.mv=="beam" and f.tm>6 then f.tx,f.ty=P.x,P.y end
  if f.tm<=0 then act(f,d,ux,uy) end
 elseif s=="leap" then
  local ld,lx,ly=dist(f,{x=f.tx,y=f.ty})
  if ld>3 then f.x+=lx/ld*min(ld,5) f.y+=ly/ld*min(ld,5) end
  if f.tm<=0 then add(R,{x=f.x,y=f.y,r=4,m=30,d=T.dmg}) shake=5 f.st,f.tm="rec",T.c end
 elseif s=="beam" then
  local bd,bx,by=dist(f,{x=f.tx,y=f.ty})
  bd=max(bd,1)
  bx,by=bx/bd,by/bd
  -- the hunter by the line of the gaze
  local px,py=P.x-f.x,P.y-6-(f.y-10)
  local along=px*bx+py*by
  if along>0 and along<110 and abs(px*by-py*bx)<6 and f.tm%4==0 then hurt_me(T.dmg*.5,f.x,f.y) end
  if f.tm<=0 then f.st,f.tm="rec",T.c end
 elseif s=="hop" then
  move(f,-ux*2.5,-uy*2.5)
  if f.tm<=0 then f.st,f.cd="move",8 end
 elseif f.tm<=0 then
  -- after a reel, a flinch, a roar or the recovery: back to it
  f.st,f.cd="move",T.c\2+rnd(20)
 end
end

-- the wind-up done: the blow, the shot, the slam...
function act(f,d,ux,uy)
 local T,mv=f.T,f.mv
 local m=sub(mv,1,1)=="m"
 if m then
  sfx(0)
  if d<T.r+5 and P.act~="dead" then hurt_me(T.dmg,f.x,f.y) end
  f.ch-=1
  if f.ch>0 then f.st,f.tm="wind",flr(T.w*.55) return end
 elseif mv=="g" or mv=="gun" or mv=="fire" then
  local n=mv=="fire" and 3 or 1
  sfx(3)
  for i=1,n do
   local a=atan2(ux,uy)+(i-(n+1)/2)*.06
   add(S,{x=f.x,y=f.y-8,vx=cos(a)*2,vy=sin(a)*2,t=60,d=T.dmg,f=mv=="fire"})
  end
 elseif mv=="ring" then
  add(R,{x=f.x,y=f.y,r=4,m=44,d=T.dmg})
  shake=5
  sfx(9)
 elseif mv=="leap" then
  f.st,f.tm,f.tx,f.ty="leap",14,P.x,P.y
  return
 elseif mv=="beam" then
  f.st,f.tm="beam",16
  sfx(9)
  return
 end
 f.st,f.tm="rec",T.c
end

-- shots and the rings of a slam
function things()
 for s in all(S) do
  s.x+=s.vx s.y+=s.vy s.t-=1
  if abs(s.x-P.x)<4 and abs(s.y-(P.y-6))<6 then hurt_me(s.d,s.x,s.y) s.t=0 end
  if s.t<=0 or solid(s.x,s.y+6) then del(S,s) end
 end
 for r in all(R) do
  r.r+=2
  local d=dist(r,P)
  if not r.h and abs(d-r.r)<4 then r.h=true hurt_me(r.d,r.x,r.y) end
  if r.r>r.m then del(R,r) end
 end
end

---------------------------------------------------------------- the lamps
function near_shrine()
 for s in all(shr) do
  if abs(s.x-P.x)<12 and abs(s.y-P.y)<14 then return s end
 end
end

function rest(s)
 if not s.lit then s.lit=true banner("lamp lit",12) sfx(10) end
 P.hx,P.hy=s.x,s.y+8
 respawn()
 st,mi="lamp",1
end

function lamp_menu()
 if btnp(2) then mi=(mi-2)%6+1 sfx(13) end
 if btnp(3) then mi=mi%6+1 sfx(13) end
 local cost=300+200*#G.path
 if btnp(4) then
  if mi==6 then st="play"
  elseif #G.path<4 and G.ec>=cost then
   G.ec-=cost
   add(G.path,mi)
   paths()
   sfx(14)
  end
 end
 if btnp(5) then st="play" end
end

---------------------------------------------------------------- the loop
function banner(s,c)
 ban,banc,bant=s,c,90
end

function _update()
 t0+=1
 if bant and bant>0 then bant-=1 end
 if st=="title" then
  if btnp(4) then new_hunt() st="play" used=true P.ho=1 end
  if btnp(5) then st="help" end
 elseif st=="help" then
  if btnp(4) or btnp(5) then st="title" end
 elseif st=="lamp" then
  lamp_menu()
 elseif st=="play" then
  G.t+=1
  input()
  hunter()
  for f in all(F) do foe(f) end
  things()
  if G.done and bant==0 then st,t0="end",0 end
 elseif t0>60 and btnp(4) then
  st,t0="title",0
 end
 if shake then shake=max(shake-1,0) end
 local x,y=cam()
 cx+=(x-cx)/4 cy+=(y-cy)/4
end

---------------------------------------------------------------- drawing
function _draw()
 cls()
 local sx,sy=0,0
 if shake and shake>0 then sx,sy=rnd(shake)-shake/2,rnd(shake)-shake/2 end
 local x0,y0=flr(cx+sx),flr(cy+sy)
 camera(x0,y0)
 local mx,my=x0\8,y0\8
 for y=my,my+16 do
  for x=mx,mx+16 do
   local u=under[y*128+x]
   if u then spr(u,x*8,y*8) end
  end
 end
 map(mx,my,mx*8,my*8,17,17)
 -- the creatures and the hunter, the nearer drawn last
 local l={}
 for f in all(F) do if abs(f.x-x0-64)<90 and abs(f.y-y0-64)<90 then add(l,f) end end
 add(l,P)
 for i=2,#l do
  local j=i
  while j>1 and l[j-1].y>l[j].y do l[j],l[j-1]=l[j-1],l[j] j-=1 end
 end
 for o in all(l) do if o==P then me() else draw_foe(o) end end
 for s in all(S) do circfill(s.x,s.y,1,s.f and 9 or 7) end
 camera()
 shade(x0,y0)
 camera(x0,y0)
 -- the lit things again over the dark: windows, lamps, the lamps of the hunter
 palt(65175)
 for y=my,my+16 do
  for x=mx,mx+16 do
   local t=mget(x,y)
   if fget(t,2) then spr(t,x*8,y*8) end
  end
 end
 palt()
 for s in all(shr) do
  if s.lit then circfill(s.x,s.y-11,1+t0\8%2,12) pset(s.x,s.y-11,7) end
 end
 -- fires: flames on the braziers and the pyres
 for f in all(fires) do
  local x,y,b=f[1],f[2],f[3]
  for i=0,(b and 5 or 2) do
   local h=(t0+i*7)%9
   circfill(x+sin(t0/20+i*.3)*(b and 4 or 1.5),y-h*(b and .8 or .5),(b and 3 or 1.5)-h/5,h<3 and 10 or h<6 and 9 or 8)
  end
 end
 for s in all(S) do if s.f then circfill(s.x,s.y,2,10) end end
 for r in all(R) do circ(r.x,r.y-2,r.r,t0%4<2 and 7 or 9) end
 -- the beams of the watcher
 for f in all(F) do
  if f.st=="beam" or f.st=="wind" and f.mv=="beam" then
   local d,dx,dy=dist(f,{x=f.tx,y=f.ty})
   d=max(d,1)
   local ex,ey=f.x+dx/d*110,f.y-10+dy/d*110
   if f.st=="beam" then
    for k=-1,1 do line(f.x+k,f.y-10,ex+k,ey,k==0 and 7 or 12) end
   elseif t0%4<2 then line(f.x,f.y-10,ex,ey,8) end
  end
 end
 -- the pistol's flash
 if gt2 and gt2>0 then gt2-=1 circfill(gx,gy,2,10) line(P.x,P.y-7,gx,gy,7) end
 -- the mist where the way is shut
 for i=1,#gt,5 do
  if not G.won[gt[i+4]] then
   fillp(t0\4%2==0 and ▒ or ░)
   rectfill(gt[i]*8-4,gt[i+1]*8-4,gt[i+2]*8+11,gt[i+3]*8+11,0x6d)
   fillp()
  end
 end
 camera()
 if st=="title" then title()
 elseif st=="help" then help()
 elseif st=="end" or st=="lost" then ending()
 else
  hud()
  if st=="lamp" then menu() end
 end
 pal(scr,1)
end

-- the darkness, as in dank tomb: every lamp, fire and the hunter's own
-- lamp light rings around them; each byte of the screen (two pixels) in
-- a ring is made darker by a table, the rest is black. A row is shared
-- between the lights by nearness (each pixel takes the nearest light)
function shade(x0,y0)
 local ls={}
 for l in all(lights) do
  local x,y=l[1]-x0,l[2]-y0
  if x>-39 and x<167 and y>-39 and y<167 then add(ls,{x,y}) end
 end
 for s in all(shr) do if s.lit then add(ls,{s.x-x0,s.y-y0-11}) end end
 for s in all(S) do if s.f then add(ls,{s.x-x0,s.y-y0}) end end
 add(ls,{P.x-x0,P.y-y0-6})
 for l in all(ls) do l[1],l[2]=flr(l[1]),flr(l[2]) end
 for i=2,#ls do
  local j=i
  while j>1 and ls[j-1][1]>ls[j][1] do ls[j],ls[j-1]=ls[j-1],ls[j] j-=1 end
 end
 local lv=split"4,3,2,1,0,1,2,3,4"
 local b={}
 for y=0,127 do
  local a,act=0x6000+y*64,{}
  for l in all(ls) do if abs(y-l[2])<39 then add(act,l) end end
  local left=0
  for i=1,#act do
   local l,m=act[i],act[i+1]
   local x,d=l[1],abs(y-l[2])
   local right=128
   if m then
    local dm,dx=abs(y-m[2]),m[1]-x
    right=dx==0 and (d<=dm and 128 or left) or (m[1]*m[1]-x*x+dm*dm-d*d)/(2*dx)
   end
   right=mid(left,right,128)
   local w1,w2,w3,w4=hw[1][d],hw[2][d],hw[3][d],hw[4][d]
   b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10]=left,x-w4,x-w3,x-w2,x-w1,x+w1,x+w2,x+w3,x+w4,right
   for j=1,9 do
    local p,q,k=mid(left,b[j],right)\2,mid(left,b[j+1],right)\2,lv[j]
    if q>p then
     if k>3 then memset(a+p,0,q-p)
     elseif k>0 then
      local t=lut[k]
      for z=a+p,a+q-1 do poke(z,t[@z]) end
     end
    end
   end
   left=right
  end
  if #act==0 then memset(a,0,64) end
 end
end

-- the hunter: three ways drawn (down, up, side; the left is the right
-- mirrored), the saw cleaver and the pistol drawn as lines
function me()
 local x,y,a=P.x-4,P.y-15,P.act
 if a=="dead" then spr(76,x-4,y+9,2,1) return end
 if a=="dodge" and P.roll then spr(90+P.at\3%2,x,y+7) return end
 if P.inv>0 and a=="hurt" and t0%4<2 then return end
 if P.d==1 then saw() end
 if a=="heal" then pal(14,8) end
 spr(64+P.d*2+(P.wk\5%2),x+(a=="atk" and P.at>=P.sw.w and P.fx or 0),y,1,2,P.fl)
 pal(14,14)
 if a=="shoot" or a=="heal" then
  local hx,hy=P.x+P.fx*3,P.y-8+P.fy*2
  line(hx,hy,hx+P.fx*4,hy+P.fy*4,a=="heal" and 8 or 5)
 end
 if P.d~=1 then saw() end
end

function saw()
 local hx,hy=P.x+(P.d==2 and (P.fl and -2 or 2) or P.d==0 and -4 or 3),P.y-6
 local a,L=P.ang,P.form==0 and 7 or 12
 local c,s=cos(a),sin(a)
 local ex,ey=hx+c*L,hy+s*L
 local bx,by=hx,hy
 if P.form==1 then
  bx,by=hx+c*5,hy+s*5
  line(hx,hy,bx,by,4)
 end
 line(bx,by,ex,ey,6)
 line(bx+s,by-c,ex+s,ey-c,13)
 -- the arc of the blow
 if P.act=="atk" and P.at>=P.sw.w and P.at<P.sw.w+P.sw.a+2 then
  for k=1,3 do
   local b=a+(P.sw.a0-P.sw.a1)*k*.07
   line(hx+cos(b)*L*.6,hy+sin(b)*L*.6,hx+cos(b)*L,hy+sin(b)*L,7)
  end
 end
 -- charged: the blade glints
 if P.ho>=20 and not used and t0%4<2 then pset(ex,ey,7) end
end

function draw_foe(f)
 local T=f.T
 local z=T.z
 local w,h=z==2 and 2 or 1,z==0 and 1 or 2
 if f.st=="dead" then
  if f.tm%4<2 then return end
 end
 -- the wind-up: the body flushed red, a glint at its end (the moment of the parry)
 if f.fl>0 then for c=1,15 do pal(c,7) end
 elseif f.st=="wind" and f.tm%6<3 then pal(2,8) pal(5,8) pal(14,8)
 elseif f.st=="stag" then pal(2,13) pal(14,13)
 end
 if T.nm=="rifleman" then pal(14,5) pal(4,13) end
 if T.nm=="mad hunter" then pal(2,14) pal(5,4) end
 local n=T.s+f.wk\6%2*w
 local x,y=f.x-w*4,f.y-h*8+1
 if f.st=="leap" then y-=8 end
 spr(n,x,y,w,h,f.fx<0)
 pal()
 -- a weapon in its hand: a fork, a rifle, an axe; raised in the wind-up
 if not f.boss and T.z==1 and T.s~=72 then
  local up=f.st=="wind"
  local hx,hy=f.x+f.fx*3,f.y-7
  local ex,ey=hx+f.fx*(up and 3 or 8),hy-(up and 9 or 1)
  line(hx,hy,ex,ey,T.mv=="g" and 5 or 4)
  if up and f.tm<=10 and t0%2==0 then pset(ex,ey,7) end
 end
 if f.boss and f.boss~=2 and f.boss~=4 then
  local up=f.st=="wind"
  local hx,hy=f.x+f.fx*7,f.y-9
  local ex,ey=hx+f.fx*(up and 2 or 9),hy-(up and 10 or -2)
  line(hx,hy,ex,ey,6) line(hx,hy+1,ex,ey+1,13)
  if up and f.tm<=10 and t0%2==0 then pset(ex,ey,7) end
 end
end

function hud()
 rectfill(1,1,42,4,1)
 rectfill(2,2,1+P.hp*2,3,8)
 if P.ral>0 then rectfill(2+P.hp*2,2,1+(P.hp+P.ral)*2,3,9) end
 rectfill(2,6,2+P.st*.4,6,11)
 spr(78,94,119)
 print(flr(G.ec),103,121,7)
 -- the form of the saw
 print(P.form==0 and "folded" or "open",2,121,5)
 if boss then
  print(boss.T.nm,2,104,7)
  rectfill(2,111,125,113,1)
  rectfill(3,112,3+122*boss.hp/boss.T.hp,112,8)
 end
 if bant and bant>0 then
  rectfill(0,54,127,66,0)
  print(ban,64-#ban*2,58,banc)
 end
end

function menu()
 rectfill(8,16,119,111,0)
 rect(8,16,119,111,5)
 print("hunter's lamp",14,20,12)
 for i=1,6 do
  local s=i<6 and pth[i] or "leave"
  print((mi==i and "> " or "  ")..s,14,24+i*8,mi==i and 7 or 13)
  local n=0
  for p in all(G.path) do if p==i then n+=1 end end
  for k=1,n do pset(90+k*4,26+i*8,G.path[1]==i and 10 or 6) end
 end
 if mi<6 then print(pdesc[mi],14,84,6) end
 print(#G.path<4 and "cost "..(300+200*#G.path) or "no room",14,94,5)
 print("echoes "..flr(G.ec),14,102,9)
end

function title()
 -- the moon over yharnam
 circfill(100,16,10,6)
 circfill(97,14,9,7)
 rectfill(0,30,127,52,0)
 print("\^w\^tyharnam",36,32,8)
 print("a hunt, made small",28,46,13)
 rectfill(0,96,127,118,0)
 print("🅾️ hunt   ❎ controls",22,100,7)
 print("by bm",54,110,5)
end

function help()
 rectfill(0,0,127,127,0)
 print("controls",48,4,8)
 print("⬅️➡️⬆️⬇️ walk\n🅾️ quick blow, again: combo\n🅾️ held: charged blow\n  (from behind: it reels)\n❎ dodge: a roll, by a foe a\n  step, standing a step back\n❎ held: run; standing: vial\n🅾️+❎ pistol: in a wind-up\n  a parry, then 🅾️ visceral\n  after a blow: the trick,\n  the saw changes form\n🅾️ at a lamp: rest, paths",2,16,6)
end

function ending()
 cls()
 local won=st=="end"
 print(won and "the night is over" or "the hunt is over",won and 30 or 32,30,won and 9 or 8)
 local s=G.t\30
 print("time   "..(s\60)..":"..(s%60<10 and "0" or "")..s%60,36,56,6)
 print("slain  "..G.kills,36,64,6)
 print("deaths "..G.deaths,36,72,6)
 print("echoes "..flr(G.ec),36,80,6)
 if t0>60 then print("🅾️ again",46,104,t0\15%2==0 and 7 or 5) end
end
__gfx__
0000000055111115dd5155555551222122212221ddd5ddd55551555131b331132332331324213111321e13135115125152252112dddddddd1511511111111111
0000000055111551155115511111111111111111ddd5ddd511111111333333131323b11111111131133312e155525214555dd211555555555151151515115111
0000000011dd1555111151112122212221222122ddd5ddd5d15551dd33511333131b331b131123331e311e1322155451d2d2251d151151111111111151511515
000000001dd55155111555111111111111111111555555551111111133533131212b1b32413113112111112225255545212d2522515115151511511111111111
00000000d155511111155511555155512221ddd1d5ddd5dd555155513333331331213113133131111211332e2451551525511212111111115151151522222222
00000000d511d551111111111111111111111111d5ddd5dd11111111135111313331333121111112213211115454445112d22522151151111111111122222222
00000000555155511111dd112155515521222122d5ddd5ddd155515533131133133b3b331111211113e11e3155512445552221d2515115151511511111111111
00000000111155111dd1d555111111111111111155555555111111113353331513323331232112341313313125555212521d5522111111115151151555555555
55515551555155515551555155222251212221222221d2222221d222000dd0001010101000000000000000000001100000000013300000001333b33333333331
555155515dddddd15dddddd15244442121222122221cc1222214412200025000d0d0d0d000000000000110000001d0000000013b310000000133333333333310
111111115d1111d15d9a9ad15244e4211111111121cc7c12214ee4120022510010101010dd6ddd6d001aa100000110000000133b3310000013333b3333333331
515551555d1d11d15da99ad15244e4212212221221c7cc12214e4412002151001111111155d555d5001a71000001d0000001333b333100001333b33333333331
515551555dddddd15dddddd15244e4212212221221cccc12214e4412022115101010101015515551001aa10000011000001333b3333310000111133333111110
111111115d1111d15d9a99d1524e44211111111121c11c12214e4412021515101010101011111111000110000001d0000013333b333331000000112442100000
555155515dddddd15d99a9d1524e44212122212221cccc12214e4412221511511010101051555155000110000011110001333333333333100000124444210000
55515551555155515dddddd152444421dddddddddddddddd21444412215111511111111111111111000110000111111013333b33333333310001122222211000
0000000000000000000d000000000000d000000d006dd60000000000000000000000000024e4e4e44e4e4e4200000000000000006ccc7cccccc7ccc600111100
003b3000000000000006000000d66d006d0660d6006666000000000000000080080000004e4e4e4ee4e4e4e4000000d66d000000d6cccccccccccc6d014ee410
033b3330002442000dd6dd000d6dd6d006d66d6000dddd000155555100002e2002e20000144444444444444100000d6cc6d000000d66cccccccc66d014e44e41
3b33b3b3024ee420000600000d6666d0006dd60006666660001555100002ee4ff4ee20001e4e4e4ee4e4e4e100dd66cccc66dd0000dd66666666dd0014e44e41
333333b302e44e20000600000d6dd6d0006666000d5d5d5000015100002e4ee22ee4e200014e4e4ee4e4e4100d66cccccccc66d001dddddddddddd1014e66e41
1333333102444420000d00000d6666d000066000055555500001510002ee8e4ee4e8ee200144444444444410d6ccccc77ccccc6d015555555555551014e44e41
0111311000122100005d50000dddddd0006dd6000d5d5d50001d1d10024ee2eeee2ee42000111111111111006ccc7cccccc7ccc6001111111111110014e44e41
000000000000000001555100115555100066660011111111010000102e4e4e4ee4e4e4e200000000000000006cccccc77cccccc6000000000000000014e44e41
14e44e41000000000000000000000000000510000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
14e44e41044444400014410000011000000510000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
014ee41004e4e4e1014ee41000156100001551000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
014ee410044e4e4101555510001cc100001551000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
014ee41004e4e4e1014ee410001c7100011551100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00144100044e4e4101555510001cc10001dddd100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0011110004444441014ee41000155100155555510000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000001111110011110000011000111111110000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0001100000011000000110000001100000000000000000000000000000000000000000000000000000d6000000d6000000000000000000000000000000000000
001221000012210000122100001221000001110000011100000440000004400000008000000080000d66dd000d66dd0000000000000000000008000000000000
01222210012222100122221001222210001222100012221000444400004444000002502000025020d6dc6d00d6dc6d0000000000000000000088800000000000
112222111122221111222211112222110112222201122222001dfd00001dfd00002558d0002558d006d6d6d006d6d6d000000080000000000888780000000000
001ff100001ff10000122100001221000001ff100001ff10000ff100000ff1000025d5d70025d5d700d6d00000d6d00001122222255d11220888880000000000
01d55d1001d55d1001d55d1001d55d100001d5100001d510002ee200002ee20002555500025555000d0d0d0000dd0d001222222225551f120288820000000000
15555551155555511555555115555551001555100015551002eeee2002eeee2025d5550025d55500d00d00d00d00d0d001111122222211200022200000000000
1d5555d11d5555d11d5555d11d5555d1001555d0001555d002e4ee2002e4ee202555d5002555d500000000000000000000008800000000000000000000000000
12122121121221211212212112122121001222100012221002eeeef002eeeef025555d7025555d70001111000011110000000000000000000000000000000000
1224422112244221122442211224422100c4421000c4421002eeee2002eeee200255550002555500012222100125521000000000000000000000000000000000
1122c2111122c21111222211112222110012221000122210002ee200002ee2000255500002555000125555211225d22100000000000000000000000000000000
012222100122221001222210012222100012221000122210002ee200002ee20002505500002550001255d5211222222100000000000000000000000000000000
012222100122221001222210012222100122221001222221002ee20002ee0e200250250000255200125225211242242100000000000000000000000000000000
0120021001201210012002100121021000120210012100210020e20002e000200200020002002200122442211224422100000000000000000000000000000000
01100110011011000110011000110110001101100110001100100100010000102200022002200200012222100122221000000000000000000000000000000000
00000000000000000000000000000000000000000000000000100110011000110000000000000020001111000011110000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000000444400000000000044440000000000000000202000000000000020200000000222222000000000022222200000000000d66d000000000000d66d000000
000004e44e400000000004e44e40000000000000025d520000000000025d520000002e4444e2000000002e4444e200000000dd6cc6dd00000000dd6cc6dd0000
000004e11e400000000004e11e4000000000002225585500000000222558550000122222222221000012222222222100000d6dc77cd6d000000d6dc77cd6d000
000004488400000000000448840000000000225555d5d5700000225555d5d570000001ffff100000000001ffff1000000006d6cccc6d60000006d6cccc6d6000
00002e4444e2000000002e4444e20000002255d555555d70002255d555555d700000016666100000000001666610000000d6dd6666dd6d0000d6dd6666dd6d00
0002eeeeeeee20000002eeeeeeee200002555d5555d5550002555d5555d5550000002566665200000000256666520000006dd2d66d2dd600006dd2d66d2dd600
002ee66666ee2000002ee66666ee2000255d555d55555200255d555d5555520000025222222520000002522222252000000d6dd66dd6d000000d6dd66dd6d000
00fe6666666ef00000fe6666666ef0002555555555d520002555555555d520000002522442252000000252244225200000d0d6dddd6d0d00000dd6dddd6dd000
00f26686666e2f0000f26686666e2f00255d555d55552000255d555d55552000000f22222222f000000f22222222f0000d0060d66d0600d000d060d66d060d00
0002666666662000000266666666200002555555d552000002555555d5520000000022244222000000002224422200000d0d00d00d00d0d000d0d0d00d0d0d00
0002e666866e20000002e666866e2000025d502555d5000000255d02555d500000002222222200000000222222220000d00d0d0000d0d00d0d00d00dd00d00d0
00002eeeeee2000000002eeeeee200000255002550255000002552025525000000002221122200000002222112222000d0d00d0000d00d0d0d0d000dd000d0d0
00002ee22ee200000002ee2002ee200002500025000250000002520020252000000022100122000000022100001220000d000d0000d000d0d00d000d0d00d00d
00001ee00ee100000001e100001e100002700027000270000002700027002700000011100111000000011100001110000000d000000d0000000d00d000d00d00
00011100001110000011100000011100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0030303040403040303040403040303040403030303040304040303030304040303040403030303040303030304030c0c0c0c0c0c03040303030303040303000
004171414171414171414171414171413040d0d0d0d0d0d0d0d0d0d04040d0d0d0d0d0d0d0d0d0d0d0d03030417141417141417141417130d0d0d0d0d0d0d030
0030403030404030304030404030d0d0d0d0d0d0d0d0d0d04040417141417141417141417141417140d0d0d0d0d030c0c0c0c0c0c04040403030303030303000
004141514141415141414151414141513030e0e0e0e0e0e0e0e0e0e04040e0e0e0e0e0e0e0e0e0e0e0e03040414151414141514141415130e0e0e0e0e0e0e030
0030403030303030303030303040e0e0e0e0e0e0e0e0e0e03030414151414141514141415141414130e0e0e0e0e0a1c0c0c0c0c0c03030303030303040403000
004141414141414141414141414141414040e0e0e0e0e0e0e0e0e0e04030f0f0f0f0f0f0f0f0f0f0f0f04030414141414141414141414130f0f0f0f0f0f0f040
0091919191919191919191919191919181f0f0f0f0f0f0f04030414141414141414141414141414130f0f0f0f0f0b1c0c0c0c0c0c04033303030304040403000
004141414141414141414141414141414030f0f0f0f0f0f0f0f0f0f0303001210101110121110101110140304141414141414141414141400121011121010140
00c0c0c0c0c0c0c0c0c0c0c0c0c0c0c081011121011101013040414141414141414141414141414130012111012130c0c0c0c0c0c03043303030303030404000
00414151414141514141415141414151303001110101112101110101303001110101110131110101210140404141514141415141414151300121013111010130
00c022c0c0c0c0c042c0c0c0c0c0c0c081012131011101013040414151414141514141415141414130011131012130c0c0c0c0c0c04030304030403040404000
00414141414141416141414141414141303001210101113101110101304030304030403040303030303030304141414141614141414141303040404040304040
00c0c0c0c0c0c0c052c0c0c0c0c032c081303030403040404040414141414141416141414141414130304030403040c0c0c0c0c0c03040304030304030303000
00505050505050505050505050505050913030404040303030303030304030304040403030303030303030303030403040303030304030304040303030303030
00c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0813030404030303040305050505050505050505050505050303030403040303040303040303030303040303040403000
00505050505050504250505050505050913030303030404030303040403030303040403030303030304040303030303030304030304030304030404040303040
00c022c0c0c0c0c0c0c0c0c0c0c0c0c08140304040403030303050506250505050505050506250504040303030304040304040303040d0d0d0d0d0d0d0d04000
00505062505050505250505050625050913030304030403030303040403050a1505050505050505050a150304030303030403040404030303030303030403040
00c0c0c0c062c0c0c0c0c062c0c032c08130403030403040303050505050505042505050505050503030303030403030303030304030e0e0e0e0e0e0e0e03000
00505050505050505050505050505050914040304040303030303030303050b1505050505050505050b150303030303030403040303040304030304030304030
00c0c0c0c0c0c0c0c0c0c0c0c0c0c0c081303040a1403030303050505050505052505050505050503030403030303030303030303040f0f0f0f0f0f0f0f03000
00505050505050505050505050505050913030304030a13030303030303050505050505050505050505050303030303030303040303040403030334040304040
00c022c0c0c0c0c0c0c0c0c0c0c0c0c081303030b130404030405050505050505050505050505050303030403040404030404030303001210101210101114000
00505050505050505050505050505050303030403040b14030403030403050505050505050505050505050303030303030304040303030404030433030303040
00c0c0c0c0c0c0c0c0c0c0c0c0c032c081404040303030a1303050505050505050505050505050503040304030a1304030303030303001110101310101113000
00505050505050505050505050505050303030403030303030304030303050505050505050505050505050303040303030303030303040304040303030403030
00c0c0c0c0c0c0c0c0c0c0c0c0c0c0c040303030303030b1403040303040304040403030303030304040404040b1403030303040304030303030404030303000
005050505050505050505050505050504040303330303030f230303040305050505050b2c2505050505050303030303030403030303040303040403030403030
c0c022c0c0c0c0c0c0c0c0c0c0c0c0c03030304030403040818181818181818181c0c0c081818181818181818130303030303030303030404030303030304000
0050505050505050505050505050505040303043303030300330403030305050505050d2e2505050505050304030303030303030304030303030303030304030
c0c0c0c0c0c0c0c0c0c0c0c0c0c032c04030303330303030c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c030404030303030403030403040303030404000
00505050505050505050505050505050304040303030303030304040303050505050505050505050505050304030303030404040403030304030304040303030
c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c03040304330404040c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c040403030304040303030303030304030303000
0050505050505050505050505050505040303040303030403030f230303050505050505050505050505050304030303040303040303030303040303040404040
c0c022c0c0c0c0c0c0c0c0c0c0c0c0c04030403030304030c0c022c0c032c0c022c0c032c0c022c0c032c0c0c040304030303040403040404030403040303000
005050505050505050505050505050503040303030304040304003404030505050505050505050505050503030403040403030304030303030304040a1403030
00c0c0c0c0c0c0c0c0c0c0c0c0c032c03030303030303030c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c03030303030a130303030303030404030303000
00505050505050505050505050505050913030303040403030303040303050a1505050505050505050a1503030403040304030303030303030303040b1304040
00c0c0c0c0c0c0c0c0c0c0c0c0c0c0c08130303030303040c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c03030303030b130303030304040304030304000
00505050505050505050505050505050913030404030304040403030303050b1505050505050505050b150304030303030303040304040304030404040404030
00c022c0c0c0c0c0c0c0c0c0c0c0c0c08140404040403030c0c032c0c022c0c032c0c022c0c032c0c022c0c0c030304030403040403030303030304030303000
00505050505050505050505050505050913030304030303030304040303030303030303030404030404030303030303030403030303030404040303040403040
00c0c0c0c0c0c0c0c0c0c0c0c0c032c08130304030403040c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c030303030304030403030304030403040303000
00505050505050505050505050505050913030303040304030304030304030304030303040304030404030304141414141414141414130303040304030304030
00c0c0c0c0c0c0c0c0c0c0c0c0c0c0c08130403030303030c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0404030304030403030d0d0d0d0d0d0d0d04000
005050505050505050505050505050509130d0d0d0d0d0d0d0d0d0d040304030304030303030304030403040414151414141514141414040d0d0d0d0d0d0d040
00c022c0c062c0c0c0c0c062c0c0c0c08140303030303030c0c022c0c032c0c022c0c032c0c022c0c032c0c0c0303030303030404030e0e0e0e0e0e0e0e03000
005050505050505050505050505050509130e0e0e0e0e0e0e0e0e0e04040d0d0d0d0d0d0d0d0d0d0d0d03040414141414141414141413030e0e0e0e0e0e0e040
00c0c0c0c0c0c0c0c0c0c0c0c0c032c08130403030303030c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0403030303030303030f0f0f0f0f0f0f0f03000
005050505050505050505050505050509130e0e0e0e0e0e0e0e0e0e03040e0e0e0e0e0e0e0e0e0e0e0e04030414141414141414141413040f0f0f0f0f0f0f030
00c0c0c0c0c0c0c0c0c0c0c0c0c0c0c08130303030403030c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c040404030303030403001110101110101113000
005050625050505050505050506250509130f0f0f0f0f0f0f0f0f0f03030f0f0f0f0f0f0f0f0f0f0f0f030304141514141415141414140300121011111010140
00c022c0c0c0c0c0c0c0c0c0c0c0c0c08140304030303030c0c032c0c022c0c032c0c022c0c032c0c022c0c0c030303040303030403001110101310101113000
00505050505050505050505050505050913001210101111101110101403001110101210111110101210130304141414141414141414130300111013111010130
00c0c0c0c0c0c0c0c0c0c0c0c0c032c08140303040403040c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c030403030303030404040303030403030303000
00505050505050505050505050505050913001110101113101110101303001110101210131110101110140304141414141614141414130303030404040304040
009191919191919191919191919191918130304030303030818181818181818181c0c0c081818181818181818130404030403030403030303030304030404000
00505050505050505050505050505050913040303030304030403030303040403030403030403030303030403030403040304030303030403030303040304030
00304030304040304030304040403040304040304030303040303040304030403040303030403030303030404040303030304040404040303030303040304000
00303030303030304030304040303030303030403030303040303030403030403030303040303030303030303030303030403030304030403030303040304030
00303040403030304040403040403030303030303030303040303030403030404040404040303030304030304030403040303030304030303030403030404000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
__gff__
01000000000000000000000000010101010105010105010909090f09090909090909090909090b0b09090909090909090909090909000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
__map__
0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000d0d0d0d0d0d0d0d0d0d020d0d0d0d0d0d0d0d0d0d0d0d0d01010d0d0d0d0d0d0d0d0d0d0d0d010d0d0d0d0d0d0d00000000000000000000000000000000000a1c1d091c1d090909090a0909090909090a0a0a1c1d090909090909090909090909090a0a09090a090a0a0a090a1c1d090a091c1d090a091c1d1c1d091c1d00
000e0e0e0e0e0e0e0e0e0e020e0e0e0e0e0e0e0e0e0e0e0e0e01010e0e0e0e0e0e0e0e0e0e0e0e010e0e0e0e0e0e0e0000000000000000000000000000000000091e1f0a1e1f090909090a090a0a09090a0a09091e1f090a0a090a090909090a0a090a090a09090a0a0a211c1d091e1f091c1d1e1f090a091e1f1e1f0a1e1f00
000f0f0f0f0f0f0f0f0f0f010f0f0f0f0f0f0f0f0f0f0f0f0f01020f0f0f0f0f0f0f0f0f0f0f0f010f0f0f0f0f0f0f00000000000000000000000000000000001c1d0a09090a1c1d08080708070707070709090a0909090a0b0b0b0b0b0b090909090b0b0c0b0909090a091e1f090a09091e1f1c1d201c1d090a200a20090a00
001012101011121012101002101110101110111110101210100101101210101110111210101210011012101112101000000000000000000000000000000000001e1f1c1d1c1d1e1f07070807080707080709090a090a090b0b0b0b0b0b0b0b0b0b0b0c0b0b0c0b0b09090909090909091c1d091e1f201e1f090909091c1d0900
0010111010111310111010021011101012101312101011101001011012101011101312101011100110111013111010000000000000000000000000000000000009091e1f1e1f1c1d0807070707070707070a0b0b0b0b0b0b0b0b0b0c0b0b0b0c0b0b0b0b0b0b0c0b0b0b0c0a09090a091e1f0909092009201c1d090a1e1f0a00
000606060606060606060601060606060606060606060606060102060606060606060606060606010606060606060619191919191919191919191919191919001c1d090909091e1f080708083307070807090b0b0b0c0b0b0b0b0a0a0b0b0b0c0b0b0b0b0c0b0b0c0b0b0b0b0a090a0909210909200909211e1f09091c1d0a00
000101020201010201010101010201010101010102020102010101020101010101010101020101010101010201020119050505050505050505050505050505001e1f090a200a210a070708073407070808090b0b0c0b0b0b0b0a0909090b0b0b0b09090909090c0b0b0b0b0c091c1d201c1d090a1c1d091c1d1c1d0a1e1f0900
00011a020201020101021a020101010101010101010101020505050505050505050505050505010d0d0d0d0d0d0d0219050505050505050505050505050505001c1d1c1d0a0a0909080707070807080707090b0b0b0b0b0a0a0a090909090a090a090a09090a090a0b0c0b0b091e1f211e1f0a091e1f091e1f1e1f1c1d090a00
00011b010201020102021b02010d0d0d0d0d0d0d0d0d01010505050505050505050532050505020e0e0e0e0e0e0e0119050526050505050505050526050505001e1f1e1f1c1d1c1d0808070707070807070a0b0b0b0a09090909090a0a090a090a09090a090a09090a0b0b0b1a09091c1d09091c1d091c1d0909211e1f1c1d00
000202010101010102020201010f0f0f0f0f0f0f0f0f01010505240505050505050505240505010f0f0f0f0f0f0f0119050505050505050505050505050505000a0a1c1d1e1f1e1f0807080707080808070a0b0b0b0a091c1d09070707070708070708091c1d0909090b0b0b1b09091e1f090a1e1f091e1f0a090909091e1f00
000101010102020101010202011011101012101012100101050525050505050505050525050501101110111110100219050505050505050505050505050505000a0a1e1f09090909090b0b0b0b0b0909091a0b0b0b0b091e1f0a070808070707082f07091e1f090a0b0b0b0c0b09090921090a0a090909201c1d09201c1d0900
000201010101330101020201011011101013101011100102050505050505050505050505050501101210131110100219050505050505050505050505050505000909090909090909090b0b0b0b0c0b090a1b0b0b0b0b09091c1d0707080807070730080a091c1d090b0b0b0c0b091c1d1c1d09091c1d20091e1f1c1d1e1f0900
00010101010234020101010102060606060606060606010205050505050505050505050505050206060606060606010105320505050505050505050505050500090b0b0b0b0b0b090c0b0b0b0b0b0b0c0b0b0b0b0b0c0a201e1f07082107070707080709091e1f090b0b0c0b0b091e1f1e1f090a1e1f09090a091e1f090a0a00
00020101020102010101020132010101020102010201010105050505050505050505050505051a0101020102010201010505050505050505050505050505050b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0c0c0b0b0b0b0c0c0a0909090708070726080807070a09200a090b0b0b0a0a09090a0a09090a1c1d1c1d1c1d211c1d090900
0001020101010101010101010101020101010102020101010505050505052b2c0505050505051b0201020102330102010505050505052728050505050505050b0b0b0b0b0b0b0c0b0c0b0b0b0b0b0b0b0c0c0b0b0b0b0a090909070708070707070708090a200a090b0b0b0b0b0b0b090909090a1e1f1e1f1e1f0a1e1f090900
0002010101010101020101020101010201010201010101010505050505052d2e05050505050502010101010234020101050505050505292a050505050505050c0b0c0b0a090c0b0c0b0b0b0b09090a09090a0c0b0b0c0909090907080708080731070709090a0a090b0b0b0b0b0b0b0b0b0b090a1c1d0a09200a201c1d090900
0001020101010201010101012f0202010102020201010202050505050505050505050505050501010102010201020102050505050505050505050505050505050a0a090a0a09090a0b0b0b0b09091c1d09090b0b0c0b090b0b0c070707080807070708091c1d09090b0b0b0b0b330b0b0b0c0b091e1f200a09090a1e1f090a00
0001010201020101010102013002010101010101020131020505050505050505050505050505010d0d0d0d0d0d0d0102050505050505050505050505050505001c1d090a091c1d0a090a0909090a1e1f09090b0b0b0b0b0b0b0b0707070808070708070a1e1f090a0c0b0c0c0b340c0b0b0b0b09090a0a0a0909090909090a00
000101020101010102010201020d0d0d0d0d0d0d0d0d02010505050505050505050505050505020e0e0e0e0e0e0e0119050505050505050505050505050505001e1f1c1d0a1e1f1c1d090a1c1d201c1d090a0b0b0b0b0c0b0c0b0c0b0a0a1c1d0909090a0a1c1d0a0b0b0c0a0807070b0b0b0b07070707070707070807070a00
000102020102010102020201010f0f0f0f0f0f0f0f0f0202051a053105050505050505051a05010f0f0f0f0f0f0f0119050505050505050505050505052f05001c1d1e1f2009091e1f200a1e1f0a1e1f0909090b0b0b0b0b0c0b0b09090a1e1f211c1d1c1d1e1f0a090909091c1d070b0c0c0b0707080807080707071c1d0a00
00011a010101020101011a01011012101011101011100101051b050505050505050505051b0501101110111210100219050505050505050505050505053005001e1f090921090909090920091c1d090a090a090b0b0b0b090a0909091c1d0909211e1f1e1f091c1d090909091e1f080b0b0b070707070707080807071e1f0a00
00011b010101020102011b02021012101013101011100101050505050505050505050505050501101210131110100219050526050505050505050526050505000a09092109201c1d1c1d0a091e1f09201c1d090b0b0b0b0909090a091e1f09210a09090921091e1f0a090c0b0b0b260707070707070708080708080726080a00
00010102020101010101020101060606060606060606010205050505050505050505050505050206062f060606060119050505050505050505050505050505001c1d09091c1d1e1f1e1f1c1d0a0a09091e1f0a0b0b0b0b09090a0a090909090a090a0909090a090a0b0b0b0b0b0b070707070808070708080807070708070900
000101020101010102020101010101020101010101020201010201020101010102010102010101020130010101010119050505050505050505050505050505001e1f09211e1f1c1d090a1e1f090920091c1d090b0c0b0b0c0b0b0b090a0c0b0b0b0b0b0b0b0b090b0b0b0b0c0b0b070707070707080708070708080807070900
000606060606060606060206060606060606060606060602010606060606060606060606060602010606060606060619191919191919191919191919191919000a201c1d09211e1f1c1d0909201c1d091e1f0a0b0c0b0b0b0b0c0b0b0b0b0b0b0b0c0b0b0b0b0b0b0b0b0b0b0707080707080707080808070707070707080900
000d0d0d0d0d0d0d0d0d010d0d0d0d0d0d0d0d0d0d0d0d01010d0d0d0d0d0d0d0d0d0d0d0d0d01010d0d0d0d0d0d0d00000000000000000000000000000000000a091e1f1c1d09091e1f0909091e1f091c1d090b0b0b0b0b0b0c0b0b0b0b0b0b0b0c0b0b0b0c0c0b0b0b09090807070808070708070707070707070807080900
000e0e0e0e0e0e0e0e0e020e0e0e0e0e0e0e0e0e0e0e0e01010e0e0e0e0e0e0e0e0e0e0e0e0e02010e0e0e0e0e0e0e00000000000000000000000000000000000a091c1d1e1f1c1d1c1d0a0a210920091e1f090c0c0b0b091a0b0b0b0b0b0b0c09090c0b0b0b0b0b0c0b0a0a070708070b0b0b0b070707080707070707070900
000f0f0f0f0f0f0f0f0f010f0f0f0f0f0f0f0f0f0f0f0f01010f0f0f0f0f0f0f0f0f0f0f0f0f02010f0f0f0f0f0f0f000000000000000000000000000000000009091e1f1c1d1e1f1e1f0909091c1d0921090909090909091b0a0a090b0b0b0b0a09090a0a0a0909090a0909080726080c0b0c0b070707070707070726080900
001011101012101011100210111010121012111010111001011011101011101211101011101001011011101112101000000000000000000000000000000000001c1d20091e1f0a0a09091c1d0a1e1f1c1d1c1d091c1d090a0909090a0909090a0a091c1d0a1c1d1c1d090909070707070b0b0b0b070808080708080708070900
001011101013101011100110111010121013111010111001011011101011101311101011101001011012101311101000000000000000000000000000000000001e1f0909090a09090a0a1e1f0909091e1f1e1f201e1f212109090a0909090909090a1e1f0a1e1f1e1f09090a090909090b0b0b0b09090a0a0a090a0909090a00
000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000b0b0b0b000000000000000000000000
__sfx__
000200002c640266301e6250000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0003000024650206501a6401462500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00020000143631e650166350000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0002000038670286631e6401862500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000300001e3601a353143350000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000200001e624226301e6250000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000300003c36037350304523043500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000400001e6701826328650142530e635000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00050000202531a243122350000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000a00000e2620d2720c2720c7620b255000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0006000024540285402b5403055230535000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000200003e350323433f3503433500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0005000024041280412d0513003500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000200003043000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000500002b54030540345503754500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000c57018065135550c04500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000300001463012625000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000e00000e2500e2300e2301a2300e2500e2300e2301a2300e2500e2300e2301a2300e2500e2300e2301a2300a2500a2300a230162300a2500a2300a230162300d2500d2300d230192300d2500d2300d23019230
000e000026530295202d5302952026530295202d5302952026530295202d5302952026530295202d53029520225302652029530265202253026520295302652024530285202b5302852024530285202b53028520
000e00003e0450000000000000000000000000000000000039045000000000000000000000000000000000003e045000000000000000000000000000000000003904500000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
__music__
03 20212244
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
00 41424344
__label__
00000000555h555h555h555hhhhhdd000000550000000000000000000000hh00hhh0hhh0555h555h555h555h555h55h0hhh0hhh00000hh000000000000000000
00000000hhhh55hhhhhh55hhhddhd5hh05505hhh00000000000000000hh0h0000000hh00hhhh55hhhhhh55hhhhhh55000000hh000hh0h0000000000000000000
0000000055hhhhh555hhhhh5dd5h555555h0hhhh00000000hh0000000000000hhh00000hdd5h5555dd5h5555dd5h5555hh00000h000000000000000000000000
0000000055hhh55h55hhh55hh55hh5h00hh00hh0000000000000000000000000hh000hh0h55hh55hh55hh55hh55hh5h0hh000hh0000000000000000000000000
00000000hhddh555hhhnnh55hhhh5hhh0000h00000hh00000000000000hh00hh00550hhhhhhh5hhhhhhnnhhhhhhh5hhh00550hhh00hh00000000000000000000
00000000hdd55h55hdhn7h55hhh555hh000hhh000hh00000000000000hh000hh055hh0hhhhh555hhhhhn7hhhhhh555h77777h0hh0hh000000h00000000000000
00000000dh555hhhdhhnnhhhhhh555hh000hhh00h000000000000000h000000050hhh000hhh555hhhhhnnhhhhhh5577777777766h0000000h000000000000000
00000000d5hhd55hd5hhh55hhhhhhh0000000000h000h00000000000h000h0005h005hh0hhhhhhhhhhhhhhhhhhh77777777777776600h0000000000000000000
00000000555h555h555hh55hhhhhddhh000055000000000000000000000000h0hhh0hhh0hhhhddhhhhhhhdhhhh77777777777777766000000000000000000000
00000000hhhh55hhhhhhh5hhhddhd5hh05505hhh0000000000000000000000000000hh00hddhd555hddhh555hd77777777777777766600000000000000000000
0000000055hhhhh555hhhhh555hhhh0h55h0hhhh00000000hh0000000000000h55h0hhhhdd5h5555dd5hh555d77777777777777777666hhhhh00000000000000
00000000hhhhh55h55hhd55h55hhh5h00hh00hh00000000000000000000000000hh00hh00h5hh55hh55hd55hh777777777777777776666000000000000000000
0000000000ddh555hhdhh555hhddh5hh0000h00000hh00000000000000hh00000000h00000hh5hhhhhhhhhhh7777777777777777777666000000000000000000
00000000055h5h55hddhdh55hdd5h0hh000hhh000hh00000000000000hh00000000hhh00000h55hhhhhhd5hh7777777777777777777666600000000000000000
0000000050hh5hhhdh5hhhhhdh55h000000hhh00h000000000000000h0000000000hhh00000h55hhhhhhh5hh7777777777777777777666600000000000000000
000000005h005h5hd5hhd55hd5005hh000000000h000h00000000000h000h00000000000000000hhhhhhdhhh7777777777777777777666600000000000000000
00000000hhh0hh5h55hhhh5hhhh0hhh00000550000000000000000000000000000005500000055hhhhhhhhhh7777777777777777777666600000000000000000
000000000000hh00000000000000hh000550h0000000000000000000000000000h505hhh05505hhh0000000h0777777777777777776666600000000000000000
0000000055h0hhhh55h0hhhhhh00000hhh000000000000000000000000000000hhh0hhhh55h0hhhh55h0hhhhh777777777777777776666600000000000000000
000000000hh00hh00hh00hh0hh000hh0hh00000000000000000000000000000000000hh00hh00hh00hh00hh0hh77777777777777766666600000000000000000
000000000000h0000000h00000550hhh00hh000000hh00000000000000hh00000000h0000000h0000000h0000077777777777777766666000000000000000000
00000000000hhh00000hhh00055hh0hh0hh000000hh000000000000000h0000000000000000hhh00000hhh000557777777777777666666000000000000000000
00000000000hhh00000hhh0050hhh000h0000000h0000000000000000000000000000000000hhh00000hhh0050hh677777777766666660000000000000000000
0000000000000000000000005h005h00h000h000h0000000000000000000h0000000000000000000000000005h00566777776666666600000000000000000000
000000000000550000005500hhh00000000000000000000000000000000000000000hh000000550000005500hhh0006666666666666000000000000000000000
000000000hh0h0hh05505hhh00000000000000000000000000000000000000000hh0h0000hh0h0hh05505hhh0000000666666666660000000000000000000000
0000000000000000000000000000000000000000hh00000000000000000000000000000000000000hh000000hh00000006666666000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000000hh000000hh000000hh000000hh000000000000000000000000000000hh000000hh0000000000000000000000hh0000000000000000000000000000
000000000hh000000hh000000hh000000hh0000000000000000000000000000000h000000hh0000000000000000000000hh00000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000000000000000000000000000000000000oo00oo00oo00oo0000oo0000oooo0000oooo000000oo0000oo00oo00000000000000000000000000000000000000
000000000000000000000000000000000000oo00oo00oo00oo0000oo0000oooo0000oooo000000oo0000oo00oo00000000000000000000000000000000000000
000000000000000000000000000000000000oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oooooo00000000000000000000000000000000000000
000000000000000000000000000000000000oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oooooo00000000000000000000000000000000000000
00000000000000000000000000000000000000oo0000oooooo00oooooo00oooo0000oo00oo00oooooo00oooooo00000000000000000000000000000000000000
00000000000000000000000000000000000000oo0000oooooo00oooooo00oooo0000oo00oo00oooooo00oooooo00000000000000000000000000000000000000
00000000000000000000000000000000000000oo0000oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00000000000000000000000000000000000000
00000000000000000000000000000000000000oo0000oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00000000000000000000000000000000000000
00000000000000000000000000000000000000oo0000oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00000000000000000000000000000000000000
00000000000000000000000000000000000000oo0000oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00oo00000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000d000000d0d0d0d0dd00ddd000000000d0d00d00dd00ddd000000dd0d0d00d00d000d0000000000000000000000000000000
0000000000000000000000000000d0d00000d0d0d0d0d0d00d0000000000ddd0d0d0d0d0d0000000d000ddd0d0d0d000d0000000000000000000000000000000
0000000000000000000000000000ddd00000ddd0d0d0d0d00d0000000000ddd0ddd0d0d0dd0000000d00ddd0ddd0d000d0000000000000000000000000000000
0000000000000000000000000000d0d00000d0d0d0d0d0d00d000d000000d0d0d0d0d0d0d000000000d0d0d0d0d0d000d0000000000000000000000000000000
0000000000000000000000000000d0d00000d0d0ddd0d0d00d00d0000000d0d0d0d0dd00ddd00000dd00d0d0d0d0ddd0ddd00000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000h5hh0hh055hh0hh000hhh00055hh0hh000h00000hh000000000000000000000000000000000000000000000000000000000000000000000
0000000000000000h0hhh00050hhh000000hhh0050hhh000000h0000h00000000000000000000000000000000000000000000000000000000000000000000000
0000000000000000h0005hh05h005h5hhhhhhhhh5h005hh000000000h000h0000000000000000000000000000000000000000000000000000000000000000000
000000000000hh0000h0hhh0hhh0hh5hhhhhddhh55h0hhh000005500000000000000000000000000000000000000000000000000000000000000000000000000
000000000hh0h0000000hh00000055hhhddhh555hhhhhh0005505h00000000000000000000000000000000000000000000000000000000000000000000000000
000000000000000055h0hhhhhh00hhh555hiihh555hh000hhh000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000hh00hh0hhhhh55h5hiiiihh55hhh5h0hh000hh0000000000000000000000000000000000000000000000000000000000000000000000000
0000000000hh00000000h00000ddh555hhiiiihhhhddh5hh00550hhh00hh00000000000000000000000000000000000000000000000000000000000000000000
000000000hh000hh000hhh00hdd55h55hdhffh55hdd55hhh055hh0hh0hh000000h00000000000000000000000000000000000000000000000000000000000000
00000000h0000000000hhh00dh555hhhdhd55dhhdh555h0050hhh000h00000000000000000000000000000000000000000000000000000000000000000000000
00000000h000h0h000000000d5hhd55hh555555hd5hhd55h5h005hh0h000h000h000000000000000000000000000000000000000000000000000000000000000
000000000000000000005500555h555hhd5555dh555h55h0hhh0hhh0000000000000000000000000000000000000000000000000000000000000000000000000
000000000000000005505hhhhhhh55hhhihiihihhhhh55hh0000hh00000000000000000000000000000000000000000000000000000000000000000000000000
00000000hh0000hhhh00000h55hhhhh56di44iih55hhhhh5hh00000h00000000hh00000000000000000000000000000000000000000000000000000000000000
00000000000000h0hh000hh055hhh55h6diicihh55hhh55hhh000hh0000000000000000000000000000000000000000000000000000000000000000000000000
000000000000000000550hhhhhddh5556diiiih5hhddh5hh00550hhh00hh00000000000000000000000000000000000000000000000000000000000000000000
0000000000000000055hh0hhhdd55h556diiiih5hdd55h55055hh0hh0hh000000000000000000000000000000000000000000000000000000000000000000000
000000000000000050hhh000dh555hhh6di55ihhdh555h0050hhh000h00000000000000000000000000000000000000000000000000000000000000000000000
00000000000000005h005hh0d5hhd55h6dhhdhhhd5hhd5h05h005hh0h000h0000000000000000000000000000000000000000000000000000000000000000000
000000000000hh00hhh0hhh0hh5h555h6d5h555h555h55h0hhh0hhh0000000000000000000000000000000000000000000000000000000000000000000000000
000000000hh0h0000000hh0000hh55hh6dhh55hhhhhh55000000hh00000000000000000000000000000000000000000000000000000000000000000000000000
000000000000000055h0hhhhhh00hhh555hhhhh555hh000h55h0hh00000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000hh00hh0hh00h55h55hhh55h55hh0hh00hh00h00000000000000000000000000000000000000000000000000000000000000000000000000
0000000000hh00000000h00000550h55hhddh555hh550hhh0000h00000hh00000000000000000000000000000000000000000000000000000000000000000000
000000000hh00000000hhh00055hh055hdd55h55055hh0hh000hhh000hh000000000000000000000000000000000000000000000000000000000000000000000
00000000h0000000000hhh0050hhh00050hhh00050hhh000000h0000h000000000000000000000000000000000000000000hh000000000000000000000000000
00000000h000h000000000005h005hh05h005hh05h005hh000000000h000h0000000000000000000000000000000000000000000000000000000000000000000
000000000000000000005500hhh0hhh0hhh0hhh0hhh0hhh00000hh00000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000hh05hhh0000hh000000hh000000hh0005h0h000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000h55h0hhhhhh00000h55h0hhhh00000000000000000000000000000000hh0000000000000000000000000000000000000000000000
0000000000000000000000h00hh00hh0hh000hh00hh00hh00000000000000000000000000000000000000000000000000h000000000000000000000000000000
0000000000hh000000hh00000000h00000550hhh0000h00000hh000000hh000000hh000000hh00000000000000hh000000hh0000000000000000000000000000
000000000hh000000hh00000000hhh00055hh0hh000h00000hh000000h00000000h000000hh00000000000000hh0000000hhhh00000000000000000000000000
00000000h0000000h00000000000000050hhh00000000000h0000000h0000000h0000000h000000000000000h0000000h0hhhh00000000000000000000000000
00000000h000h000h000h00000000000h000h00000000000h000h000h0000000h000h000h000h00000000000h000h000h00hh000000000000000000000000000
0000000000000000000000000000hh00000000000000hh00000000000000000000000000000000000000hh000000000000000000000000000000000000000000
00000000000000000000hh0005h0h000000000000hh0h0000000000000000000000000000000000005505hhh00000000000000000h0000000000000000000000
000000000000000hhh00000h55h0000000000000000000000000000000000000hh0000000000000h55h0hhhhhh000000hh000000hh0000000000000000000000
0000000000000hh0hh000hh00hh00h00000000000000000000000000000000000000000000000hh00hh00hh0hh000h0000000000000000000000000000000000
0000000000550hhh00550hhh0000h00000hh000000hh00000000000000hh00000000000000550hhh0000h00000550hhh00000000000000000000000000000000
00000000055hh0hh055hh0hh000hhh000hh000000hh000000000000000h0000000000000055hh0hh000hhh00055hh0hh00000000000000000000000000000000
0000000050hhh00050hhh000000hhh0050000000h000000000000000000000000000hh0050hhh000000hhh0050hhh00000000000000000000000000000000000
000000005h005hh05h005hh0000000005h00h000h000h000000000000000h000000000005h005hh0000000005h005hh000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000777770000007070707077007770000000000000077777000000077007007700777077000700700007700000000000000000000000
00000000000000000000007700077000007070707070700700000000000000770707700000700070707070070070707070700070000000000000000000000000
00000000000000000000007700077000007770707070700700000000000000777077700000700070707070070077007070700007000000000000000000000000
00000000000000000000007700077000007070707070700700000000000000770707700000700070707070070070707070700000700000000000000000000000
00000000000000000000000777770000007070777070700700000000000000077777000000077007007070070070700700777077000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000055005050000055005050000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000050505050000050505550000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000055000500000055005550000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000050500500000050505050000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000055000500000055005050000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
000000005h005h5hd5hhd55hd5005hh05h005h00h000h00000000000h000h000000000005h005h5hd5hhd55hhh0000000000000000000000009n990000000000
00000000hhh0hh5h55hhhh5hhhh0hhh0hhh0hh0000000000000000000000000000005500hhh0hh5h55hhhh5h0000550000005500000000000099n90000000000
000000000000hh00000000000000hh00000000000000000000000000000000000h505hhh0000hh000000000005505hhh0550h000000000000000000000000000
00000000hh00000hhh00000h55h0hhhh55h000000000000000000000000000000000000hhh00000h55h0hhhhhh00000hhh000000000000000000000000000000
00000000hh000hh0hh000hh00hh00hh00h00000000000000000000000000000000000hh0hh000hh00hh00hh0hh000hh0hh000000000000000000000000000000
0000000000550hhh00550hhh0000h0000000000000hh00000000000000hh000000hh0hhh00550hhh0000h00000550hhh00hh0000h00000000000000000000000
00000000055hh0hh055hh0hh000hhh00000000000hh000000000000000h000000hh000hh055hh0hh000hhh00055hh0hh0hh00000000000000000000000000000
0000000050hhh00050hhh000000hhh0000000000h00000000000000000000000h000000050hhh000000hhh0050hhh000h0000000000000000000000000000000
00000000h0005hh05h005hh00000000000000000h0000000000000000000h000h000h000h0005hh0000000005h005h00h000h000000000000000000000000000
