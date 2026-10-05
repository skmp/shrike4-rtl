// f1_sum.cpp -- DIRECTION 1, part A: consolidation and the honest size of the one result that survived.
// 1. exactly where the positive and one's-complement alignments of the final square product differ;
// 2. which of the 2280 chains keep the guard nodes exact for the baseline field;
// 3. whether the 23-bit sine field and the top 23 bits of the 26-bit C0 ROM are the same numbers;
// 4. float-level RN/RZ mismatches over all 65536 angles (baseline 162/154) for the surviving variant.
#include "f1_final.h"
static int SGV=0, KCV=1, RMV=0, FMV=0;      // the variant under test, for Vof
static i64 SS[17]; static int UU[17];
static i64 Vof(int m,int t,int sg){ int neg=0; if(m>=33){m-=32;neg=1;} int flip=0; if(m>=17){m=32-m;flip=1;}
    i64 dx=(i64)t<<13; i64 s=flip?-1:1;
    i64 T1=TB[m].C1f? sm(arr(TB[m].C1f,dx)+K,23-TB[m].e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[m].c3? sm(arr(TB[m].c3<<5,A3)+K,26):0;
    i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
    i64 P = SS[m]? arr(J,SS[m]) : 0;
    i64 T2 = SS[m]? ((sg? P+K-1 : P+K)>>UU[m]) : 0;
    i64 V=F1_C0[m]+s*T1-T2-s*T3; return neg?-V:V; }
int main(){ if(!load_fsca(FF))return 1; build_tables(); f1_precompute();
    FVar base{23,0,0,1,30,0,1,0}; Ctx CB; f1_field(base,CB.S,CB.u,CB.kk);
    memcpy(SS,CB.S,sizeof SS); memcpy(UU,CB.u,sizeof UU);

    puts("1. where the positive rule (P+K)>>u and the one's-complement rule (P+K-1)>>u differ");
    { int ndiff=0,fixed=0,broke=0;
      for(int t=-512;t<=511;t++){ int it=t+512; i64 dx=(i64)t<<13;
        i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
          i64 P=arr(J,CB.S[n]); i64 A=(P+K)>>CB.u[n], B=(P+K-1)>>CB.u[n];
          if(A==B) continue; ndiff++;
          i64 VA=BASE[it][node]-A, VB=BASE[it][node]-B;
          bool oa=(VA<VLO[it][node]||VA>VHI[it][node]), ob=(VB<VLO[it][node]||VB>VHI[it][node]);
          if(oa&&!ob){fixed++; printf("   node %2d t=%4d : T2 %lld -> %lld  FIXES a violation (window [%lld,%lld])\n",
              node,t,(long long)A,(long long)B,(long long)VLO[it][node],(long long)VHI[it][node]); }
          if(!oa&&ob){broke++; printf("   node %2d t=%4d : T2 %lld -> %lld  BREAKS a hit\n",node,t,(long long)A,(long long)B); } } }
      printf("   the two rules differ on %d of 31744 (node,t) pairs; %d fix a violation, %d break a hit\n",
             ndiff,fixed,broke);
      printf("   -> the 147 -> 146 gain rests on %d sample(s) landing on an exact multiple of 2^u.\n",fixed); }

    puts("\n2. the chains that keep nodes 0, 1/31, 2/30 and 16 exact (baseline field, 2280 chains)");
    for(int s1=16;s1<=30;s1++)for(int s2=10;s2<=28;s2++)for(int ro=0;ro<2;ro++)for(int k1=0;k1<2;k1++)
      for(int k2=0;k2<2;k2++){ i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0; Chain ch{s1,s2,ro,K1,K2,0}; int g[4];
        int tot=f1_full(base,CB,ch,g); if((g[0]|g[1]|g[2]|g[3])==0)
          printf("   s1=%2d s2=%2d %s K1=%-5s K2=%-5s -> %d violations\n",s1,s2,ro?"arr(dx,KC)":"arr(KC,dx)",
                 k1?"2^19":"0",k2?"2^19":"0",tot); }

    puts("\n3. the 23-bit normalised sine field vs the top 23 bits of the normalised 26-bit C0 ROM");
    for(int rm=0;rm<4;rm++){ FVar a{23,rm,0,1,30,0,1,0}, b{23,rm,4,1,30,0,1,0};
      i64 Sa[17],Sb[17]; int ua[17],ub[17],ka[17],kb[17]; f1_field(a,Sa,ua,ka); f1_field(b,Sb,ub,kb);
      int d=0; i64 mx=0; for(int n=1;n<=16;n++){ i64 e=Sa[n]-Sb[n]; if(e){d++; if(llabs(e)>mx)mx=llabs(e);} }
      printf("   rounding %-8s : differ at %2d of 16 nodes, largest difference %lld unit(s)\n",RMNAME[rm],d,(long long)mx); }

    puts("\n4. float-level mismatches over all 65536 angles (both rounding modes)");
    for(int sg=0;sg<2;sg++){ int rn=0,rz=0,wv=0;
      for(int a=0;a<65536;a++){ int q=(a+512)>>10, m=q&63, t=a-1024*q; if(t<-512)t+=65536;
        i64 V=Vof(m,t,sg); if(to_float(V,0)!=FF.srn[a])rn++; if(to_float(V,1)!=FF.srz[a])rz++;
        Win w=decode(FF.srz[a],FF.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
        if(V<lo||V>hi)wv++; }
      printf("   %-22s : RN %d / RZ %d float mismatches, %d window violations over the whole dump\n",
             sg?"one's-complement rule":"positive rule (current)",rn,rz,wv); }
    return 0; }
