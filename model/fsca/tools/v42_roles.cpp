#include "base2.h"
#include <cstdio>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){FILE*f=fopen("jwin18.txt","r");char ln[256];while(fgets(ln,sizeof ln,f)){if(ln[0]=='#')continue;long long t,l,h;
 if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;}}fclose(f);}
int main(){build_tables();load();
 for(int r1=0;r1<2;r1++)for(int r2=0;r2<2;r2++){
   int sc=0; i64 j17=0,j342=0,jm342=0;
   for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
     i64 I1=((r1? arr(dx,KC):arr(KC,dx))+K)>>23;
     i64 J =((r2? arr(dx,I1):arr(I1,dx))+K)>>18;
     int i=t+512; if(have[i]&&JL[i]<=J&&J<=JH[i])sc++;
     if(t==17)j17=J; if(t==342)j342=J; if(t==-342)jm342=J; }
   printf("rec1=%s rec2=%s  score %4d/1024  J(17)=%lld J(342)=%lld J(-342)=%lld\n",
     r1?"dx":"KC", r2?"dx":"I1", sc,(long long)j17,(long long)j342,(long long)jm342); }
 return 0;}
