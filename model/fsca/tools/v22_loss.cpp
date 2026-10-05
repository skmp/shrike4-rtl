// v22_loss.cpp -- independent check of the two array facts the branch leans on:
//  (1) arr(rec,mul) = rec*mul + loss(rec mod 2^18, mul mod 2^16)   [the loss depends only on the residues]
//  (2) the range of loss over the branch-2 operand domain, and the signed-28-bit recoder range.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
int main(){ build_tables();
    std::mt19937_64 rng(12345);
    // (1) residue-only dependence: for random (rec,mul), loss must agree with a table keyed on the residues
    std::map<std::pair<i64,i64>,i64> tab; long long tested=0,bad=0;
    i64 lmin=0,lmax=0; bool first=true;
    for(long long it=0;it<2000000;it++){
        i64 rec=(i64)(rng()%(1ull<<26)), mul=(i64)(rng()%(1ull<<22));
        if(rng()&1) mul=-mul;                       // negative multiplicand occurs in the model
        i64 got=arr(rec,mul), want=rec*mul, loss=got-want;
        auto key=std::make_pair(rec&((1ll<<18)-1), ((mul%(1ll<<16))+(1ll<<16))%(1ll<<16));
        auto ins=tab.insert({key,loss});
        if(!ins.second && ins.first->second!=loss) bad++;
        tested++;
        if(first){lmin=lmax=loss;first=false;} else {if(loss<lmin)lmin=loss; if(loss>lmax)lmax=loss;} }
    printf("(1) loss depends only on (rec mod 2^18, mul mod 2^16): %lld violations of %lld samples\n",bad,tested);
    printf("    loss range over these samples: [%lld, %lld]   (branch claims [-513024, 0], LMAX bracket 2^21=%d)\n",
        (long long)lmin,(long long)lmax,1<<21);
    // exhaustive over the actual branch-2 domain used by the landmark scan: rec = I1 values, mul = dx
    i64 gmin=0,gmax=0; first=true;
    for(int w=6;w<=18;w++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; if(llabs(dx)>=(1ll<<27))continue;
        for(int k=0;k<200;k++){ i64 rec=(i64)(rng()%(1ull<<27));
            i64 loss=arr(rec,dx)-rec*dx; if(first){gmin=gmax=loss;first=false;}
            else {if(loss<gmin)gmin=loss; if(loss>gmax)gmax=loss;} } }
    printf("    loss range over the branch-2 operand domain (sampled): [%lld, %lld]\n",(long long)gmin,(long long)gmax);
    // (2) signed 28-bit recoder: where does the sign flip?
    printf("(2) recoder sign: arr(2^27-1, 1000) = %lld ; arr(2^27, 1000) = %lld ; arr(2^28-1,1000) = %lld\n",
        (long long)arr((1ll<<27)-1,1000),(long long)arr(1ll<<27,1000),(long long)arr((1ll<<28)-1,1000));
    printf("    -> the physically meaningful stored-constant range is |KCw| < 2^27 = %lld\n",(long long)(1ll<<27));
    return 0; }
