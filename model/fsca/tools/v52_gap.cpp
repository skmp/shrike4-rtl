// v52_gap.cpp -- the t-level magnitude readings, each computed explicitly (no +(e+1) indirection).
#include "v52_common.h"
static JW W;
static i64 Jm(int m){ i64 dx=(i64)m<<13; return chainJ_dx(dx,dx); }
static int sc(int mode){ int s=0; for(int t=-512;t<=511;t++){ int m;
    switch(mode){ case 0: m=t; break;                         // baseline, signed t
                  case 1: m = t>=0? t : ~t;   break;          // TRUE 1s magnitude  (= |t|-1)
                  case 2: m = t>=0? t : -t;   break;          // 2s magnitude       (= |t|)
                  case 3: m = t>=0? t : t+1;  break;          // negative operand read as 1s (value t+1)
                  case 4: m = t>=0? t : ~t+1; break;          // what m5_mag actually ran at e=0
                  default: m=t; }
    i64 j=Jm(m); if(j>=W.lo[t+512]&&j<=W.hi[t+512])s++; } return s; }
int main(){ if(!load_jwin(W))return 1; build_tables();
  const char* nm[5]={"baseline signed t","mag = ~t  (TRUE 1s, |t|-1)","mag = -t  (2s, |t|)","operand = t+1 for t<0 (1s read)","m5_mag's e=0 expression ~t+1"};
  for(int i=0;i<5;i++) printf("  %-34s : %4d/1024\n",nm[i],sc(i));
  return 0; }
