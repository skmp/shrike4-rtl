// v10_hyp.cpp -- adversarial re-test of the user's two hypotheses, written independently.
//  A: t computed 2s-complement, consumed 1s-complement (t -> t+-1 on the negative branch only)
//  B: t carries one extra bit of precision (half-LSB operand, finer first latch, wrong shift width w)
// Both are tested (i) CONSISTENTLY (t changes feed the linear and cubic terms too, windows rebuilt)
// and (ii) square-path-only, which is what the jwin18 contract scores.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static i64 WL[1024],WH[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); char l[512];
    while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)==4){WL[t+512]=a;WH[t+512]=b;} } fclose(f); }
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}

// ---------- generic model pieces ------------------------------------------------------------
// tt   : the t actually used by the LINEAR and CUBIC terms (and by dx for the square unless overridden)
// tsq  : the t used to form the square path's dx
// ed   : integer offset applied to dx in the square path when dx<0 (sd<0) or always (sd==0)
// ei   : integer offset applied to I1 when I1<0 (si<0) or always (si==0)
struct Cfg { int w=13,s1=23,s2c=20,s2f=18; int e_t_neg=0; int e_dx_neg=0,e_dx_all=0; int e_i1_neg=0,e_i1_all=0; };
static inline i64 Jrule(int t,const Cfg&C,bool fine){
    int tu = (t<0)? t + C.e_t_neg : t;
    i64 dx = (i64)tu << C.w;
    if(C.e_dx_all) dx += C.e_dx_all; if(dx<0) dx += C.e_dx_neg;
    i64 I1 = (arr(KC,dx)+K) >> C.s1;
    if(C.e_i1_all) I1 += C.e_i1_all; if(I1<0) I1 += C.e_i1_neg;
    return (arr(I1,dx)+K) >> (fine?C.s2f:C.s2c);
}
// full model, with the t-transform applied to EVERY term (the physically consistent reading)
static int full_viol(const Cfg&C,bool fine,bool t_everywhere){ int viol=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){
            int tl = (t_everywhere && t<0)? t + C.e_t_neg : t;
            i64 dxl=(i64)tl<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dxl)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)tl*tl)<<3,dxl)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 J=Jrule(t,C,fine);
            i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]+(fine?2:0)):0;
            i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            if(V<lo||V>hi)viol++; } }
    return viol; }
static int jscore(const Cfg&C){ int sc=0; for(int t=-512;t<=511;t++){ i64 j=Jrule(t,C,true); if(j>=WL[t+512]&&j<=WH[t+512])sc++; } return sc; }
// rebuild the fine J-window when the ODD terms use a modified t (so the contract stays self-consistent)
static void rebuild_win(int e_t_neg,i64*L,i64*H){ const int DU=2; const i64 LIM=12000000ll<<DU;
    for(int t=-512;t<=511;t++){ int tl=(t<0)?t+e_t_neg:t; i64 dxl=(i64)tl<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)tl*tl)<<3,dxl)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dxl)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,TB[n].S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,TB[n].S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        L[t+512]=gl;H[t+512]=gh; } }
int main(){ if(!load_fsca(F))return 1; build_tables(); load_jwin();
  puts("=== v10_hyp: the user's two hypotheses, independently re-run ===");
  Cfg B; printf("baseline: jwin18 %d/1024, full(coarse) %d, J(17)=%lld J(342)=%lld J(-342)=%lld\n\n",
      jscore(B),full_viol(B,false,false),(long long)Jrule(17,B,true),(long long)Jrule(342,B,true),(long long)Jrule(-342,B,true));

  puts("--- RIGIDITY: sign of every square-path intermediate on the positive landmarks ---");
  for(int t:{17,342,-342,-17}){ i64 dx=(i64)t<<13,a1=arr(KC,dx),i1=(a1+K)>>23,a2=arr(i1,dx);
      printf("  t=%+4d dx=%+10lld arr(KC,dx)=%+16lld I1=%+10lld arr(I1,dx)=%+16lld  -> any 'negative-only' rule is INERT at t>0: %s\n",
        t,(long long)dx,(long long)a1,(long long)i1,(long long)a2,(t>0?"yes":"n/a")); }

  puts("\n--- HYPOTHESIS A: complement mis-read, at three placements ---");
  puts("  (i) at the t level, applied CONSISTENTLY to linear+cubic+square, windows REBUILT:");
  for(int e:{+1,-1}){ Cfg C=B; C.e_t_neg=e; static i64 L[1024],H[1024]; rebuild_win(e,L,H);
      int sc=0,feas=0; for(int t=-512;t<=511;t++){ if(L[t+512]<=H[t+512])feas++; i64 j=Jrule(t,C,true); if(j>=L[t+512]&&j<=H[t+512])sc++; }
      printf("      t<0 -> t%+d : full(consistent) %6d ; rebuilt-window feasible %4d/1024 ; score %4d ; J(-342)=%lld\n",
        e,full_viol(C,false,true),feas,sc,(long long)Jrule(-342,C,true)); }
  puts("  (ii) at the t level, square path only (what the jwin18 contract scores):");
  for(int e:{+1,-1}){ Cfg C=B; C.e_t_neg=e;
      printf("      t<0 -> t%+d : jwin18 %4d ; full %6d ; J(-342)=%lld (window [%lld,%lld])\n",
        e,jscore(C),full_viol(C,false,false),(long long)Jrule(-342,C,true),(long long)WL[-342+512],(long long)WH[-342+512]); }
  puts("  (iii) at the dx level (negate with ~ instead of -), and (iv) at I1:");
  for(int e:{+1,-1}){ Cfg C=B; C.e_dx_neg=e;
      printf("      dx<0 -> dx%+d : jwin18 %4d ; full %6d ; J(-342)=%lld ; J(17)=%lld ; J(342)=%lld\n",
        e,jscore(C),full_viol(C,false,false),(long long)Jrule(-342,C,true),(long long)Jrule(17,C,true),(long long)Jrule(342,C,true)); }
  for(int e:{+1,-1}){ Cfg C=B; C.e_i1_neg=e;
      printf("      I1<0 -> I1%+d : jwin18 %4d ; full %6d ; J(-342)=%lld ; J(17)=%lld ; J(342)=%lld\n",
        e,jscore(C),full_viol(C,false,false),(long long)Jrule(-342,C,true),(long long)Jrule(17,C,true),(long long)Jrule(342,C,true)); }

  puts("\n--- HYPOTHESIS A joint scan over (e_t, e_dx, e_I1), negative branch only ---");
  {  int hit17=0,hit342=0,both=0,best=-1,cnt=0; Cfg bc;
     for(int et=-2;et<=2;et++) for(int ed=-8;ed<=8;ed++) for(int ei=-8;ei<=8;ei++){
        Cfg C=B; C.e_t_neg=et; C.e_dx_neg=ed; C.e_i1_neg=ei; cnt++;
        i64 j17=Jrule(17,C,true), j342=Jrule(342,C,true);
        if(j17==45617)hit17++; if(j342==18466836)hit342++; if(j17==45617&&j342==18466836)both++;
        int s=jscore(C); if(s>best){best=s;bc=C;} }
     printf("  %d configs; hit J(17)=45617: %d ; hit J(342)=18466836: %d ; BOTH: %d ; best jwin18 %d (e_t=%d e_dx=%d e_I1=%d)\n",
        cnt,hit17,hit342,both,best,bc.e_t_neg,bc.e_dx_neg,bc.e_i1_neg); }

  puts("\n--- HYPOTHESIS B: one extra bit of precision ---");
  puts("  (i) half-LSB on the operand, dx = (t<<13) +- 4096, square path only and everywhere:");
  for(int e:{+4096,-4096}){ Cfg C=B; C.e_dx_all=e;
      printf("      dx %+6d : jwin18 %4d ; full(square only) %6d\n",e,jscore(C),full_viol(C,false,false)); }
  { // half-LSB in the LINEAR term too (t' = t+-0.5 -> use dx = t<<13 +- 4096 in T1 and A3)
    for(int e:{+4096,-4096}){ int viol=0;
      for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dxl=((i64)t<<13)+e;
          i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dxl)+K,23-TB[n].e1):0;
          i64 A3=((arr(((i64)t*t)<<3,dxl)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
          i64 I1=(arr(KC,dxl)+K)>>23, J=(arr(I1,dxl)+K)>>20;
          i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]):0;
          i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi); if(V<lo||V>hi)viol++; } }
      printf("      dx %+6d in ALL terms: full %6d\n",e,viol); } }
  puts("  (ii) an extra bit kept by the first chain latch (s1+s2 = 41 fine / 43 coarse, KC fixed, w=13):");
  for(int s1=21;s1<=26;s1++){ Cfg C=B; C.s1=s1; C.s2f=41-s1; C.s2c=43-s1;
      printf("      s1=%2d (fine s2=%2d / coarse s2=%2d): jwin18 %4d ; full %6d\n",s1,C.s2f,C.s2c,jscore(C),full_viol(C,false,false)); }
  puts("  (iii) the operand alignment itself: w sweep with s1+s2 = 2w+15 (fine) / 2w+17 (coarse), KC fixed");
  puts("        NOTE dxmax = 512<<w ; the FIPR-validated array domain is operands < 2^24.");
  for(int w=8;w<=20;w++){ int bj=-1,bs1=0,bfull=1<<30,bfs1=0;
      for(int s1=2;s1<=2*w+13;s1++){ Cfg C=B; C.w=w; C.s1=s1; C.s2f=2*w+15-s1; C.s2c=2*w+17-s1;
          if(C.s2f<0||C.s2c<0) continue;
          int j=jscore(C); if(j>bj){bj=j;bs1=s1;} int fv=full_viol(C,false,false); if(fv<bfull){bfull=fv;bfs1=s1;} }
      Cfg Cb=B; Cb.w=w; Cb.s1=bs1; Cb.s2f=2*w+15-bs1; Cb.s2c=2*w+17-bs1;
      printf("      w=%2d dxmax=2^%.1f | best jwin18 %4d at s1=%2d (J17=%lld J342=%lld) | best full %6d at s1=%2d\n",
        w,log2((double)(512ll<<w)),bj,bs1,(long long)Jrule(17,Cb,true),(long long)Jrule(342,Cb,true),bfull,bfs1); }
  return 0; }
