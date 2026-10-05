// wf_a.cpp -- regenerate the J (square-latch) interval dataset and characterise it exactly.
// (1) pinned count + width histogram, (2) pinned J vs exact KC*t^2/2^17, (3) dev(t) intervals,
// (4) +t/-t disjointness, (5) the exact algebraic decomposition of dev for the current chain.
#include "base2.h"
#include <cstring>
#include <map>
#include <vector>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
i64 JL[1024],JH[1024]; char OKT[1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh; }
    {FILE*f=fopen("wf_jwin.txt","w"); for(int t=-512;t<=511;t++) fprintf(f,"%d %lld %lld %d\n",t,(long long)JL[t+512],(long long)JH[t+512],(int)OKT[t+512]); fclose(f);}
    // cross-check against the existing jwin.txt
    {FILE*f=fopen("jwin.txt","r"); int diff=0; if(f){int t;long long a,b;int o; while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){ if(a!=JL[t+512]||b!=JH[t+512]||o!=OKT[t+512])diff++; } fclose(f);} printf("lines differing from the committed jwin.txt: %d\n",diff);}
    // (1) pinned + width histogram
    int feas=0,pinned=0; std::map<i64,int> hist;
    for(int t=-512;t<=511;t++){ if(!OKT[t+512])continue; feas++; i64 w=JH[t+512]-JL[t+512]; if(w==0)pinned++;
        i64 b = w<=10?w : (w<=20?15 : (w<=50?30 : (w<=100?75 : (w<=200?150:999)))); hist[b]++; }
    printf("feasible t: %d, pinned to a single value: %d\n",feas,pinned);
    printf("width histogram (width 0..10 exact, then buckets 15=[11,20] 30=[21,50] 75=[51,100] 150=[101,200] 999=>200):\n");
    for(auto&p:hist) printf("  w=%-4lld  %d\n",(long long)p.first,p.second);
    // (2) pinned values vs exact rational
    printf("\npinned t: J, KC*t*t (=J*2^17 target), dev=J*2^17-KC*t*t\n");
    int shown=0;
    for(int t=-512;t<=511;t++){ if(!OKT[t+512]||JH[t+512]!=JL[t+512])continue; i64 J=JL[t+512];
        i64 dev=J*131072-KC*(i64)t*t; if(shown<40) printf("  t=%4d J=%8lld dev=%9lld  dev/2^17=%+7.3f\n",t,(long long)J,(long long)dev,(double)dev/131072.0);
        shown++; }
    printf("  (%d pinned total)\n",shown);
    // (3) dev interval extremes
    i64 dmin=1ll<<60,dmax=-(1ll<<60);
    for(int t=-512;t<=511;t++){ if(!OKT[t+512])continue; i64 lo=JL[t+512]*131072-KC*(i64)t*t, hi=JH[t+512]*131072-KC*(i64)t*t;
        if(lo<dmin)dmin=lo; if(hi>dmax)dmax=hi; }
    printf("\ndev over all feasible t: allowed-lo min %lld (%.3f *2^17), allowed-hi max %lld (%.3f *2^17)\n",(long long)dmin,(double)dmin/131072.0,(long long)dmax,(double)dmax/131072.0);
    // (4) +t/-t disjointness
    int disj=0,bothfeas=0; std::vector<int> dl;
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!OKT[ip]||!OKT[im])continue; bothfeas++;
        if(JH[ip]<JL[im]||JH[im]<JL[ip]){disj++; if((int)dl.size()<60)dl.push_back(a);} }
    printf("\n|t| with both sides feasible: %d, with DISJOINT J intervals: %d\n",bothfeas,disj);
    printf("first disjoint |t|: "); for(int a:dl) printf("%d ",a); printf("\n");
    int sameint=0; for(int a=1;a<=511;a++){int ip=a+512,im=-a+512; if(!OKT[ip]||!OKT[im])continue; if(JL[ip]==JL[im]&&JH[ip]==JH[im])sameint++;}
    printf("|t| where the two intervals are literally identical: %d\n",sameint);
    // (5) exact decomposition of dev for the current chain: dev = 64t + 65536 - t*(e1+r1)/8192 - e2/8 - r2/8
    printf("\ncurrent chain audit (exact integers):\n");
    int bad=0, inwin=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 A1=arr(KC,dx); i64 I1=(A1+K)>>23; i64 r1=(A1+K)-(I1<<23); i64 e1=KC*dx-A1;
        i64 A2=arr(I1,dx); i64 I2=(A2+K)>>20; i64 r2=(A2+K)-(I2<<20); i64 e2=I1*dx-A2;
        // identity check: 8*(2^17*I2) == 8*KC*t^2 + 512t + 2^19 - t*(e1+r1)/1024 - e2 - r2   (all times 8)
        i64 lhs = 8*(131072*I2);
        i64 rhs = 8*KC*(i64)t*t + 512*(i64)t + (1ll<<19) - (i64)t*(e1+r1)/1024 - e2 - r2;
        if(((i64)t*(e1+r1))%1024!=0){ if(bad<3)printf("  t=%d: (e1+r1)*t not divisible by 1024 (e1=%lld r1=%lld)\n",t,(long long)e1,(long long)r1); }
        if(lhs!=rhs){ if(bad<5) printf("  IDENTITY MISMATCH t=%d lhs=%lld rhs=%lld\n",t,(long long)lhs,(long long)rhs); bad++; }
        if(OKT[t+512]&&I2>=JL[t+512]&&I2<=JH[t+512])inwin++;
    }
    printf("  identity mismatches: %d ; current chain inside window for %d of %d feasible t\n",bad,inwin,feas);
    // distribution of e1, e2 (array losses) and r1
    {std::map<i64,int> he1,he2; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 A1=arr(KC,dx); i64 I1=(A1+K)>>23; i64 A2=arr(I1,dx);
        he1[KC*dx-A1]++; he2[I1*dx-A2]++; }
     printf("  distinct e1 values: %zu  distinct e2 values: %zu\n",he1.size(),he2.size());
     printf("  e1 values:"); int c=0; for(auto&p:he1){ if(c++<12) printf(" %lld(x%d)",(long long)p.first,p.second);} printf("\n");
     printf("  e2 values:"); c=0; for(auto&p:he2){ if(c++<12) printf(" %lld(x%d)",(long long)p.first,p.second);} printf("\n"); }
    return 0; }
