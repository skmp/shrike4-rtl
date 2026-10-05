// b23.cpp -- the complete required-asymmetry table D(|t|) = J(-t) - J(t), with the tightest bounds,
// plus the symmetric part.  Written to dtab.txt; the informative rows are printed.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static char OKT[1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh; }
    FILE*f=fopen("dtab.txt","w");
    int nz=0,npos=0,nneg=0,nfree=0;
    fprintf(f,"# |t|  Dlo  Dhi   JL(+t) JH(+t) JL(-t) JH(-t)  KC*t^2 (exact numerator over 2^17)\n");
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!OKT[ip]||!OKT[im]){ fprintf(f,"%d INFEAS\n",a); continue; }
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        fprintf(f,"%4d %5lld %5lld  %9lld %9lld %9lld %9lld  %lld\n",a,(long long)dlo,(long long)dhi,
            (long long)JL[ip],(long long)JH[ip],(long long)JL[im],(long long)JH[im],(long long)((i64)KC*a*a));
        if(dlo<=0&&dhi>=0){ if(dlo==0&&dhi==0) nz++; else nfree++; } else if(dlo>0) npos++; else nneg++; }
    fclose(f);
    printf("|t| with D pinned to 0: %d ; D forced >0: %d ; D forced <0: %d ; D unconstrained-ish: %d\n",nz,npos,nneg,nfree);
    puts("\nrows where D is confined to a width <= 4 window (the informative ones):");
    puts(" |t|  Dlo Dhi    t mod 8  t mod 16  t mod 32   J(+t) window       J(-t) window");
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip]; if(dhi-dlo>4) continue;
        printf("%4d %4lld %3lld     %2d      %3d       %3d     [%9lld,%9lld] [%9lld,%9lld]\n",a,(long long)dlo,(long long)dhi,
            a&7,a&15,a&31,(long long)JL[ip],(long long)JH[ip],(long long)JL[im],(long long)JH[im]); }
    return 0; }
