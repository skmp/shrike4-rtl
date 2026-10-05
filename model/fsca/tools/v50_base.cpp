// v50_base.cpp -- independent baseline + SELF-TESTS of the harness.
#include "v50_core.h"
int main(){
    if(!load_fsca(VF)){puts("no dump");return 1;} build_tables();
    if(!v50_load_jwin()){puts("no jwin18");return 1;}
    // --- self test 1: the width column and feasibility
    int feas=0; i64 sw=0; for(int i=0;i<1024;i++){ if(VJL[i]<=VJH[i]){feas++; sw+=VJH[i]-VJL[i]+1;} }
    printf("SELFTEST jwin18: %d/1024 feasible, mean width %.1f\n",feas,(double)sw/feas);
    // --- self test 2: a free latch inside the window must give 0 full-model violations at shift 18
    {   auto jf=[&](int t){ return VJL[t+512]; };
        printf("SELFTEST free-latch(JL) at shift 18 -> full-model violations nodes0..31 = %d (must be 0)\n",
               v50_full(jf,18,dx_id,dx_id));
        auto jh=[&](int t){ return VJH[t+512]; };
        printf("SELFTEST free-latch(JH) at shift 18 -> %d (must be 0)\n", v50_full(jh,18,dx_id,dx_id));
        // and the WRONG unit (shift 20 value scored as if shift 18) must be catastrophic
        VCfg c20; c20.s2=20; auto jw=[&](int t){ return v50_J(dx_id(t),dx_id(t),c20); };
        printf("SELFTEST unit-conflation guard: shift-20 latch used at shift 18 -> %d violations (must be huge)\n",
               v50_full(jw,18,dx_id,dx_id));
    }
    puts("");
    for(int ro=0;ro<2;ro++){
        VCfg c; c.ro=ro; c.s2=18;
        auto jf=[&](int t){ return v50_J(dx_id(t),dx_id(t),c); };
        int p,q; int sc=v50_score(jf,&p,&q);
        VCfg c20=c; c20.s2=20; auto j20=[&](int t){ return v50_J(dx_id(t),dx_id(t),c20); };
        int f31=v50_full(j20,20,dx_id,dx_id); int pv; int f64=v50_full64(j20,20,dx_id,dx_id,&pv);
        printf("arr(%s): jwin18 %d/1024 (t>=0 %d/512, t<0 %d/512) | full nodes0..31 %d | full 65536 %d (pos-t %d)\n",
               ro?"dx,KC":"KC,dx",sc,p,q,f31,f64,pv);
        printf("          J(17)=%lld [need %lld..%lld]  J(342)=%lld [%lld..%lld]  J(-342)=%lld [%lld..%lld]\n",
            (long long)jf(17),(long long)VJL[17+512],(long long)VJH[17+512],
            (long long)jf(342),(long long)VJL[342+512],(long long)VJH[342+512],
            (long long)jf(-342),(long long)VJL[-342+512],(long long)VJH[-342+512]);
    }
    // required correction vs the proportionality law
    { VCfg c; c.s2=18; auto jf=[&](int t){ return v50_J(dx_id(t),dx_id(t),c); };
      i64 r17=VJH[17+512]-jf(17), r342=VJH[342+512]-jf(342);
      printf("\nrequired dJ at t=17: %+lld ; at t=342: %+lld ; ratio %.4f ; t-ratio 17/342 = %.4f\n",
             (long long)r17,(long long)r342,(double)r17/(double)r342,17.0/342.0);
    }
    return 0; }
