// w1_wfull.cpp -- close the "is the operand alignment right" question: full-model score for every
// operand width w with the derived compensating shift law, plus a fine UNIFORM-bias scan of the square
// path (the only kind of bias that is not sign-dependent and could in principle move J(17) and J(342)).
#include "base2.h"
#include <cstring>
#include <algorithm>
static Fsca F; static i64 VL[65536],VH[65536]; static i64 JL[1024],JH[1024];
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
struct Kn { int w,s1,s2; i64 ba,bb; };   // ba = uniform bias on dx into multiply 1, bb = into multiply 2
static inline i64 Jf(int t,const Kn&k){ i64 d=(i64)t<<k.w;
  i64 I1=(arr(KC,d+k.ba)+K)>>k.s1; return (arr(I1,d+k.bb)+K)>>k.s2; }
static int sj(const Kn&k){ int s=0; for(int t=-512;t<=511;t++){ i64 j=Jf(t,k);
  if(j>=JL[t+512]&&j<=JH[t+512])s++; } return s; }
static int sfull(const Kn&k){ int v=0;
  for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n];
    for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 I2=Jf(t,k)>>2; i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
      i64 s=flip?-1:1; i64 V=C0TAB[n]+s*T1-T2-s*T3; if(V<VL[a]||V>VH[a])v++; } }
  return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;} }
  { FILE*f=fopen("jwin18.txt","r"); char ln[256]; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
    long long t,l,h,wd; if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&wd)==4){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
  printf("[A] operand width w, derived shift law s1+s2 = 2w+15, KC fixed.  Best split by jwin18, then full model.\n");
  printf("    w  (s1,s2)  jwin18  FULL   J(17)     J(342)     J(-342)\n");
  int gbest=1<<30;
  for(int w=8;w<=20;w++){ int bs=-1; Kn bk{w,0,0,0,0};
    for(int s1=0;s1<=2*w+15;s1++){ int s2=2*w+15-s1; if(s2<0||s2>40) continue;
      Kn k{w,s1,s2,0,0}; int s=sj(k); if(s>bs){bs=s;bk=k;} }
    int fv=sfull(bk); if(fv<gbest)gbest=fv;
    printf("   %3d  (%2d,%2d)   %4d   %5d  %-9lld %-10lld %-10lld%s\n",w,bk.s1,bk.s2,bs,fv,
      (long long)Jf(17,bk),(long long)Jf(342,bk),(long long)Jf(-342,bk),w==13?"  <== published":""); }
  printf("    best full-model score over the whole width sweep: %d (baseline 148)\n",gbest);
  printf("\n[B] UNIFORM bias on the square path's two dx operands (sign-INDEPENDENT, so it CAN move J(17)).\n");
  printf("    ba in [-8192,8192] step 64, bb likewise: %d configs.\n",257*257);
  { int best=-1,h17=0,h342=0,hboth=0; Kn bk{13,23,18,0,0};
    for(i64 ba=-8192;ba<=8192;ba+=64) for(i64 bb=-8192;bb<=8192;bb+=64){
      Kn k{13,23,18,ba,bb}; if(Jf(17,k)==45617){h17++; if(Jf(342,k)==18466836)hboth++;}
      if(Jf(342,k)==18466836)h342++; int s=sj(k); if(s>best){best=s;bk=k;} }
    printf("    best jwin18 = %d at ba=%+lld bb=%+lld  (full %d)   J(17)=%lld J(342)=%lld\n",
      best,(long long)bk.ba,(long long)bk.bb,sfull(bk),(long long)Jf(17,bk),(long long)Jf(342,bk));
    printf("    configs hitting J(17)=45617: %d    J(342)=18466836: %d    BOTH: %d\n",h17,h342,hboth); }
  printf("\n[C] the same at full resolution on a 1-unit grid, ba,bb in [-512,512]: %d configs\n",1025*1025);
  { int best=-1,h17=0,h342=0,hboth=0; Kn bk{13,23,18,0,0};
    for(i64 ba=-512;ba<=512;ba++) for(i64 bb=-512;bb<=512;bb++){
      Kn k{13,23,18,ba,bb}; if(Jf(17,k)==45617){h17++; if(Jf(342,k)==18466836)hboth++;}
      if(Jf(342,k)==18466836)h342++; int s=sj(k); if(s>best){best=s;bk=k;} }
    printf("    best jwin18 = %d at ba=%+lld bb=%+lld  (full %d)\n",best,(long long)bk.ba,(long long)bk.bb,sfull(bk));
    printf("    configs hitting J(17)=45617: %d    J(342)=18466836: %d    BOTH: %d\n",h17,h342,hboth); }
  return 0; }
