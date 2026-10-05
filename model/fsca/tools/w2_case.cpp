// w2_case.cpp -- exhibit the real obstruction found by branch 2.  At some alignments ONE stored
// constant KCw reaches J(17)=45617 and ONE stored constant reaches J(342)=18466836 -- the same KCw --
// but only if the two latches are allowed DIFFERENT rounding constants at the two offsets.  Since K1
// and K2 are wires, not per-sample values, that is not available to the hardware.  This prints the
// latch values, the breakpoints and the selection each landmark demands, for the closest cases.
#include "base2.h"
#include <cstdio>
#include <cmath>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void show(int w,int s1,int s2,i64 lo,i64 hi){
    i64 d17=(i64)17<<w, dp=(i64)342<<w;
    printf("\n=== w=%d s1=%d s2=%d, KCw in [%lld,%lld] ===\n",w,s1,s2,(long long)lo,(long long)hi);
    for(i64 kc=lo;kc<=hi;kc++){
        i64 P17=arr(kc,d17), Pp=arr(kc,dp);
        i64 v17=P17>>s1, vp=Pp>>s1;
        i64 b17=((i64)1<<s1)-(P17-(v17<<s1)), bp=((i64)1<<s1)-(Pp-(vp<<s1));
        int need17=-1, needp=-1, e17=-1, ep=-1;
        for(int c=0;c<2;c++){ i64 Q=arr(v17+c,d17), q=Q>>s2;
            if(q==JL[529]){need17=c;e17=0;} else if(q+1==JL[529]){need17=c;e17=1;} }
        for(int c=0;c<2;c++){ i64 Q=arr(vp+c,dp), q=Q>>s2;
            if(q>=JL[854]&&q<=JH[854]){needp=c;ep=0;} else if(q+1>=JL[854]&&q+1<=JH[854]){needp=c;ep=1;} }
        if(need17<0||needp<0) continue;
        printf("KCw=%lld :\n",(long long)kc);
        printf("   t= 17 : first latch base %lld, steps at K1=%lld ; t=17 needs I1 = base+%d\n",
            (long long)v17,(long long)b17,need17);
        printf("   t=342 : first latch base %lld, steps at K1=%lld ; t=342 needs I1 = base+%d\n",
            (long long)vp,(long long)bp,needp);
        bool k1ok;
        if(need17==0&&needp==0) k1ok=true;
        else if(need17==1&&needp==1) k1ok=(b17<((i64)1<<s1))&&(bp<((i64)1<<s1));
        else if(need17==1&&needp==0) k1ok=(b17<bp)&&(b17<((i64)1<<s1));
        else k1ok=(bp<b17)&&(bp<((i64)1<<s1));
        i64 Q17=arr(v17+need17,d17), Qp=arr(vp+needp,dp);
        i64 q17=Q17>>s2, qp=Qp>>s2;
        i64 k17=((i64)1<<s2)-(Q17-(q17<<s2)), kp=((i64)1<<s2)-(Qp-(qp<<s2));
        bool k2ok;
        if(e17==0&&ep==0) k2ok=true;
        else if(e17==1&&ep==1) k2ok=(k17<((i64)1<<s2))&&(kp<((i64)1<<s2));
        else if(e17==1&&ep==0) k2ok=(k17<kp)&&(k17<((i64)1<<s2));
        else k2ok=(kp<k17)&&(kp<((i64)1<<s2));
        printf("   second latch: t=17 needs +%d (steps at K2=%lld), t=342 needs +%d (steps at K2=%lld)\n",
            e17,(long long)k17,ep,(long long)kp);
        printf("   -> one common K1 possible: %s ; one common K2 possible: %s  => JOINT: %s\n",
            k1ok?"yes":"NO",k2ok?"yes":"NO",(k1ok&&k2ok)?"REACHABLE":"impossible");
    } }
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    printf("need J(17)=%lld and J(342)=%lld\n",(long long)JL[529],(long long)JL[854]);
    show(5,12,14,10347090,10347120);
    show(6,15,14,20694230,20694260);
    show(8,17,14,5173550,5173575);
    return 0; }
