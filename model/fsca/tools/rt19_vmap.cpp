// rt19_vmap.cpp -- where, in (ROM word, t), does the model leave the hardware window, in which DIRECTION, and how far is
// the model's square product from its truncation boundary there?   frac = fractional part of (arr(J,S)+K)/2^u.
// "hw>" = hardware value above the model (T2 must be smaller), "hw<" = below.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables();
    static i64 Jc[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; }
    int up=0,dn=0; double sfu=0,sfd=0; int hist_up[10]={0},hist_dn[10]={0};
    for(int n=3;n<=15;n++){ const Tab&T=TB[n]; printf("word %2d:",n);
        for(int i=0;i<2;i++){ int m=i?32-n:n; int flip=m>=17; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 P=arr(Jc[t+512],T.S)+K; i64 T2=P>>T.u2; double fr=(double)(P&((1ll<<T.u2)-1))/(double)(1ll<<T.u2);
                int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;}
                i64 V=C0TAB[n]+s*T1-s*T3-T2; if(V>=lo&&V<=hi)continue;
                bool hwup = V<lo; if(hwup){up++;sfu+=fr;hist_up[(int)(fr*10)]++;} else {dn++;sfd+=fr;hist_dn[(int)(fr*10)]++;}
                printf(" %s%d%s(%.3f)",i?"m":"",t,hwup?"^":"v",fr); } }
        puts(""); }
    printf("\nhardware ABOVE model (T2 too big): %d, mean frac of the square product %.3f   |  hardware BELOW (T2 too small): %d, mean frac %.3f\n",up,sfu/up,dn,sfd/dn);
    printf("frac histogram (tenths) hw^ :"); for(int i=0;i<10;i++)printf(" %d",hist_up[i]); printf("\n                        hw v :"); for(int i=0;i<10;i++)printf(" %d",hist_dn[i]); puts("");
    return 0; }
