#include "v52_common.h"
static Fsca F; static JW W;
static int fullw(int w,int s1,int s2,i64 kc,int order,int* per=nullptr){
  int v=0; if(per) for(int i=0;i<64;i++)per[i]=0;
  for(int a=0;a<65536;a++){
    int node=((a+512)>>10)&63; int t=a-1024*node; if(t>32767)t-=65536; if(t<-32768)t+=65536;
    int nd=node,hs=1; if(nd>=32){nd-=32;hs=-1;} int flip=nd>=17,n=flip?32-nd:nd;
    const Tab&T=TB[n]; i64 dx13=(i64)t<<13, dxw=(i64)t<<w; i64 s=flip?-1:1;
    i64 T1=T.C1f? sm(arr(T.C1f,dx13)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx13)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1= order? (arr(dxw,kc)+K)>>s1 : (arr(kc,dxw)+K)>>s1;
    i64 I2=(arr(I1,dxw)+K)>>s2;
    i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
    i64 V=(C0TAB[n]+s*T1-T2-s*T3)*hs;
    Win ww=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(ww.sign>0){lo=ww.lo;hi=ww.hi;}else{lo=-ww.hi;hi=-ww.lo;}
    if(V<lo||V>hi){v++; if(per)per[node]++;} }
  return v; }
int main(){ load_fsca(F); load_jwin(W); build_tables();
  int per[64];
  struct C{int w,s1,s2,o;}; C cs[]={{13,23,20,1},{13,23,20,0},{14,23,22,0},{15,23,24,0},{15,23,24,1},{16,23,26,0},{16,23,26,1}};
  for(auto&c:cs){ int v=fullw(c.w,c.s1,c.s2,KC,c.o,per);
    printf("w=%2d s1=%2d s2=%2d order=%d : %3d violations ; guard nodes 0/1/2/16 = %d/%d/%d/%d (mirrors 32/33/34/48 = %d/%d/%d/%d)\n",
      c.w,c.s1,c.s2,c.o,v,per[0],per[1],per[2],per[16],per[32],per[33],per[34],per[48]); }
  puts("");
  puts("KC sweep at w=15 s1=23 s2=24 order=0 (full model):");
  { int best=1<<30; i64 bk=0; for(i64 kc=KC-64;kc<=KC+64;kc++){ int v=fullw(15,23,24,kc,0); if(v<best){best=v;bk=kc;} }
    printf("   best %d at KC=%lld (published=%lld, delta %+lld)\n",best,(long long)bk,(long long)KC,(long long)(bk-KC)); }
  puts("");
  puts("jwin18 (fine scale, s2 = 18+2(w-13)) at the same alignments, both orders:");
  for(int w=13;w<=16;w++){ int s2=18+2*(w-13);
    for(int o=0;o<2;o++){ int s=0; i64 j17=0,j342=0,jm=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1= o?(arr(dx,KC)+K)>>23:(arr(KC,dx)+K)>>23; i64 j=(arr(I1,dx)+K)>>s2;
        if(t==17)j17=j; if(t==342)j342=j; if(t==-342)jm=j; if(j>=W.lo[t+512]&&j<=W.hi[t+512])s++; }
      printf("   w=%2d s2=%2d order=%d : %d/1024   J(17)=%lld J(342)=%lld J(-342)=%lld\n",w,s2,o,s,(long long)j17,(long long)j342,(long long)jm); } }
  puts("   targets                                 45617     18466836     [18466858,18466867]");
  return 0; }
