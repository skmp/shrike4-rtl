// wb_sq.cpp -- is the hardware's dx^2 product sign-symmetric?  Measure arr(dx,dx) - t^2*2^26 for both
// signs of t, and the asymmetry e2(t) = arr(-dx,-dx) - arr(dx,dx).
#include "base2.h"
#include <cstring>
#include <map>
int main(){ build_tables();
    std::map<i64,int> hp,hn,he;
    for(int t=1;t<=512;t++){ i64 dx=(i64)t<<13; i64 p=arr(dx,dx), n=arr(-dx,-dx); i64 ex=(i64)t*t<<26;
        hp[p-ex]++; hn[n-ex]++; he[n-p]++; }
    printf("arr(dx,dx)-t^2*2^26 for t>0 :"); for(auto&kv:hp) printf(" %lld x%d",(long long)kv.first,kv.second); printf("\n");
    printf("arr(-dx,-dx)-t^2*2^26      :"); for(auto&kv:hn) printf(" %lld x%d",(long long)kv.first,kv.second); printf("\n");
    printf("asymmetry e2 = arr(-)-arr(+):"); for(auto&kv:he) printf(" %lld x%d",(long long)kv.first,kv.second); printf("\n");
    // also arr(dx,-dx) and arr(-dx,dx) (mixed signs, i.e. the true product is negative)
    std::map<i64,int> hm1,hm2;
    for(int t=1;t<=512;t++){ i64 dx=(i64)t<<13; i64 ex=(i64)t*t<<26;
        hm1[arr(dx,-dx)+ex]++; hm2[arr(-dx,dx)+ex]++; }
    printf("arr(dx,-dx)+t^2*2^26       :"); for(auto&kv:hm1) printf(" %lld x%d",(long long)kv.first,kv.second); printf("\n");
    printf("arr(-dx,dx)+t^2*2^26       :"); for(auto&kv:hm2) printf(" %lld x%d",(long long)kv.first,kv.second); printf("\n");
    // what does the K=2^19 rounding do at coarser latch shifts?
    for(int s=18;s<=26;s+=2){ int bad=0; for(int t=1;t<=512;t++){ i64 dx=(i64)t<<13;
            if(((arr(dx,dx)+K)>>s)!=((arr(-dx,-dx)+K)>>s)) bad++; }
        printf("latch (arr+K)>>%2d : %d of 512 |t| have different +/- values\n",s,bad); }
    return 0; }
