// rt4_merged_res.cpp -- resolution bound on the patent's fraction-feedback ("1 - q") square product.
// The merged operand F = 2^W - q_W must fit the Booth recoder (< 2^27, q1_mono.cpp), so W <= 26, while the square
// term's value is q = T2/C0 = J / 2^33 (J = the coarse chain latch; CORRECTED 2026-09-18 night, an earlier version of
// this file said 2^29).  Hence the merged form can carry J only to multiples of 2^(33-W) >= 128 coarse units = 512
// fine units, whatever produces it.  Count the offsets whose jwin18 window
// contains NO such value: a lower bound on the offsets a merged form must violate even with a FREE quantised J.
#include <cstdio>
#include <cstring>
int main(){ FILE*f=fopen("jwin18.txt","r"); static long JL[1024],JH[1024]; char line[256]; int n=0; while(fgets(line,256,f)){ if(line[0]=='#')continue; int t; long a,b,w; if(sscanf(line,"%d %ld %ld %ld",&t,&a,&b,&w)==4){JL[t+512]=a;JH[t+512]=b;n++;} }
  printf("%d offsets\n",n);
  for(int g=0;g<=9;g++){ long q=1l<<g; int miss=0; for(int i=0;i<1024;i++){ long lo=JL[i],hi=JH[i]; long first=((lo+q-1)/q)*q; if(first>hi)miss++; }
    printf("J quantised to multiples of %3ld fine (= %5.2f coarse; recoder W = %2d): %4d of 1024 offsets unreachable\n",q,q/4.0,35-g,miss); }
  return 0; }
