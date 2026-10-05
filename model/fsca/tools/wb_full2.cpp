// wb_full2.cpp -- end-to-end model over nodes 0..31 for the variants this session produced.
//  rule 0  current chain (baseline 148)
//  rule 1  chain with K2 dropped
//  rule 2  best chain from wb_chain2: s1=25 s2=20 A=20694294 K1=-3276800, stage2 sm
//  rule 3  best chain: s1=24 s2=20 A=10347147 K1=-1703936 role1=1
//  rule 4  symmetric single multiply arr(2t^2,2586787)>>17
// even-part form: 0 = separate 23-bit S (baseline), 1 = MERGED fraction feedback with the 26-bit table field
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static int RULE=0, EVEN=0;
static inline i64 Jof(int t){ i64 dx=(i64)t<<13;
    switch(RULE){
      case 0:{ i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
      case 1:{ i64 I1=(arr(KC,dx)+K)>>23; return arr(I1,dx)>>20; }
      case 2:{ i64 L=(arr(20694294,dx)-3276800)>>25; return sm(arr(L,dx),20); }
      case 3:{ i64 L=(arr(dx,10347147)-1703936)>>24; return (arr(L,dx))>>20; }
      case 4: return arr(((i64)t*t)<<1,2586787)>>17;
    } return 0; }
static inline i64 Eof(int n,i64 J){ const Tab&T=TB[n];
    if(!T.S) return C0FIT[n];
    if(EVEN==0) return C0FIT[n]-sm(arr(J,T.S)+K,U2FIT[n]);
    return (arr(C0FIT[n],(1ll<<33)-J)+((1ll<<33)-1-K))>>33; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    RULE=argc>1?atoi(argv[1]):0; EVEN=argc>2?atoi(argv[2]):0;
    int grand=0,per[32]={0};
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 V=Eof(n,Jof(t))+s*T1-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            if(V<lo||V>hi){grand++;per[node]++;} } }
    printf("rule=%d even=%d : violations %4d   guard(0,1,2,16)=%d,%d,%d,%d %s\n",RULE,EVEN,grand,
        per[0],per[1],per[2],per[16],(per[0]|per[1]|per[2]|per[16])?"":"GUARDS EXACT");
    return 0; }
