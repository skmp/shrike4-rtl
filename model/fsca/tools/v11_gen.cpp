// v11_gen.cpp -- the general (non-equispaced) 5-point certificate across whole partitions, so the
// 1024-wide and 2048-wide comparators are computed by the SAME, STRONGER method.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <climits>
#include <random>
static Fsca F; static i64 VL[65536],VH[65536];
typedef __int128 i128;
static long double tol(i128 x){ bool n=x<0; if(n)x=-x;
    long double r=(long double)(unsigned long long)(x>>64)*18446744073709551616.0L+(long double)(unsigned long long)(x&(((i128)1<<64)-1));
    return n?-r:r; }
static long double cert_gen(const i64*lo,const i64*hi,int len,long long iters,unsigned seed){
    std::mt19937_64 rng(seed); long double best=-1e30L; int idx[5];
    // seed with the equispaced family first
    for(int s=1;4*s<=len-1;s++) for(int off=0;off+4*s<=len-1;off++){
        i128 acc=0,wid=0; static const i128 C4[5]={1,4,6,4,1};
        for(int i=0;i<5;i++){ int k=off+i*s; acc+=(i&1?-1:1)*C4[i]*(i128)(lo[k]+hi[k]); wid+=C4[i]*(i128)(hi[k]-lo[k]); }
        i128 a=(acc<0?-acc:acc)-wid; long double d=tol(a)/32.0L; if(d>best)best=d; }
    for(long long it=0;it<iters;++it){
        for(int i=0;i<5;i++) idx[i]=(int)(rng()%len); std::sort(idx,idx+5);
        bool dup=false; for(int i=1;i<5;i++) if(idx[i]==idx[i-1])dup=true; if(dup)continue;
        i128 lam[5],num=0,den=0,sub=0;
        for(int i=0;i<5;i++){ i128 p=1; for(int j=0;j<5;j++) if(j!=i) for(int k=j+1;k<5;k++) if(k!=i) p*=(i128)(idx[k]-idx[j]);
            lam[i]=(i&1)?-p:p; }
        for(int i=0;i<5;i++){ i128 al=lam[i]<0?-lam[i]:lam[i];
            num+=lam[i]*(i128)(lo[idx[i]]+hi[idx[i]]); den+=al; sub+=al*(i128)(hi[idx[i]]-lo[idx[i]]); }
        i128 a=(num<0?-num:num)-sub; long double d=tol(a)/(2.0L*tol(den)); if(d>best)best=d; }
    return best; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;} }
    long long IT = argc>1?atoll(argv[1]):2000000;
    static i64 lo[4096],hi[4096];
    printf("GENERAL 5-point certificate (equispaced family + %lld random 5-subsets), same method both sizes.\n\n",IT);
    printf("== 64-node partition, intervals [1024k-512, 1024k+511] (the published one) ==\n");
    long double worst64=0; int wc=0;
    for(int c=1024;c<=16384;c+=1024){ for(int i=0;i<1024;i++){ int a=(c-512+i)&0xffff; lo[i]=VL[a]; hi[i]=VH[a]; }
        long double d=cert_gen(lo,hi,1024,IT,999u+c); if(d>worst64){worst64=d;wc=c;}
        printf("   centre %5d : %8.3f\n",c,(double)d); }
    printf("  WORST over the partition: %.3f at centre %d\n\n",(double)worst64,wc);
    printf("== 32-node partition, intervals [2048k-1024, 2048k+1023] ==\n");
    long double worst32=0; int wc2=0;
    for(int c=0;c<=16384;c+=2048){ for(int i=0;i<2048;i++){ int a=(c-1024+i)&0xffff; lo[i]=VL[a]; hi[i]=VH[a]; }
        long double d=cert_gen(lo,hi,2048,IT,999u+c); if(d>worst32){worst32=d;wc2=c;}
        printf("   centre %5d : %8.3f\n",c,(double)d); }
    printf("  WORST over the partition: %.3f at centre %d\n\n",(double)worst32,wc2);
    printf("RATIO worst32/worst64 = %.2f\n",(double)(worst32/worst64));
    return 0; }
