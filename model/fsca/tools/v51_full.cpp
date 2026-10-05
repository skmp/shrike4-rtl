// v51_full.cpp -- ADVERSARIAL VERIFY: my own full-model evaluator over all 65536 angles.
// Written from fsca_ref2.h's stated rules, not from w5_model.h.
#include "base2.h"
#include <cstdio>
#include <cstring>
static Fsca F;
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
struct Cfg{ int rho=512; int dlin=0,dsq=0,dcub=0;  // t+d when t<0, per term
            int e=0;                                // dx=(2t+e)<<12
            int halfcub=0; int order=1;             // 1: arr(dx,KC) = published ref2 order
            int qlin=0,qsq=0,qcub=0;                // dx-level offset when t<0
            int rfb=0; int u=0; };                  // rfb: I1 offset when t<0 ; u: UNIFORM t offset
static inline i64 mkdx(i64 tv,const Cfg&C){ return C.e? ((2*tv+C.e)<<12) : (tv<<13); }
static inline i64 Vof(int n,int t,int flip,const Cfg&C){ const Tab&T=TB[n];
  i64 tl=t+C.u+(t<0?C.dlin:0), ts=t+C.u+(t<0?C.dsq:0), tc=t+C.u+(t<0?C.dcub:0);
  i64 dxl=mkdx(tl,C)+(t<0?C.qlin:0), dxs=mkdx(ts,C)+(t<0?C.qsq:0), dxc=mkdx(tc,C)+(t<0?C.qcub:0);
  i64 T1=T.C1f? sm(arr(T.C1f,dxl)+K,23-T.e1):0;
  i64 sq = C.halfcub? (((2*tc+C.e)*(2*tc+C.e))>>2) : (tc*tc);
  i64 A3=((arr(sq<<3,dxc)+K)>>20)>>3;
  i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
  i64 I1=C.order? ((arr(dxs,KC)+K)>>23) : ((arr(KC,dxs)+K)>>23);
  if(t<0) I1+=C.rfb;
  i64 J=(arr(I1,dxs)+K)>>20;
  i64 T2=T.S? sm(arr(J,T.S)+K,U2T[n]):0;
  i64 s=flip?-1:1; return C0T[n]+s*T1-T2-s*T3; }
static int score(const Cfg&C,int*vp=nullptr,int*vn=nullptr){ int v=0,p=0,q=0;
  for(int a=0;a<65536;a++){ int nr=(a+C.rho)>>10, node=nr&63, t=a-1024*nr;
    int so=1,m=node; if(m>=32){so=-1;m-=32;} int flip=m>=17,n=flip?32-m:m;
    i64 V=so*Vof(n,t,flip,C);
    Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi;
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;}
    if(V<lo||V>hi){ v++; if(t>=0)p++; else q++; } }
  if(vp)*vp=p; if(vn)*vn=q; return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
  Cfg base; int bp,bn; int b=score(base,&bp,&bn);
  printf("BASELINE full model over 65536 angles: %d violations (pos-t %d, neg-t %d)\n\n",b,bp,bn);
  printf("--- A: t+d for t<0, applied to ALL THREE terms ---\n");
  for(int d=-2;d<=2;d++){ Cfg C; C.dlin=C.dsq=C.dcub=d; int p,q; int v=score(C,&p,&q);
    printf("  d=%+d : %6d violations (pos %d of 32768, neg %d of 32768)%s\n",d,v,p,q,
      d==1?"  <- 2s computed / 1s read":(d==-1?"  <- converse":"")); }
  printf("--- A: per-term, d=+1 ---\n");
  { Cfg a1;a1.dlin=1; Cfg a2;a2.dsq=1; Cfg a3;a3.dcub=1;
    printf("  lin-only %d   sq-only %d   cub-only %d\n",score(a1),score(a2),score(a3)); }
  printf("--- A3: UNIFORM t+u (missing carry-in) ---\n");
  for(int u=-1;u<=1;u++){ Cfg C; C.u=u; printf("  u=%+d : %6d\n",u,score(C)); }
  printf("\n--- B(i): dx=(2t+e)<<12 ---\n");
  for(int e=-2;e<=2;e++){ Cfg C; C.e=e; Cfg D; D.e=e; D.halfcub=1;
    printf("  e=%+d : %6d   (with halved cubic square factor: %6d)\n",e,score(C),score(D)); }
  printf("\n--- B(iii): node rule rho ---\n");
  for(int rho : {0,256,448,510,511,512,513,514,576,768,1023}){ Cfg C; C.rho=rho;
    printf("  rho=%4d : %6d%s\n",rho,score(C),rho==512?"  <- published":""); }
  printf("\n--- B(iii) sharp: only the 64 boundary angles ---\n");
  { auto bv=[&](int rho,int off)->int{ Cfg C; C.rho=rho; int v=0;
      for(int nn=0;nn<64;nn++){ int a=(1024*nn+off)&0xffff;
        int nr=(a+C.rho)>>10, node=nr&63, t=a-1024*nr;
        int so=1,m=node; if(m>=32){so=-1;m-=32;} int flip=m>=17,n2=flip?32-m:m;
        i64 V=so*Vof(n2,t,flip,C); Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} if(V<lo||V>hi)v++; }
      return v; };
    printf("  a=1024n+512: rho=512 (t=-512, upper node) %d viol ; rho=511 (t=+512, lower node) %d viol\n",bv(512,512),bv(511,512));
    printf("  a=1024n+511: rho=512 (t=+511) %d viol ; rho=513 (t=-513, upper node) %d viol\n",bv(512,511),bv(513,511)); }
  printf("\n--- the labelled FITTED diagnostic: dx-level sign-gated offsets qsq and I1 offset rfb ---\n");
  { int best=1<<30,bq=0,br=0;
    for(int q=-2;q<=2;q++) for(int r=-2;r<=2;r++){ Cfg C; C.qsq=q; C.rfb=r; int v=score(C);
      if(v<best){best=v;bq=q;br=r;} }
    printf("  best over qsq,rfb in [-2,2]^2: %d violations at qsq=%+d rfb=%+d (baseline %d)\n",best,bq,br,b);
    Cfg C; C.qsq=1; C.rfb=-1; int p,q2; int v=score(C,&p,&q2);
    printf("  at qsq=+1 rfb=-1 : %d violations (pos %d, neg %d)\n",v,p,q2); }
  return 0; }
