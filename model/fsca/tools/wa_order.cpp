// wa_order.cpp -- the OTHER tied square order that CURRENT.md says was never decided:
//     I1 = latch1(arr(Kc, dx) + K1, s1)            (shared, ~5052*t)
//     I2 = latch2(arr(I1, S_n) + K2, s2 [+k])      (PER NODE -- an extra truncation the
//                                                   shared-J model does not have)
//     T2 = sm  (arr(I2, dx) + K3, u  [+k])
// scale: s1 + s2 + u = 73, the sine octave k going to s2 or to u.
#include "wa_common.h"
#include <omp.h>
static Fsca F;
static const i64 C0B[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2B[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 BASE[32][1024],WLO[32][1024],WHI[32][1024];
struct Oc { int s1,s2,u,oct,r1,r2,r3,rule1,rule2,k1,k2,k3; };
static inline i64 kc(int sel,int s){ return sel==0?0 : sel==1?(1ll<<19) : (s>0?(1ll<<(s-1)):0); }
static int eval(const Oc&O,const int*nodes,int nn,int limit,int*perC0=nullptr){
    i64 I1[1024];
    i64 K1=kc(O.k1,O.s1);
    for(int t=-512;t<=511;t++) I1[t+512]=latchv(O.r1?arr((i64)t<<13,KC):arr(KC,(i64)t<<13),K1,O.s1,O.rule1);
    int tot=0;
    for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=m>=17,n=flip?32-m:m; i64 S=TB[n].S; int k=U2B[n]-30; if(n==0)k=0;
        int s2=O.s2+(O.oct==0?k:0), u=O.u+(O.oct==1?k:0);
        if(s2<1||u<1||s2>50||u>50) return 1<<28;
        i64 K2=kc(O.k2,s2), K3=kc(O.k3,u);
        int bestv=1<<28;
        for(i64 dc=-3;dc<=3;dc++){ int v=0; i64 C0=C0B[n]+dc;
            for(int t=-512;t<=511;t++){ i64 T2=0;
                if(S){ i64 I2=latchv(O.r2?arr(S,I1[t+512]):arr(I1[t+512],S),K2,s2,O.rule2);
                       i64 P=O.r3?arr((i64)t<<13,I2):arr(I2,(i64)t<<13); T2=sm(P+K3,u); }
                i64 V=C0+BASE[m][t+512]-T2;
                if(V<WLO[m][t+512]||V>WHI[m][t+512]){ v++; if(v>=bestv) break; } }
            if(v<bestv)bestv=v; }
        tot+=bestv; if(tot>limit) return tot; }
    return tot; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            BASE[m][t+512]=s*T1-s*T3; int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            WLO[m][t+512]=lo;WHI[m][t+512]=hi; } }
    int guard[5]={1,2,16,30,31}; int all[32]; for(int i=0;i<32;i++)all[i]=i;
    std::vector<Oc> cfgs;
    for(int s1=19;s1<=27;s1++) for(int s2=18;s2<=32;s2++){ int u=73-s1-s2; if(u<10||u>34)continue;
      for(int oct=0;oct<2;oct++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
        for(int rule1=0;rule1<3;rule1++) for(int rule2=0;rule2<3;rule2++)
          for(int k1=0;k1<3;k1++) for(int k2=0;k2<3;k2++) for(int k3=0;k3<3;k3++)
            cfgs.push_back({s1,s2,u,oct,r1,r2,r3,rule1,rule2,k1,k2,k3}); }
    printf("configs %zu\n",cfgs.size()); fflush(stdout);
    std::vector<int> gs(cfgs.size());
    double t0=omp_get_wtime();
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t i=0;i<cfgs.size();i++) gs[i]=eval(cfgs[i],guard,5,40);
    int gbest=1<<28; for(size_t i=0;i<cfgs.size();i++) gbest=std::min(gbest,gs[i]);
    size_t nz=std::count(gs.begin(),gs.end(),0);
    printf("guard(nodes 1,2,16,30,31) best = %d ; exact configs = %zu  (%.1fs)\n",gbest,nz,omp_get_wtime()-t0);
    std::vector<size_t> idx; for(size_t i=0;i<cfgs.size();i++) if(gs[i]<=std::max(gbest,0)+2) idx.push_back(i);
    printf("evaluating %zu on all 32 nodes\n",idx.size()); fflush(stdout);
    std::vector<int> fs(idx.size());
    #pragma omp parallel for schedule(dynamic,4)
    for(size_t i=0;i<idx.size();i++) fs[i]=eval(cfgs[idx[i]],all,32,1<<27);
    std::vector<size_t> o(idx.size()); for(size_t i=0;i<o.size();i++)o[i]=i;
    std::sort(o.begin(),o.end(),[&](size_t a,size_t b){return fs[a]<fs[b];});
    puts(" viol guard | s1 s2  u oct r1 r2 r3 rule1 rule2 K1 K2 K3");
    for(int i=0;i<25&&i<(int)o.size();i++){ Oc&c=cfgs[idx[o[i]]];
        printf("%5d %5d | %2d %2d %2d  %d  %d  %d  %d  %-5s %-5s %d  %d  %d\n",fs[o[i]],gs[idx[o[i]]],
            c.s1,c.s2,c.u,c.oct,c.r1,c.r2,c.r3,RULENAME[c.rule1],RULENAME[c.rule2],c.k1,c.k2,c.k3); }
    printf("total %.1fs\n",omp_get_wtime()-t0);
    return 0; }
