// m1_tform.cpp -- USER HYPOTHESIS: t / dx is formed differently for the square path.
//   dx' = (t<<13) + p + q*[t<0]     p = uniform bias, q = 1s-vs-2s complement on the operand
//   I1' = I1      + r*[t<0]         1s-vs-2s complement on the FEEDBACK value
//   plus: each chain shift is either an arithmetic >> (2s complement, floor) or sm() (sign-magnitude,
//   truncate-toward-zero).  4 combos.  That is literally "1s vs 2s complement error" at each latch.
// Scored first on the 3 pinned offsets, then fully on jwin18.txt.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024], JH[1024]; static bool have[1024];
static inline i64 shf(i64 v,int u,int mode){ return mode? sm(v,u) : (v>>u); }
struct Cfg{ int p,q,r,m1,m2; };
static inline i64 chainJ(int t,const Cfg&c){
    i64 dx = ((i64)t<<13) + c.p + (t<0? c.q:0);
    i64 I1 = shf(arr(KC,dx)+K,23,c.m1);
    if(t<0) I1 += c.r;
    return shf(arr(I1,dx)+K,18,c.m2);
}
int main(){
    for(int i=0;i<1024;i++) have[i]=false;
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h; if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){ int i=(int)t+512; JL[i]=l;JH[i]=h;have[i]=true; } }
    fclose(f);
    printf("pinned: J(17)=[%lld,%lld]  J(342)=[%lld,%lld]  J(-342)=[%lld,%lld]\n",
        (long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],
        (long long)JL[-342+512],(long long)JH[-342+512]);
    Cfg base{0,0,0,0,0};
    printf("baseline model: J(17)=%lld  J(342)=%lld  J(-342)=%lld   (D=%lld, need %lld..%lld)\n\n",
        (long long)chainJ(17,base),(long long)chainJ(342,base),(long long)chainJ(-342,base),
        (long long)(chainJ(-342,base)-chainJ(342,base)),
        (long long)(JL[-342+512]-JH[342+512]),(long long)(JH[-342+512]-JL[342+512]));
    const int P=4096,Q=96,R=96;
    std::vector<Cfg> hits;
    long long tried=0, p17=0, p342=0, pall=0;
    #pragma omp parallel for collapse(2) schedule(dynamic) reduction(+:tried,p17,p342,pall)
    for(int m1=0;m1<2;m1++) for(int m2=0;m2<2;m2++){
        std::vector<Cfg> loc;
        for(int p=-P;p<=P;p++) for(int q=-Q;q<=Q;q++){
            Cfg c{p,q,0,m1,m2};
            i64 j17=chainJ(17,c); bool ok17 = j17>=JL[17+512]&&j17<=JH[17+512];
            i64 jp=chainJ(342,c); bool okp = jp>=JL[342+512]&&jp<=JH[342+512];
            for(int r=-R;r<=R;r++){
                c.r=r; tried++;
                // r only affects t<0, so ok17/okp are r-independent
                if(ok17) p17++;
                if(okp) p342++;
                if(!(ok17&&okp)) continue;
                i64 jm=chainJ(-342,c);
                if(jm>=JL[-342+512]&&jm<=JH[-342+512]){ pall++; loc.push_back(c); }
            }
        }
        #pragma omp critical
        hits.insert(hits.end(),loc.begin(),loc.end());
    }
    printf("scanned %lld configs (p +-%d, q +-%d, r +-%d, 4 shift conventions)\n",tried,P,Q,R);
    printf("  pass t=17:    %lld\n  pass t=342:   %lld\n  pass all three: %lld\n\n",p17,p342,pall);
    int best=-1; Cfg bc{};
    for(auto&c:hits){ int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=chainJ(i-512,c); if(j>=JL[i]&&j<=JH[i]) sc++; }
        if(sc>best){best=sc;bc=c;} }
    if(best>=0) printf("BEST full score %d / 1024   at p=%d q=%d r=%d shifts=(%s,%s)\n",
        best,bc.p,bc.q,bc.r,bc.m1?"sm":">>",bc.m2?"sm":">>");
    else printf("no config passed all three pinned offsets.\n");
    return 0; }
