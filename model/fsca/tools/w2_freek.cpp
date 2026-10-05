// w2_freek.cpp -- BRANCH 2 with BOTH rounding constants free as well as the stored constant.
// Stage 1 (rigorous upper bound, early-abort): with K1,K2 free each latch output takes exactly two
//   values over a full period, so choosing them INDEPENDENTLY per offset over-counts.  That relaxed
//   count is an upper bound on the true max over (K1,K2).  Offsets are visited largest-|t| first and
//   the KCw is dropped as soon as the miss count exceeds the budget.
// Stage 2 (exact): survivors get the b38 treatment -- K1 swept through its sorted breakpoints while a
//   segment tree keeps the maximum overlap of the per-offset K2 intervals -> exact max over (K1,K2).
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
static int ORD[1024], NORD=0;
static void brk(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
struct Seg{ std::vector<int> mx,lz; int n;
    void init(int m){ n=1; while(n<m)n<<=1; mx.assign(2*n,0); lz.assign(2*n,0); }
    void upd(int nd,int l,int r,int a,int b,int v){ if(b<l||r<a)return; if(a<=l&&r<=b){mx[nd]+=v;lz[nd]+=v;return;}
        int m=(l+r)>>1; upd(2*nd,l,m,a,b,v); upd(2*nd+1,m+1,r,a,b,v); mx[nd]=std::max(mx[2*nd],mx[2*nd+1])+lz[nd]; }
    void add(int a,int b,int v){ if(a>b)return; upd(1,0,n-1,a,b,v); }
    int best(){ return mx[1]; } };
// exact max over (K1,K2) for one (w,s1,s2,KCw)
static int exact_k(int w,int s1,int s2,i64 kc,i64&bK1){
    i64 per1=1ll<<s1, per2=1ll<<s2;
    std::vector<i64> alo(1024),ahi(1024),blo(1024),bhi(1024),bp(1024); std::vector<char> ok(1024,0);
    std::vector<i64> co; co.reserve(4096);
    for(int t=-512;t<=511;t++){ int i=t+512; if(!have[i])continue; i64 dx=(i64)t<<w;
        i64 P=arr(kc,dx); i64 v0=P>>s1; i64 rem=P-(v0<<s1); bp[i]= rem? per1-rem : per1; ok[i]=1;
        for(int k=0;k<2;k++){ i64 Q=arr(v0+k,dx); i64 lo=JL[i]*per2-Q, hi=(JH[i]+1)*per2-1-Q;
            if(k==0){alo[i]=lo;ahi[i]=hi;} else {blo[i]=lo;bhi[i]=hi;}
            co.push_back(lo); co.push_back(hi+1); } }
    std::sort(co.begin(),co.end()); co.erase(std::unique(co.begin(),co.end()),co.end());
    auto idx=[&](i64 v){ return (int)(std::lower_bound(co.begin(),co.end(),v)-co.begin()); };
    Seg sg; sg.init((int)co.size()+1);
    for(int i=0;i<1024;i++) if(ok[i]) sg.add(idx(alo[i]),idx(ahi[i]+1)-1,1);
    int best=sg.best(); bK1=0;
    std::vector<int> ord; for(int i=0;i<1024;i++) if(ok[i]&&bp[i]<per1) ord.push_back(i);
    std::sort(ord.begin(),ord.end(),[&](int a,int b){return bp[a]<bp[b];});
    for(size_t q=0;q<ord.size();q++){ int i=ord[q];
        sg.add(idx(alo[i]),idx(ahi[i]+1)-1,-1); sg.add(idx(blo[i]),idx(bhi[i]+1)-1,1);
        if(q+1==ord.size()||bp[ord[q+1]]!=bp[i]){ int v=sg.best(); if(v>best){best=v;bK1=bp[i];} } }
    return best; }
int main(int argc,char**argv){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    for(int i=0;i<1024;i++) if(have[i]) ORD[NORD++]=i;
    std::sort(ORD,ORD+NORD,[](int a,int b){ return abs(a-512)>abs(b-512); });
    int TARGET = argc>1? atoi(argv[1]) : 926;          // look for anything scoring >= TARGET
    int BUDGET = NORD - TARGET;                        // allowed misses
    long long scanned=0, surv=0; int gbest=-1; int bw=0,bs1=0,bs2=0; i64 bkc=0,bK1=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:scanned,surv)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++){
      for(int s2=4;s2<=40;s2++){
        long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
        i64 L=KCMAX,H=1;
        for(int t=459;t<=511;t++){ i64 a,b; brk(t,w,s1,s2,a,b); if(a<L)L=a; if(b>H)H=b; }
        for(int t=-512;t<=-460;t++){ i64 a,b; brk(t,w,s1,s2,a,b); if(a<L)L=a; if(b>H)H=b; }
        if(H<L) continue; scanned+=H-L+1;
        for(i64 kc=L;kc<=H;kc++){
            int miss=0; bool dead=false;
            for(int k=0;k<NORD;k++){ int i=ORD[k]; int t=i-512; i64 dx=(i64)t<<w;
                i64 P=arr(kc,dx), v0=P>>s1; int good=0;
                for(int c=0;c<2&&!good;c++){ i64 q=arr(v0+c,dx)>>s2;
                    if((q>=JL[i]&&q<=JH[i])||(q+1>=JL[i]&&q+1<=JH[i])) good=1; }
                if(!good){ if(++miss>BUDGET){dead=true;break;} } }
            if(dead) continue;
            surv++;
            i64 kk1; int ex=exact_k(w,s1,s2,kc,kk1);
            #pragma omp critical
            { if(ex>gbest){ gbest=ex;bw=w;bs1=s1;bs2=s2;bkc=kc;bK1=kk1;
                printf("  new best exact score %d/1024 at w=%d s1=%d s2=%d KCw=%lld K1=%lld\n",ex,w,s1,s2,(long long)kc,(long long)kk1); fflush(stdout);} }
        }
      } }
    printf("\nKCw values scanned (region B): %lld ; survived the relaxed >=%d filter: %lld\n",scanned,TARGET,surv);
    printf("BEST EXACT SCORE over (w,s1,s2,KCw,K1,K2): %d/1024  at w=%d s1=%d s2=%d KCw=%lld K1=%lld\n",
        gbest,bw,bs1,bs2,(long long)bkc,(long long)bK1);
    return 0; }
