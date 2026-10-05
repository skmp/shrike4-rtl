// wa_win.cpp -- independent recomputation of the allowed-J windows + the asymmetry dataset.
// Writes wa_jwin.txt (same format as jwin.txt) and prints the forced-asymmetry table.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
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
    FILE*f=fopen("wa_jwin.txt","w"); for(int t=-512;t<=511;t++) fprintf(f,"%d %lld %lld %d\n",t,(long long)JL[t+512],(long long)JH[t+512],(int)OKT[t+512]); fclose(f);
    // compare with jwin.txt
    FILE*g=fopen("jwin.txt","r"); int diff=0,nf=0; if(g){ for(int i=0;i<1024;i++){ int t,ok; long long a,b; if(fscanf(g,"%d %lld %lld %d",&t,&a,&b,&ok)!=4){printf("read fail %d\n",i);break;}
            if(t!=i-512||a!=JL[i]||b!=JH[i]||ok!=OKT[i])diff++; } fclose(g);} 
    for(int i=0;i<1024;i++) nf+=OKT[i];
    printf("feasible t: %d/1024 ; differences vs jwin.txt: %d\n",nf,diff);
    // widths
    int wmin=1<<30,wmax=0; long long wsum=0; for(int i=0;i<1024;i++) if(OKT[i]){int w=(int)(JH[i]-JL[i]); wsum+=w; if(w<wmin)wmin=w; if(w>wmax)wmax=w;}
    printf("window width: min %d max %d mean %.2f\n",wmin,wmax,(double)wsum/nf);
    // model J and its error
    int inside=0; for(int t=-512;t<=511;t++){ if(!OKT[t+512])continue; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23,J=(arr(I1,dx)+K)>>20;
        if(J>=JL[t+512]&&J<=JH[t+512])inside++; }
    printf("current chain inside: %d/%d\n",inside,nf);
    int inside2=0; for(int t=-512;t<=511;t++){ if(!OKT[t+512])continue; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23,J=(arr(I1,dx))>>20;
        if(J>=JL[t+512]&&J<=JH[t+512])inside2++; }
    printf("current chain, K2 dropped: %d/%d\n",inside2,nf);
    // asymmetry
    puts("\nforced asymmetry D=J(-t)-J(t): entries where 0 is NOT allowed, or where D is pinned to a single value");
    puts(" |t|   Dlo   Dhi   w(+t) w(-t)   D/|t|*2^7   ideal4pi2t2-JLmid(+)");
    std::vector<std::pair<i64,int>> forced;
    int nzero=0,npin=0;
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        if(dlo>0||dhi<0){ forced.push_back({dlo>0?dlo:-dhi,a}); }
        if(dlo<=0&&dhi>=0)nzero++;
        if(dlo==dhi)npin++; }
    printf("(t with 0 allowed: %d ; with D pinned to one value: %d ; forced nonzero: %zu)\n",nzero,npin,forced.size());
    std::sort(forced.begin(),forced.end(),[](auto&x,auto&y){return x.first>y.first;});
    for(size_t i=0;i<forced.size();i++){ int a=forced[i].second; int ip=a+512,im=-a+512;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        printf("%4d %5lld %5lld   %4lld %4lld   %8.4f\n",a,(long long)dlo,(long long)dhi,
            (long long)(JH[ip]-JL[ip]),(long long)(JH[im]-JL[im]),(double)dlo*128.0/a); }
    return 0; }
