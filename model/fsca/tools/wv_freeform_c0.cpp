#include "base2.h"
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
  int out=0; for(int n=0;n<=16;n++){ i64 lo,hi;bool ex; win_of(n*1024,lo,hi,ex);
    bool in = C0TAB[n]>=lo && C0TAB[n]<=hi;
    printf("n=%2d  t=0 hardware window [%lld,%lld]  C0=%lld  %s   RN26=%lld delta %+lld\n",n,
      (long long)lo,(long long)hi,(long long)C0TAB[n], in?"INSIDE":"*** OUTSIDE ***",
      (long long)TB[n].sin26,(long long)(C0TAB[n]-TB[n].sin26)); if(!in)out++; }
  printf("C0 values outside the hardware t=0 window: %d/17\n",out); return 0; }
