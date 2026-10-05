// wc_fit.cpp -- EXACT max-coverage interval fits of the required square latch J(t).
//   (a) J = floor((A*t^2 + R) / 2^24)                 2 parameters, per side and joint
//   (b) J = floor((A*t^2 + B*t + R) / 2^24)           3 parameters (a signed linear term = asymmetry ~ |t|)
//   (c) J = floor((A*t^2 + R_sign) / 2^24)            symmetric scale, sign-dependent offset
// All integer.  For a fixed A (and B) the feasible R per t is an interval, so max coverage is a sweep.
#include "wc_core.h"
#include <vector>
#include <algorithm>
static i64 JLo[1024],JHi[1024]; static char JOK[1024];
static void build_jwin(){
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue; const Tab&T=TB[n];
            i64 base=C0B[n]+LT[node][t+512];
            i64 t2lo=base-WHI[node][t+512], t2hi=base-WLO[node][t+512]; int u=T.u2;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLo[t+512]=gl;JHi[t+512]=gh;JOK[t+512]=gl<=gh; } }
static const int SH=24;
// max coverage over R for a given A,B on a set of t
static int cover(i64 A,i64 B,int lo,int hi,i64*bestR){
    static thread_local std::vector<std::pair<i64,int>> ev; ev.clear();
    for(int t=lo;t<=hi;t++){ if(!JOK[t+512])continue; i64 tt=(i64)t*t;
        i64 base=A*tt + B*(i64)t;
        i64 rl = (JLo[t+512]<<SH) - base, rh = ((JHi[t+512]+1)<<SH) - 1 - base;
        ev.push_back({rl,1}); ev.push_back({rh+1,-1}); }
    std::sort(ev.begin(),ev.end());
    int cur=0,best=0; i64 bR=0;
    for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best){best=cur;bR=ev[i].first;} }
    if(bestR)*bestR=bR; return best; }
int main(){ wc_init(); build_jwin();
    int nfP=0,nfN=0; for(int t=1;t<=511;t++)nfP+=JOK[t+512]; for(int t=-512;t<=-1;t++)nfN+=JOK[t+512];
    printf("feasible: positive %d, negative %d, total %d\n",nfP,nfN,nfP+nfN+JOK[512]);
    i64 A0=(i64)KC<<7;             // KC/2^17 expressed at 2^-24
    int bp=0,bn=0,bj=0; i64 apA=0,anA=0,ajA=0,apR=0,anR=0,ajR=0;
    #pragma omp parallel
    { int lp=0,ln=0,lj=0; i64 lpA=0,lnA=0,ljA=0,lpR=0,lnR=0,ljR=0;
      #pragma omp for schedule(static)
      for(int d=-40000;d<=40000;d++){ i64 A=A0+d; i64 R;
        int cp=cover(A,0,1,511,&R); if(cp>lp){lp=cp;lpA=A;lpR=R;}
        int cn=cover(A,0,-512,-1,&R); if(cn>ln){ln=cn;lnA=A;lnR=R;}
        int cj=cover(A,0,-512,511,&R); if(cj>lj){lj=cj;ljA=A;ljR=R;} }
      #pragma omp critical
      { if(lp>bp){bp=lp;apA=lpA;apR=lpR;} if(ln>bn){bn=ln;anA=lnA;anR=lnR;} if(lj>bj){bj=lj;ajA=ljA;ajR=ljR;} } }
    printf("(a) J=floor((A t^2 + R)/2^24):\n");
    printf("    positive side : %d / %d   A=%lld (A/2^24=%.9f)  R=%lld\n",bp,nfP,(long long)apA,apA/16777216.0,(long long)apR);
    printf("    negative side : %d / %d   A=%lld (%.9f)  R=%lld\n",bn,nfN,(long long)anA,anA/16777216.0,(long long)anR);
    printf("    joint         : %d / %d   A=%lld (%.9f)  R=%lld\n",bj,nfP+nfN+JOK[512],(long long)ajA,ajA/16777216.0,(long long)ajR);
    // (b) 3 parameter
    int b3=0; i64 b3A=0,b3B=0,b3R=0;
    #pragma omp parallel
    { int l3=0; i64 l3A=0,l3B=0,l3R=0;
      #pragma omp for schedule(dynamic)
      for(int d=-3000;d<=3000;d++){ i64 A=A0+d*8;
        for(i64 B=-400000;B<=400000;B+=2000){ i64 R; int c=cover(A,B,-512,511,&R);
            if(c>l3){l3=c;l3A=A;l3B=B;l3R=R;} } }
      #pragma omp critical
      if(l3>b3){b3=l3;b3A=l3A;b3B=l3B;b3R=l3R;} }
    printf("(b) J=floor((A t^2 + B t + R)/2^24): %d / %d   A=%lld B=%lld R=%lld  (B/2^24=%.4f per t)\n",
        b3,nfP+nfN+JOK[512],(long long)b3A,(long long)b3B,(long long)b3R,b3B/16777216.0);
    // (c) symmetric A, sign-dependent R -> just bp+bn with a COMMON A
    int bc=0; i64 bcA=0,bcRp=0,bcRn=0;
    #pragma omp parallel
    { int lc=0; i64 lcA=0,lcRp=0,lcRn=0;
      #pragma omp for schedule(static)
      for(int d=-40000;d<=40000;d++){ i64 A=A0+d,Rp,Rn;
        int c=cover(A,0,1,511,&Rp)+cover(A,0,-512,-1,&Rn); if(c>lc){lc=c;lcA=A;lcRp=Rp;lcRn=Rn;} }
      #pragma omp critical
      if(lc>bc){bc=lc;bcA=lcA;bcRp=lcRp;bcRn=lcRn;} }
    printf("(c) common A, sign-dependent R: %d / %d   A=%lld  R+=%lld R-=%lld  (delta=%.3f J units)\n",
        bc,nfP+nfN,(long long)bcA,(long long)bcRp,(long long)bcRn,(bcRn-bcRp)/16777216.0);
    return 0; }
