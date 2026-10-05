// rt16_permtest.cpp -- are nodes 1, 2, 16 (and mirrors) ANOMALOUSLY clean?
// Truth proxy J_T(t) = the point of the jwin18 window nearest to the chain's J (so J_T satisfies every node).
// e(t) = J_chain - J_T is non-zero at 106 offsets.  Null hypothesis: the error is node-independent and its placement
// over t is unrelated to the nodes.  Randomise the placement (e applied at permuted offsets, sign kept) and count the
// violations that land on the guard nodes {1,2,16,30,31} and on all nodes.  Observed: 0 on the guard set.
#include "base2.h"
#include <random>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
static void score(const i64* J,int& total,int& guard,int& n16,int& n12){ total=guard=n16=n12=0; for(int m=1;m<32;m++){ const Tab&T=TB[TABN[m]]; bool g=(m<=2||m==16||m>=30);
    for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; i64 T2=sm(arr(J[i],T.S)+K,T.u2+2); i64 V=c.base-T2; if(V<c.lo||V>c.hi){ total++; if(g){guard++; if(m==16)n16++; else n12++;} } } } }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    FILE*f=fopen("jwin18.txt","r"); static long JL[1024],JH[1024]; char line[256]; while(fgets(line,256,f)){ if(line[0]=='#')continue; int t; long a,b,w; if(sscanf(line,"%d %ld %ld %ld",&t,&a,&b,&w)==4){JL[t+512]=a;JH[t+512]=b;} }
    static i64 Jm[1024],JT[1024],e[1024]; int nz=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18; Jm[t+512]=J; i64 jt=J; if(J<JL[t+512])jt=JL[t+512]; if(J>JH[t+512])jt=JH[t+512]; JT[t+512]=jt; e[t+512]=J-jt; nz+=e[t+512]!=0; }
    int tot,g,n16,n12; score(JT,tot,g,n16,n12); printf("truth proxy J_T: total %d (must be 0)\n",tot); score(Jm,tot,g,n16,n12); printf("chain (fine scale): total %d, guard %d (node16 %d, nodes 1/2/30/31 %d); %d offsets carry an error\n",tot,g,n16,n12,nz);
    std::mt19937 rng(12345); const int R=400; std::vector<int> G,T,N16,N12; int zero=0;
    for(int r=0;r<R;r++){ std::vector<int> idx(1024); for(int i=0;i<1024;i++)idx[i]=i; std::shuffle(idx.begin(),idx.end(),rng); static i64 J[1024]; for(int i=0;i<1024;i++){ J[i]=JT[i]+e[idx[i]]; if(J[i]<0)J[i]=0; }
        int a,b,c,d; score(J,a,b,c,d); T.push_back(a); G.push_back(b); N16.push_back(c); N12.push_back(d); zero+=b==0; }
    auto stat=[&](std::vector<int>&v,const char*nm){ std::sort(v.begin(),v.end()); double s=0; for(int x:v)s+=x; printf("  %-28s mean %6.2f  median %3d  5%%..95%% %3d..%3d  min %d\n",nm,s/v.size(),v[v.size()/2],v[v.size()/20],v[v.size()*19/20],v[0]); };
    printf("same errors at RANDOM offsets (%d permutations):\n",R); stat(T,"total violations"); stat(G,"guard-set violations"); stat(N16,"  of which node 16"); stat(N12,"  of which nodes 1,2,30,31");
    printf("  permutations with a perfectly clean guard set: %d of %d\n",zero,R);
    return 0; }
