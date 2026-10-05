// r2_rmode.cpp -- r1 was cancelled by the half-turn mirror. Redo on the PRE-NEGATE V (always positive),
// so the mirror cannot cancel, and split by rounding-mode failure class.
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>
static Fsca F;
int main(){ if(!load_fsca(F)) return 1;
    long long nw[4]={0},nin[4]={0}; double s1[4]={0},s2v[4]={0};
    int lo1=0,hi1=0;  // direction of the error on the PRE-NEGATE V
    double bt[8]={0}; int nt[8]={0};
    for(int a=0;a<65536;a++){
        int q=(int)((a+512u)>>10), m=q&63, t=a-1024*q, neg=0; if(t<-512)t+=65536;
        if(m>=33){m-=32;neg=1;} if(m>=17)m=32-m;
        uint32_t hrn=F.srn[a],hrz=F.srz[a];
        Win w=decode(hrz,hrn); int64_t lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        int64_t Vlo=neg?-hi:lo, Vhi=neg?-lo:hi;            // pre-negate window
        int64_t V=neg? -fsca2_internal(a) : fsca2_internal(a);
        int W=(int)(Vhi-Vlo+1); if(W<1||W>3) continue;
        nw[W]++;
        if(V<Vlo) lo1++; else if(V>Vhi) hi1++;
        else { double c=(double)(Vlo+Vhi)/2.0, d=(double)V-c; nin[W]++; s1[W]+=d; s2v[W]+=d*d;
               int b=(t<0?-t:t)/64; if(b>7)b=7; bt[b]+=d; nt[b]++; }
    }
    printf("PRE-NEGATE V (mirror cannot cancel):\n");
    printf("  model BELOW its window: %d   ABOVE: %d   ratio %.2f : 1\n",lo1,hi1,hi1?(double)lo1/hi1:0);
    printf("  -> V too LOW means T2 too HIGH means J too HIGH.  A real systematic direction.\n\n");
    printf("position inside the window, by window width (0 = unbiased; width 1 carries no information):\n");
    for(int W=1;W<=3;W++) if(nin[W])
        printf("  width %d: n=%-6lld  mean %+.5f  rms %.4f\n",W,nin[W],s1[W]/nin[W],sqrt(s2v[W]/nin[W]));
    double tot=0; long long ntot=0; for(int W=2;W<=3;W++){tot+=s1[W];ntot+=nin[W];}
    printf("  widths 2+3 combined: n=%lld  mean %+.5f\n",ntot,ntot?tot/ntot:0);
    printf("  by |t| band:"); for(int b=0;b<8;b++) printf(" %d:%+.4f",b*64,nt[b]?bt[b]/nt[b]:0); printf("\n");
    return 0; }
