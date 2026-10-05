#include "base2.h"
#include <cstdio>
static i64 JL[1024],JH[1024];
int main(){ build_tables(); FILE*f=fopen("jwin18.txt","r"); char ln[512];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h,w;
    if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&w)==4){JL[t+512]=l;JH[t+512]=h;} } fclose(f);
  printf("B(i) square path on jwin18: dx=(2t+e)<<12\n");
  for(int e=-4;e<=4;e++){ int s=0;
    for(int t=-512;t<=511;t++){ i64 dx=((i64)2*t+e)<<12; i64 I1=(arr(KC,dx)+K)>>23;
      i64 j=(arr(I1,dx)+K)>>18; if(j>=JL[t+512]&&j<=JH[t+512])s++; }
    printf("  e=%+d : %4d/1024%s\n",e,s,e==0?"  <- identical to baseline by construction":""); }
  return 0; }
