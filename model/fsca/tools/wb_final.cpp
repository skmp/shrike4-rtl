// wb_final.cpp -- final end-to-end matrix for the fraction-feedback family, on top of the corrected chain
// operand order arr(dx,KC) (CURRENT.md 2026-09-18, baseline 147) and at both latch shifts.
//   even=0 separate:  T2 = sm(arr(J,S_n) + K, u2)              (23-bit normalised sine field)
//   even=1 merged:    E  = (arr(C0_n, 2^W - J) + Kr) >> W      (26-bit table field, fraction feedback)
//   shift: 20 (u2=30+k, W=33) or 18 (u2=32+k, W=35)
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int order=0;order<2;order++) for(int sh=20;sh>=18;sh-=2) for(int even=0;even<2;even++){
        int W=33+(20-sh), du=(20-sh);
        i64 Kr=(1ll<<W)-1-K;
        int grand=0,per[32]={0};
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 I1=order? ((arr(dx,KC)+K)>>23) : ((arr(KC,dx)+K)>>23);
                i64 J=(arr(I1,dx)+K)>>sh;
                i64 E;
                if(!T.S) E=C0FIT[n];
                else if(even==0) E=C0FIT[n]-sm(arr(J,T.S)+K,U2FIT[n]+du);
                else E=(arr(C0FIT[n],(1ll<<W)-J)+Kr)>>W;
                i64 V=E+s*T1-s*T3;
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi){grand++;per[node]++;} } }
        printf("order=%s shift=%d even=%s : violations %4d  guard(0,1,2,16)=%d,%d,%d,%d %s\n",
            order?"arr(dx,KC)":"arr(KC,dx)",sh,even?"MERGED  ":"separate",grand,per[0],per[1],per[2],per[16],
            (per[0]|per[1]|per[2]|per[16])?"":"GUARDS EXACT"); }
    return 0; }
