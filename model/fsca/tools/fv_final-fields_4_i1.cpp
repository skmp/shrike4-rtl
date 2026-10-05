// fv_final-fields_4_i1.cpp -- INDEPENDENT test of the "t=342 admits no integer I1" certificate and of the
// claim that it is SHIFT-DEPENDENT.  Regenerates the fine-scale latch windows from the dump (my own code),
// cross-checks them against jwin18.txt, then asks for J = (arr(I1,dx)+K2)>>s2 in [JL,JH]:
//   (a) EXHAUSTIVELY over the whole representable recoded range I1 in [-2^27, 2^27) for t = 17, +-342;
//   (b) by validated monotone binary search for the full 1024-offset census at each s2.
// Also measures where arr(I1,dx) stops being monotone in I1 (the claimed 2^27 Booth-recoder limit).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 JL[1024],JH[1024];
static inline void win_of(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
static const int DU=2;                                   // fine latch scale: u = 32 + k
int main(){ if(!load_fsca(F))return 1; build_tables();
    // ---- 1. regenerate the fine-scale latch windows (baseline field, frozen coefficients)
    const i64 LIM=12000000ll<<DU; int feas=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13,gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=TB[n].u2+DU;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,TB[n].S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,TB[n].S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; if(gl<=gh)feas++; }
    printf("regenerated fine windows: %d/1024 feasible\n",feas);
    { FILE*f=fopen("jwin18.txt","r"); char ln[256]; int bad=0,nl=0;
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,w;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&w)==4){ nl++; if(JL[t+512]!=a||JH[t+512]!=b)bad++; } }
      fclose(f); printf("cross-check vs jwin18.txt: %d lines, %d mismatches\n\n",nl,bad); }
    printf("pinned offsets from MY regeneration:  t=17 -> [%lld,%lld]   t=+342 -> [%lld,%lld]   t=-342 -> [%lld,%lld]\n\n",
        (long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],
        (long long)JL[-342+512],(long long)JH[-342+512]);
    // ---- 2. EXHAUSTIVE scan over the whole recoded range for the three critical offsets
    const i64 LO=-(1ll<<27), HI=(1ll<<27);
    int S2[5]={18,19,20,21,22}; i64 K2V[2]={0,1ll<<19};
    int TT[3]={17,342,-342};
    puts("== EXHAUSTIVE: every integer I1 in [-2^27, 2^27) ==");
    for(int ti=0;ti<3;ti++){ int t=TT[ti]; i64 dx=(i64)t<<13; i64 lo=JL[t+512],hi=JH[t+512];
        long long cnt[5][2]; i64 wit[5][2];
        for(int i=0;i<5;i++)for(int j=0;j<2;j++){cnt[i][j]=0;wit[i][j]=0;}
        long nonmono=0; i64 firstnm=0;
        #pragma omp parallel
        { long long lc[5][2]; i64 lw[5][2]; for(int i=0;i<5;i++)for(int j=0;j<2;j++){lc[i][j]=0;lw[i][j]=0;}
          long lnm=0; i64 lfn=0;
          #pragma omp for schedule(static)
          for(i64 blk=0;blk<4096;blk++){ i64 a0=LO+blk*((HI-LO)/4096), a1=a0+((HI-LO)/4096);
            i64 prev=arr(a0-1,dx);
            for(i64 I1=a0;I1<a1;I1++){ i64 A=arr(I1,dx);
              if(dx>0){ if(A<prev){ lnm++; if(!lfn)lfn=I1; } } else { if(A>prev){ lnm++; if(!lfn)lfn=I1; } }
              prev=A;
              for(int i=0;i<5;i++)for(int j=0;j<2;j++){ i64 J=(A+K2V[j])>>S2[i];
                  if(J>=lo&&J<=hi){ if(!lc[i][j])lw[i][j]=I1; lc[i][j]++; } } } }
          #pragma omp critical
          { for(int i=0;i<5;i++)for(int j=0;j<2;j++){ if(lc[i][j]){ if(!cnt[i][j])wit[i][j]=lw[i][j]; cnt[i][j]+=lc[i][j]; } }
            nonmono+=lnm; if(lfn&&(!firstnm||lfn<firstnm))firstnm=lfn; } }
        printf("t=%+4d  window [%lld,%lld]\n",t,(long long)lo,(long long)hi);
        for(int j=0;j<2;j++){ printf("   K2=%-7lld :",(long long)K2V[j]);
            for(int i=0;i<5;i++) printf("  s2=%d %s(%lld sol, e.g. I1=%lld)",S2[i],cnt[i][j]?"YES":"NO ",
                (long long)cnt[i][j],(long long)wit[i][j]); printf("\n"); }
        printf("   monotonicity of arr(I1,dx) over the whole range: %ld violating steps%s\n\n",nonmono,
               nonmono?"":" (monotone everywhere)"); }
    // ---- 3. full 1024-offset no-I1 census by monotone binary search, validated against the exhaustive scan
    puts("== CENSUS: offsets with NO integer I1 (binary search over [-2^27,2^27), monotone) ==");
    printf("  s2  K2=0   K2=2^19   (J-lattice spacing at t=342 = 342*2^13/2^s2)\n");
    for(int s2=14;s2<=26;s2++){ int c[2]={0,0};
      for(int j=0;j<2;j++){
        for(int t=-512;t<=511;t++){ if(JL[t+512]>JH[t+512]){continue;} i64 dx=(i64)t<<13;
            i64 lo=JL[t+512],hi=JH[t+512]; bool ok=false;
            if(dx==0){ i64 J=(arr(0,dx)+K2V[j])>>s2; ok=(J>=lo&&J<=hi); }
            else { // J monotone increasing in I1 when dx>0, decreasing when dx<0
                i64 a=LO,b=HI-1;
                while(a<b){ i64 m=a+(b-a)/2; i64 J=(arr(m,dx)+K2V[j])>>s2;
                    if(dx>0){ if(J>=lo)b=m; else a=m+1; } else { if(J<=hi)b=m; else a=m+1; } }
                for(i64 I1=a; I1<HI && I1<a+4096; I1++){ i64 J=(arr(I1,dx)+K2V[j])>>s2;
                    if(J>=lo&&J<=hi){ok=true;break;} if(dx>0&&J>hi)break; if(dx<0&&J<lo)break; } }
            if(!ok)c[j]++; } }
      printf("  %2d  %5d   %7d      %.4f\n",s2,c[0],c[1],342.0*8192.0/(double)(1ll<<s2)); }
    return 0; }
