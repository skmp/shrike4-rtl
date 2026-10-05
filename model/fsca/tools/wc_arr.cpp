// wc_arr.cpp -- how sign-asymmetric is the array in each operand role?  eps = arr(a,-b) + arr(a,b)
// (0 would mean perfectly odd).  Units of 2^16 because rows are cut below column 16.
#include "base2.h"
#include <cstring>
#include <map>
int main(){ build_tables();
    auto survey=[&](const char* name, i64 A, int recdx){
        std::map<i64,int> h; i64 mn=1ll<<62,mx=-(1ll<<62);
        for(int t=1;t<=512;t++){ i64 dp=(i64)t<<13, dm=-dp;
            i64 p = recdx? arr(dp,A) : arr(A,dp);
            i64 m = recdx? arr(dm,A) : arr(A,dm);
            i64 e = m+p; h[e>>16]++; if(e<mn)mn=e; if(e>mx)mx=e; }
        printf("%-28s eps range [%lld,%lld] = [%.1f,%.1f]*2^16 ; distinct/2^16:",name,
            (long long)mn,(long long)mx,mn/65536.0,mx/65536.0);
        int k=0; for(auto&kv:h){ printf(" %lld:%d",(long long)kv.first,kv.second); if(++k>14){printf(" ...");break;} } puts("");
    };
    survey("arr(KC,dx)  (KC recoded)", 0x4ef145, 0);
    survey("arr(dx,KC)  (dx recoded)", 0x4ef145, 1);
    survey("arr(S9,dx)  (S recoded)", TB[9].S, 0);
    survey("arr(dx,S9)  (dx recoded)", TB[9].S, 1);
    survey("arr(C1f9,dx)", TB[9].C1f, 0);
    // and the second multiply: I1*dx with I1 the signed chain latch
    { std::map<i64,int> h; i64 mn=1ll<<62,mx=-(1ll<<62);
      for(int t=1;t<=512;t++){ i64 dp=(i64)t<<13, dm=-dp;
        i64 I1p=(arr(0x4ef145,dp)+K)>>23, I1m=(arr(0x4ef145,dm)+K)>>23;
        i64 p=arr(I1p,dp), m=arr(I1m,dm); i64 e=m-p; h[e>>16]++; if(e<mn)mn=e; if(e>mx)mx=e; }
      printf("%-28s arr(I1m,dxm)-arr(I1p,dxp) range [%lld,%lld] = [%.2f,%.2f] J-units(2^20)\n",
          "second multiply",(long long)mn,(long long)mx,mn/1048576.0,mx/1048576.0); }
    // exact t^2 latch at several shifts: is it symmetric?
    for(int b=14;b<=26;b+=2){ int asym=0; for(int t=1;t<=512;t++){ i64 dp=(i64)t<<13;
        if(((arr(dp,dp)+K)>>b)!=((arr(-dp,-dp)+K)>>b)) asym++; }
        printf("t^2 latch >>%2d : asymmetric at %d/512 |t|\n",b,asym); }
    return 0; }
