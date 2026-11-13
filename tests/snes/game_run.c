/* game_run.c: a host harness that plays a SNES ROM for N frames with a scripted pad, saves pictures and logs the DSP DR traffic (built with -DSNES_TRACE).
 *   gcc -O1 -DSNES_TRACE -Icore/src/bm -o game_run tests/snes/game_run.c core/src/bm/snes_{bus,gsu,dsp77,cpu,ppu,spc,dsp}.c
 *   game_run rom fw|- frames script outprefix [dsplog]   script lines: "first last padhex" (frames); pictures at the frames listed after "shot" lines: "shot f"
 * Script: one line "first last padhex" holds the pad for those frames (pad bits as in snes.h: START 1000, A 0080, UP 0800); "shot f" saves picture f (PPM) and, with DUMPSTATE set, the OAM, CGRAM and WRAM of that frame.
 * Environment (all optional; the harness writes vram.bin, wram.bin, cgram.bin, oam.bin at the end and prints the PPU and DMA state):
 *   RENDER_EVERY=n                 draws only every n-th frame and the shot frames (the frameskip of the cart): the shot must equal the one of a run that draws all
 *   POKE="frame:wramaddr=val,..."  writes the work RAM before a frame (a test hook: Zelda damage is 4 at $0373)
 *   REGLOG=file REGFROM=f REGFRAME=f2  logs, for frames f..f2: the PPU registers (HDMA too), the DMA to the video RAM and to the OAM, the CPU registers $4300-$420C, $4200, $4207, $4209
 *   WLO=hex WHI=hex [WRAW=1]       with REGLOG: the CPU writes in that address range (runs; WRAW: each one with its PC)
 *   WATCH=wramaddr                 with REGLOG: the PC that changes a work RAM byte;  PRINTW=wramaddr  the byte and the two after it at the end of each frame
 *   IRQLOG=pchex, LINEWATCH=line   with REGLOG: when the CPU is at a PC (with the line, the clock and the PC before it); the first PC of a line
 *   CAP_PC=lo CAP_HI=hi CAP_FRAME=f CAP_N=n  from the first instruction in [lo,hi) at frame f: cap.txt (the registers) and cap_wram.bin, and n lines of trace_ours.txt (PC, A, X, Y, S, D, P, line, clock)
 * The DSP-1 traffic is written to the log file named by the sixth argument (frame, W or R, byte, address, PC).
 * The ROM and the firmware are read from the user's disk; nothing of them goes into the repository. */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *dl; static int frame; static FILE *rl; static int rlf=-1; static unsigned rlf0=0x7fffffff;
static void on_dma(int ch,int to_b,uint32_t abus,uint8_t bbad,uint32_t cnt){ if(rl&&frame>=rlf0&&frame<=rlf&&bbad==4) fprintf(rl,"OAMDMA f%d L%d clk %d A=%06X cnt %u oamaddr %04X buf hi %02X %02X %02X link %02X%02X%02X%02X %02X%02X%02X%02X\n",frame,S.line,S.line_clk,abus,cnt,S.oamaddr,S.wram[(abus&0x1FFFF)+0x200+25],S.wram[(abus&0x1FFFF)+0x200+26],S.wram[(abus&0x1FFFF)+0x200+27],S.wram[(abus&0x1FFFF)+408],S.wram[(abus&0x1FFFF)+409],S.wram[(abus&0x1FFFF)+410],S.wram[(abus&0x1FFFF)+411],S.wram[(abus&0x1FFFF)+412],S.wram[(abus&0x1FFFF)+413],S.wram[(abus&0x1FFFF)+414],S.wram[(abus&0x1FFFF)+415]); if(rl&&frame>=rlf0&&frame<=rlf&&bbad>=0x18&&bbad<=0x19) fprintf(rl,"DMA f%d L%d ch%d %s A=%06X B=21%02X count %u dmap %02X vmaddr %04X vmain %02X\n",frame,S.line,ch,to_b?"A->B":"B->A",abus,bbad,cnt,S.dma[ch].dmap,S.vmaddr,S.vmain); }
static void on_wr(uint32_t a,uint8_t v,uint32_t pc){ if(getenv("DMAREG")&&rl&&frame>=rlf0&&frame<=rlf&&(a&0xFFFF)>=0x4300&&(a&0xFFFF)<=0x430F&&(a>>16)<0x40) fprintf(rl,"R f%d L%d %04X<-%02X pc %06X\n",frame,S.line,a&0xFFFF,v,pc);
  if(getenv("DMAREG")&&rl&&frame>=rlf0&&frame<=rlf&&((a&0xFFFF)==0x420B||(a&0xFFFF)==0x420C)&&(a>>16)<0x40) fprintf(rl,"R f%d L%d %04X<-%02X pc %06X\n",frame,S.line,a&0xFFFF,v,pc);
  static int lf=-1; static uint32_t lpc,lo_a,hi_a; static long cnt; const char*e=getenv("WLO"); if(!e||!rl) return; unsigned lo=strtoul(e,0,16),hi=strtoul(getenv("WHI"),0,16); (void)v; if(getenv("WRAW")){ if(frame>=rlf0&&frame<=rlf&&a>=lo&&a<hi) fprintf(rl,"W f%d L%d %06X<-%02X pc %06X\n",frame,S.line,a,v,pc); return; }
  if(frame>=rlf0&&frame<=rlf&&a>=lo&&a<hi){ if(frame==lf&&(pc>>8)==(lpc>>8)){ cnt++; if(a<lo_a)lo_a=a; if(a>hi_a)hi_a=a; return; } if(cnt) fprintf(rl,"W f%d pc %06X.. x%ld addr %06X-%06X\n",lf,lpc,cnt,lo_a,hi_a); lf=frame; lpc=pc; cnt=1; lo_a=hi_a=a; } }
static int capon; static long capn; static FILE *tf; static unsigned caplo,caphi; static int capfr;
static void on_opr(const cpu_regs_t *r, int clk){ uint32_t pc=((uint32_t)r->pb<<16)|r->pc;
  if(!capon){ if(pc<caplo||pc>=caphi||frame<capfr) return; capon=1; FILE*o=fopen("cap.txt","w"); fprintf(o,"%04X %04X %04X %04X %04X %04X %02X %d %02X %02X\n",r->a,r->x,r->y,r->s,r->d,r->pc,r->p,r->e,r->pb,r->db); fclose(o); o=fopen("cap_wram.bin","wb"); fwrite(S.wram,1,131072,o); fclose(o); tf=fopen("trace_ours.txt","w"); }
  if(tf&&capn++<atol(getenv("CAP_N"))) fprintf(tf,"%06X %04X %04X %04X %04X %04X %02X L%d c%d\n",pc,r->a,r->x,r->y,r->s,r->d,r->p,S.line,clk); else if(tf){ fclose(tf); tf=0; } }
static unsigned last_pc_hint; static void on_op1(uint32_t pc){ { static int pv=-1; const char*w=getenv("WATCH"); if(w&&rl&&frame>=rlf0&&frame<=rlf){ unsigned ad=strtoul(w,0,16); int v=S.wram[ad]; if(v!=pv){ if(pv>=0) fprintf(rl,"Wt f%d L%d %05X %02X->%02X after pc %06X (now %06X)\n",frame,S.line,ad,pv,v,last_pc_hint,pc); pv=v; } } } { static int pl=-1; if(getenv("LINEWATCH")&&rl&&frame>=rlf0&&frame<=rlf&&S.line!=pl){ if(S.line==atoi(getenv("LINEWATCH"))||S.line==atoi(getenv("LINEWATCH"))+1) fprintf(rl,"Lw f%d L%d clk %d first pc %06X prev %06X\n",frame,S.line,S.line_clk,pc,last_pc_hint); pl=S.line; } } if(getenv("IRQLOG")&&rl&&frame>=rlf0&&frame<=rlf&&pc==strtoul(getenv("IRQLOG"),0,16)) fprintf(rl,"N f%d L%d clk %d mainpc %06X oamhi %02X %02X %02X link %02X%02X%02X%02X %02X%02X%02X%02X\n",frame,S.line,S.line_clk,last_pc_hint,S.wram[0xA20+25],S.wram[0xA20+26],S.wram[0xA20+27],S.wram[0x800+408],S.wram[0x800+409],S.wram[0x800+410],S.wram[0x800+411],S.wram[0x800+412],S.wram[0x800+413],S.wram[0x800+414],S.wram[0x800+415]); last_pc_hint=pc; if(getenv("IRQLOG")&&rl&&frame>=rlf0&&frame<=rlf&&(pc==strtoul(getenv("IRQLOG"),0,16))) fprintf(rl,"I f%d L%d clk %d enter pc %06X nmitimen %02X nmiflag %d\n",frame,S.line,S.line_clk,pc,S.nmitimen,S.nmiflag); } static void on_reg(uint16_t o,uint8_t v){ if(getenv("DMAREG")&&rl&&frame>=rlf0&&frame<=rlf&&((o>=0x4300&&o<=0x430F)||o==0x420B||o==0x420C||o==0x4200||o==0x4207||o==0x4209)) fprintf(rl,"R f%d L%d %04X<-%02X pc %06X\n",frame,S.line,o,v,last_pc_hint); }
static int vwr[2],vwr_n;
static void on_ppu(uint16_t o,uint8_t v){ if(rl&&frame>=rlf0&&frame<=rlf&&(o==0x2115||o==0x2116||o==0x2117||o==0x2100||o==0x2105)) fprintf(rl,"f%d L%d %04X<-%02X\n",frame,S.line,o,v);
  else if(rl&&frame==rlf&&o!=0x2118&&o!=0x2119&&o!=0x2122&&o!=0x2104) fprintf(rl,"L%d %04X<-%02X\n",S.line,o,v);
  if(rl&&frame>=rlf0&&frame<=rlf&&(o==0x2118||o==0x2119)) { static int last=-1,cnt; if(o!=last){ if(cnt) fprintf(rl,"  f%d run %04X x%d\n",frame,last,cnt); last=o;cnt=0;} cnt++; } }
static void on_dsp(int k, uint32_t a, uint8_t v){ if(dl && k!=2) { cpu_regs_t r; cpu_get(&r); fprintf(dl,"%d %c %02X %06X %02X%04X\n",frame,k?'W':'R',v,a,r.pb,r.pc); } }
static uint8_t *slurp(const char*p,long*n){FILE*f=fopen(p,"rb");if(!f)return 0;fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);uint8_t*d=malloc(*n);fread(d,1,*n,f);fclose(f);return d;}
static void shot(const char*pre,int fr){char nm[256]; if(getenv("DUMPSTATE")){ FILE*o; sprintf(nm,"%s%d.oam",pre,fr); o=fopen(nm,"wb"); fwrite(S.oam,1,544,o); fclose(o); sprintf(nm,"%s%d.wr",pre,fr); o=fopen(nm,"wb"); fwrite(S.wram,1,131072,o); fclose(o); sprintf(nm,"%s%d.cg",pre,fr); o=fopen(nm,"wb"); fwrite(S.cgram,1,512,o); fclose(o); }
  sprintf(nm,"%s%d.ppm",pre,fr);int ln;const uint16_t*pc=snes_picture(&ln);FILE*o=fopen(nm,"wb");fprintf(o,"P6 256 %d 255\n",ln);for(int i=0;i<256*ln;i++){uint16_t c=pc[i];fputc((c>>11)<<3,o);fputc(((c>>5)&63)<<2,o);fputc((c&31)<<3,o);}fclose(o);}
int main(int argc,char**argv){
  if(argc<6){puts("usage");return 1;}
  long n; uint8_t*d=slurp(argv[1],&n); const char*why;
  if(snes_load(d,n,&why)){printf("load: %s\n",why);return 1;}
  if(strcmp(argv[2],"-")){long m; uint8_t*fw=slurp(argv[2],&m); if(snes_dsp_firmware(fw,m,NULL,0,&why)) printf("fw: %s\n",why);}
  printf("title %s dsp %d fw %d\n",snes_info()->title,snes_info()->dsp,snes_info()->dsp_fw);
  int maxf=atoi(argv[3]); int f0[256],f1[256],pd[256],ns=0,sh[256],nsh=0; char ln[128];
  FILE*sc=fopen(argv[4],"r"); while(sc&&fgets(ln,128,sc)){ int a,b,c; if(!strncmp(ln,"shot",4)){sh[nsh++]=atoi(ln+5);} else if(sscanf(ln,"%d %d %x",&a,&b,&c)==3){f0[ns]=a;f1[ns]=b;pd[ns++]=c;} }
  if(getenv("REGLOG")){rl=fopen(getenv("REGLOG"),"w");rlf=atoi(getenv("REGFRAME"));if(getenv("REGFROM"))rlf0=atoi(getenv("REGFROM"));snes_trace.ppu=on_ppu;snes_trace.dma=on_dma;snes_trace.wr=on_wr;snes_trace.reg=on_reg;snes_trace.op=on_op1;}
  if(getenv("CAP_PC")){ caplo=strtoul(getenv("CAP_PC"),0,16); caphi=strtoul(getenv("CAP_HI"),0,16); capfr=atoi(getenv("CAP_FRAME")); snes_trace.opr=on_opr; }
  if(argc>6){dl=fopen(argv[6],"w"); snes_trace.dsp=on_dsp;}
  for(frame=0;frame<maxf;frame++){ int p=0; for(int i=0;i<ns;i++) if(frame>=f0[i]&&frame<=f1[i]) p|=pd[i];
    int isshot=0; for(int i=0;i<nsh;i++) if(sh[i]==frame) isshot=1;
    { const char*pk=getenv("POKE"); /* "frame:wramaddr=val,..." test hook: writes the work RAM before the frame */
      while(pk&&*pk){ int fr; unsigned ad,vl; int k; if(sscanf(pk,"%d:%x=%x%n",&fr,&ad,&vl,&k)==3){ if(fr==frame) S.wram[ad&0x1FFFF]=(uint8_t)vl; pk+=k; if(*pk==',')pk++; } else break; } }
    { const char*re=getenv("RENDER_EVERY"); /* a test hook: draws only every Nth frame (the cart's frameskip), plus the shot frames */ int rn=re?atoi(re):1; snes_frame(p,0,isshot||frame==maxf-1||rn<=1||frame%rn==0); } if(getenv("PRINTW")&&frame>=rlf0&&frame<=rlf){ unsigned ad=strtoul(getenv("PRINTW"),0,16); fprintf(rl?rl:stdout,"PW f%d %05X=%02X %02X %02X\n",frame,ad,S.wram[ad],S.wram[ad+1],S.wram[ad+2]); } if(isshot||frame==maxf-1) shot(argv[5],frame); }
  {FILE*o=fopen("vram.bin","wb");fwrite(S.vram,1,65536,o);fclose(o);o=fopen("wram.bin","wb");fwrite(S.wram,1,131072,o);fclose(o);o=fopen("cgram.bin","wb");fwrite(S.cgram,1,512,o);fclose(o);o=fopen("oam.bin","wb");fwrite(S.oam,1,544,o);fclose(o);}
  printf("htime %d vtime %d nmitimen %02X\n",S.htime,S.vtime,S.nmitimen);printf("bgmode %02X m7sel %02X tm %02X ts %02X hdmaen %02X inidisp %02X setini %02X m7 a%d b%d c%d d%d x%d y%d ho%d vo%d\n",S.bgmode,S.m7sel,S.tm,S.ts,S.hdmaen,S.inidisp,S.setini,S.m7a,S.m7b,S.m7c,S.m7d,S.m7x,S.m7y,S.m7hofs,S.m7vofs);
  for(int c=0;c<8;c++) printf("dma%d dmap %02X bbad %02X a1 %02X:%04X das %04X dasb %02X tbl %04X line %02X ind %04X\n",c,S.dma[c].dmap,S.dma[c].bbad,S.dma[c].a1b,S.dma[c].a1t,S.dma[c].das,S.dma[c].dasb,S.dma[c].tbl,S.dma[c].line,S.dma[c].ind);
  printf("done %d frames\n",frame); return 0; }
