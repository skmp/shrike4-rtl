// w2_verify.cpp -- re-run the FULL model over nodes 0..31 with branch 2's best square rule substituted,
// and report the real window-violation count (baseline 147).  jwin18 is a bound, not a model, so a
// jwin18 score means nothing until this is run.  The rule is used at the FINE latch scale that jwin18
// was built at: T2 = sm(arr(J,S)+K, u2+2) with u2 = 30+k, i.e. shift 18 rather than 20.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <utility>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} ex=w.exact; }
struct Rule{ int w,s1,s2; i64 kc,K1,K2; int du; };     // du = extra shift on u2 (0 = the 20-scale, 2 = jwin18's)
static inline i64 Jof(int t,const Rule&R){ i64 dx=(i64)t<<R.w;
    i64 I1=(arr(R.kc,dx)+R.K1)>>R.s1; return (arr(I1,dx)+R.K2)>>R.s2; }
static inline i64 Vof(int n,int t,int flip,i64 C0,int u2,const Rule&R){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 J=Jof(t,R); i64 T2=T.S? sm(arr(J,T.S)+K,u2+R.du):0;
    i64 s=flip?-1:1; return C0+s*T1-T2-s*T3; }
static int score(const Rule&R,int*guard){ int grand=0; int g=0;
    for(int n=0;n<=16;n++){ int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17); int tn=flip?32-m:m; int v=0;
            for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 V=Vof(tn,t,flip,C0TAB[tn],U2TAB[tn],R); if(V<lo||V>hi) v++; }
            grand+=v; if(tn==0||tn==1||tn==2||tn==16) g+=v; } }
    *guard=g; return grand; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    struct Named{ const char*n; Rule r; };
    Named rules[]={
      {"baseline  w=13 s1=23 s2=20 KC  K=2^19 (shift-20 latch)", {13,23,20,KC,K,K,0}},
      {"baseline at the FINE latch      w=13 s1=23 s2=18",       {13,23,18,KC,K,K,2}},
      {"branch2 best FIXED-K  w=14 s1=27 s2=19 KCw=41388583",    {14,27,19,41388583,K,K,2}},
      {"branch2 best FIXED-K  w=14 s1=28 s2=19 KCw=82777166",    {14,28,19,82777166,K,K,2}},
      {"branch2 best FREE-K   w=16 s1=30 s2=21 KCw=82777165",    {16,30,21,82777165,85590016,0,2}},
    };
    printf("%-56s | jwin18 | full model viol (baseline 147) | guard nodes 0,1,2,16\n","rule");
    // jwin18 score for each
    static i64 JL[1024],JH[1024]; static bool have[1024];
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    for(auto&nm:rules){ int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=Jof(i-512,nm.r); if(j>=JL[i]&&j<=JH[i])sc++; }
        int g; int v=score(nm.r,&g);
        printf("%-56s | %4d   | %6d                         | %d\n",nm.n,sc,v,g); fflush(stdout); }
    // exact best K2 for a given (w,s1,s2,KCw,K1): each offset admits an interval of K2, sweep the
    // 1024 intervals for the point of maximum overlap, then re-run the full model there.
    if(argc>5){ int CLAMP = argc>6? atoi(argv[6]) : 1; Rule R; R.w=atoi(argv[1]); R.s1=atoi(argv[2]); R.s2=atoi(argv[3]);
        R.kc=atoll(argv[4]); R.K1=atoll(argv[5]); R.du=2; R.K2=0;
        i64 per2=1ll<<R.s2;
        std::vector<std::pair<i64,int>> ev;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<R.w;
            i64 I1=(arr(R.kc,dx)+R.K1)>>R.s1; i64 Q=arr(I1,dx);
            i64 lo=JL[i]*per2-Q, hi=(JH[i]+1)*per2-1-Q;
            if(CLAMP){ if(hi<0||lo>=per2) continue; if(lo<0)lo=0; if(hi>=per2)hi=per2-1; }
            ev.push_back({lo,1}); ev.push_back({hi+1,-1}); }
        std::sort(ev.begin(),ev.end());
        int cur=0,best=0; i64 bk2=0;
        for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second;
            if(cur>best && (i+1==ev.size()||ev[i+1].first!=ev[i].first)){ best=cur; bk2=ev[i].first; } }
        R.K2=bk2; int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=Jof(i-512,R); if(j>=JL[i]&&j<=JH[i])sc++; }
        int g; int v=score(R,&g);
        printf("\n[K2 %s to one latch period] w=%d s1=%d s2=%d KCw=%lld K1=%lld : exact best K2=%lld -> jwin18 %d (overlap %d), FULL MODEL %d viol, guard %d\n",
            CLAMP?"RESTRICTED":"UNRESTRICTED (b38 convention)",R.w,R.s1,R.s2,(long long)R.kc,(long long)R.K1,(long long)bk2,sc,best,v,g);
        printf("  landmarks: J(17)=%lld (need 45617)  J(342)=%lld (need 18466836)  J(-342)=%lld (need [18466858,18466867])\n",
            (long long)Jof(17,R),(long long)Jof(342,R),(long long)Jof(-342,R));
        // the same rule at the coarse (shift-20-equivalent) latch scale, for comparability with the 147 baseline
        Rule R0=R; R0.du=0; R0.s2=R.s2+2; int g0; int v0=score(R0,&g0);
        printf("  same rule at the COARSE latch (s2+2, du=0): FULL MODEL %d viol, guard %d\n",v0,g0);
    }
    return 0; }
