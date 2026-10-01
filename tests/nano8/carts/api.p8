pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- nano8 api test
-- by bm
-- every check prints "fail ..." with printh; the last line says how many
fails=0
checks=0
function eq(name,a,b)
 checks+=1
 if a!=b then
  fails+=1
  printh("fail "..name..": "..tostr(a).." ~= "..tostr(b))
 end
end

-- the dialect: compound assignments
a=1 a+=2 eq("+=",a,3)
a-=1 eq("-=",a,2)
a*=3 eq("*=",a,6)
a/=4 eq("/=",a,1.5)
a=7 a\=2 eq("\\=",a,3)
a%=2 eq("%=",a,1)
a=2 a^=3 eq("^=",a,8)
s="a" s..="b" eq("..=",s,"ab")
a=5 a|=2 eq("|=",a,7)
a&=3 eq("&=",a,3)
a^^=1 eq("^^=",a,2)
a<<=2 eq("<<=",a,8)
a>>=1 eq(">>=",a,4)
tb={x=1} tb.x+=1 eq("field +=",tb.x,2)
tb[1]=5 tb[1]-=2 eq("index -=",tb[1],3)

-- operators
eq("!=",1!=2,true)
eq("\\",7\2,3)
eq("\\ neg",-7\2,-4)
eq("&",0x0f&0x3c,0x0c)
eq("|",1|2,3)
eq("^^",5^^3,6)
eq("~",~0,0xffff.ffff)
eq("<<",1<<4,16)
eq(">>",-16>>2,-4)
eq(">>>",-1>>>28,0x0.000f)
eq("<<>",0x8000<<>1,0x0.0001)
eq(">><",1>><1,0.5)
eq("prec & ==",3&1==1,true)
eq("prec + &",1+2&3,3)
eq("prec ..",1 .."2"..3,"123")
eq("unary -",- -2,2)
eq("pow",-2^2,-4)
poke(0x4300,12) eq("@",@0x4300,12)
poke2(0x4302,-2) eq("%",%0x4302,-2)
poke4(0x4304,1.5) eq("$",$0x4304,1.5)

-- short if and while
a=4
if (a==4) b=1 else b=2
eq("short if",b,1)
if (a!=4) b=3
eq("short if false",b,1)
if (a==4) b=5 c=6
eq("short if two",c,6)
i=0 while (i<5) i+=1
eq("short while",i,5)
function early(x)
 if (x) return 1
 return 2
end
eq("short return",early(true),1)
eq("short return 2",early(false),2)
if (a==4) then b=7 end
eq("if () then",b,7)
if (a)==4 then b=8 end
eq("if () ==",b,8)

-- literals
eq("hex frac",0x0.8,0.5)
eq("bin",0b101,5)
eq("bin frac",0b0.1,0.5)
eq("0xffff",0xffff,-1)
eq("wrap",32768,-32768)
eq("escapes",#"\^w\#3a",5)
eq("\\*",ord("\*"),1)
s=[[a
b]]
eq("long string",#s,3)
a=1 // a comment
eq("// comment",a,1)
eq("glyphs",⬅️+➡️*2+⬆️*4+⬇️*8,0+2+8+24)
eq("buttons",🅾️+❎,9)

-- numbers as text
eq("tostr 1/3",tostr(1/3),"0.3333")
eq("tostr -0.5",tostr(-0.5),"-0.5")
eq("tostr 3",tostr(3),"3")
eq("tostr 6/2",tostr(6/2),"3")
eq("tostr hex",tostr(1,1),"0x0001.0000")
eq("tostr nil",tostr(nil),"[nil]")
eq("tostr bool",tostr(true),"true")
eq("concat",""..3,"3")
eq("concat frac","x"..1.5,"x1.5")
eq("concat div","="..(10/2),"=5")
eq("tonum hex",tonum("0x10"),16)
eq("tonum",tonum("12.5"),12.5)
eq("tonum bad",tonum("abc"),nil)
eq("tonum bin",tonum("0b101"),5)

-- math
eq("flr",flr(-1.5),-2)
eq("ceil",ceil(1.2),2)
eq("sgn 0",sgn(0),1)
eq("sgn",sgn(-3),-1)
eq("mid",mid(3,1,2),2)
eq("abs",abs(-3),3)
eq("sqrt",sqrt(16),4)
eq("sin",sin(0.25),-1)
eq("cos",cos(0.5),-1)
eq("atan2 right",atan2(1,0),0)
eq("atan2 up",atan2(0,-1),0.25)
eq("band",band(12,10),8)
eq("bor",bor(12,10),14)
eq("bxor",bxor(12,10),6)
eq("shl",shl(3,2),12)
eq("shr",shr(12,2),3)
eq("div by 0 ok",1\0==1\0,true)
srand(7) r1=rnd(10) srand(7)
eq("srand",rnd(10),r1)
eq("rnd range",r1>=0 and r1<10,true)
eq("rnd table",rnd({4}),4)

-- tables
tb={}
add(tb,1) add(tb,2) add(tb,3)
del(tb,2)
eq("del",#tb,2)
eq("del shift",tb[2],3)
eq("deli",deli(tb,1),1)
add(tb,9,1)
eq("add at",tb[1],9)
eq("count",count(tb),2)
eq("count v",count({1,2,1},1),2)
tb={1,2,3,4} s=0
for v in all(tb) do
 if (v==2) del(tb,v)
 s+=v
end
eq("all del",s,10)
s=0 foreach({1,2,3},function(v) s+=v end)
eq("foreach",s,6)
s=0 for i,v in inext,{4,5} do s+=v end
eq("inext",s,9)
eq("split",#split("1,2,3"),3)
eq("split num",split("1,2,3")[2],2)
eq("split sep",split("a b"," ")[2],"b")
eq("split chars",split("abc","")[3],"c")
eq("split size",split("123",1)[2],2)
eq("split keep",split("1,2",",",false)[1],"1")
eq("sub",sub("hello",2,3),"el")
eq("sub neg",sub("hello",-3),"llo")
eq("sub end",sub("hello",2),"ello")
eq("ord",ord("A"),65)
eq("chr",chr(104,105),"hi")
x,y=ord("ab",1,2)
eq("ord n",y,98)
mt={__index=function(t,k) return k*2 end}
o=setmetatable({},mt)
eq("metatable",o[21],42)
c=cocreate(function() yield(1) end)
ok,v=coresume(c)
eq("coroutine",v,1)
eq("costatus",costatus(c),"suspended")
eq("select",select("#",1,2,3),3)
eq("unpack",select(2,unpack({7,8})),8)

-- memory
memset(0x4300,7,4)
eq("memset",peek(0x4303),7)
memcpy(0x4400,0x4300,4)
eq("memcpy",peek(0x4402),7)
p,q=peek(0x4300,2)
eq("peek n",q,7)
poke(0x4500,1,2,3)
eq("poke n",peek(0x4502),3)

-- drawing
cls(1)
eq("cls",pget(0,0),1)
pset(5,5,8)
eq("pset",pget(5,5),8)
rectfill(10,10,12,12,9)
eq("rectfill",pget(11,11),9)
eq("rectfill edge",pget(13,13),1)
circfill(64,64,3,10)
eq("circfill",pget(64,61),10)
eq("circfill out",pget(64,60),1)
line(0,20,10,20,11)
eq("line",pget(5,20),11)
line(20,30)
eq("line cont",pget(15,25),11)
camera(10,0)
pset(10,30,12)
camera()
eq("camera",pget(0,30),12)
clip(0,0,5,5)
rectfill(0,0,20,20,13)
clip()
eq("clip in",pget(4,4),13)
eq("clip out",pget(6,6),1)
pal(7,8)
pset(1,40,7)
pal()
eq("pal",pget(1,40),8)
color(3)
pset(2,40)
eq("pen",pget(2,40),3)
sset(0,0,7) sset(1,0,0)
spr(0,0,50)
eq("spr",pget(0,50),7)
eq("spr transparent",pget(1,50),1)
palt(0,false)
spr(0,0,52)
palt()
eq("palt",pget(1,52),0)
sspr(0,0,2,1,0,60,4,2)
eq("sspr stretch",pget(1,61),7)
fset(1,0,true)
eq("fset bit",fget(1,0),true)
eq("fget",fget(1),1)
fset(1,0xff)
eq("fset byte",fget(1),255)
mset(3,4,5)
eq("mset",mget(3,4),5)
mset(1,40,6)
eq("map low half",peek(0x1000+(40-32)*128+1),6)
sset(8,0,14)
mset(0,0,1)
map(0,0,0,70,1,1)
eq("map",pget(0,70),14)
eq("print width",print("abc",0,100),12)
cursor(0,0)
print("x")
eq("cursor moves",peek(0x5f27),6)
fillp(0x5a5a)
rectfill(0,110,3,113,0x2e)
fillp()
eq("fillp a",pget(0,110),0xe)
eq("fillp b",pget(1,110),2)
oval(30,30,40,36,4)
eq("oval",pget(35,30),4)
eq("btn",btn(0),false)
eq("time",t()>=0,true)
cartdata("nano8_api_test")
dset(3,4.5)
eq("dset",dget(3),4.5)
eq("stat fps",stat(8),30)
eq("pen after oval",peek(0x5f25)&15,4)

printh("api: "..checks.." checks, "..fails.." failed")
if fails==0 then
 printh("api: all tests passed")
end
cls(fails==0 and 3 or 8)
print(fails==0 and "all tests passed" or fails.." failed",4,4,7)
