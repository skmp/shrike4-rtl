// b22.cpp -- b17's chain inversion, but repeated under every convention for the FINAL square multiply
// (sine field width/rounding, operand roles, rounding constant).  Those conventions define the J dataset,
// so a chain that is impossible under one of them may be possible under another.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static char OKT[1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    printf("Svar r3 K3 | feasible t | best chain hits | structure\n");
    for(int Svar=0;Svar<4;Svar++) for(int r3=0;r3<2;r3++) for(int k3=0;k3<2;k3++){
        i64 SV[17]; int UV[17];
        for(int n=0;n<=16;n++){ if(!TB[n].S){SV[n]=0;UV[n]=30;continue;} int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
            i64 f23r=rn_scale(s,23+k), f24r=rn_scale(s,24+k);
            i64 f23t=(i64)((s<<(23+k))/QONE), f24t=(i64)((s<<(24+k))/QONE);
            switch(Svar){ case 0: SV[n]=std::min(f23r,(i64)((1ll<<23)-1)); UV[n]=30+k; break;
                          case 1: SV[n]=std::min(f23t,(i64)((1ll<<23)-1)); UV[n]=30+k; break;
                          case 2: SV[n]=std::min(f24r,(i64)((1ll<<24)-1)); UV[n]=31+k; break;
                          default:SV[n]=std::min(f24t,(i64)((1ll<<24)-1)); UV[n]=31+k; break; } }
        i64 K3v=k3?0:(1ll<<19);
        int feas=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!SV[n])continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 t2lo=base-hi,t2hi=base-lo; int u=UV[n]; i64 SS=SV[n];
                auto T2of=[&](i64 J){ i64 P=r3? arr(SS,J):arr(J,SS); return sm(P+K3v,u); };
                i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(T2of(m)>=t2lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(T2of(m)<=t2hi)a2=m;else b2=m-1;}
                if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh; feas+=OKT[t+512]; }
        // chain inversion (as b17)
        std::vector<i64> K2c; K2c.push_back(0); for(int j=0;j<=32;j++) K2c.push_back(1ll<<j);
        struct S2{int g2,r2,s2;size_t k2;}; std::vector<S2> s2c;
        for(int g2=8;g2<=14;g2++) for(int r2=0;r2<2;r2++) for(int s2=8;s2<=32;s2++)
            for(size_t k=0;k<K2c.size();k++){ if(K2c[k]&&K2c[k]>=(1ll<<s2))continue; s2c.push_back({g2,r2,s2,k}); }
        int bestH=-1; int bg1=0,bg2=0,bs1=0,bs2=0,br1=0,br2=0,bdk=0,bsg=0; i64 bK2=0,bK1l=0,bK1h=0;
        #pragma omp parallel for schedule(dynamic)
        for(size_t si=0;si<s2c.size();si++){ S2 c=s2c[si]; i64 K2=K2c[c.k2];
            std::vector<i64> il(1024),ih(1024); std::vector<char> okk(1024,0);
            for(int t=-512;t<=511;t++){ if(!OKT[t+512]||t==0) continue; i64 d2=(i64)t<<c.g2;
                auto Jof=[&](i64 v){ i64 P=c.r2? arr(d2,v):arr(v,d2); return (P+K2)>>c.s2; };
                i64 lim=6000000; bool inc=Jof(1000)>Jof(0); i64 a1,b1,a2,b2;
                if(inc){ a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)>=JL[t+512])b1=m;else a1=m+1;}
                         a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)<=JH[t+512])a2=m;else b2=m-1;} }
                else   { a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)<=JH[t+512])b1=m;else a1=m+1;}
                         a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)>=JL[t+512])a2=m;else b2=m-1;} }
                if(a1<=a2){il[t+512]=a1;ih[t+512]=a2;okk[t+512]=1;} }
            for(int g1=8;g1<=14;g1++){ int s1=g1+c.g2+17-c.s2; if(s1<6||s1>40)continue;
              for(int r1=0;r1<2;r1++) for(int sg=0;sg<2;sg++) for(int dk=-3;dk<=3;dk++){
                i64 kc=(sg?-1:1)*(0x4ef145+dk); std::vector<std::pair<i64,int>> ev; ev.reserve(2048);
                for(int t=-512;t<=511;t++){ if(!okk[t+512]||t==0)continue; i64 d1=(i64)t<<g1;
                    i64 P1=r1? arr(d1,kc):arr(kc,d1);
                    ev.push_back({il[t+512]*(1ll<<s1)-P1,1}); ev.push_back({(ih[t+512]+1)*(1ll<<s1)-P1,-1}); }
                std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bl=0,bh=0;
                for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best&&i+1<ev.size()){best=cur;bl=ev[i].first;bh=ev[i+1].first-1;} }
                #pragma omp critical
                if(best>bestH){ bestH=best; bg1=g1;bg2=c.g2;bs1=s1;bs2=c.s2;br1=r1;br2=c.r2;bdk=dk;bsg=sg;bK2=K2;bK1l=bl;bK1h=bh; } } } }
        printf("  %d   %d  %d  | %4d       | %4d           | g1=%d g2=%d s1=%d s2=%d r1=%d r2=%d kc=%06llx%s K2=%lld K1=[%lld,%lld]\n",
            Svar,r3,k3,feas,bestH,bg1,bg2,bs1,bs2,br1,br2,(long long)(0x4ef145+bdk),bsg?" NEG":"",(long long)bK2,(long long)bK1l,(long long)bK1h);
        fflush(stdout); }
    return 0; }
