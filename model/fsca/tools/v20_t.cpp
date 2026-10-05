// v20_t.cpp -- the user's parenthetical: is t the distance from the previous LUT ENTRY POINT, or
// the signed distance from the node CENTRE?  Decided from the raw dump with no model at all.
#include "base2.h"
#include <cstdio>
static Fsca F;
int main(){ if(!load_fsca(F)) return 1;
    // If 1024n is a node CENTRE, the hardware's curve must be (nearly) symmetric about it -- at
    // node 16 exactly so, because cos(pi/2)=0 kills the entire odd part.
    for(int n : {8,16,24}){
        int a0=n*1024, eq=0, le1=0; long long maxd=0;
        for(int t=1;t<=512;t++){ uint32_t x=F.srn[(a0+t)&0xffff], y=F.srn[(a0-t)&0xffff];
            if(x==y) eq++; long long d=(long long)x-(long long)y; if(d<0)d=-d; if(d<=1) le1++; if(d>maxd)maxd=d; }
        printf("node %2d (angle %5d): sin(a0+t)==sin(a0-t) bit-exact for %3d/512 t, |ulp diff|<=1 for %3d/512, max %lld\n",
               n,a0,eq,le1,maxd);
    }
    // the alternative reading: 1024n as a segment START.  Then the curve would be one-sided there,
    // i.e. the two half-segments about a segment BOUNDARY (angle 1024n+512) would match instead.
    for(int n : {8,16}){
        int a0=n*1024+512, eq=0; long long maxd=0;
        for(int t=1;t<=512;t++){ uint32_t x=F.srn[(a0+t)&0xffff], y=F.srn[(a0-t)&0xffff];
            if(x==y) eq++; long long d=(long long)x-(long long)y; if(d<0)d=-d; if(d>maxd)maxd=d; }
        printf("boundary %5d      : sin(a+t)==sin(a-t) bit-exact for %3d/512 t, max ulp diff %lld\n",a0,eq,maxd);
    }
    // and the direct fact: at angle 1024n the value is a pure table constant (b43) -- show the
    // internal window at t=0 is width<=2 for all 17 entries.
    int wide=0; for(int n=0;n<=16;n++){ Win w=decode(F.srz[n*1024],F.srn[n*1024]);
        if(w.hi-w.lo>2) wide++; }
    printf("t=0 internal windows wider than 2 units, over the 17 table entries: %d\n",wide);
    return 0; }
