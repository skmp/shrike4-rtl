// v52_fit2.cpp -- (1) the fitted diagnostic in the SAME parameterisation w5 used: dx-level offsets
//                     qlin/qsq/qcub applied to the whole dx for t<0, plus an offset rfb on the LATCH I1.
//                 (2) the 924/1024 alignment config on the full model, with guard nodes.
#include "v52_common.h"
static Fsca F;
static int fullq(int qlin,int qsq,int qcub,int rfb,int order,int*per=nullptr){
  int v=0; if(per)for(int i=0;i<64;i++)per[i]=0;
  for(int a=0;a<65536;a++){
    int nr=(a+512)>>10; int node=nr&63; int t=a-1024*nr;
    int so=1,m=node; if(m>=32){so=-1;m-=32;}
    int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n];
    i64 base=(i64)t<<13; i64 dxl=base+(t<0?qlin:0), dxs=base+(t<0?qsq:0), dxc=base+(t<0?qcub:0);
    i64 T1=T.C1f? sm(arr(T.C1f,dxl)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dxc)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1= order? (arr(dxs,KC)+K)>>23 : (arr(KC,dxs)+K)>>23; if(t<0) I1+=rfb;
    i64 I2=(arr(I1,dxs)+K)>>20; i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
    i64 s=flip?-1:1; i64 V=so*(C0TAB[n]+s*T1-T2-s*T3);
    Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
    if(V<lo||V>hi){v++; if(per)per[node]++;} }
  return v; }
static int fullw(int w,int s1,int s2,i64 kc,int order,int*per=nullptr){
  int v=0; if(per)for(int i=0;i<64;i++)per[i]=0;
  for(int a=0;a<65536;a++){
    int nr=(a+512)>>10; int node=nr&63; int t=a-1024*nr;
    int so=1,m=node; if(m>=32){so=-1;m-=32;}
    int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n];
    i64 dx13=(i64)t<<13, dxw=(i64)t<<w;
    i64 T1=T.C1f? sm(arr(T.C1f,dx13)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx13)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1= order? (arr(dxw,kc)+K)>>s1 : (arr(kc,dxw)+K)>>s1;
    i64 I2=(arr(I1,dxw)+K)>>s2; i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
    i64 s=flip?-1:1; i64 V=so*(C0TAB[n]+s*T1-T2-s*T3);
    Win ww=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(ww.sign>0){lo=ww.lo;hi=ww.hi;}else{lo=-ww.hi;hi=-ww.lo;}
    if(V<lo||V>hi){v++; if(per)per[node]++;} }
  return v; }
int main(){ load_fsca(F); build_tables();
  printf("baseline (order=1): %d\n",fullq(0,0,0,0,1));
  puts("(1) fitted diagnostic, w5's parameterisation, 5^4 = 625 combinations in [-2,2]^4:");
  { int best=1<<30,ba=0,bb=0,bc=0,br=0; long long n=0;
    for(int a=-2;a<=2;a++)for(int b=-2;b<=2;b++)for(int c=-2;c<=2;c++)for(int r=-2;r<=2;r++){ n++;
      int v=fullq(a,b,c,r,1); if(v<best){best=v;ba=a;bb=b;bc=c;br=r;} }
    printf("   %lld combos : best %d at qlin=%+d qsq=%+d qcub=%+d rfb=%+d\n",n,best,ba,bb,bc,br);
    printf("   at qlin=0 qsq=+1 qcub=0 rfb=-1 : %d\n",fullq(0,1,0,-1,1)); }
  puts("");
  puts("(2) the best alignment configs, on the FULL model at the published output scale:");
  int per[64];
  struct C{int w,s1,s2,o;}; C cs[]={{13,23,20,1},{14,24,21,0},{14,24,21,1},{16,26,23,0},{15,25,22,0}};
  for(auto&c:cs){ int v=fullw(c.w,c.s1,c.s2,KC,c.o,per);
    printf("   w=%2d s1=%2d s2=%2d order=%d : %3d violations ; guard nodes 0/1/2/16 = %d/%d/%d/%d\n",
      c.w,c.s1,c.s2,c.o,v,per[0],per[1],per[2],per[16]); }
  return 0; }
