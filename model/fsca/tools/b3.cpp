// b3.cpp -- Decisive: is the middle-node residual a perturbation of the SHARED square-chain latch I2
// (which depends only on t, not on the node), or is it node-dependent?
// For every node instance (table n, cos-sign flip) and every t, find which integer offsets dI2 of the
// latch I2 make the model land inside the hardware window.  Then intersect over all node instances per t.
#include "base2.h"
#include <cstring>
#include <vector>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
#define RAD 24
int main(){ if(!load_fsca(F))return 1; build_tables();
    // per t: mask of allowed dI2 in [-RAD,RAD]
    std::vector<uint64_t> mask(1024, ~0ull);
    std::vector<int> nconstr(1024,0);
    int viol=0;
    for(int t=-512;t<=511;t++){
        i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, I2=(arr(I1,dx)+K)>>20;
        uint64_t m=~0ull;
        for(int node=1;node<=31;node++){
            int flip=node>=17, n=flip?32-node:node; if(!TB[n].S) continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi, t2hi=base-lo;                 // T2 must be in [t2lo,t2hi]
            uint64_t mm=0;
            for(int d=-RAD;d<=RAD;d++){ i64 T2=sm(arr(I2+d,T.S)+K,U2TAB[n]); if(T2>=t2lo&&T2<=t2hi) mm|=1ull<<(d+RAD); }
            m&=mm; nconstr[t+512]++;
            i64 T2_0=sm(arr(I2,T.S)+K,U2TAB[n]); if(T2_0<t2lo||T2_0>t2hi) viol++;
        }
        mask[t+512]=m;
    }
    // report
    int empty=0, zero_ok=0, needs_nonzero=0; std::vector<int> bad_t;
    for(int i=0;i<1024;i++){ uint64_t m=mask[i]; if(!m){empty++;bad_t.push_back(i-512);continue;}
        if(m&(1ull<<RAD)) zero_ok++; else needs_nonzero++; }
    printf("baseline violations (dI2=0) over nodes 1..31: %d\n",viol);
    printf("t values where SOME common dI2 works and dI2=0 works : %d\n",zero_ok);
    printf("t values where SOME common dI2 works but 0 does NOT  : %d\n",needs_nonzero);
    printf("t values with NO common dI2 in [-%d,%d]              : %d\n",RAD,RAD,empty);
    if(empty){ printf("first empty t:"); for(size_t i=0;i<bad_t.size()&&i<40;i++)printf(" %d",bad_t[i]); printf("\n"); }
    puts("\nt where a nonzero common offset is REQUIRED (t : allowed offsets):");
    int shown=0;
    for(int i=0;i<1024;i++){ uint64_t m=mask[i]; if(!m||(m&(1ull<<RAD)))continue;
        printf("  t=%4d :",i-512); for(int d=-RAD;d<=RAD;d++) if(m&(1ull<<(d+RAD))) printf(" %+d",d); printf("\n"); if(++shown>=80)break; }
    // also: the union over t of "0 not allowed" -- count per node instance to see which nodes drive it
    return 0; }
