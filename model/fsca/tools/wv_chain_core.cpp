// wv_chain_core.cpp -- independent test of the claimed 4-element infeasible core
//   { t=-342 in [4616715,4616716], t=376 = 5580285, t=-376 = 5580285, t=258 = 2627358 }
// over two-multiply chains, with K1 and K2 SOLVED (not sampled).
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static const int NT = 4;
static const int TT[NT]   = { -342, 376, -376, 258 };
static const i64 WL[NT]   = { 4616715, 5580285, 5580285, 2627358 };
static const i64 WH[NT]   = { 4616716, 5580285, 5580285, 2627358 };

// latch rules, all taking (P, K, s)
static inline i64 R_floor(i64 P,i64 Kc,int s){ return (P+Kc)>>s; }
static inline i64 R_ceil (i64 P,i64 Kc,int s){ return -((-(P+Kc))>>s); }
static inline i64 R_tz   (i64 P,i64 Kc,int s){ i64 x=P+Kc; return x>=0? (x>>s) : -((-x)>>s); }
static inline i64 R_smp  (i64 P,i64 Kc,int s){ i64 x=P+Kc; return x>=0? (x>>s) : -((~x)>>s); }
static inline i64 R_magsm(i64 P,i64 Kc,int s){ return P>=0? ((P+Kc)>>s) : -(((~P)+Kc)>>s); }
static inline i64 RULE(int r,i64 P,i64 Kc,int s){
    switch(r){case 0:return R_floor(P,Kc,s);case 1:return R_ceil(P,Kc,s);case 2:return R_tz(P,Kc,s);
              case 3:return R_smp(P,Kc,s);default:return R_magsm(P,Kc,s);} }
static inline i64 MASK(int m,i64 v){
    if(m==0) return v;
    if(m==1) return v & ~(i64)1;
    i64 a = v<0?-v:v; a &= ~(i64)1; return v<0?-a:a; }          // m==2: magnitude low-bit clear

int main(){
    build_tables();
    // constants: (0x4EF145 + d) << j, both signs
    std::vector<i64> CST; std::vector<int> CJ;
    for(int d=-8;d<=8;d++) for(int j=0;j<3;j++){ i64 c=((i64)0x4EF145+d)<<j; CST.push_back(c); CJ.push_back(j);
                                                 CST.push_back(-c); CJ.push_back(j); }
    long long tried=0, best_cnt=0; int bestdesc[16]={0};
    long long hit3=0;
    #pragma omp parallel for schedule(dynamic) reduction(+:tried,hit3)
    for(size_t ci=0; ci<CST.size(); ci++){
      i64 cst=CST[ci]; int j=CJ[ci];
      for(int g1=8; g1<=15; g1++) for(int r1role=0;r1role<2;r1role++){
        i64 P1[NT];
        for(int i=0;i<NT;i++){ i64 dx=(i64)TT[i]<<g1; P1[i]= r1role? arr(dx,cst) : arr(cst,dx); }
        for(int e=-9;e<=-3;e++){ int s1=g1+17+e+j;  if(s1<1||s1>40) continue;
          for(int rule1=0;rule1<5;rule1++){
            // I1 takes at most two values as K1 sweeps [0,2^s1); find them and the breakpoint
            i64 v0[NT],v1[NT],bp[NT]; bool two[NT];
            i64 period=(i64)1<<s1;
            for(int i=0;i<NT;i++){ v0[i]=RULE(rule1,P1[i],0,s1); v1[i]=RULE(rule1,P1[i],period-1,s1);
              two[i]= v0[i]!=v1[i];
              if(two[i]){ i64 lo=0,hi=period-1;               // monotone in K1 (either direction)
                bool inc = v1[i]>v0[i];
                while(lo<hi){ i64 m=lo+(hi-lo)/2; i64 v=RULE(rule1,P1[i],m,s1);
                  bool past = inc ? (v!=v0[i]) : (v!=v0[i]);
                  if(past) hi=m; else lo=m+1; }
                bp[i]=lo; } else bp[i]=period; }
            // K1 segments delimited by the sorted breakpoints
            i64 cuts[NT+2]; int nc=0; cuts[nc++]=0;
            for(int i=0;i<NT;i++) if(two[i]) cuts[nc++]=bp[i];
            cuts[nc++]=period; std::sort(cuts,cuts+nc); nc=(int)(std::unique(cuts,cuts+nc)-cuts);
            for(int g2=8; g2<=15; g2++){ int s2=g1+g2+17-(s1-j); if(s2<1||s2>40) continue;
              for(int r2role=0;r2role<2;r2role++) for(int rule2=0;rule2<5;rule2++)
                for(int m1=-1;m1<=1;m1++) for(int mk=0;mk<3;mk++) for(int sg=0;sg<2;sg++){
                  tried++;
                  i64 K2lo=-((i64)1<<(s2+2)), K2hi=((i64)1<<(s2+2));
                  bool alive=true; int got=0;
                  for(int seg=0; seg+1<nc && alive; seg++){
                    i64 k1=cuts[seg];
                    i64 lo2=K2lo, hi2=K2hi; int cnt=0;
                    for(int i=0;i<NT;i++){
                      i64 I1=RULE(rule1,P1[i],k1,s1)+m1; I1=MASK(mk,I1);
                      i64 dx=(i64)TT[i]<<g2;
                      i64 P2= r2role? arr(dx,I1) : arr(I1,dx);
                      // J(K2) = sg? -RULE(rule2,P2,K2,s2) : RULE(...)  -- monotone in K2
                      auto Jof=[&](i64 K2)->i64{ i64 v=RULE(rule2,P2,K2,s2); return sg? -v : v; };
                      i64 a=Jof(K2lo), b=Jof(K2hi);
                      i64 L,H;
                      if(a<=b){ // nondecreasing: lowest K2 with J>=WL, highest with J<=WH
                        if(a>WH[i]||b<WL[i]){ L=1;H=0; }
                        else { i64 x=K2lo,y=K2hi; while(x<y){ i64 m=x+((y-x)>>1); if(Jof(m)>=WL[i]) y=m; else x=m+1; } L=x;
                               x=K2lo;y=K2hi; while(x<y){ i64 m=x+((y-x+1)>>1); if(Jof(m)<=WH[i]) x=m; else y=m-1; } H=x; }
                      } else {  // nonincreasing
                        if(b>WH[i]||a<WL[i]){ L=1;H=0; }
                        else { i64 x=K2lo,y=K2hi; while(x<y){ i64 m=x+((y-x)>>1); if(Jof(m)<=WH[i]) y=m; else x=m+1; } L=x;
                               x=K2lo;y=K2hi; while(x<y){ i64 m=x+((y-x+1)>>1); if(Jof(m)>=WL[i]) x=m; else y=m-1; } H=x; }
                      }
                      if(L>H){ cnt=-1; break; }
                      if(L>lo2) lo2=L; if(H<hi2) hi2=H;
                      if(lo2>hi2){ cnt=-1; break; }
                      cnt++;
                    }
                    if(cnt==NT && lo2<=hi2){ got=NT;
                      #pragma omp critical
                      { printf("!!! ALL %d SATISFIED: cst=%lld j=%d g1=%d role1=%d s1=%d rule1=%d K1=%lld"
                               " g2=%d role2=%d s2=%d rule2=%d m1=%d mask=%d sign=%d K2 in [%lld,%lld]\n",
                          NT,(long long)cst,j,g1,r1role,s1,rule1,(long long)k1,g2,r2role,s2,rule2,m1,mk,sg,
                          (long long)lo2,(long long)hi2); }
                      alive=false; }
                  }
                  if(got==NT) hit3++;
                }
            }
          }
        }
      }
    }
    printf("structures tested: %lld   structures satisfying all 4 core constraints: %lld\n", tried, hit3);
    return 0; }
