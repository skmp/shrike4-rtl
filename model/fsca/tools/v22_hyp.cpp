// v22_hyp.cpp -- INDEPENDENT test of the user's two hypotheses about t, plus the loss-law bound.
//  H-A: t computed in 2s complement, consumed as 1s complement (t_eff = t+1 for t<0), and the
//       reverse (t-1), at every placement in the square chain; plus a lost sign extension.
//  H-B: t carries one extra bit of precision (t_eff = t +- 1/2 ULP, +-1/4, +-1 ULP).
// Everything integer. Scoring is my own jwin18 loader.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f){fprintf(stderr,"no jwin18\n");exit(1);} char ln[256];
  int n=0; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){ JL[t+512]=l; JH[t+512]=h; n++; } } fclose(f);
  if(n!=1024){fprintf(stderr,"jwin18 lines %d\n",n);exit(1);} }

// ---- transform modes ------------------------------------------------------------------
// applied to dx (the operand actually presented to the array) and/or to the I1 feedback value.
enum { TR_NONE=0, TR_T_P1, TR_T_M1, TR_DX_P1, TR_DX_M1, TR_FB_P1, TR_FB_M1, TR_BOTH_P1, TR_BOTH_M1,
       TR_SEXT_P1024, TR_SEXT_M1024, TR_NMODE };
static const char* TRN[TR_NMODE] = {"none",
  "t+1 if t<0 (2s read as 1s, t level)", "t-1 if t<0 (1s read as 2s, t level)",
  "dx+1 if dx<0 (dx level)",             "dx-1 if dx<0 (dx level)",
  "I1+1 if I1<0 (feedback)",             "I1-1 if I1<0 (feedback)",
  "dx+1 and I1+1 if <0",                 "dx-1 and I1-1 if <0",
  "t+1024 if t<0 (lost sign extension)", "t-1024 if t<0 (lost sign extension)"};

struct Cfg { int w,s1,s2; i64 kc,K1,K2; int tr; int hb_num, hb_den; int hb_mag; };
// hb: dx = ((2^hb_den * t + hb_num) << w) >> hb_den, done exactly in integers (hb_num in half/quarter ULPs)
static inline i64 mkdx(int t,const Cfg&c){
    int tt=t;
    if(c.tr==TR_T_P1 && t<0) tt=t+1; if(c.tr==TR_T_M1 && t<0) tt=t-1;
    if(c.tr==TR_SEXT_P1024 && t<0) tt=t+1024; if(c.tr==TR_SEXT_M1024 && t<0) tt=t-1024;
    i64 dx;
    if(c.hb_num==0) dx=(i64)tt<<c.w;
    else { int sgn = c.hb_mag ? (tt<0?-1:1) : 1;              // magnitude-first variant
           i64 num = (i64)tt*c.hb_den + (i64)sgn*c.hb_num;     // t +- k/hb_den  in units of 1/hb_den
           i64 sh = c.w; i64 v = num<<sh;                      // v = (t*den + num) * 2^w
           dx = v / c.hb_den;                                  // exact when hb_den | 2^w
           if(v % c.hb_den) { fprintf(stderr,"hb not exact\n"); exit(1); } }
    if(c.tr==TR_DX_P1 && dx<0) dx+=1; if(c.tr==TR_DX_M1 && dx<0) dx-=1;
    if(c.tr==TR_BOTH_P1 && dx<0) dx+=1; if(c.tr==TR_BOTH_M1 && dx<0) dx-=1;
    return dx; }
static inline i64 Jof(int t,const Cfg&c){ i64 dx=mkdx(t,c);
    i64 I1=(arr(c.kc,dx)+c.K1)>>c.s1;
    if((c.tr==TR_FB_P1||c.tr==TR_BOTH_P1) && I1<0) I1+=1;
    if((c.tr==TR_FB_M1||c.tr==TR_BOTH_M1) && I1<0) I1-=1;
    return (arr(I1,dx)+c.K2)>>c.s2; }
static int jscore(const Cfg&c){ int s=0; for(int i=0;i<1024;i++){ i64 j=Jof(i-512,c); if(j>=JL[i]&&j<=JH[i])s++; } return s; }

int main(){ build_tables(); load_jwin();
    const i64 TA=45617, TB_=18466836, TCLO=18466858, TCHI=18466867;
    printf("=== H-A: sign-conditional transforms, baseline alignment w=13 s1=23 s2=18 KC K1=K2=2^19 ===\n");
    printf("%-38s | jwin18 | J(17)  | J(342)   | J(-342)  | D=J(-342)-J(342)\n","variant");
    for(int tr=0;tr<TR_NMODE;tr++){ Cfg c{13,23,18,KC,K,K,tr,0,1,0};
        printf("%-38s | %4d   | %6lld | %8lld | %8lld | %lld\n",TRN[tr],jscore(c),
            (long long)Jof(17,c),(long long)Jof(342,c),(long long)Jof(-342,c),
            (long long)(Jof(-342,c)-Jof(342,c))); }
    printf("targets: J(17)=%lld  J(342)=%lld  J(-342) in [%lld,%lld]\n",(long long)TA,(long long)TB_,(long long)TCLO,(long long)TCHI);

    printf("\n=== H-B: extra precision on t, baseline alignment ===\n");
    printf("%-38s | jwin18 | J(17)  | J(342)   | J(-342)\n","variant");
    struct HB{const char*n;int num,den,mag;};
    HB hbs[]={{"t + 1/2 ULP",1,2,0},{"t - 1/2 ULP",-1,2,0},{"|t| + 1/2 ULP (magnitude)",1,2,1},
              {"|t| - 1/2 ULP (magnitude)",-1,2,1},{"t + 1/4 ULP",1,4,0},{"t - 1/4 ULP",-1,4,0},
              {"t + 1 ULP",1,1,0},{"t - 1 ULP",-1,1,0}};
    for(auto&h:hbs){ Cfg c{13,23,18,KC,K,K,TR_NONE,h.num,h.den,h.mag};
        printf("%-38s | %4d   | %6lld | %8lld | %8lld\n",h.n,jscore(c),
            (long long)Jof(17,c),(long long)Jof(342,c),(long long)Jof(-342,c)); }

    // ---- H-B decisive: refit the stored constant at EVERY alignment so J(342) is exact,
    //      then measure how far J(17) lands from 45617.  The constant only rescales the
    //      quadratic; a half-ULP adds a LINEAR term, so the two cannot be traded.
    printf("\n=== decisive: over all (w,s1,s2), choose KCw so J(342)=%lld exactly; min |J(17)-45617| ===\n",(long long)TB_);
    printf("(space: w in [6,18], s2 in [w+9,w+22], s1 with KCw scale in [2^14,2^27); K1=K2=2^19)\n");
    struct Var{const char*n;int num,den,mag;};
    Var vars[]={{"unbiased t (the model)",0,1,0},{"t + 1/2 ULP",1,2,0},{"t - 1/2 ULP",-1,2,0},
                {"|t| + 1/2 ULP",1,2,1},{"|t| - 1/2 ULP",-1,2,1},{"t + 1/4 ULP",1,4,0},{"t - 1/4 ULP",-1,4,0},
                {"t + 1 ULP",1,1,0},{"t - 1 ULP",-1,1,0}};
    for(auto&v:vars){ i64 bestd=-1; Cfg bc{}; long long nhit=0, ntrip=0;
        for(int w=6;w<=18;w++) for(int s2=w+9;s2<=w+22;s2++) for(int s1=8;s1<=2*w+22;s1++){
            long double ctr=157.88623046875L*powl(2.0L,(long double)(s1+s2-2*w));
            if(ctr<16384.0L||ctr>=134217728.0L) continue; ntrip++;
            i64 c0=(i64)llroundl(ctr); i64 span=(i64)(ctr*1e-4L)+64;
            bool any=false;
            for(i64 kc=c0-span;kc<=c0+span;kc++){ if(kc<=0) continue;
                Cfg c{w,s1,s2,kc,K,K,TR_NONE,v.num,v.den,v.mag};
                if(Jof(342,c)!=TB_) continue; any=true;
                i64 d=llabs(Jof(17,c)-TA); if(bestd<0||d<bestd){bestd=d;bc=c;} }
            if(any) nhit++; }
        if(bestd<0) printf("%-24s : NO alignment reaches J(342) exactly (%lld triples scanned)\n",v.n,ntrip);
        else printf("%-24s : min |J(17)-45617| = %6lld   at w=%d s1=%d s2=%d KCw=%lld   (J(17)=%lld, J(-342)=%lld)  [%lld/%lld triples could hit J(342)]\n",
            v.n,(long long)bestd,bc.w,bc.s1,bc.s2,(long long)bc.kc,(long long)Jof(17,bc),(long long)Jof(-342,bc),nhit,ntrip); }
    return 0; }
