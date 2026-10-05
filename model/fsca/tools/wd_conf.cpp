// wd_conf.cpp -- for a given Cfg, list every t with an empty J intersection and dump the binding
// constraints (node, table, flip, required T2 window, required J interval).
#include "wd_core.h"
static void dump(Model&M,int t){
    printf("=== t=%d  chainJ=%lld ===\n",t,(long long)chainJ(t,M.c));
    i64 gl=0,gh=JMAX; std::vector<std::pair<i64,i64>> iv;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
        int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
        i64 b=base_of(M,n,t,flip); i64 aa,bb;
        bool ok=jint(M,n,b-h,b-l,aa,bb);
        printf(" node%2d tab%2d f%d  win[%9lld,%9lld]%s  T2in[%4lld,%4lld]  J in ",node,n,flip,
            (long long)l,(long long)h,e?"*":" ",(long long)(b-h),(long long)(b-l));
        if(!ok) printf("EMPTY\n"); else { printf("[%8lld,%8lld]\n",(long long)aa,(long long)bb);
            if(aa>gl)gl=aa; if(bb<gh)gh=bb; iv.push_back({aa,bb}); } }
    printf("  intersection: [%lld,%lld]%s\n",(long long)gl,(long long)gh,gl>gh?"  EMPTY":"");
    // binding pair
    for(size_t i=0;i<iv.size();i++)for(size_t j=0;j<iv.size();j++) if(iv[i].first>iv[j].second)
        { printf("  binding: needs J>=%lld and J<=%lld\n",(long long)iv[i].first,(long long)iv[j].second); goto done; }
    done:; }
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    Cfg c; if(argc>1){ c.sqK=4; c.sqKv=atoll(argv[1]); }
    Model M; M.c=c; build_model(M); fit_C0(M); build_thr(M);
    printf("C0:"); for(int n=0;n<=16;n++)printf(" %lld",(long long)M.C0[n]); printf("\n");
    for(int t=-512;t<=511;t++){
        int nc=0; std::vector<std::pair<i64,i64>> iv;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 b=base_of(M,n,t,flip); nc++; i64 aa,bb; if(jint(M,n,b-h,b-l,aa,bb)) iv.push_back({aa,bb}); }
        std::vector<std::pair<i64,int>> ev; for(auto&p:iv){ev.push_back({p.first,1});ev.push_back({p.second+1,-1});}
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; for(auto&e2:ev){cur+=e2.second; if(cur>best)best=cur;}
        if(best<nc){ printf("\n** t=%d : %d of %d constraints simultaneously satisfiable\n",t,best,nc); dump(M,t); } }
    return 0; }
