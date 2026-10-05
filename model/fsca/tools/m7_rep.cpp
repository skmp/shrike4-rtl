// m7_rep.cpp -- USER HYPOTHESES:
//  A) t computed in 2s complement but USED as 1s complement -> value = t+1 for t<0 (t-level, = +8192
//     at the dx level, far outside the +-96 previously scanned).
//  B) t carries one more bit of precision -> dx = (2t+e)<<12.
#include "base2.h"
#include <cstdio>
#include <set>
static i64 JL[1024],JH[1024]; static bool have[1024];
static inline i64 chain(i64 dx){ i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; }
static i64 JA(int t,int d){ return chain(((i64)(t + (t<0?d:0)))<<13); }              // A: t-level, t<0 only
static i64 JB(int t,int e){ return chain(((i64)(2*t+e))<<12); }                      // B: extra LSB
static i64 JP(int t,i64 p){ return chain(((i64)t<<13)+p); }                          // uniform dx bias
static int scoreA(int d){ int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=JA(i-512,d); if(j>=JL[i]&&j<=JH[i]) s++; } return s; }
static int scoreB(int e){ int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=JB(i-512,e); if(j>=JL[i]&&j<=JH[i]) s++; } return s; }
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    printf("targets  J(17)=45617   J(342)=18466836   J(-342) in [18466858,18466867]   baseline 918/1024\n\n");
    printf("=== A) value = t + d for t<0  (d=+1 IS the '2s computed, 1s interpreted' case) ===\n");
    for(int d=-4;d<=4;d++){
        printf("  d=%+d : %4d/1024   J(17)=%lld  J(342)=%lld  J(-342)=%lld %s%s\n",d,scoreA(d),
            (long long)JA(17,d),(long long)JA(342,d),(long long)JA(-342,d),
            d==1?"  <== the 1s-interpreted case":"",
            (JA(-342,d)>=JL[512-342]&&JA(-342,d)<=JH[512-342])?"  [J(-342) in window]":"");
    }
    printf("\n  STRUCTURAL DISPROOF: every d leaves J(17)=%lld and J(342)=%lld unchanged, because the\n",
        (long long)JA(17,3),(long long)JA(342,-3));
    printf("  hypothesis only alters t<0. The pinned positive offsets need -13 and -10. A sign-only\n");
    printf("  change cannot supply them, whatever d is.\n");
    printf("\n=== B) extra LSB: dx = (2t+e)<<12 ===\n");
    for(int e=-4;e<=4;e++) printf("  e=%+d : %4d/1024   J(17)=%lld  J(342)=%lld  J(-342)=%lld%s\n",e,scoreB(e),
        (long long)JB(17,e),(long long)JB(342,e),(long long)JB(-342,e), e?"":"   <- identical to the model");
    printf("\n=== granularity: is J(342)=18466836 reachable by ANY uniform dx bias p? ===\n");
    printf("  p:      "); for(i64 p=-4;p<=4;p++) printf("%+lld:%lld  ",(long long)p,(long long)JP(342,p)); printf("\n");
    { bool hit=false; i64 hp=0;
      for(i64 p=-2000000;p<=2000000;p++){ if(JP(342,p)==18466836){hit=true;hp=p;break;} }
      printf("  scanned p in +-2,000,000 (= +-244 units of t): J(342)==18466836 %s\n",
        hit?"REACHED":"NEVER reached"); if(hit) printf("   at p=%lld\n",(long long)hp); }
    { std::set<i64> got; for(i64 p=-40;p<=40;p++) got.insert(JP(342,p));
      printf("  achievable J(342) near the target, p in +-40: "); int n=0;
      for(i64 v:got) if(v>18466800&&v<18466880){ printf("%lld ",(long long)v); if(++n>12)break; } printf("\n");
      printf("  -> the step is ~13 per unit of dx; the target sits BETWEEN achievable values.\n"); }
    return 0; }
