// wa_sfit.cpp -- is the required +t/-t asymmetry of J real, or an artefact of the assumed table
// sine field S?  S is a STORED 23-bit table value like C0, so letting it move a few steps is
// legitimate.  Tests:
//   A  current chain J, (S,C0,u2) free per table entry      -> violations
//   B  free EVEN J(|t|), (S,C0,u2) free per table entry     -> violations   (alternating)
//   C  free J(t),        (S,C0,u2) at the b2 values         -> violations   (control, = 2)
#include "wa_common.h"
#include <omp.h>
static Fsca F;
static const i64 C0B[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2B[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// base_{m,t} = C0 + s*T1 - s*T3 (everything but the square term), and the window
static i64 BASE[32][1024], WLO[32][1024], WHI[32][1024];
static i64 JT[1024];
static i64 Sv[17], C0v[17]; static int U2v[17];
static inline int entry_of(int m){ return m<=16? m : 32-m; }
static int viol_entry(int n,i64 S,i64 C0,int u2){ int v=0;
    int ms[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
    for(int i=0;i<nn;i++){ int m=ms[i]; if(m>31) continue;
        for(int t=-512;t<=511;t++){ i64 T2 = S? sm(arr(JT[t+512],S)+K,u2) : 0;
            i64 V = C0 + BASE[m][t+512] - T2;
            if(V<WLO[m][t+512]||V>WHI[m][t+512]) v++; } }
    return v; }
static int total_viol(){ int s=0; for(int n=0;n<=16;n++) s+=viol_entry(n,Sv[n],C0v[n],U2v[n]); return s; }
// violations at one t over all node instances, for a candidate J
static inline int viol_at(int t,i64 J){ int v=0;
    for(int m=0;m<32;m++){ int n=entry_of(m); if(!Sv[n]) { i64 V=C0v[n]+BASE[m][t+512];
            if(V<WLO[m][t+512]||V>WHI[m][t+512])v++; continue; }
        i64 T2=sm(arr(J,Sv[n])+K,U2v[n]); i64 V=C0v[n]+BASE[m][t+512]-T2;
        if(V<WLO[m][t+512]||V>WHI[m][t+512])v++; }
    return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            BASE[m][t+512]=s*T1-s*T3; int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            WLO[m][t+512]=lo; WHI[m][t+512]=hi; } }
    for(int n=0;n<=16;n++){ Sv[n]=TB[n].S; C0v[n]=C0B[n]; U2v[n]=U2B[n]; }
    // ---------- A: current chain J, S/C0/u2 free
    Cfg cur{0x4ef145,13,23,0,0,0,0,13,20,0,0};
    for(int t=-512;t<=511;t++) JT[t+512]=Jof(cur,1ll<<19,1ll<<19,t);
    printf("A0 current chain, b2 table values: %d violations\n",total_viol());
    auto refit=[&](int verbose){ int tot=0;
        #pragma omp parallel for schedule(dynamic) reduction(+:tot)
        for(int n=0;n<=16;n++){ int bv=1<<30; i64 bS=Sv[n],bC=C0v[n]; int bU=U2v[n];
            i64 S0=TB[n].S;
            for(i64 S=(S0?std::max((i64)0,S0-24):0); S<=(S0?std::min((i64)(1ll<<23)-1,S0+24):0); S++)
              for(i64 C=C0B[n]-4;C<=C0B[n]+4;C++) for(int u=U2B[n]-1;u<=U2B[n]+1;u++){
                int v=viol_entry(n,S,C,u); if(v<bv){bv=v;bS=S;bC=C;bU=u;} }
            Sv[n]=bS;C0v[n]=bC;U2v[n]=bU; tot+=bv;
            if(verbose) printf("   entry %2d: S %+lld (=%lld), C0 %+lld, u2 %d -> %d\n",n,(long long)(bS-S0),(long long)bS,(long long)(bC-C0B[n]),bU,bv); }
        return tot; };
    printf("A1 current chain, (S,C0,u2) refitted: %d violations\n",refit(1)); fflush(stdout);
    // ---------- B: free EVEN J, alternating with the table refit
    for(int n=0;n<=16;n++){ Sv[n]=TB[n].S; C0v[n]=C0B[n]; U2v[n]=U2B[n]; }
    auto evenJ=[&](){ int tot=0;
        #pragma omp parallel for schedule(dynamic) reduction(+:tot)
        for(int a=0;a<=511;a++){ std::vector<i64> cd;
            for(int sgn=0;sgn<2;sgn++){ int t=sgn? -a : a; if(a==0&&sgn)continue;
                for(int m=0;m<32;m++){ int n=entry_of(m); if(!Sv[n])continue;
                    i64 lo=C0v[n]+BASE[m][t+512]-WHI[m][t+512], hi=C0v[n]+BASE[m][t+512]-WLO[m][t+512];
                    i64 x=0,y=12000001; while(x<y){i64 mm=(x+y)/2; if(sm(arr(mm,Sv[n])+K,U2v[n])>=lo)y=mm;else x=mm+1;} cd.push_back(x);
                    i64 p=-1,q=12000000; while(p<q){i64 mm=(p+q+1)/2; if(sm(arr(mm,Sv[n])+K,U2v[n])<=hi)p=mm;else q=mm-1;} cd.push_back(p);
                    cd.push_back(p+1); cd.push_back(x>0?x-1:0); } }
            cd.push_back(JT[a+512]); std::sort(cd.begin(),cd.end()); cd.erase(std::unique(cd.begin(),cd.end()),cd.end());
            int bv=1<<30; i64 bj=0;
            for(i64 j:cd){ if(j<0)continue; int v=viol_at(a,j)+(a? viol_at(-a,j):0); if(v<bv){bv=v;bj=j;} }
            JT[a+512]=bj; if(a) JT[-a+512]=bj; tot+=bv; }
        return tot; };
    int b=evenJ(); printf("B0 free even J, b2 table values: %d violations\n",b); fflush(stdout);
    for(int it=0;it<6;it++){ int r=refit(0); int e=evenJ();
        printf("B%d  refit -> %d ; even-J -> %d\n",it+1,r,e); fflush(stdout); }
    puts("final table deltas (even-J model):");
    for(int n=0;n<=16;n++) printf("   entry %2d: S %+lld (RN23=%lld -> %lld)  C0 %+lld  u2 %d\n",
        n,(long long)(Sv[n]-TB[n].S),(long long)TB[n].S,(long long)Sv[n],(long long)(C0v[n]-C0B[n]),U2v[n]);
    // ---------- C control: free J with the ORIGINAL table values
    for(int n=0;n<=16;n++){ Sv[n]=TB[n].S; C0v[n]=C0B[n]; U2v[n]=U2B[n]; }
    int tot=0;
    #pragma omp parallel for schedule(dynamic) reduction(+:tot)
    for(int t=-512;t<=511;t++){ std::vector<i64> cd;
        for(int m=0;m<32;m++){ int n=entry_of(m); if(!Sv[n])continue;
            i64 lo=C0v[n]+BASE[m][t+512]-WHI[m][t+512], hi=C0v[n]+BASE[m][t+512]-WLO[m][t+512];
            i64 x=0,y=12000001; while(x<y){i64 mm=(x+y)/2; if(sm(arr(mm,Sv[n])+K,U2v[n])>=lo)y=mm;else x=mm+1;} cd.push_back(x);
            i64 p=-1,q=12000000; while(p<q){i64 mm=(p+q+1)/2; if(sm(arr(mm,Sv[n])+K,U2v[n])<=hi)p=mm;else q=mm-1;} cd.push_back(p); }
        std::sort(cd.begin(),cd.end()); cd.erase(std::unique(cd.begin(),cd.end()),cd.end());
        int bv=1<<30; for(i64 j:cd){ if(j<0)continue; int v=viol_at(t,j); if(v<bv)bv=v; } tot+=bv; }
    printf("C  free per-t J, b2 table values: %d violations (control, expect 2)\n",tot);
    return 0; }
