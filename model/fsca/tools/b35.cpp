// b35.cpp -- independent check of the emitted ../fsca_ref2.h against the dump (compile it standalone).
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
int main(){ Fsca F; if(!load_fsca(F)) return 1; int rn=0,rz=0,wv=0,cr=0,cz=0;
    for(int a=0;a<65536;a++){ uint32_t s0=fsca2_sin(a,0), s1=fsca2_sin(a,1);
        if(s0!=F.srn[a])rn++; if(s1!=F.srz[a])rz++;
        if(fsca2_cos(a,0)!=F.crn[a])cr++; if(fsca2_cos(a,1)!=F.crz[a])cz++;
        Win w=decode(F.srz[a],F.srn[a]); int64_t V=fsca2_internal(a); int64_t lo,hi;
        if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} if(V<lo||V>hi)wv++; }
    printf("fsca_ref2.h standalone: sin RN %d RZ %d, cos RN %d RZ %d, window violations %d\n",rn,rz,cr,cz,wv);
    return 0; }
