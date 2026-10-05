// f2_gran.cpp -- the array's output granularity.  arr() accumulates (row>>16)<<16 plus hot ones at 2^(2k>=18)
// plus the two 2^16 group terms, so every value it returns is a multiple of 2^16.  Consequence: in
// J = (ARR + K2) >> s2 only the part of K2 at or above bit 16 can change anything, which is why every
// admissible rounding-constant set in f2_k2_table.txt has a size that is a multiple of 65536.
#include "base2.h"
int main(){ build_tables(); long n=0,bad=0;
  for(int g=8;g<=14;g++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<g;
    for(i64 v=-4000;v<=4000;v++){ i64 a=arr(v,dx), b=arr(dx,v); n+=2; if(a&0xffff)bad++; if(b&0xffff)bad++; }
    for(i64 v=5173000;v<=5174000;v++){ i64 a=arr(v,dx); n++; if(a&0xffff)bad++; } }
  printf("arr() outputs sampled: %ld ; not a multiple of 2^16: %ld\n",n,bad);
  printf("-> the second latch can only be moved by K2 >> 16; K2 mod 2^16 is invisible.\n");
  return 0; }
