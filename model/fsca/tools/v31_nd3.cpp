// v31_nd3.cpp -- audit the ONE soundness assumption the per-ND window regeneration relies on:
// gen_windows() binary-searches m in [0,48e6] assuming sm(arr(m,S,nd)+K,u) is non-decreasing in m.
#include "base2.h"
#include <cstdio>
typedef __int128 i128;
static i64 v31_arr(i64 rec,i64 mul,int ND){ i128 R[48]; i128 T=0;
    for(int k=0;k<ND;k++){ auto bit=[&](int b)->int{return b<0?0:(int)((rec>>(b>62?62:b))&1);};
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i128 mag=(i128)(d<0?-d:d)*mul; i128 row=d<0?~mag:mag;
        R[k]=row*((i128)1<<(2*k)); T+=(R[k]>>16)<<16; if(d<0&&2*k>=18) T+=(i128)1<<(2*k); }
    if(ND>=3){int m15=(int)((R[0]>>15)&1)+(int)((R[1]>>15)&1)+(int)((R[2]>>15)&1); T+=((i128)1<<16)*(m15>=2);}
    if(ND>=9){int p16=(int)(((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1); T-=((i128)1<<16)*p16;}
    return (i64)T; }
int main(){ build_tables();
    const i64 LIM=12000000ll<<2;
    printf("monotonicity of m -> sm(arr(m,S_n,ND)+2^19, u2+2) over m in [0,%lld]\n",(long long)LIM);
    printf(" ND   node  first descent at m      (2^25 = %d)\n", 1<<25);
    for(int nd=13;nd<=15;nd++){
        for(int n : {1,8,16}){
            if(!TB[n].S) continue; int u=TB[n].u2+2; i64 prev=sm(v31_arr(0,TB[n].S,nd)+K,u); i64 bad=-1; long long nbad=0;
            for(i64 m=1;m<=LIM;m++){ i64 v=sm(v31_arr(m,TB[n].S,nd)+K,u); if(v<prev){ if(bad<0)bad=m; nbad++; } prev=v; }
            printf(" %2d   %4d  %-20lld  descents=%lld\n", nd, n, (long long)bad, nbad);
        }
    }
    return 0; }
