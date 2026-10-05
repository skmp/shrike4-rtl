// e4_admissible.cpp -- DIRECTION 2, step 4.  Every structure search so far assumed ONE array model.  But the
// array's behaviour outside FIPR's domain is not pinned: the negative-multiplicand row-sign rule admits at
// least two variants that both preserve FIPR and both keep nodes 0/32 exact, and the negative-RECODED-operand
// rule is constrained by nothing at all.  Enumerate the admissible array models, and for each redo the chain
// scan -- does any of them make the pinned offsets reachable?
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024];
// mc = negative-multiplicand row-sign rule (0 digit-sign, 1 XOR of both signs); both preserve FIPR
static i64 arrM(i64 rec,i64 mul,int mc){ i64 R[14]; i64 T=0; int negm=mul<0; i64 amul=negm?-mul:mul;
    for(int k=0;k<14;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag,row; int rowneg;
        if(mc==0){ mag=(i64)(d<0?-d:d)*mul; row=(d<0)?~mag:mag; rowneg=(d<0); }
        else { mag=(i64)(d<0?-d:d)*amul; rowneg=((d<0)^negm)&&d!=0; row=rowneg?~mag:mag; }
        R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16; if(rowneg&&2*k>=18) T+=1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2);
    int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16; return T; }
// rc = negative-recoded-operand rule; constrained by nothing
static inline i64 A(i64 rec,i64 mul,int mc,int rc){
    if(rec>=0||rc==0) return arrM(rec,mul,mc);
    if(rc==1) return -arrM(-rec,mul,mc);
    if(rc==2) return ~arrM(-rec,mul,mc);
    { int sm2=mul<0; i64 p=arrM(-rec, sm2?-mul:mul, mc); return sm2? p : -p; } }
int main(){ if(!load_fsca(F))return 1; build_tables();
    // build the latch windows at the FINE scale with the CURRENT array (they depend only on the final multiply)
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=48000000;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+2;
            i64 a1=0,b1=48000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,TB[n].S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=48000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,TB[n].S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; }
    puts("mc rc | node0+32 | best full score | best jwin18 | hits {17, 342, -342} | structure");
    for(int mc=0;mc<2;mc++) for(int rc=0;rc<4;rc++){
        // guard: nodes 0 and 32 must stay exact (they never use a negative rec, so rc is free there)
        int g=0;
        for(int half=0;half<2;half++){ int node=half?32:0; int n=0; i64 s=half?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=sm(A(TB[n].C1f,dx,mc,rc)+K,23-TB[n].e1);
                i64 A3=((A(((i64)t*t)<<3,dx,mc,rc)+K)>>20)>>3; i64 T3=sm(A(TB[n].c3<<5,A3,mc,rc)+K,26);
                i64 V=s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi) g++; } }
        int bestfull=1<<30, bestj=-1, besthit=-1; int bs1=0,bk1=0,bk2=0;
        for(int s1=20;s1<=27;s1++) for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++){
            i64 K1=k1?(1ll<<19):0, K2=k2?(1ll<<19):0;
            int s2f=41-s1, s2c=43-s1; if(s2f<8||s2c<8) continue;
            auto Jf=[&](int t){ i64 dx=(i64)t<<13; i64 I=(A(dx,KC,mc,rc)+K1)>>s1; return (A(I,dx,mc,rc)+K2)>>s2f; };
            int jw=0; for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i])continue; i64 j=Jf(t); if(j>=JL[i]&&j<=JH[i])jw++; }
            int hits=(Jf(17)==JL[17+512])+(Jf(342)==JL[342+512])+(Jf(-342)>=JL[-342+512]&&Jf(-342)<=JH[-342+512]);
            // full model at the coarse scale (the published objective)
            int viol=0;
            for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
                for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                    i64 T1=TB[n].C1f? sm(A(TB[n].C1f,dx,mc,rc)+K,23-TB[n].e1):0;
                    i64 A3=((A(((i64)t*t)<<3,dx,mc,rc)+K)>>20)>>3; i64 T3=TB[n].c3? sm(A(TB[n].c3<<5,A3,mc,rc)+K,26):0;
                    i64 I=(A(dx,KC,mc,rc)+K1)>>s1; i64 J=(A(I,dx,mc,rc)+K2)>>s2c;
                    i64 T2=TB[n].S? sm(A(J,TB[n].S,mc,rc)+K,U2TAB[n]):0;
                    i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    if(V<lo||V>hi) viol++; } }
            if(jw>bestj){ bestj=jw; besthit=hits; }
            if(viol<bestfull){ bestfull=viol; bs1=s1; bk1=k1; bk2=k2; } }
        printf(" %d  %d |   %4d   |      %4d       |    %4d     |         %d            | s1=%d K1=%s K2=%s\n",
            mc,rc,g,bestfull,bestj,besthit,bs1,bk1?"2^19":"0",bk2?"2^19":"0"); }
    puts("\nrc: 0 = two's-complement rec (current) | 1 = magnitude, negate | 2 = magnitude, one's complement | 3 = both magnitudes");
    puts("mc: 0 = row negated by the digit's sign (current) | 1 = row negated by the XOR of both signs");
    puts("Both mc values preserve FIPR exactly and keep nodes 0/32 exact, so the array is NOT pinned there.");
    return 0; }
