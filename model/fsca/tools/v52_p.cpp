// v52_p.cpp -- the RATIO argument: a uniform dx-level offset p moves J by ~ (KC*t/2^27)*p, proportional to t.
#include "v52_common.h"
static JW W;
int main(){ if(!load_jwin(W))return 1; build_tables();
  puts("uniform dx offset p applied to BOTH chain operands:");
  printf("   p      J(17)     J(342)      J(-342)    score/1024\n");
  for(int p=-30;p<=10;p++){ i64 J[1024];
    for(int t=-512;t<=511;t++){ i64 dx=((i64)t<<13)+p; J[t+512]=chainJ_dx(dx,dx); }
    int s=jscore(W,J);
    printf("  %+4d  %8lld  %9lld  %9lld   %4d%s%s\n",p,(long long)J[17+512],(long long)J[342+512],(long long)J[-342+512],s,
       J[17+512]==45617?"   <- J(17) EXACT":"", J[342+512]==18466836?"   <- J(342) EXACT":""); }
  puts("");
  puts("the achievable lattice at t=342 under ANY uniform dx offset (target 18466836, window width 1):");
  { bool hit=false; i64 prev=-1; int n=0;
    for(long long p=-2000000;p<=2000000;p++){ i64 dx=((i64)342<<13)+p; i64 j=chainJ_dx(dx,dx);
      if(j==18466836){hit=true;printf("   HIT at p=%lld\n",p);break;} }
    printf("   18466836 reachable over p in +-2,000,000 : %s\n",hit?"YES":"NO");
    printf("   nearby teeth: "); for(int p=-4;p<=2;p++){ i64 dx=((i64)342<<13)+p; printf("p=%+d:%lld ",p,(long long)chainJ_dx(dx,dx)); } printf("\n"); (void)prev;(void)n; }
  puts("");
  puts("the same for t=17 (target 45617):");
  { bool hit=false; long long ph=0;
    for(long long p=-2000000;p<=2000000;p++){ i64 dx=((i64)17<<13)+p; if(chainJ_dx(dx,dx)==45617){hit=true;ph=p;break;} }
    printf("   45617 reachable : %s%s", hit?"YES at p=":"NO",""); if(hit)printf("%lld",ph); printf("\n"); }
  puts("");
  puts("sensitivity dJ/dp measured (units of J per unit of dx):");
  for(int t : {17,342}){ i64 a=chainJ_dx(((i64)t<<13)-50,((i64)t<<13)-50), b=chainJ_dx(((i64)t<<13)+50,((i64)t<<13)+50);
    printf("   t=%4d : (J(p=+50)-J(p=-50))/100 = %.4f   predicted KC*t/2^27 = %.4f\n",t,(double)(b-a)/100.0,(double)KC*t/134217728.0); }
  return 0; }
