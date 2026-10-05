#include "base2.h"
#include <cstdio>
static Fsca F;
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static int fullscore(int w,i64 kc,int s1,int s2f,int*gnode=nullptr){ int v=0,g=0;
  for(int a=0;a<65536;a++){ int nr=(a+512)>>10,node=nr&63,t=a-1024*nr;
    int so=1,m=node; if(m>=32){so=-1;m-=32;} int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n];
    i64 dx13=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx13)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx13)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 dx=(i64)t<<w; i64 I1=(arr(dx,kc)+K)>>s1; i64 J=(arr(I1,dx)+K)>>s2f;
    i64 T2=T.S? sm(arr(J,T.S)+K,U2T[n]):0; i64 s=flip?-1:1;
    i64 V=so*(C0T[n]+s*T1-T2-s*T3);
    Win wd=decode(F.srz[a],F.srn[a]); i64 lo,hi;
    if(wd.sign>0){lo=wd.lo;hi=wd.hi;}else{lo=-wd.hi;hi=-wd.lo;}
    if(V<lo||V>hi){v++; int mm=node%32; if(mm==0||mm==1||mm==2||mm==16)g++;} }
  if(gnode)*gnode=g; return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
  printf("KC sweep at w=14, (s1,s2full)=(22,23):  full-model violations / guard-node(0,1,2,16) violations\n");
  for(long long d=-4;d<=4;d++){ int g; int v=fullscore(14,0x4ef145+d,22,23,&g);
    printf("  KC=0x4EF145%+lld : %5d   guard %d%s\n",d,v,g,d==0?"   <- nominal, unchanged constant":""); }
  int g13; int v13=fullscore(13,0x4ef145,23,20,&g13);
  printf("published w=13 (23,20) KC nominal : %5d   guard %d\n",v13,g13);
  return 0; }
