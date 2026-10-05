#include "base2.h"
#include <cstdio>
static i64 JL[1024],JH[1024];
static inline i64 shf(i64 v,int u,int m){ return m? sm(v,u):(v>>u); }
int main(){ build_tables(); FILE*f=fopen("jwin18.txt","r"); char ln[512];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h,w;
    if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&w)==4){JL[t+512]=l;JH[t+512]=h;} } fclose(f);
  int best=-1,bda=0,bdb=0,br=0,bm1=0,bm2=0,bo=0; long long n=0;
  for(int order=0;order<2;order++) for(int m1=0;m1<2;m1++) for(int m2=0;m2<2;m2++)
   for(int da=-4;da<=4;da++) for(int db=-4;db<=4;db++) for(int r=-8;r<=8;r++){ n++;
     int s=0; for(int t=-512;t<=511;t++){
       i64 dxa=(i64)(t+(t<0?da:0))<<13, dxb=(i64)(t+(t<0?db:0))<<13;
       i64 I1=order? shf(arr(dxa,KC)+K,23,m1) : shf(arr(KC,dxa)+K,23,m1);
       if(t<0)I1+=r; i64 j=shf(arr(I1,dxb)+K,18,m2);
       if(j>=JL[t+512]&&j<=JH[t+512])s++; }
     if(s>best){best=s;bda=da;bdb=db;br=r;bm1=m1;bm2=m2;bo=order;} }
  printf("sign-gated space: %lld configs, BEST %d/1024 at order=%d da=%+d db=%+d r=%+d conv=(%s,%s)\n",
    n,best,bo,bda,bdb,br,bm1?"sm":">>",bm2?"sm":">>");
  return 0; }
