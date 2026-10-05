// we_rob.cpp -- robustness of the required sign asymmetry: can a different per-node sine field S_n (or u2_n)
// make the square latch SYMMETRIC?  Recompute the J windows with S_n shifted, node by node, and report
// the number of |t| whose +/- J windows are disjoint (45 at the baseline) and the even-J ceiling (977).
#include "we_win.h"
#include <cstring>
#include <algorithm>
static i64 SOV[17]; static int UOV[17];
static void windows(std::vector<i64>&JL,std::vector<i64>&JH,std::vector<char>&ok){
    JL.assign(1024,0);JH.assign(1024,0);ok.assign(1024,0);
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1; i64 S=SOV[n]; int u=UOV[n];
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; } }
static void stats(int&feas,int&forced,int&evenmax,int&bigD){ std::vector<i64>JL,JH;std::vector<char>ok; windows(JL,JH,ok);
    feas=0;for(int i=0;i<1024;i++)feas+=ok[i]; forced=0;evenmax=0;bigD=0;
    for(int a=0;a<=512;a++){ int ip=a+512,im=-a+512; bool okp=a<=511&&ok[ip],okm=ok[im];
        if(a==0){evenmax+=ok[512]?1:0;continue;}
        if(okp&&okm){ i64 lo=std::max(JL[ip],JL[im]),hi=std::min(JH[ip],JH[im]);
            if(lo<=hi)evenmax+=2; else {evenmax+=1;forced++; if(JL[im]-JH[ip]>2)bigD++;} }
        else if(okp||okm)evenmax+=1; } }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    for(int n=0;n<=16;n++){SOV[n]=TB[n].S;UOV[n]=U2TAB[n];}
    int f,fo,ev,bd; stats(f,fo,ev,bd); printf("baseline: feas=%d forcedD=%d evenMax=%d  (forced with D>2: %d)\n",f,fo,ev,bd);
    puts("\nper-node sine-field shift (one node at a time):");
    puts(" n  dS : feas forcedD evenMax bigD");
    for(int n=1;n<=16;n++){ i64 base=TB[n].S;
        for(int d=-4;d<=4;d++){ if(!d)continue; SOV[n]=base+d; stats(f,fo,ev,bd);
            if(fo<=45) printf("%2d %+3d : %4d %6d %7d %5d%s\n",n,d,f,fo,ev,bd, (fo<45||ev>977)?"  <-- improves":""); }
        SOV[n]=base; }
    puts("\nper-node u2 shift:");
    for(int n=1;n<=16;n++){ int base=U2TAB[n];
        for(int d=-1;d<=1;d++){ if(!d)continue; UOV[n]=base+d; stats(f,fo,ev,bd);
            printf("%2d %+3d : %4d %6d %7d %5d\n",n,d,f,fo,ev,bd); }
        UOV[n]=base; }
    return 0; }
