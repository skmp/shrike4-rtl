// b28.cpp -- THE TWO-FACTOR SQUARE.   2 pi^2 dx^2 sin(x) = (pi dx) * (2 pi dx sin(x))
// The second factor is the COSINE output's linear product, i.e. the SAME 17-entry linear coefficient table
// read at index 16-n (cos((16-n)pi/32) = sin(n pi/32)).  Both factors are signed and proportional to t, so each
// carries a one-LSB floor asymmetry -- giving a relative product asymmetry of ~2^-20, exactly the size the data
// demands.  The economised quadratic coefficient then lives in the single stored constant Kq.
//   B_n = latch(arr(C1f[16-n], dx) + K1, s1 - e1[16-n])
//   A   = latch(arr(Kq,        dx) + K2, s2)
//   T2  = sm(arr(A,B_n) + K3, u3)      with s1 + s2 + u3 = tot
// Required Kq ~ KC / pi * 2^(s1+s2+u3-71).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 CLO[32][1024],CHI[32][1024]; static char USE[32]; static int TABN[32];
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    // constraint intervals for T2 per (node instance, t), with the established odd part
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; USE[node]=1; TABN[node]=n; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CLO[node][t+512]=base-hi; CHI[node][t+512]=base-lo; } }
    int TOTLO=argc>1?atoi(argv[1]):70, TOTHI=argc>2?atoi(argv[2]):76;
    struct R{ int tot,s1,s2,u3,r1,r2,r3,k1,k2,k3; i64 kq; int guard,viol; };
    std::vector<R> best; int bestguard=1<<30;
    for(int tot=TOTLO;tot<=TOTHI;tot++){
        // nominal Kq for this tot
        long double kqn = (long double)KC/3.14159265358979323846L*powl(2.0L,(long double)(tot-71));
        i64 kq0=(i64)llroundl(kqn);
        printf("tot=%d nominal Kq=%lld (pi*2^%d would be %.0Lf)\n",tot,(long long)kq0,tot-52,
               3.14159265358979323846L*powl(2.0L,(long double)(tot-52)));
        std::vector<R> cfg;
        for(int s1=16;s1<=34;s1++) for(int s2=16;s2<=34;s2++){ int u3=tot-s1-s2; if(u3<8||u3>34)continue;
            for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
              for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++)
                for(i64 dq=-40;dq<=40;dq++) cfg.push_back({tot,s1,s2,u3,r1,r2,r3,k1,k2,k3,kq0+dq,0,0}); }
        #pragma omp parallel for schedule(dynamic)
        for(size_t ci=0;ci<cfg.size();ci++){ R&c=cfg[ci];
            i64 K1=c.k1?(1ll<<19):0, K2=c.k2?(1ll<<19):0, K3=c.k3?(1ll<<19):0;
            int guard=0,viol=0;
            const int gnodes[4]={1,2,16,31};
            for(int pass=0;pass<2;pass++){
              int lo_=pass?1:0, hi_=pass?31:3;
              for(int gi=lo_;gi<=hi_;gi++){ int node=pass?gi:gnodes[gi]; if(!USE[node])continue; int n=TABN[node];
                int ns=16-n; i64 Cs=TB[ns].C1f; int es=TB[ns].e1; int sh1=c.s1-es;
                for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T2;
                    if(!Cs) T2=0; else { i64 B=((c.r1? arr(dx,Cs):arr(Cs,dx))+K1)>>sh1;
                        i64 A=((c.r2? arr(dx,c.kq):arr(c.kq,dx))+K2)>>c.s2;
                        i64 P=c.r3? arr(B,A):arr(A,B); T2=sm(P+K3,c.u3); }
                    if(T2<CLO[node][t+512]||T2>CHI[node][t+512]){ if(pass) viol++; else guard++; } }
                if(!pass&&guard) break; }
              if(!pass&&guard) break; }
            c.guard=guard; c.viol=guard?-1:viol; }
        std::sort(cfg.begin(),cfg.end(),[](const R&a,const R&b){ if(a.guard!=b.guard)return a.guard<b.guard; return a.viol<b.viol; });
        for(int i=0;i<3&&i<(int)cfg.size();i++){ R&r=cfg[i];
            printf("  guard=%d viol=%d | s1=%d s2=%d u3=%d r=%d%d%d k=%d%d%d Kq=%lld\n",
                r.guard,r.viol,r.s1,r.s2,r.u3,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,(long long)r.kq); }
        if(cfg.size()&&cfg[0].guard<bestguard){ bestguard=cfg[0].guard; }
        fflush(stdout); }
    return 0; }
