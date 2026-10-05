// w1_err2.cpp -- MODEL-FREE node-structure probe.  E(a) = V_hw(a) - RN(sin(a)*2^26), folded to the
// fundamental domain a in [0,32768) (the upper half is the exact negation and carries no information).
// If the hardware evaluates a degree-3 polynomial per node, E is that node's approximation error:
// small at the node CENTRE (where C0 is a table entry) and extreme at the node EDGES.  The period and
// phase of E therefore read the node split straight off the dump, with no model at all.
#include "base2.h"
#include <cstring>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 S26[65536]; static double EM[65536];
int main(){ if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ int q=a>>14,r=a&16383,neg=(q>=2),u=(q&1)?16384-r:r;
        i128 ang=PI_()*u/32768; S26[a]=rn_scale(sin_(ang),26)*(neg?-1:1);
        i64 lo,hi;bool ex; win_of(a,lo,hi,ex); EM[a]=0.5*(double)(lo+hi)-(double)S26[a]; }
    double mx=0; for(int a=0;a<32768;a++) mx=std::max(mx,std::fabs(EM[a]));
    printf("max |E| over a in [0,32768) = %.1f units of 2^-26\n\n",mx);
    printf("How much of E's variance is explained by the residue (a mod L)?  a in [0,32768)\n");
    for(int L : {128,256,512,1024,2048,4096,8192}){
        std::vector<double> s(L,0); std::vector<long long> c(L,0); double gs=0,gs2=0; long long gc=0;
        for(int a=0;a<32768;a++){ double e=EM[a]; s[a%L]+=e; c[a%L]++; gs+=e; gs2+=e*e; gc++; }
        double gm=gs/gc, tot=gs2/gc-gm*gm, bet=0;
        for(int r=0;r<L;r++) if(c[r]) { double m=s[r]/c[r]; bet+=(double)c[r]/gc*(m-gm)*(m-gm); }
        printf("  L=%5d  %6.2f %%\n",L,100.0*bet/tot); }
    printf("\nE averaged over nodes, as a function of position within a 1024-wide window,\n");
    printf("for two phases: CENTRED (t=a-1024n, n=round) and START-ALIGNED (u=a mod 1024)\n");
    printf("   pos      E(centred t)    E(start-aligned u)\n");
    { std::vector<double> sc(1024,0),ss(1024,0); std::vector<int> cc(1024,0),cs(1024,0);
      for(int a=0;a<32768;a++){ int t=((a+512)&1023)-512; sc[t+512]+=EM[a]; cc[t+512]++;
                                int u=a&1023; ss[u]+=EM[a]; cs[u]++; }
      for(int i=0;i<1024;i+=32) printf("  %+5d   %10.3f      (u=%4d) %10.3f\n",i-512,sc[i]/cc[i],i,ss[i]/cs[i]); }
    printf("\nE for three individual nodes, sampled (centred t):\n");
    for(int n : {5,10,20}){ printf("  node %d (centre angle %d):",n,n*1024);
        for(int t=-512;t<=511;t+=64) printf(" %+.1f",EM[(n*1024+t)&0xffff]); printf("\n"); }
    return 0; }
