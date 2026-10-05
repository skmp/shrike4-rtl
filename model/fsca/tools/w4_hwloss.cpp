// w4_hwloss.cpp -- BRANCH 4: HOW TIGHTLY DOES REAL HARDWARE PIN THE ARRAY LOSS at exactly the operand
// patterns the FSCA square path uses?  The 2026-09-18 probe (../../fipr/kos_loss_probe) ran 31,104 FIPR
// vectors on the console whose low bits are FSCA's low bits.  Each observed (RN,RZ) float pair confines
// the array sum T -- hence the loss -- to an interval; the variants intersect.
// If the interval is narrower than the correction the residual needs, an array-loss / truncation-alignment
// explanation is refuted BY MEASUREMENT, not by modelling.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
static const char* DIR="../../fipr/kos_loss_probe/";
// single-product FIPR output, exponents both 127 (mantissas 24-bit), sign +
static uint32_t outof(i64 T,int rz){ i64 pp=(T+(1ll<<19))>>20; if(pp<=0) return 0;
    i64 S=pp; int top=63-__builtin_clzll((u64)S); int sh=top-23; i64 q;
    if(sh>0){ i64 r=S&((1ll<<sh)-1), h=1ll<<(sh-1); q=S>>sh; if(!rz) q+=(r>h)||(r==h&&(q&1)); }
    else q=S<<(-sh);
    if(q>>24){q>>=1;top++;}
    int e=top+254-280+127; if(e<=0) return 0; if(e>=255) return rz?0x7f7fffffu:0x7f800000u;
    return ((uint32_t)(e&0xff)<<23)|((uint32_t)q&0x7fffffu); }
int main(){ build_tables();
    char p[512]; snprintf(p,512,"%sprobe_vectors.bin",DIR); FILE*fv=fopen(p,"rb");
    snprintf(p,512,"%sprobe_hw.bin",DIR); FILE*fh=fopen(p,"rb");
    if(!fv||!fh){ puts("probe files missing"); return 1; }
    std::vector<uint32_t> V,H; uint32_t b[2];
    while(fread(b,4,2,fv)==2){V.push_back(b[0]);V.push_back(b[1]);}
    while(fread(b,4,2,fh)==2){H.push_back(b[0]);H.push_back(b[1]);}
    fclose(fv);fclose(fh); size_t n=std::min(V.size(),H.size())/2;
    printf("probe vectors with hardware answers: %zu\n",n);
    const i64 LO=-655360, HI=65536;   // the proved a-priori loss range
    std::map<std::pair<i64,i64>,std::pair<i64,i64>> iv;    // pattern -> [Llo,Lhi] consistent with hardware
    for(size_t i=0;i<n;i++){ i64 rec=V[2*i], mul=V[2*i+1]; i64 ex=rec*mul;
        std::pair<i64,i64> key{rec&((1ll<<18)-1), mul&((1ll<<16)-1)};
        // monotone in T: binary-search the L interval reproducing BOTH observed floats
        auto ok=[&](i64 L){ return outof(ex+L,0)==H[2*i] && outof(ex+L,1)==H[2*i+1]; };
        // find any consistent L by scanning coarsely from the model's own loss (must be consistent: 0 mismatches)
        i64 L0 = arr(rec,mul) - ex; if(!ok(L0)){ printf("  model loss inconsistent at vector %zu!\n",i); continue; }
        i64 lo=L0, hi=L0;
        { i64 a=LO,c=L0; while(a<c){ i64 m=a+(c-a)/2; if(ok(m)) c=m; else a=m+1; } lo=a; }
        { i64 a=L0,c=HI; while(a<c){ i64 m=a+(c-a+1)/2; if(ok(m)) a=m; else c=m-1; } hi=a; }
        auto it=iv.find(key);
        if(it==iv.end()) iv[key]={lo,hi};
        else { it->second.first=std::max(it->second.first,lo); it->second.second=std::min(it->second.second,hi); } }
    printf("distinct low-bit patterns: %zu\n",iv.size());
    { int sat=0; for(auto&kv:iv) if(kv.second.first<=LO&&kv.second.second>=HI) sat++;
      printf("patterns where hardware adds NO constraint inside that range: %d of %zu\n",sat,iv.size()); }
    i64 wmin=1e18,wmax=0; double wsum=0;
    for(auto&kv:iv){ i64 w=kv.second.second-kv.second.first; wmin=std::min(wmin,w); wmax=std::max(wmax,w); wsum+=w; }
    printf("hardware-consistent loss interval width: min %lld  mean %.0f  max %lld  (a-priori range 720896)\n",
        (long long)wmin,wsum/iv.size(),(long long)wmax);
    printf("\nthe square path's OWN calls, at the pinned offsets (model I1 = (arr(KC,dx)+K)>>23):\n");
    for(int t : {17,342,-342}){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23;
        std::pair<i64,i64> k2{ ((I1%(1ll<<18))+(1ll<<18))%(1ll<<18), ((dx%65536)+65536)%65536 };
        std::pair<i64,i64> k1{ ((i64)KC%(1ll<<18)), ((dx%65536)+65536)%65536 };
        auto f2=iv.find(k2), f1=iv.find(k1);
        i64 L2=arr(I1,dx)-I1*dx, L1=arr(KC,dx)-(i64)KC*dx;
        printf("  t=%+4d  mul1 pattern %s : loss1=%lld allowed [%lld,%lld] -> J may move by %+.3f..%+.3f units\n",
            t, f1==iv.end()?"NOT PROBED":"probed", (long long)L1,
            f1==iv.end()?0LL:(long long)f1->second.first, f1==iv.end()?0LL:(long long)f1->second.second,
            f1==iv.end()?0.0:(double)(f1->second.first-L1)/8388608.0*(double)(dx)/262144.0,
            f1==iv.end()?0.0:(double)(f1->second.second-L1)/8388608.0*(double)(dx)/262144.0);
        printf("          mul2 pattern %s : loss2=%lld allowed [%lld,%lld] -> J may move by %+.3f..%+.3f units\n",
            f2==iv.end()?"NOT PROBED":"probed", (long long)L2,
            f2==iv.end()?0LL:(long long)f2->second.first, f2==iv.end()?0LL:(long long)f2->second.second,
            f2==iv.end()?0.0:(double)(f2->second.first-L2)/262144.0,
            f2==iv.end()?0.0:(double)(f2->second.second-L2)/262144.0); }
    // coverage of every square-path call
    int cov1=0,cov2=0,tot=0; i64 w1=0,w2=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; tot++;
        std::pair<i64,i64> k1{ ((i64)KC%(1ll<<18)), ((dx%65536)+65536)%65536 };
        std::pair<i64,i64> k2{ ((I1%(1ll<<18))+(1ll<<18))%(1ll<<18), ((dx%65536)+65536)%65536 };
        auto f1=iv.find(k1), f2=iv.find(k2);
        if(f1!=iv.end()){cov1++; w1=std::max(w1,f1->second.second-f1->second.first);} 
        if(f2!=iv.end()){cov2++; w2=std::max(w2,f2->second.second-f2->second.first);} }
    printf("\ncoverage: first multiply %d/%d offsets probed (max interval %lld), second multiply %d/%d (max interval %lld)\n",
        cov1,tot,(long long)w1,cov2,tot,(long long)w2);
    printf("max J movement the hardware still permits: first multiply %.4f latch units, second %.4f\n",
        (double)w1/8388608.0*(512.0*8192.0)/262144.0, (double)w2/262144.0);
    return 0; }
