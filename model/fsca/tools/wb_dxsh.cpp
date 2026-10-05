// wb_dxsh.cpp -- the patent presents dx through an "8 bit shift and 6 bit sign expander", i.e. dx = t<<8 in
// a 24-bit field, not t<<13.  (a) does the linear term stay exact at node 0 with other shifts?
// (b) how does the array's sign asymmetry e(t) = -arr(A,-dx)-arr(A,dx) depend on the shift?
#include "wb_even.h"
#include <map>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts("(a) linear term at node 0 with dx = t<<g, unit shift u = 23-e1-(13-g):");
    for(int g=6;g<=16;g++){ int u=23-TB[0].e1-(13-g); if(u<0||u>40)continue; int bad=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<g; i64 T1=sm(arr(TB[0].C1f,dx)+K,u);
            i64 A3=((arr(((i64)t*t)<<3,(i64)t<<13)+K)>>20)>>3; i64 T3=sm(arr(TB[0].c3<<5,A3)+K,26);
            i64 V=T1-T3; int a=t&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex); if(V<lo||V>hi)bad++; }
        printf("   g=%2d u=%2d : %d/1024 misses %s\n",g,u,bad,bad==0?"<== exact":""); }
    puts("\n(b) e(t) = -arr(KC,-dx) - arr(KC,dx) for dx = t<<g  (distribution over t=1..512):");
    for(int g=6;g<=14;g++){ std::map<i64,int> h;
        for(int t=1;t<=512;t++){ i64 dx=(i64)t<<g; h[-arr(KC,-dx)-arr(KC,dx)]++; }
        printf("   g=%2d : ",g); int k=0; for(auto&kv:h){ printf("%lld x%d  ",(long long)kv.first,kv.second); if(++k>8){printf("... (%zu distinct)",h.size());break;} } printf("\n"); }
    puts("\n(c) same for the dx^2 product arr(dx,dx):");
    for(int g=6;g<=14;g++){ std::map<i64,int> h;
        for(int t=1;t<=512;t++){ i64 dx=(i64)t<<g; h[arr(-dx,-dx)-arr(dx,dx)]++; }
        printf("   g=%2d : ",g); int k=0; for(auto&kv:h){ printf("%lld x%d  ",(long long)kv.first,kv.second); if(++k>8){printf("... (%zu distinct)",h.size());break;} } printf("\n"); }
    return 0; }
