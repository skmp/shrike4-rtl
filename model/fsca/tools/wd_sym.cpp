// wd_sym.cpp -- with the finer latch, is a SIGN-SYMMETRIC latch (even in t) still excluded?
// Also: the per-t allowed latch window and the required asymmetry D(|t|) = J(-t)-J(t).
#include "wd_core.h"
static i64 JL[1024],JH[1024];
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int s2=argc>1?atoi(argv[1]):18;
    Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=s2; c.u2base=50-s2;
    Model M; M.c=c; build_model(M); fit_C0(M); build_thr(M);
    int nf=0;
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=JMAX; bool ok=true;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 b=base_of(M,n,t,flip); i64 aa,bb; if(!jint(M,n,b-h,b-l,aa,bb)){ok=false;break;}
            if(aa>gl)gl=aa; if(bb<gh)gh=bb; }
        JL[t+512]=gl; JH[t+512]=gh; if(!ok||gl>gh){JL[t+512]=1;JH[t+512]=0;nf++;} }
    printf("s2=%d  infeasible t: %d\n",s2,nf);
    int nsym=0, nover=0; i64 maxD=0,minD=0;
    for(int a=0;a<=511;a++){ int ip=a+512, im=(a?-a:0)+512;
        if(JL[ip]>JH[ip]||JL[im]>JH[im]) continue;
        i64 lo=std::max(JL[ip],JL[im]), hi=std::min(JH[ip],JH[im]);
        if(lo<=hi) nover++; else nsym++;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        if(dlo>maxD)maxD=dlo; if(dhi<minD)minD=dhi; }
    printf("|t| with DISJOINT windows for +t and -t (asymmetry REQUIRED): %d of 512\n",nsym);
    printf("|t| where one symmetric value works: %d\n",nover);
    printf("forced D>0 max %lld ; forced D<0 min %lld\n",(long long)maxD,(long long)minD);
    // exact bound: min violations with the latch forced even in t
    int tot=0;
    for(int a=0;a<=511;a++){ int ip=a+512, im=(a?-a:0)+512;
        std::vector<std::pair<i64,i64>> iv; int nc=0;
        for(int sgn=0;sgn<(a?2:1);sgn++){ int t=sgn?-a:a;
            for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
                int ang=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(ang,l,h,e);
                i64 b=base_of(M,n,t,flip); nc++; i64 aa,bb; if(jint(M,n,b-h,b-l,aa,bb)) iv.push_back({aa,bb}); } }
        std::vector<std::pair<i64,int>> ev; for(auto&p:iv){ev.push_back({p.first,1});ev.push_back({p.second+1,-1});}
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; for(auto&e2:ev){cur+=e2.second; if(cur>best)best=cur;}
        tot+=nc-best; (void)ip;(void)im; }
    printf("minimum violations with the latch forced EVEN in t: %d  (free: 0)\n",tot);
    // print the tightest windows
    printf("\ntightest windows (width<=4):\n t      JL       JH  width  chain\n");
    for(int t=-512;t<=511;t++) if(JH[t+512]-JL[t+512]<4&&JL[t+512]<=JH[t+512])
        printf("%4d %8lld %8lld %5lld %8lld\n",t,(long long)JL[t+512],(long long)JH[t+512],
            (long long)(JH[t+512]-JL[t+512]+1),(long long)chainJ(t,M.c));
    return 0; }
