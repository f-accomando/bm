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
   if t==$BRAZIER or t==$PYRE then add(fires,{x*8+(t==$PYRE and 8 or 4),y*8+(t==$PYRE and 4 or 1),t==$PYRE}) end
   if t==$SHRINE then add(shr,{x=x*8+4,y=y*8+8}) end
  end
 end
 sp=split"$SPAWNS"
 gt=split"$GATES"
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
 palt($GLOW)
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
