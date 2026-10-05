// v52_fit.cpp -- (1) the labelled fitted diagnostic: sign-dependent dx offsets on the square path.
//                (2) the w=14 / s2=20 alignment that beat 918 in the s1-fixed slice, checked properly.
#include "v52_common.h"
static Fsca F; static JW W;
// full model with sign-dependent dx offsets on the two square-chain operands
static int full_sq(int qsq,int r,bool signdep){
    int v=0;
    for(int a=0;a<65536;a++){
        int node=((a+512)>>10)&63; int t=a-1024*node; if(t>32767)t-=65536; if(t<-32768)t+=65536;
        int nd=node,hs=1; if(nd>=32){nd-=32;hs=-1;} int flip=nd>=17,n=flip?32-nd:nd;
        const Tab&T=TB[n]; i64 dx=(i64)t<<13; i64 s=flip?-1:1;
        i64 oa = (signdep && t>=0)?0:qsq, ob = (signdep && t>=0)?0:r;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 I1=(arr(dx+oa,KC)+K)>>23; i64 I2=(arr(I1,dx+ob)+K)>>20;
        i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
        i64 V=(C0TAB[n]+s*T1-T2-s*T3)*hs;
        Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
        if(V<lo||V>hi)v++; }
    return v; }
int main(){ if(!load_fsca(F)||!load_jwin(W))return 1; build_tables();
  puts("(1) FITTED DIAGNOSTIC (not structure): sign-dependent dx offsets qsq (1st mult) and r (feedback),");
  puts("    applied only when t<0.  Baseline 294.");
  { int best=1<<30,bq=0,br=0; long long n=0;
    for(int q=-12;q<=12;q++)for(int r=-12;r<=12;r++){ n++; int v=full_sq(q,r,true); if(v<best){best=v;bq=q;br=r;} }
    printf("    %lld combinations : best %d violations at qsq=%+d r=%+d\n",n,best,bq,br);
    printf("    at qsq=+1 r=-1 : %d violations\n",full_sq(1,-1,true));
    printf("    at qsq= 0 r= 0 : %d violations (baseline)\n",full_sq(0,0,true)); }
  puts("");
  puts("(2) ALIGNMENT SLICE w5 DID NOT SCAN: s1 held at 23, s2 = 18+2(w-13), KC swept +-4096 around 5173573");
  printf("    (this keeps s1+s2 = 15+2w, so the nominal constant stays 0x4EF145 at every w)\n");
  for(int w=11;w<=16;w++){ int s2=18+2*(w-13); if(s2<0)continue;
    int best=-1; i64 bk=0;
    for(i64 kc=KC-4096;kc<=KC+4096;kc++){ int s=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+K)>>23; i64 j=(arr(I1,dx)+K)>>s2;
        if(j>=W.lo[t+512]&&j<=W.hi[t+512])s++; }
      if(s>best){best=s;bk=kc;} }
    i64 j17,j342,jm342;
    { i64 dx=(i64)17<<w; i64 I1=(arr(bk,dx)+K)>>23; j17=(arr(I1,dx)+K)>>s2;
      dx=(i64)342<<w; I1=(arr(bk,dx)+K)>>23; j342=(arr(I1,dx)+K)>>s2;
      dx=(i64)-342<<w; I1=(arr(bk,dx)+K)>>23; jm342=(arr(I1,dx)+K)>>s2; }
    printf("    w=%2d s1=23 s2=%2d : best %d/1024 at KC=%lld%s  J(17)=%lld J(342)=%lld J(-342)=%lld%s\n",
      w,s2,best,(long long)bk,bk==KC?" (published)":"",(long long)j17,(long long)j342,(long long)jm342,
      best>918?"   <<< BEATS 918":""); }
  puts("");
  puts("    landmark targets:                                     J(17)=45617 J(342)=18466836 J(-342) in [18466858,18466867]");
  return 0; }
