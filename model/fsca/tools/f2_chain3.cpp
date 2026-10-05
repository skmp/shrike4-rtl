// f2_chain3.cpp -- the PARAMETRIC side, decided exactly.  "No integer I1 exists" is structural; "the chain
// misses" is parametric.  This solves the chain exactly against the three pinned offsets alone.
//   I1 = ((ARR1 + r1) >> s1) + p        ARR1 = arr(dx,KC') or arr(KC',dx)
//   J  = ((ARR2 + r2) >> s2) + q        ARR2 = arr(I1,dx) or arr(dx,I1)
// r1 and r2 are EXHAUSTED over their full periods: as r1 sweeps [0,2^s1) the latch takes exactly two values
// per offset, so the three offsets have at most 4 distinct choice vectors, found by sorting the thresholds.
// Same for r2.  p and q are integer biases (a rounding constant beyond the shift), solved in closed form.
#include "f2_core.h"
#include <algorithm>
#include <set>
static Fsca F;
static int PLO=-4,PHI=4; static i64 DKC=64;
int main(int argc,char**argv){ if(argc>1){PLO=atoi(argv[1]);PHI=atoi(argv[2]);DKC=atoll(argv[3]);}
  if(!load_fsca(F))return 1; build_tables(); FIN_F=&F; Fin f{0,0,1,0}; FinCtx C; fin_build(f,C,2);
  const int T[3]={17,342,-342};
  i64 WL[3],WH[3]; for(int k=0;k<3;k++){ WL[k]=C.JL[T[k]+512]; WH[k]=C.JH[T[k]+512]; }
  printf("targets: J(17) in [%lld,%lld]  J(342) in [%lld,%lld]  J(-342) in [%lld,%lld]\n",
    (long long)WL[0],(long long)WH[0],(long long)WL[1],(long long)WH[1],(long long)WL[2],(long long)WH[2]);
  long long tried=0, hit3=0; int bestmask=0; long long bestcnt[8]={0};
  struct Best{int g,s1,s2,r1,r2;i64 kc;int p,q;} B{}; bool have=false;
  for(int g=8;g<=14;g++) for(int s1=8;s1<=32;s1++) for(int s2=8;s2<=24;s2++)
   for(int role1=0;role1<2;role1++) for(int role2=0;role2<2;role2++)
    for(i64 dkc=-DKC;dkc<=DKC;dkc++){ i64 kc=KC+dkc; if(kc<=0) continue;
      i64 ARR1[3], n1[3], m1[3], thr[3];
      bool bad=false;
      for(int k=0;k<3;k++){ i64 dx=(i64)T[k]<<g; ARR1[k]= role1? arr(kc,dx) : arr(dx,kc);
        n1[k]=ARR1[k]>>s1; m1[k]=ARR1[k]-(n1[k]<<s1); thr[k]=(1ll<<s1)-m1[k]; }
      // the 4 (at most) choice vectors of r1
      i64 cuts[5]={0,thr[0],thr[1],thr[2],(1ll<<s1)}; std::sort(cuts,cuts+5);
      std::set<int> seen1;
      for(int ci=0;ci<4;ci++){ i64 r1=cuts[ci]; if(r1>=(1ll<<s1)) continue;
        int mask1=0; i64 I1b[3]; for(int k=0;k<3;k++){ int c=(r1>=thr[k]); mask1|=c<<k; I1b[k]=n1[k]+c; }
        if(seen1.count(mask1)) continue; seen1.insert(mask1);
        for(int p=PLO;p<=PHI;p++){
          i64 ARR2[3],n2[3],m2[3],th2[3];
          for(int k=0;k<3;k++){ i64 dx=(i64)T[k]<<g; i64 v=I1b[k]+p;
            if(v<=-(1ll<<26)||v>=(1ll<<26)){bad=true;break;}
            ARR2[k]= role2? arr(dx,v) : arr(v,dx);
            n2[k]=ARR2[k]>>s2; m2[k]=ARR2[k]-(n2[k]<<s2); th2[k]=(1ll<<s2)-m2[k]; }
          if(bad){bad=false;continue;}
          i64 c2[5]={0,th2[0],th2[1],th2[2],(1ll<<s2)}; std::sort(c2,c2+5); std::set<int> seen2;
          for(int cj=0;cj<4;cj++){ i64 r2=c2[cj]; if(r2>=(1ll<<s2)) continue;
            int mask2=0; i64 J0[3]; for(int k=0;k<3;k++){ int c=(r2>=th2[k]); mask2|=c<<k; J0[k]=n2[k]+c; }
            if(seen2.count(mask2)) continue; seen2.insert(mask2); tried++;
            // choose the integer bias q: q in [WL[k]-J0[k], WH[k]-J0[k]] for every k we want to satisfy
            i64 ql=-(1ll<<40), qh=(1ll<<40); int m=0;
            for(int k=0;k<3;k++){ i64 a=WL[k]-J0[k], b=WH[k]-J0[k]; if(std::max(ql,a)<=std::min(qh,b)){} }
            // exact: count the largest satisfiable subset, and test all three
            for(int sub=7;sub>=1;sub--){ i64 lo=-(1ll<<40),hi=(1ll<<40);
              for(int k=0;k<3;k++) if(sub>>k&1){ lo=std::max(lo,WL[k]-J0[k]); hi=std::min(hi,WH[k]-J0[k]); }
              if(lo<=hi && std::abs((long long)lo)<=64){ bestcnt[sub]++;
                if(sub==7){ hit3++; if(!have){have=true;B={g,s1,s2,role1,role2,kc,p,(int)lo};}
                    if(p>=-1&&p<=1) printf("HIT3(|p|<=1) g=%d s1=%d s2=%d role1=%d role2=%d KC=%lld p=%d q in [%lld,%lld] r1=%lld r2=%lld mask1=%d mask2=%d\n",
                      g,s1,s2,role1,role2,(long long)kc,p,(long long)lo,(long long)hi,(long long)r1,(long long)r2,mask1,mask2); }
                if(__builtin_popcount(sub)>__builtin_popcount(bestmask)) bestmask=sub; } }
          } } } }
  printf("chain configurations examined (r1,r2 exhausted over full periods, p,q solved): %lld\n",tried);
  printf("configurations hitting ALL THREE pinned offsets: %lld\n",hit3);
  const char* nm[3]={"17","342","-342"};
  for(int sub=1;sub<8;sub++){ printf("  subset {");
    for(int k=0;k<3;k++) if(sub>>k&1) printf("%s ",nm[k]);
    printf("}: %lld configurations\n",bestcnt[sub]); }
  if(have) printf("witness: g=%d s1=%d s2=%d role1=%d role2=%d KC'=%lld p=%d q=%d\n",B.g,B.s1,B.s2,B.r1,B.r2,(long long)B.kc,B.p,B.q);
  return 0; }
