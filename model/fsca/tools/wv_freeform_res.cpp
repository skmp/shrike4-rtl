// wv_freeform_res.cpp -- (1) SYMCAP done correctly (t=-512 is unpaired), (2) the latch-resolution claim (item 9).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static int OKT[1024];
struct Cons{ i64 lo,hi,S; int u; };
static std::vector<Cons> CON[1024];
static void build(int g){ for(int t=-512;t<=511;t++){ CON[t+512].clear(); i64 dx=(i64)t<<13;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; if(!T.S)continue;
        i64 s=flip?-1:1;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        CON[t+512].push_back({base-hi,base-lo,T.S,U2TAB[n]+g}); } } }
int main(){ if(!load_fsca(F))return 1; build_tables();
    { FILE*f=fopen("jwin.txt","r"); for(int i=0;i<1024;i++){int t,ok;long long a,b;
        if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&ok)!=4)return 1; JL[t+512]=a;JH[t+512]=b;OKT[t+512]=ok;} fclose(f); }
    // ---- SYMCAP, counting the unpaired t = -512
    { int cap=0,disj=0,pos=0;
      if(OKT[0]) cap++;                                   // t = -512 has no +512 partner: free
      if(OKT[512]) cap++;                                 // t = 0
      for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; bool p=OKT[ip],m=OKT[im];
        if(p&&m){ i64 lo=std::max(JL[ip],JL[im]),hi=std::min(JH[ip],JH[im]);
            if(lo<=hi)cap+=2; else {cap+=1;disj++; if(JL[im]>JH[ip])pos++;} }
        else cap += (p||m)?1:0; }
      printf("[D'] SYMCAP = %d / 1022   (disjoint pairs %d, all forcing J(-t)>J(t): %s)\n",
          cap,disj,pos==disj?"yes":"NO"); }
    // ---- latch resolution sweep
    for(int g=0; g<=3; g++){ build(g);
        int feas=0, floor_=0; i64 w17lo=-1,w17hi=-1;
        #pragma omp parallel for reduction(+:feas,floor_) schedule(dynamic)
        for(int i=0;i<1024;i++){ int t=i-512;
            i64 ideal=((KC*(i64)t*(i64)t)<<g)>>17; i64 lo=ideal-(6000ll<<g); if(lo<0)lo=0; i64 hi=ideal+(6000ll<<g);
            i64 fl=-1,fh=-1; int best=1<<30;
            for(i64 J=lo;J<=hi;J++){ int v=0;
                for(const Cons&c:CON[i]){ i64 T2=sm(arr(J,c.S)+K,c.u); if(T2<c.lo||T2>c.hi){ v++; if(v>=best)break; } }
                if(v<best)best=v; if(v==0){ if(fl<0)fl=J; fh=J; } }
            if(fl>=0)feas++; floor_+=best;
            if(t==17){ w17lo=fl; w17hi=fh; } }
        printf("[9] latch shift u2+%d : feasible t = %4d/1024 ; free-latch violation floor = %d ; window at t=+17 = [%lld,%lld]\n",
            g,feas,floor_,(long long)w17lo,(long long)w17hi); }
    return 0; }
