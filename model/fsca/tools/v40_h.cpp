// v40_h.cpp -- what IS t?  The user asks whether t is "the distance from the last LUT entry point".
// Model-free check from the raw dump: node 16 has cos(pi/2)=0, so the odd (linear+cubic) part vanishes
// and the dumped value IS the even part.  If t were an UNSIGNED distance from the previous entry, the
// square path would see the same operand magnitudes on both sides of a node and could not be asymmetric.
#include "base2.h"
#include <cstdio>
int main(){ Fsca F; if(!load_fsca(F)){ printf("no dump\n"); return 1; }
    for(int t : {385,-385,17,-17,342,-342}){ int ang=16*1024+t;
        Win w=decode(F.srz[ang],F.srn[ang]);
        printf("  node16 t=%+4d angle %5d  RN %08x RZ %08x -> internal value in [%lld,%lld]\n",
            t,ang,F.srn[ang],F.srz[ang],(long long)w.lo,(long long)w.hi); }
    int a=16*1024+385,b=16*1024-385; Win p=decode(F.srz[a],F.srn[a]), q=decode(F.srz[b],F.srn[b]);
    printf("  => |t|=385 at node 16: [%lld,%lld] vs [%lld,%lld] : %s\n",(long long)p.lo,(long long)p.hi,(long long)q.lo,(long long)q.hi,
        (p.hi<q.lo||q.hi<p.lo)?"DISJOINT -- the value at +t and -t genuinely differ, so the offset is SIGNED":"overlapping");
    return 0; }
