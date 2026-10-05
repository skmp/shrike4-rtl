// m8_delta.cpp -- THE decisive measurement: what IS the required correction delta(t) = J_req - J_model?
// Reported as an INTERVAL per t (the window gives an interval, not a point).  Then: is delta a
// FUNCTION of any small key?  If yes, that key names the hardware mechanism directly.
// Also tests the user's "low bits of t become fractional" in its concrete forms.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <climits>
static i64 JL[1024],JH[1024]; static bool have[1024];
static i64 Jm(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; }
static i64 dl[1024],dh[1024];
// is delta a function of key(t)?  -> do all t sharing a key have intersecting delta intervals?
static void testkey(const char* name,int (*key)(int),int nk){
    std::vector<i64> lo(nk, LLONG_MIN), hi(nk, LLONG_MAX); std::vector<int> cnt(nk,0);
    int bad=0;
    for(int i=0;i<1024;i++){ if(!have[i])continue; int t=i-512; int k=key(t); if(k<0||k>=nk)continue;
        lo[k]=std::max(lo[k],dl[i]); hi[k]=std::min(hi[k],dh[i]); cnt[k]++; }
    for(int k=0;k<nk;k++) if(cnt[k]&&lo[k]>hi[k]) bad++;
    printf("  %-28s keys=%4d  contradictory=%3d  %s\n",name,nk,bad, bad? "NOT a function of this key":"*** IS a function of this key ***");
}
static int k_and(int t,int m){ return ((t%1024)+1024+ (0))&m; }
static int M; static int key_and(int t){ return (t& M); }
static int K2S; static int key_ands(int t){ return ((t&K2S)<<1)|(t<0?1:0); }
static int SH; static int key_hi(int t){ return ((t>>SH)+1024)&2047; }
static int key_sign(int t){ return t<0?1:0; }
static int key_abs(int t){ int a=t<0?-t:t; return a; }
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    int need=0; i64 mnl=1<<30,mxh=-(1<<30);
    for(int i=0;i<1024;i++) if(have[i]){ i64 j=Jm(i-512); dl[i]=JL[i]-j; dh[i]=JH[i]-j;
        if(!(dl[i]<=0&&0<=dh[i])) need++;
        mnl=std::min(mnl,dl[i]); mxh=std::max(mxh,dh[i]); }
    printf("required correction delta(t) = J_required - J_model, as an interval per t\n");
    printf("  offsets where delta=0 is NOT allowed: %d / 1024      delta range over all t: [%lld, %lld]\n",
        need,(long long)mnl,(long long)mxh);
    { int h[9]; memset(h,0,sizeof h);
      for(int i=0;i<1024;i++) if(have[i]){ i64 c = dl[i]>0? dl[i] : (dh[i]<0? dh[i]:0);
          int b = c==0?4 : (c<-8?0: c<-4?1: c<-2?2: c<0?3: c>8?8: c>4?7: c>2?6:5); h[b]++; }
      const char* lab[9]={"<-8","-8..-5","-4..-3","-2..-1","0 ok","+1..+2","+3..+4","+5..+8",">+8"};
      printf("  nearest allowed correction: "); for(int b=0;b<9;b++) printf("%s:%d  ",lab[b],h[b]); printf("\n"); }
    printf("  landmarks: t=17 delta in [%lld,%lld]   t=342 [%lld,%lld]   t=-342 [%lld,%lld]\n\n",
        (long long)dl[512+17],(long long)dh[512+17],(long long)dl[512+342],(long long)dh[512+342],
        (long long)dl[512-342],(long long)dh[512-342]);
    printf("IS delta A FUNCTION OF A SMALL KEY?  (contradictory = key values with empty intersection)\n");
    for(int b=0;b<=10;b++){ M=(1<<b)-1; char nm[64]; snprintf(nm,64,"t & %d  (low %d bits)",M,b); testkey(nm,key_and,M+1); }
    for(int b=0;b<=6;b++){ K2S=(1<<b)-1; char nm[64]; snprintf(nm,64,"(t & %d) + sign",K2S); testkey(nm,key_ands,(K2S+1)*2); }
    for(int s=0;s<=6;s++){ SH=s; char nm[64]; snprintf(nm,64,"t >> %d  (high bits)",s); testkey(nm,key_hi,2048); }
    testkey("sign(t) only",key_sign,2);
    testkey("|t|",key_abs,513);
    printf("\nUSER HYPOTHESIS: low k bits of t are dropped as 'fractional' -> J loses c*(t & (2^k-1))^2\n");
    printf("  (if real, delta should equal -c*t_lo^2 for one constant c)\n");
    for(int k=1;k<=6;k++){ int m=(1<<k)-1;
        // for each t, delta must lie in [dl,dh]; require delta = -c*(t&m)^2 -> bound c
        long double clo=-1e18,chi=1e18; int viol=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; long double q=(long double)((t&m)*(t&m));
            if(q==0){ if(!(dl[i]<=0&&0<=dh[i])) viol++; continue; }
            long double a=-(long double)dh[i]/q, b=-(long double)dl[i]/q;   // c in [a,b]
            clo=std::max(clo,a); chi=std::min(chi,b); }
        printf("   k=%d: t_lo in 0..%d   c must lie in [%.4Lf, %.4Lf] %s  (t_lo=0 offsets violating: %d)\n",
            k,m,clo,chi, clo>chi? " -> EMPTY, no such c":" -> feasible", viol);
    }
    return 0; }
