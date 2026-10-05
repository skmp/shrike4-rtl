// b18.cpp -- NEW HYPOTHESIS: the square term is the OTHER output's linear product times pi*dx.
//   2 pi^2 dx^2 sin(x) = (2 pi dx sin(x)) * (pi dx)
// The sine-side linear coefficient C1s = RN23(sin * pi/2 * 2^23) is a table entry the hardware needs anyway
// (it is the COSINE output's linear term), and pi*dx is one product shared by both outputs.
//   Ls_n = latch(arr(C1sf_n, dx) + K1, s1 - es_n)      (node dependent, ~ sin * pi * t * 2^(35-s1))
//   Q    = latch(arr(Kpi,     dx) + K2, s2)            (node independent, ~ pi * t * 2^(34-s2))
//   T2   = sm(arr(Ls_n, Q) + K3, u3)      with s1 + s2 + u3 = 74
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 C1S[17]; static int ES[17];                 // sine-side linear coefficient field and octave
static void build_sine_linear(){ for(int n=0;n<=16;n++){ i128 s=sin_(PI_()*n/32);
        if(n==0){ C1S[0]=0; ES[0]=0; continue; }
        i128 v=s*PI_()/QONE*((i128)1<<22); int e=0;             // sin*(pi/2)*2^23
        while(v<((i128)1<<23)*QONE){ v<<=1; --e; }
        i128 q=v/(2*QONE), r=v%(2*QONE); if(r>QONE||(r==QONE&&(q&1)))++q;
        C1S[n]=(i64)q; ES[n]=e; } }
static i64 c0lo[17],c0hi[17];
int main(){ if(!load_fsca(F))return 1; build_tables(); build_sine_linear();
    for(int n=0;n<=16;n++){ i64 l,h;bool e; win_of(n*1024,l,h,e); c0lo[n]=l;c0hi[n]=h; }
    printf("node  C1s field  es   (check: sin field S=%s)\n","RN23(sin)");
    for(int n=0;n<=16;n++) printf("%2d  %08llx  %+d\n",n,(long long)C1S[n],ES[n]);
    struct R{ int s1,s2,u3,r1,r2,r3,k1,k2,k3; i64 kpi; int viol; int g; };
    std::vector<R> cfg;
    for(int s1=18;s1<=32;s1++) for(int s2=18;s2<=32;s2++){ int u3=74-s1-s2; if(u3<10||u3>36) continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
          for(int k1=0;k1<3;k1++) for(int k2=0;k2<3;k2++) for(int k3=0;k3<3;k3++)
            for(int dk=-2;dk<=2;dk++) cfg.push_back({s1,s2,u3,r1,r2,r3,k1,k2,k3,6588397+dk,0,0}); }
    printf("configs: %zu\n",cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfg.size();ci++){ R&c=cfg[ci];
        i64 K1=c.k1==0?0:c.k1==1?(1ll<<19):(1ll<<(c.s1-1));
        i64 K2=c.k2==0?0:c.k2==1?(1ll<<19):(1ll<<(c.s2-1));
        i64 K3=c.k3==0?0:c.k3==1?(1ll<<19):(1ll<<(c.u3-1));
        int viol=0,guard=0;
        // C0 per node: pick the endpoint that gives fewer violations for that table entry
        for(int n=0;n<=16;n++){ int bestn=1<<30; 
            for(i64 C0=c0lo[n];C0<=c0hi[n];C0++){ int v=0;
                int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
                for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17);
                    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                        i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                        i64 T2=0;
                        if(C1S[n]){ int sh1=c.s1-ES[n];
                            i64 Ls=((c.r1? arr(dx,C1S[n]) : arr(C1S[n],dx))+K1)>>sh1;
                            i64 Q =((c.r2? arr(dx,c.kpi)  : arr(c.kpi,dx)) +K2)>>c.s2;
                            i64 P = c.r3? arr(Q,Ls) : arr(Ls,Q); T2=sm(P+K3,c.u3); }
                        i64 s=flip?-1:1; i64 V=C0+s*T1-T2-s*T3;
                        int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                        if(V<lo||V>hi) v++; } }
                if(v<bestn) bestn=v; }
            viol+=bestn; if(n==0||n==1||n==2||n==16) guard+=bestn; }
        c.viol=viol; c.g=guard; }
    std::sort(cfg.begin(),cfg.end(),[](const R&a,const R&b){ if(a.g!=b.g) return a.g<b.g; return a.viol<b.viol; });
    printf("viol guard | s1 s2 u3 r1 r2 r3 k1 k2 k3 kpi\n");
    for(int i=0;i<25;i++){ R&r=cfg[i]; printf("%5d %5d | %2d %2d %2d  %d  %d  %d  %d  %d  %d  %lld\n",
        r.viol,r.g,r.s1,r.s2,r.u3,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,(long long)r.kpi); }
    // best overall by total violations
    std::sort(cfg.begin(),cfg.end(),[](const R&a,const R&b){ return a.viol<b.viol; });
    printf("\nbest by total violations:\n");
    for(int i=0;i<10;i++){ R&r=cfg[i]; printf("%5d %5d | %2d %2d %2d  %d  %d  %d  %d  %d  %d  %lld\n",
        r.viol,r.g,r.s1,r.s2,r.u3,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,(long long)r.kpi); }
    return 0; }
