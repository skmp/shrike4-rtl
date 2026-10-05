// wb_full.cpp -- full model over nodes 0..31, pluggable J rule; reports window violations (baseline 148)
// and the guard-node counts.  Usage: wb_full <rule>
//   0 = current chain  1 = symmetric arr(2t^2,A)>>17  2 = floor(KC t^2/2^17)  3 = chain without K in stage 2
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static int RULE=0; static i64 AA=2586787;
static inline i64 Jof(int t){ i64 dx=(i64)t<<13;
    switch(RULE){
      case 0:{ i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
      case 1: return arr(((i64)t*t)<<1,AA)>>17;
      case 2: return (i64)KC*t*t>>17;
      case 3:{ i64 I1=(arr(KC,dx)+K)>>23; return arr(I1,dx)>>20; }
    } return 0; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    RULE=argc>1?atoi(argv[1]):0; if(argc>2)AA=atoll(argv[2]);
    int grand=0; int per[32]={0};
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 T2=T.S? sm(arr(Jof(t),T.S)+K,U2FIT[n]):0;
            i64 V=C0FIT[n]+s*T1-T2-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            if(V<lo||V>hi){grand++;per[node]++;} } }
    printf("rule %d (A=%lld): total window violations over nodes 0..31 = %d\n",RULE,(long long)AA,grand);
    printf("per node:"); for(int i=0;i<32;i++) printf(" %d:%d",i,per[i]); printf("\n");
    printf("guard nodes 0,1,2,16: %d %d %d %d  -> %s\n",per[0],per[1],per[2],per[16],
        (per[0]==0&&per[1]==0&&per[2]==0&&per[16]==0)?"EXACT":"BROKEN");
    return 0; }
