// wa_bind.cpp -- which node instance binds each J window, and how fragile the forced asymmetry is.
#include "wa_common.h"
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
struct Cn { int node; i64 lo,hi; bool exact; };
static void constraints(int t,std::vector<Cn>&out){ out.clear(); i64 dx=(i64)t<<13;
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
        const Tab&T=TB[n]; i64 s=flip?-1:1;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
        i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
        out.push_back({node,a1,a2,ex}); } }
int main(){ if(!load_fsca(F))return 1; build_tables();
    int hard[]={342,411,447,277,469,251,40,376,258};
    for(int h:hard){ for(int sg=0;sg<2;sg++){ int t=sg?-h:h; std::vector<Cn> c; constraints(t,c);
        i64 gl=0,gh=12000000; int nl=-1,nh=-1;
        for(auto&x:c){ if(x.lo>gl){gl=x.lo;nl=x.node;} if(x.hi<gh){gh=x.hi;nh=x.node;} }
        // second tightest
        i64 gl2=0,gh2=12000000; for(auto&x:c){ if(x.node!=nl&&x.lo>gl2)gl2=x.lo; if(x.node!=nh&&x.hi<gh2)gh2=x.hi; }
        printf("t=%4d  J in [%8lld,%8lld]  lowbind node %2d (next %8lld, +%lld)  highbind node %2d (next %8lld, %+lld)\n",
            t,(long long)gl,(long long)gh,nl,(long long)gl2,(long long)(gl-gl2),nh,(long long)gh2,(long long)(gh2-gh)); } 
        puts(""); }
    // how many forced asymmetries survive if each single node is dropped?
    puts("forced-asymmetry count when one node instance is dropped:");
    std::vector<std::vector<Cn>> CC(1024); for(int t=-512;t<=511;t++) constraints(t,CC[t+512]);
    for(int drop=0;drop<=31;drop++){
        i64 JL[1024],JH[1024]; char ok[1024];
        for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
            for(auto&x:CC[t+512]){ if(x.node==drop)continue; if(x.lo>gl)gl=x.lo; if(x.hi<gh)gh=x.hi; }
            JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; }
        int nf=0,forced=0,maxd=0; for(int i=0;i<1024;i++)nf+=ok[i];
        for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!ok[ip]||!ok[im])continue;
            i64 dlo=JL[im]-JH[ip],dhi=JH[im]-JL[ip];
            if(dlo>0||dhi<0){forced++; int m=(int)(dlo>0?dlo:-dhi); if(m>maxd)maxd=m;} }
        printf("  drop node %2d: feasible %4d, forced-asymmetric |t| %3d, max forced D %d\n",drop,nf,forced,maxd); }
    return 0; }
