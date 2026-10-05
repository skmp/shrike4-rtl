// w3l_order.cpp -- operand-order baselines at the COARSE published scale, and the best complement-family
// variant measured at both scales.  (Sequenced calls: no unspecified-evaluation-order traps.)
#include "w3_common.h"
static void show(const char* tag, i64(*f)(int)){ int g; int v=full_violations(f,&g);
    printf("  %-44s %4d violations, guard(0,1,2,16) %d\n", tag, v, g); }
int main(){ if(!load_fsca(FD))return 1; build_tables();
    DU=0;
    { auto f=[](int t)->i64{ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23; return (arr(I1,dx)+K)>>20; };
      int g; int v=full_violations(f,&g); printf("COARSE arr(dx,KC) [published 147]      : %d, guard %d\n",v,g); }
    { auto f=[](int t)->i64{ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
      int g; int v=full_violations(f,&g); printf("COARSE arr(KC,dx) [published 148]      : %d, guard %d\n",v,g); }
    { auto f=[](int t)->i64{ i64 dxa=((i64)t<<13)+(t<0?1:0), dxb=(i64)t<<13; i64 I1=(arr(dxa,KC)+K)>>23; return (arr(I1,dxb)+K)>>20; };
      int g; int v=full_violations(f,&g); printf("COARSE arr(dx,KC), dx+1 for t<0 at mul1: %d, guard %d\n",v,g); }
    { auto f=[](int t)->i64{ i64 dxa=((i64)t<<13)+(t<0?1:0), dxb=(i64)t<<13; i64 I1=(arr(KC,dxa)+K)>>23; return (arr(I1,dxb)+K)>>20; };
      int g; int v=full_violations(f,&g); printf("COARSE arr(KC,dx), dx+1 for t<0 at mul1: %d, guard %d\n",v,g); }
    DU=2;
    { auto f=[](int t)->i64{ i64 dxa=((i64)t<<13)+(t<0?1:0), dxb=(i64)t<<13; i64 I1=(arr(KC,dxa)+K)>>23; return (arr(I1,dxb)+K)>>18; };
      int g; int v=full_violations(f,&g); printf("FINE   arr(KC,dx), dx+1 for t<0 at mul1: %d, guard %d\n",v,g); }
    { auto f=[](int t)->i64{ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; };
      int g; int v=full_violations(f,&g); printf("FINE   baseline                        : %d, guard %d\n",v,g); }
    return 0; }
