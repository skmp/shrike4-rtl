// wd_c0.cpp -- pin the C0 table.  The t=0 window leaves 2-3 candidates on 7 nodes; with the corrected
// latch (s2=18) the free-latch bound is 0, so a wrong C0 shows up as an INFEASIBLE t.  For each
// ambiguous node, test every candidate and report the resulting free-latch bound.
#include "wd_core.h"
static int freeBound(Model&M,int&infeas){ build_thr(M); int tot=0; infeas=0;
    for(int t=-512;t<=511;t++){ std::vector<std::pair<i64,i64>> iv; int nc=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e); i64 b=base_of(M,n,t,flip);
            if(!M.S[n]){ i64 V=b; if(V<l||V>h) tot++; continue; }
            nc++; i64 aa,bb; if(jint(M,n,b-h,b-l,aa,bb)) iv.push_back({aa,bb}); }
        std::vector<std::pair<i64,int>> ev; for(auto&p:iv){ev.push_back({p.first,1});ev.push_back({p.second+1,-1});}
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; for(auto&e2:ev){cur+=e2.second; if(cur>best)best=cur;}
        tot+=nc-best; if(best<nc) infeas++; }
    return tot; }
int main(){ if(!load_fsca(FF))return 1;
    Cfg c0; c0.i2K=3; c0.i2Kv=1ll<<19; c0.s2v=18; c0.u2base=32;
    Model M; M.c=c0; build_model(M); fit_C0(M);
    i64 FIT[17]; for(int n=0;n<=16;n++) FIT[n]=M.C0[n];
    printf("n   t=0 window        candidates -> free-latch bound (infeasible t)   PINNED   RN26     delta\n");
    for(int n=0;n<=16;n++){
        i64 l1,h1,l2,h2; bool e; win_of(n*1024,l1,h1,e); win_of(((32-n)*1024)&0xffff,l2,h2,e);
        i64 lo=std::max(l1,l2), hi=std::min(h1,h2);
        printf("%2d  [%9lld,%9lld]  ",n,(long long)lo,(long long)hi);
        i64 good=-1; int ngood=0;
        for(i64 cc=lo;cc<=hi;cc++){ Cfg c=c0; for(int m=0;m<=16;m++) c.C0ov[m]=FIT[m]; c.C0ov[n]=cc;
            Model M2; M2.c=c; build_model(M2); for(int m=0;m<=16;m++) M2.C0[m]=c.C0ov[m]; M2.C0[0]=0;
            int inf; int fb=freeBound(M2,inf);
            printf("%lld:%d/%d  ",(long long)cc,fb,inf);
            if(fb==0){good=cc;ngood++;} }
        printf(" %s", ngood==1?"UNIQUE":(ngood==0?"NONE":"ambiguous"));
        if(ngood==1) printf("  C0=%lld  RN26=%lld  d=%+lld",(long long)good,(long long)TB[n].sin26,(long long)(good-TB[n].sin26));
        printf("\n"); }
    return 0; }
