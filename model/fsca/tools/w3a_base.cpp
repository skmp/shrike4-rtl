// w3a_base.cpp -- sanity: harness reproduces the published baselines exactly.
#include "w3_common.h"
int main(){ if(!load_fsca(FD)) return 1; build_tables(); if(!load_jwin()) return 2;
    gen_windows();
    int diff=0; for(int i=0;i<1024;i++) if(JLw[i]!=JLf[i]||JHw[i]!=JHf[i]) diff++;
    printf("gen_windows() vs jwin18.txt: %d of 1024 rows differ (expect 0)\n", diff);
    auto Jb=[&](int t){ return baseline_J(t); };
    printf("baseline jwin18 score: %d / 1024   (expect 918)\n", score_jwin(Jb, JLf, JHf));
    int g; int v = full_violations(Jb,&g);
    printf("baseline full-model violations over nodes 0..31: %d   (expect 147), guard-node(0,1,2,16) viol %d (expect 0)\n", v, g);
    report_pins("baseline", Jb, JLf, JHf);
    // operand range audit at default width
    i64 mxKC=0,mxI1=0,mxJ=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
        if(llabs(KC)>mxKC)mxKC=llabs(KC); if(llabs((long long)I1)>mxI1)mxI1=llabs((long long)I1); if(llabs((long long)J)>mxJ)mxJ=llabs((long long)J); }
    printf("recoded-operand magnitudes: KC=%lld (needs %d digits)  max I1=%lld (%d)  max J=%lld (%d)\n",
        (long long)mxKC, (int)(65-__builtin_clzll(mxKC))/2+1, (long long)mxI1, (int)(65-__builtin_clzll(mxI1))/2+1,
        (long long)mxJ, (int)(65-__builtin_clzll(mxJ))/2+1);
    return 0; }
