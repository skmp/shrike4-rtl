// we_d.cpp -- the COMPLETE required-asymmetry table D(|t|) = J(-t) - J(t), plus the array truncation
// losses of every intermediate, and the correlation between D and those losses.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int nf=0; for(int i=0;i<1024;i++) nf+=JOK[i];
    printf("feasible t: %d/1024\n",nf);
    // model J
    auto Jmodel=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
    int sc=0; for(int t=-512;t<=511;t++) if(JOK[t+512]&&Jmodel(t)>=JLo[t+512]&&Jmodel(t)<=JHi[t+512]) sc++;
    printf("current chain score: %d\n",sc);
    // complete D table
    int nboth=0,nforce_pos=0,nforce_neg=0,nzero_ok=0;
    printf("\n#### COMPLETE D TABLE  (a=|t|, Dlo=JL(-a)-JH(a), Dhi=JH(-a)-JL(a)); w+=JH-JL at +a, w-=at -a\n");
    printf("# a  Dlo  Dhi   w+   w-   e1sym  Dmodel  a%%4 a%%8\n");
    for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512; if(!JOK[ip]||!JOK[im]) continue; nboth++;
        i64 dlo=JLo[im]-JHi[ip], dhi=JHi[im]-JLo[ip];
        if(dlo>0)nforce_pos++; else if(dhi<0)nforce_neg++; else nzero_ok++;
        i64 dxp=(i64)a<<13, dxm=-((i64)a<<13);
        i64 e1p=arr(KC,dxp)-(i64)KC*dxp, e1m=arr(KC,dxm)-(i64)KC*dxm;
        i64 dm=Jmodel(-a)-Jmodel(a);
        printf("%4d %5lld %5lld %4lld %4lld  %s %6lld  %d %d\n",a,(long long)dlo,(long long)dhi,
            (long long)(JHi[ip]-JLo[ip]),(long long)(JHi[im]-JLo[im]), e1p==e1m?"eq ":"NEQ",(long long)dm,a%4,a%8);
    }
    printf("\nboth-feasible |t|: %d ; D forced >0: %d ; D forced <0: %d ; D=0 allowed: %d\n",
        nboth,nforce_pos,nforce_neg,nzero_ok);
    // the informative subset: where D is pinned to a narrow range
    printf("\n#### |t| where D is FORCED (interval excludes 0)\n# a Dlo Dhi  e1p e1m  e1m-e1p  a%%4\n");
    for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512; if(!JOK[ip]||!JOK[im]) continue;
        i64 dlo=JLo[im]-JHi[ip], dhi=JHi[im]-JLo[ip]; if(dlo<=0&&dhi>=0) continue;
        i64 dxp=(i64)a<<13, dxm=-((i64)a<<13);
        i64 e1p=arr(KC,dxp)-(i64)KC*dxp, e1m=arr(KC,dxm)-(i64)KC*dxm;
        printf("%4d %5lld %5lld  %9lld %9lld %9lld  %d\n",a,(long long)dlo,(long long)dhi,
            (long long)e1p,(long long)e1m,(long long)(e1m-e1p),a%4);
    }
    return 0; }
