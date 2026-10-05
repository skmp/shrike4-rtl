// h4_protocol.cpp -- turn the decomposition into a runnable hardware experiment.
// FIPR returns floor((P + L + 2^19)/2^20) with P = rec*mul known exactly, so ONE vector only tells us
//     L >= 2^19 - (P mod 2^20)
// -- a one-sided comparison of the loss against a known threshold.  But the loss depends ONLY on the low bits,
// so we may vary the HIGH bits freely: that moves P mod 2^20 without moving L, sweeping the threshold.
// A handful of vectors per pattern therefore pins the loss exactly, by bisection.
#include "base2.h"
#include <cstdio>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
int main(){ build_tables();
    // the low-bit patterns the FSCA square path produces
    std::set<std::pair<i64,i64>> pat;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
        i64 rr[3]={KC,I1,J}, mm[3]={dx,dx,TB[8].S};
        for(int i=0;i<3;i++) pat.insert({ ((rr[i]%(1ll<<18))+(1ll<<18))%(1ll<<18),
                                          ((mm[i]%(1ll<<16))+(1ll<<16))%(1ll<<16) }); }
    printf("distinct (rec mod 2^18, mul mod 2^16) patterns used by the square path: %zu\n",pat.size());
    // for each pattern, find high-bit choices whose P mod 2^20 spreads across the range
    FILE*f=fopen("h_fipr_probe.txt","w");
    fprintf(f,"# Loss-probe vectors.  Each line:  rec_mant  mul_mant  threshold  predicted_bit\n"
              "# FIPR with these mantissas returns floor((rec*mul + L + 2^19)/2^20).  Since rec*mul is known,\n"
              "# the returned value says whether L >= threshold.  Varying the high bits sweeps the threshold\n"
              "# without changing L, so the set of answers per pattern pins L exactly.\n"
              "# predicted_bit is what the CURRENT model says; any disagreement on hardware is a model error.\n");
    long nv=0; int covered=0, thin=0;
    for(auto&p : pat){
        i64 rlo=p.first, mlo=p.second;
        i64 L = arr((1ll<<23)|rlo,(1ll<<23)|mlo) - ((1ll<<23)|rlo)*((1ll<<23)|mlo);
        // collect probes with distinct thresholds, bracketing L
        std::vector<std::pair<i64,std::pair<i64,i64>>> probes;   // threshold -> (rec,mul)
        for(i64 rh=(1ll<<5); rh<(1ll<<6); rh++) for(i64 mh=(1ll<<7); mh<(1ll<<8); mh+=7){
            i64 rec=(rh<<18)|rlo, mul=(mh<<16)|mlo;
            if(rec<(1ll<<23)||rec>=(1ll<<24)||mul<(1ll<<23)||mul>=(1ll<<24)) continue;
            i64 P=rec*mul; i64 thr=(1ll<<19)-(P%(1ll<<20));
            probes.push_back({thr,{rec,mul}});
        }
        std::sort(probes.begin(),probes.end());
        // keep ~16 spread over the thresholds that actually bracket L
        int kept=0; i64 last=-(1ll<<62);
        for(auto&pr:probes){ if(pr.first-last < 40000) continue; last=pr.first; 
            i64 rec=pr.second.first, mul=pr.second.second;
            fprintf(f,"%lld %lld %lld %d\n",(long long)rec,(long long)mul,(long long)pr.first,(int)(L>=pr.first));
            nv++; if(++kept>=16) break; }
        if(kept>=8) covered++; else thin++;
    }
    fclose(f);
    printf("wrote h_fipr_probe.txt: %ld vectors, %d patterns with >= 8 bracketing probes (%d thin)\n",nv,covered,thin);
    printf("that is a dump of about %.1f KB of results -- seconds on the console.\n",nv*8/1024.0);
    return 0; }
