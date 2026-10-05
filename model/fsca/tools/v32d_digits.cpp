#include "v32_common.h"
int main(){ if(!load_fsca(FD))return 1; build_tables();
  auto dig=[](i64 v){ u64 a=(u64)(v<0?-v:v); int b=a?64-__builtin_clzll(a):0; return b/2+1; };
  int mKC=dig(KC), mI1=0,mJ20=0,mJ18=0,mA3=0,mC1=0,mc3=0; i64 maxJ20=0,maxJ18=0;
  for(int n=0;n<=16;n++){ if(dig(TB[n].C1f)>mC1)mC1=dig(TB[n].C1f); if(dig(TB[n].c3<<5)>mc3)mc3=dig(TB[n].c3<<5); }
  int n20=0,n18=0;
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23;
    i64 J20=(arr(I1,dx)+K)>>20, J18=(arr(I1,dx)+K)>>18;
    if(dig(I1)>mI1)mI1=dig(I1); if(dig(J20)>mJ20){mJ20=dig(J20);maxJ20=J20;} if(dig(J18)>mJ18){mJ18=dig(J18);maxJ18=J18;}
    if(dig(((i64)t*t)<<3)>mA3)mA3=dig(((i64)t*t)<<3);
    if(dig(J20)>13)n20++; if(dig(J18)>13)n18++; }
  printf("max Booth digits needed by the recoded operand at each site:\n");
  printf("  arr(C1f,dx) %d   arr(t^2<<3,dx) %d   arr(c3<<5,A3) %d   arr(KC,dx) %d   arr(I1,dx) %d\n",mC1,mA3,mc3,mKC,mI1);
  printf("  arr(J20,S) %d (max J20=%lld, >13 digits at %d offsets)   arr(J18,S) %d (max J18=%lld, >13 at %d)\n",
     mJ20,(long long)maxJ20,n20,mJ18,(long long)maxJ18,n18);
  return 0; }
