/* doom_trace.c: a host harness for the SNES core trace hooks (snes_trace in snes_int.h, built with -DSNES_TRACE). It runs a Super FX ROM from the
 * reset and prints the CPU writes to $7E:5980-$7E:5B80 and $7F:4F72-$7F:5000, every mvn/mvp, every general DMA and every $2180 run, and stops at the
 * first jump into $7E:5980-$7E:5B80. No ROM data is kept in the repository. Build and run (WSL, from the repository root):
 *   gcc -O1 -DSNES_TRACE -Icore/src/bm -o /tmp/doom_trace tests/snes/doom_trace.c core/src/bm/snes_{bus,gsu,dsp77,cpu,ppu,spc,dsp}.c
 *   /tmp/doom_trace doom.sfc 2500      (the second argument is the number of frames; pic*.ppm are written at the end)
 */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t ring[64]; static int rn; static int hit; static int frame;
static uint32_t last_mv_s, last_mv_d, mvn_cnt, mv_pc; static int mv_on, mv_dir;
static void mv_flush(void){ if(mv_on){ printf("f%d MV%c %06X -> %06X count %u (pc %06X)\n",frame,mv_dir?'P':'N',last_mv_s,last_mv_d,mvn_cnt,mv_pc); mv_on=0; } }
static void on_wr(uint32_t a,uint8_t v,uint32_t pc){
  uint32_t w=a; /* wram linear */
  int lin=-1; unsigned b=a>>16, o=a&0xFFFF;
  if(b==0x7E) lin=o; else if(b==0x7F) lin=0x10000+o; else if((b&0x7F)<0x40 && o<0x2000) lin=o;
  if((((a&0xFFFF)>=0x2180&&(a&0xFFFF)<=0x2183)||((a&0xFFFF)>=0x2115&&(a&0xFFFF)<=0x2119&&frame>=1086)||((a&0xFFFF)==0x420B&&frame>=1086))&&(a>>16)<0x40){printf("f%d REG %04X <- %02X pc %06X\n",frame,a&0xFFFF,v,pc);}
  if(lin<0) return; (void)w;
  if((lin>=0x5980&&lin<0x5B80)||(lin>=0x14F72&&lin<0x15000)) { mv_flush(); printf("f%d W %06X <- %02X  pc %06X\n",frame,a,v,pc); }
}
static void on_mv(uint32_t s,uint32_t d,uint32_t c,int p,uint32_t pc){
  if(mv_on && pc==mv_pc){ return; }
  mv_flush(); mv_on=1; last_mv_s=s; last_mv_d=d; mvn_cnt=c; mv_dir=p; mv_pc=pc;
  { static int total; if(total++<5000) {} }
}
static void on_dma(int ch,int to_b,uint32_t abus,uint8_t bbad,uint32_t cnt){
  if(1) { mv_flush(); printf("f%d DMA ch%d %s A=%06X B=21%02X count %u dmap %02X vmaddr %04X vmain %02X\n",frame,ch,to_b?"A->B":"B->A",abus,bbad,cnt,S.dma[ch].dmap,S.vmaddr,S.vmain); }
}
static uint32_t wport_start; static uint32_t wport_n, wport_next; static int wport_on;
static void on_wport(uint32_t addr,uint8_t v){ (void)v;
  if(wport_on && addr==wport_next){ wport_n++; wport_next++; return; }
  if(wport_on){ printf("f%d WPORT run %05X len %u\n",frame,wport_start,wport_n); }
  wport_on=1; wport_start=addr; wport_n=1; wport_next=addr+1; }
static void on_op(uint32_t pc){
  ring[rn++&63]=pc;
  if(!hit && (pc>>16)==0x7E && (pc&0xFFFF)>=0x5980 && (pc&0xFFFF)<0x5B80){
    hit=1; mv_flush(); printf("f%d FIRST JUMP %06X; last pcs:\n",frame,pc);
    {FILE*o=fopen("wram.bin","wb");fwrite(S.wram,1,131072,o);fclose(o);}
    for(int i=32;i>=1;i--) printf("  %06X\n",ring[(rn-1-i)&63]);
    cpu_regs_t r; cpu_get(&r); printf("regs a=%04X x=%04X y=%04X s=%04X d=%04X pc=%04X p=%02X e=%d pb=%X db=%X\n",r.a,r.x,r.y,r.s,r.d,r.pc,r.p,r.e,r.pb,r.db);
    printf("stack bytes at s..s+16:"); for(int i=0;i<16;i++) printf(" %02X",S.wram[(r.s+1+i)&0x1FFFF]); printf("\n");
    printf("tbl 7F:4F72:"); for(int i=0;i<16;i++) printf(" %02X",S.wram[0x14F72+i]); printf("\n");
    printf("wram 780: "); for(int i=0;i<48;i++) printf(" %02X",S.wram[0x780+i]); printf("\n");
    printf("vmaddr %04X vmain %02X\n",S.vmaddr,S.vmain);
    printf("wram 5980: "); for(int i=0;i<16;i++) printf(" %02X",S.wram[0x5980+i]); printf("\n");
    printf("wram 2000: "); for(int i=0;i<16;i++) printf(" %02X",S.wram[0x2000+i]); printf("\n");
    printf("wram 51A0: "); for(int i=0;i<16;i++) printf(" %02X",S.wram[0x51A0+i]); printf("\n");
    fflush(stdout);
  }
}
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); uint8_t*d=malloc(n); fread(d,1,n,f); fclose(f);
  const char*why; if(snes_load(d,n,&why)){printf("load: %s\n",why);return 1;}
  printf("rom %ld hirom %d gsu %d sram %u\n",n,S.hirom,S.gsu,S.sram_size);
  int maxf=argc>2?atoi(argv[2]):1200;
  snes_trace.wr=on_wr; snes_trace.mv=on_mv; snes_trace.dma=on_dma; snes_trace.wport=on_wport; snes_trace.op=on_op;
  for(frame=0;frame<maxf&&!hit;frame++){ snes_frame(0,0,frame%50==49||frame==maxf-1); if(frame==1200||frame==1800||frame==maxf-1){char nm[64];sprintf(nm,"pic%d.ppm",frame);int ln;const uint16_t*pc=snes_picture(&ln);FILE*o=fopen(nm,"wb");fprintf(o,"P6 256 %d 255\n",ln);for(int i=0;i<256*ln;i++){uint16_t c=pc[i];fputc((c>>11)<<3,o);fputc(((c>>5)&63)<<2,o);fputc((c&31)<<3,o);}fclose(o);} }
  mv_flush(); printf("end frame %d hit %d\n",frame,hit); return 0; }
