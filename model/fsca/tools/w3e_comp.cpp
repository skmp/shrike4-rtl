// w3e_comp.cpp -- USER HYPOTHESIS, branch 3 framing: t is computed in 2s complement but CONSUMED as 1s
// complement (or vice versa), at every placement INCLUDING the linear and cubic paths -- which shifts the
// latch windows themselves, so the windows are regenerated per configuration.  m1/m4 only ever mangled the
// square path and scored against a fixed jwin18.
// Sites: 0 = T1 linear, 1 = T3 cubic (t^2 operand), 2 = square mul1 dx, 3 = square mul2 dx, 4 = I1 feedback.
#include "w3_common.h"
#include <cstring>
static int MASK = 0, MODE = 0;
// mangled offset for a site; returns dx (already shifted) so dx-level variants are expressible
static inline i64 mdx(int t, int site) {
    i64 dx = (i64)t << 13; if (!((MASK >> site) & 1)) return dx;
    switch (MODE) {
      case 0: return ((i64)(t + (t<0? 1:0))) << 13;   // 2s pattern read as 1s  (|t| -> |t|-1)
      case 1: return ((i64)(t - (t<0? 1:0))) << 13;   // 1s computed, read as 2s (|t| -> |t|+1)
      case 2: return ((i64)(t + 1)) << 13;            // uniform +1 (end-around carry added everywhere)
      case 3: return ((i64)(t - 1)) << 13;            // uniform -1 (missing carry everywhere)
      case 4: return dx + (t<0? 1:0);                 // 1s complement taken on the SHIFTED value
      case 5: return dx - (t<0? 1:0);
      case 6: return dx + 1;                          // uniform LSB of dx
      case 7: return dx - 1;
    } return dx; }
static inline i64 mfb(i64 I1, int t) {                // site 4: the feedback latch, same convention
    if (!((MASK>>4)&1)) return I1;
    switch (MODE) { case 0: return I1 + (t<0?1:0); case 1: return I1 - (t<0?1:0); case 2: return I1+1;
                    case 3: return I1-1; case 4: return I1 + (t<0?1:0); case 5: return I1 - (t<0?1:0);
                    case 6: return I1+1; case 7: return I1-1; } return I1; }
static inline i64 Jof(int t){ i64 I1 = (arr(KC, mdx(t,2)) + K) >> 23; I1 = mfb(I1,t); return (arr(I1, mdx(t,3)) + K) >> 18; }
static i64 L[1024], H[1024];
static void gen(){ const i64 LIM = 12000000ll<<2;
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3, mdx(t,1))+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f, mdx(t,0))+K,23-T.e1):0; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+2;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;   while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        L[t+512]=gl; H[t+512]=gh; } }
static int full(){ int v=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){
            i64 T1=T.C1f? sm(arr(T.C1f, mdx(t,0))+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3, mdx(t,1))+K)>>20)>>3;
            i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 T2=T.S? sm(arr(Jof(t),T.S)+K,U2TAB[n]+2):0;
            i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            if(V<lo||V>hi) v++; } } return v; }
int main(){ if(!load_fsca(FD))return 1; build_tables();
    const char* MN[8]={"t+1 if t<0 (2s read as 1s)","t-1 if t<0 (1s read as 2s)","t+1 uniform","t-1 uniform",
                       "dx+1 if t<0","dx-1 if t<0","dx+1 uniform","dx-1 uniform"};
    printf("sites: L=linear T1, C=cubic t^2 operand, 1=square mul1, 2=square mul2, F=I1 feedback\n");
    printf("windows regenerated per config; score = in-window t of 1024; full = model violations (baseline 161 at this fine scale)\n\n");
    int bestsc=-1, bestfull=1<<30; char bs[64]={0}, bf[64]={0};
    for(MODE=0;MODE<8;MODE++){
        printf("%-28s  sites      feas  score  full   J(17)     J(342)    J(-342)   pins\n", MN[MODE]);
        for(MASK=0;MASK<32;MASK++){
            gen(); int feas=0; for(int i=0;i<1024;i++) feas += L[i]<=H[i];
            auto Jf=[&](int t){ return Jof(t); };
            int sc=score_jwin(Jf,L,H); int fv=full();
            int pins=(Jf(17)>=L[17+512]&&Jf(17)<=H[17+512])+(Jf(342)>=L[342+512]&&Jf(342)<=H[342+512])
                    +(Jf(-342)>=L[-342+512]&&Jf(-342)<=H[-342+512]);
            char sm_[8]="-----"; const char* nm="LC12F"; for(int b=0;b<5;b++) if((MASK>>b)&1) sm_[b]=nm[b];
            char tag[64]; snprintf(tag,64,"m%d %s",MODE,sm_);
            if(sc>bestsc){bestsc=sc;strcpy(bs,tag);} if(fv<bestfull){bestfull=fv;strcpy(bf,tag);}
            if(MASK==0 || sc>=900 || fv<=170 || pins>0)
              printf("%-28s  %s  %4d  %4d  %5d  %-9lld %-9lld %-9lld  %d%s\n","",sm_,feas,sc,fv,
                (long long)Jf(17),(long long)Jf(342),(long long)Jf(-342),pins,pins?"  <== LANDMARK":"");
        }
        printf("\n");
    }
    printf("BEST over all 256 configs: score %d (%s)   fewest full violations %d (%s)\n", bestsc,bs,bestfull,bf);
    return 0; }
