// wv_chain_alt.cpp -- checks the claim's "best physically defensible" variant, and re-prints the
// intermediates of the core-A counterexample step by step.
#include "base2.h"
#include <cstdio>
static Fsca F;
static i64 WL[1024],WH[1024]; static int WF[1024];
static inline void win_of(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
  if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} }
static i64 HLO[65536],HHI[65536];
static int end_to_end(const i64*Jt,int*guard){ int grand=0;
  for(int n=0;n<=16;n++){ const Tab&T=TB[n]; int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
    i64 T1v[1024],T3v[1024];
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      T1v[t+512]=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; T3v[t+512]=T.c3? sm(arr(T.c3<<5,A3)+K,26):0; }
    i64 lo0,hi0; win_of((n*1024)&0xffff,lo0,hi0); int bv=1<<30;
    for(i64 c=lo0-3;c<=hi0+3;c++) for(int u=T.u2-1;u<=T.u2+1;u++){ int v=0;
      i64 T2v[1024]; for(int t=-512;t<=511;t++) T2v[t+512]=T.S? sm(arr(Jt[t+512],T.S)+K,u):0;
      for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=m>=17; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff;
          i64 V=c+s*T1v[t+512]-T2v[t+512]-s*T3v[t+512]; if(V<HLO[a]||V>HHI[a]) v++; } }
      if(v<bv) bv=v; }
    if(n==0||n==1||n==2||n==16) guard[n==16?3:n]=bv;
    grand+=bv; }
  return grand; }
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int a=0;a<65536;a++) win_of(a,HLO[a],HHI[a]);
  { FILE*f=fopen("jwin.txt","r"); for(int i=0;i<1024;i++){int t,fe;long long a,b;
    if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&fe)!=4)return 1; WL[i]=a;WH[i]=b;WF[i]=fe;} fclose(f); }
  i64 Jt[1024]; int guard[4];
  auto run=[&](const char*name){ int sc=0; for(int i=0;i<1024;i++){ if(!WF[i])continue;
      if(Jt[i]>=WL[i]&&Jt[i]<=WH[i]) sc++; }
    int g=end_to_end(Jt,guard);
    printf("%-34s score %4d/1022   end-to-end %3d   guard tabs 0,1,2,16 = %d %d %d %d\n",
      name,sc,g,guard[0],guard[1],guard[2],guard[3]); };
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(0x4EF145,dx)+589824)>>23;
    Jt[t+512]=(arr(I1,dx)+(1ll<<19))>>20; } run("K1=589824(2^19+2^16), K2=2^19");
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(0x4EF145,dx)+589824)>>23;
    Jt[t+512]=(arr(I1,dx)+0)>>20; } run("K1=589824, K2=0");
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(0x4EF145,dx)+(1ll<<19))>>23;
    Jt[t+512]=(arr(I1,dx)+(1ll<<19))>>20; } run("baseline K1=K2=2^19");
  // --- core-A counterexample, printed step by step
  puts("\ncore-A counterexample  cst=5173578 g1=9 role arr(cst,dx) s1=18 rule=toward-zero K1=65536");
  puts("                       mask=clear low bit, g2=11 role arr(I1,dx) s2=19 rule=floor K2=-1966081");
  int ct[6]={-342,376,-376,258,40,-40};
  for(int q=0;q<6;q++){ int t=ct[q]; i64 P1=arr(5173578,(i64)t<<9); i64 x=P1+65536;
    i64 I1=(x>=0? (x>>18) : -((-x)>>18)) & ~(i64)1;
    i64 P2=arr(I1,(i64)t<<11); i64 J=(P2-1966081)>>19; int i=t+512;
    printf("  t=%5d  P1=%14lld  I1=%8lld  P2=%16lld  J=%9lld  window [%lld,%lld] %s\n",
      t,(long long)P1,(long long)I1,(long long)P2,(long long)J,(long long)WL[i],(long long)WH[i],
      (J>=WL[i]&&J<=WH[i])?"OK":"miss"); }
  return 0; }
