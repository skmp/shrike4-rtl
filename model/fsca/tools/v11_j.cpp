// v11_j.cpp -- independent re-measurement of: the jwin18 scoring contract, the 918 baseline, the full-model
// 147/148, the rigidity of the POSITIVE branch, and the user's two hypotheses (1s-vs-2s complement on t,
// and one extra bit of precision on t).  All integer.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <climits>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static i64 FJL[1024],FJH[1024];        // from jwin18.txt
static i64 BJL[1024],BJH[1024];        // rebuilt here, shift 18
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static void build_jwin18(){ const i64 LIM=12000000ll<<2;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+2;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,TB[n].S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,TB[n].S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        BJL[t+512]=gl; BJH[t+512]=gh; } }
// ---- configurable J rule
struct Cfg{ int w=13; int s1=23,s2=18; i64 kc=KC; int e_t=0,e_dx=0,e_I1=0; int bdx=0; bool ro=false; };
static inline i64 Jof(int t,const Cfg&c){
    int tt = t<0 ? t+c.e_t : t;
    i64 dx = ((i64)tt<<c.w) + c.bdx + (t<0?c.e_dx:0);
    i64 I1 = ((c.ro? arr(dx,c.kc):arr(c.kc,dx))+K)>>c.s1; if(t<0) I1 += c.e_I1;
    return (arr(I1,dx)+K)>>c.s2; }
static int score_jwin(const Cfg&c,const i64*L,const i64*H){ int s=0;
    for(int t=-512;t<=511;t++){ i64 j=Jof(t,c); if(j>=L[t+512]&&j<=H[t+512])s++; } return s; }
// ---- full model, J from Cfg, T2 shift adjusted to keep the product weight (u2 = 30+k at s2=20)
// du keeps the T2 product weight fixed: J scales as 2^(2w-s1-s2); baseline (w13,s1 23,s2 20) has du=0
static int full_viol(const Cfg&c){ int viol=0; int du=2*c.w-c.s1-c.s2+17;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 J=Jof(t,c); i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]+du):0;
            i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            if(V<lo||V>hi)viol++; } }
    return viol; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    { FILE*f=fopen("jwin18.txt","r"); if(!f){printf("no jwin18.txt\n");return 1;} char L[256]; int nread=0;
      while(fgets(L,sizeof L,f)){ if(L[0]=='#')continue; long long t,a,b,w;
        if(sscanf(L,"%lld %lld %lld %lld",&t,&a,&b,&w)==4){ FJL[t+512]=a; FJH[t+512]=b; nread++; } }
      fclose(f); printf("jwin18.txt: %d lines read\n",nread); }
    build_jwin18();
    { int diff=0,minw=1<<30,cnt0=0; for(int i=0;i<1024;i++){ if(FJL[i]!=BJL[i]||FJH[i]!=BJH[i])diff++;
        if(FJH[i]<FJL[i])cnt0++; minw=std::min(minw,(int)(FJH[i]-FJL[i])); }
      printf("  rebuilt-vs-file mismatches: %d ; infeasible t: %d ; narrowest window width: %d\n\n",diff,cnt0,minw); }

    Cfg base; printf("=== 1. BASELINE ===\n");
    printf("  jwin18 score (file windows)    : %d/1024\n",score_jwin(base,FJL,FJH));
    printf("  jwin18 score (rebuilt windows) : %d/1024\n",score_jwin(base,BJL,BJH));
    Cfg b20=base; b20.s2=20; printf("  full model (arr(KC,dx), s2=20) : %d violations\n",full_viol(b20));
    Cfg b20r=b20; b20r.ro=true; printf("  full model (arr(dx,KC), s2=20) : %d violations\n",full_viol(b20r));
    printf("  landmarks: J(17)=%lld (need %lld)  J(342)=%lld (need %lld)  J(-342)=%lld (need [%lld,%lld])\n\n",
        (long long)Jof(17,base),(long long)FJL[17+512],(long long)Jof(342,base),(long long)FJL[342+512],
        (long long)Jof(-342,base),(long long)FJL[-342+512],(long long)FJH[-342+512]);

    printf("=== 2. RIGIDITY OF THE POSITIVE BRANCH (every intermediate at t>0) ===\n");
    for(int t : {17,342,-342}){ i64 dx=(i64)t<<13; i64 p1=arr(KC,dx); i64 I1=(p1+K)>>23; i64 p2=arr(I1,dx);
        printf("  t=%5d : dx=%12lld  arr(KC,dx)=%16lld  I1=%10lld  arr(I1,dx)=%16lld  J=%10lld\n",
            t,(long long)dx,(long long)p1,(long long)I1,(long long)p2,(long long)((p2+K)>>18));
        printf("            signs: dx %s  arr1 %s  I1 %s  arr2 %s\n",
            dx<0?"NEG":"non-neg",p1<0?"NEG":"non-neg",I1<0?"NEG":"non-neg",p2<0?"NEG":"non-neg"); }
    printf("  -> a complement convention is by construction sign-dependent, so at t=+17 and t=+342 it is a no-op.\n\n");

    printf("=== 3. HYPOTHESIS A: 2s-complement t consumed as 1s complement, at each placement ===\n");
    printf("  placement           delta  jwin18   full   J(17)     J(342)      J(-342)\n");
    struct P{const char*nm;int lvl;};
    for(P p : {P{"t   (t<0 -> t+d)",0},P{"dx  (dx<0 -> dx+d)",1},P{"I1  (I1<0 -> I1+d)",2}})
      for(int d : {-1,1}){
        Cfg c; if(p.lvl==0)c.e_t=d; else if(p.lvl==1)c.e_dx=d; else c.e_I1=d;
        printf("  %-20s %+3d   %4d   %5d   %8lld  %10lld  %10lld\n",p.nm,d,
            score_jwin(c,FJL,FJH),full_viol([&]{Cfg q=c;q.s2=20;return q;}()),
            (long long)Jof(17,c),(long long)Jof(342,c),(long long)Jof(-342,c)); }
    printf("  targets                        918     147   %8lld  %10lld  [%lld,%lld]\n\n",
        (long long)FJL[17+512],(long long)FJL[342+512],(long long)FJL[-342+512],(long long)FJH[-342+512]);

    printf("=== 4. joint scan over (e_t, e_dx, e_I1): how many configs hit each pinned landmark? ===\n");
    { int hit17=0,hit342=0,hitboth=0,bestsc=0; Cfg bc; long long ntot=0;
      for(int et=-3;et<=3;et++) for(int ed=-96;ed<=96;ed++) for(int ei=-8;ei<=8;ei++){
          Cfg c; c.e_t=et;c.e_dx=ed;c.e_I1=ei; ntot++;
          bool h17=Jof(17,c)==FJL[17+512], h342=Jof(342,c)==FJL[342+512];
          if(h17)hit17++; if(h342)hit342++; if(h17&&h342)hitboth++;
          int s=score_jwin(c,FJL,FJH); if(s>bestsc){bestsc=s;bc=c;} }
      printf("  %lld configs: hit J(17) %d, hit J(342) %d, hit BOTH %d ; best jwin18 %d at (e_t=%d,e_dx=%d,e_I1=%d)\n\n",
          ntot,hit17,hit342,hitboth,bestsc,bc.e_t,bc.e_dx,bc.e_I1); }

    printf("=== 5. HYPOTHESIS B(i): half-LSB on dx (an extra bit of t below the stored LSB) ===\n");
    for(int b : {-8192,-4096,-2048,0,2048,4096,8192}){ Cfg c; c.bdx=b; Cfg cf=c; cf.s2=20;
        printf("  dx += %+6d : jwin18 %4d   full(coarse) %6d   J(17)=%lld\n",b,score_jwin(c,FJL,FJH),full_viol(cf),(long long)Jof(17,c)); }
    printf("\n=== 6. HYPOTHESIS B(ii): the s1/s2 ladder with s1+s2 = 41 held and KC fixed ===\n");
    for(int s1=20;s1<=27;s1++){ Cfg c; c.s1=s1; c.s2=41-s1; if(c.s2<10||c.s2>25)continue;
        Cfg cf=c; cf.s2=c.s2+2; printf("  (s1=%2d,s2=%2d) : jwin18 %4d   full(fine) %6d   full(coarse s2+2) %6d   J(17)=%lld J(342)=%lld\n",
            s1,c.s2,score_jwin(c,FJL,FJH),full_viol(c),full_viol(cf),(long long)Jof(17,c),(long long)Jof(342,c)); }
    printf("\n=== 7. HYPOTHESIS B(iii): operand width w with the compensating law s1+s2 = 2w+15, KC fixed ===\n");
    printf("   w   best(s1,s2)  jwin18  full(fine) full(coarse)   J(17)   J(342)\n");
    for(int w=8;w<=20;w++){ int bs=0,bs1=0,bs2=0,bf=1<<30;
        for(int s1=2;s1<=40;s1++){ int s2=2*w+15-s1; if(s2<2||s2>45)continue;
            Cfg c; c.w=w;c.s1=s1;c.s2=s2; int s=score_jwin(c,FJL,FJH); if(s>bs){bs=s;bs1=s1;bs2=s2;} }
        Cfg c; c.w=w;c.s1=bs1;c.s2=bs2; Cfg cc=c; cc.s2=bs2+2; bf=full_viol(cc);
        printf("  %2d   (%2d,%2d)     %4d   %6d   %6d   %7lld %10lld\n",w,bs1,bs2,bs,full_viol(c),bf,(long long)Jof(17,c),(long long)Jof(342,c)); }
    return 0; }
