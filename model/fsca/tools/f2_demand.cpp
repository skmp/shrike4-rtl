// f2_demand.cpp -- DIRECTION 2, part D.  What the pinned latch values actually DEMAND.
//
// Exact quadratic on the fine latch grid:  Q(t) = KC * t^2 / 2^15   (exact in units of 2^-20: KC*t^2*32)
// Deficit                                  D(t) = Q(t) - J(t)
// For a two-multiply chain  I1 = (ARR1 + K1)>>s1 ,  J = (ARR2 + K2)>>s2  with dx = t<<g and s1+s2 = 2g+15,
// write the array deficits a1 = |dx|*KC - |ARR1| >= 0 and a2 = |I1*dx| - ARR2 >= 0 (both >= 0: the array
// never overshoots -- f2_arrprobe measures max(arr - exact) = 0), and the two latch residues d1,d2 in [0,1).
// Then, with w = |dx| and S = s1+s2:
//     t>0 :  D = (a1-K1) w / 2^S + d1 w / 2^s2 + a2 / 2^s2 + d2 - K2 / 2^s2
//     t<0 :  D = -(a1-K1) w / 2^S - d1 w / 2^s2 + a2 / 2^s2 - d2 - K2 / 2^s2 ... (signs of the two latch
//            residues flip because the magnitude is rounded the other way)
// so every term is bounded and the demand becomes an arithmetic budget.  This tool prints the budget.
#include "f2_core.h"
#include <cstdio>
#include <algorithm>
static Fsca F;
typedef __int128 i128;
static void pr(const char*nm,i128 v20){ // print a value given in units of 2^-20
    long long ip=(long long)(v20>>20); i128 fr=v20-((i128)ip<<20); long long f6=(long long)(fr*1000000>>20);
    printf("%s%lld.%06lld",nm,ip,f6); }

// the chain's achievable deficit interval at offset t, in units of 2^-20, for (g,s2), K1=K2=2^19.
// a1 in [0,A1], a2 in [0,A2] measured at the operands that actually occur; d1,d2 in [0,1).
static void chainD(int t,int g,int s2,i128&minD,i128&maxD){
  int s1=2*g+15-s2; i64 w=(i64)std::abs(t)<<g; i64 dx=(i64)t<<g;
  i64 A1=dx*KC-arr(dx,KC); if(A1<0)A1=-A1;
  i64 I1id=(i64)(((i128)dx*KC+(1ll<<19))>>s1); i64 A2=0;
  for(i64 d=-64;d<=64;d++){ i64 v=I1id+d; if(v<=-(1ll<<26)||v>=(1ll<<26))continue;
    i64 e=(i64)((i128)v*dx-arr(v,dx)); if(e<0)e=-e; A2=std::max(A2,e); }
  i128 ONE=(i128)1<<20, S=2*g+15, K=(i128)(1ll<<19);
  i128 tK1w=K*w*ONE/((i128)1<<(int)S), tA1w=(i128)A1*w*ONE/((i128)1<<(int)S);
  i128 tws2=((i128)w<<20)/(1ll<<s2), tA2=((i128)A2<<20)/(1ll<<s2), tK2=(K<<20)/(1ll<<s2);
  if(t>0){ maxD=tA1w-tK1w+tws2+tA2+ONE-tK2; minD=-tK1w-tK2; }
  else   { maxD=tK1w+tA2-tK2+ONE;           minD=-(tA1w-tK1w)-tws2-tK2; } }
int main(){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F; Fin f{0,0,1,0}; FinCtx C; fin_build(f,C,2);
  puts("=== D(t) = KC*t^2/2^15 - J(t), the deficit the square path must produce (fine latch grid) ===");
  puts("   t       JL         JH        exact KC t^2/2^15        D from JH          D from JL     width");
  std::vector<int> ts; for(int t=-512;t<=511;t++){ int i=t+512; if(!t)continue; if(C.JH[i]-C.JL[i]<=12) ts.push_back(t); }
  for(int t: ts){ int i=t+512; i128 Q=(i128)KC*t*t*32;      // exact, in units of 2^-20
    i128 DH=Q-((i128)C.JH[i]<<20), DL=Q-((i128)C.JL[i]<<20);
    printf("%5d %10lld %10lld   ",t,(long long)C.JL[i],(long long)C.JH[i]);
    pr("",Q); printf("   "); pr("",DH); printf("   "); pr("",DL);
    printf("   %lld\n",(long long)(C.JH[i]-C.JL[i]+1)); }
  puts("\n=== the same three requirements as a chain budget ===");
  puts("For each (g,s2) with s1 = 2g+15-s2, K1 = K2 = 2^19: the LARGEST deficit the chain can produce at t,");
  puts("using the measured array-deficit envelope a in [0,A] (A measured per operand pair, not assumed).");
  int TT[3]={17,342,-342};
  printf("  g  s2  s1 |");
  for(int k=0;k<3;k++) printf("   t=%4d: need D in            chain D can be        ? |",TT[k]);
  printf("\n");
  for(int g=8;g<=14;g++) for(int s2=std::max(8,g-2);s2<=g+9;s2++){ int s1=2*g+15-s2; if(s1<10||s1>30) continue;
    printf(" %2d  %2d  %2d |",g,s2,s1);
    for(int k=0;k<3;k++){ int t=TT[k]; int i=t+512; i64 w=(i64)std::abs(t)<<g;
      i128 Q=(i128)KC*t*t*32; i128 needlo=Q-((i128)C.JH[i]<<20), needhi=Q-((i128)C.JL[i]<<20);
      // measured array deficits for the operands that actually occur at this (g,s1,s2)
      i64 dx=(i64)t<<g; i64 A1=dx*KC-arr(dx,KC); if(A1<0)A1=-A1;
      i64 I1id=(i64)(((i128)dx*KC+(1ll<<19))>>s1);
      i64 A2=0; for(i64 d=-64;d<=64;d++){ i64 v=I1id+d; i64 e=(i64)((i128)v*dx-arr(v,dx)); if(e<0)e=-e; A2=std::max(A2,e); }
      // the chain's deficit interval, both ends, in units of 2^-20:
      //   t>0 : D = (a1-K1)w/2^S + d1 w/2^s2 + a2/2^s2 + d2 - K2/2^s2
      //   t<0 : D = -(a1-K1)w/2^S - d1 w/2^s2 + a2/2^s2 - K2/2^s2 + d2
      // with a1 in [0,A1], a2 in [0,A2] (measured, not assumed), d1,d2 in [0,1), K1=K2=2^19.
      i128 ONE=(i128)1<<20, S=2*g+15, K=(i128)(1ll<<19);
      i128 tK1w = K*w*ONE/((i128)1<<(int)S);            // K1 * w / 2^S
      i128 tA1w = (i128)A1*w*ONE/((i128)1<<(int)S);     // A1 * w / 2^S
      i128 tws2 = ((i128)w<<20)/(1ll<<s2);              // w / 2^s2
      i128 tA2  = ((i128)A2<<20)/(1ll<<s2);             // A2 / 2^s2
      i128 tK2  = (K<<20)/(1ll<<s2);                    // K2 / 2^s2
      i128 maxD,minD;
      if(t>0){ maxD = tA1w-tK1w + tws2 + tA2 + ONE - tK2;  minD = -tK1w - tK2; }
      else   { maxD = tK1w + tA2 - tK2 + ONE;              minD = -(tA1w-tK1w) - tws2 - tK2; }
      bool ok = maxD>=needlo && minD<=needhi;
      printf(" ["); pr("",needlo); printf(","); pr("",needhi); printf("]  chain D in ["); pr("",minD); printf(","); pr("",maxD);
      printf("] %-3s |", ok?"yes":"NO"); }
    printf("\n"); }
  puts("\n=== the BIAS-FREE form of the same test ===");
  puts("A constant subtracted at either latch shifts D(t) by the same amount at every offset, so it cancels");
  puts("in a difference.  Two differences the data pins:");
  puts("   D(17) - D(376)     must lie in [12.544, 15.544]   (both offsets pinned; +376 window width 4)");
  puts("   D(342) - D(-342)   must lie in [22.000, 31.000]");
  puts("  g  s2  s1 | D(17)-D(376): chain can reach at most | D(342)-D(-342): at most | both? |");
  int nboth=0,nrow=0;
  for(int g=8;g<=14;g++) for(int s2=std::max(8,g-2);s2<=g+9;s2++){ int s1=2*g+15-s2; if(s1<10||s1>32) continue;
    i128 a,b,c,d,e,f2,h,i2; chainD(17,g,s2,a,b); chainD(376,g,s2,c,d); chainD(342,g,s2,e,f2); chainD(-342,g,s2,h,i2);
    i128 d1=b-c, d2=f2-h; bool o1=d1>=(i128)(12544ll*1048576/1000), o2=d2>=(i128)22*1048576;
    nrow++; if(o1&&o2) nboth++;
    printf(" %2d  %2d  %2d |",g,s2,s1); pr("            ",d1); printf(" %-3s |",o1?"yes":"NO");
    pr("      ",d2); printf(" %-3s |",o2?"yes":"NO"); printf(" %-4s |\n",(o1&&o2)?"BOTH":""); }
  printf("\n(g,s2) rows satisfying both bias-free differences: %d of %d\n",nboth,nrow);
  return 0; }