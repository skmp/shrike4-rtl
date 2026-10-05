// v51_msb.cpp -- ADVERSARIAL VERIFY of branch-5's B(ii) node-split certificate, written from scratch.
// 4th central difference of a cubic is identically 0: p(x-2h)-4p(x-h)+6p(x)-4p(x+h)+p(x+2h)=0.
#include "base2.h"
#include <cstdio>
#include <algorithm>
static Fsca F;
static void win_signed(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a&0xffff],F.srn[a&0xffff]);
  if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
int main(){ if(!load_fsca(F))return 1; build_tables();
  // -- premise check: how wide are the dump windows?
  { long long sum=0; i64 mx=0; int hist[8]={0}; 
    for(int a=0;a<65536;a++){ i64 l,h; win_signed(a,l,h); i64 w=h-l+1; sum+=w; if(w>mx)mx=w;
      hist[std::min<i64>(w,7)]++; }
    printf("dump window widths over 65536 angles: mean %.3f, max %lld ; histogram w=1..>=7: ",
      (double)sum/65536,(long long)mx);
    for(int i=1;i<8;i++)printf("%d ",hist[i]); printf("\n");
    printf("  => max slack of the 4th-difference combination (sum|coeff|=16) = 16 * (maxwidth-1) = %lld\n\n",
      16*(mx-1)); }
  const int wt[5]={1,-4,6,-4,1};
  for(int span : {512,1024,2048,4096}){ int h=span/4;
    i64 worst=0; int worstc=0; int nexcl=0,ntot=0; i64 worstslack=0;
    for(int c=0;c<65536;c+=span){ i64 dmin=0,dmax=0;
      for(int k=0;k<5;k++){ i64 l,hh; win_signed(c+(k-2)*h,l,hh);
        if(wt[k]>0){dmin+=wt[k]*l; dmax+=wt[k]*hh;} else {dmin+=wt[k]*hh; dmax+=wt[k]*l;} }
      ntot++; i64 m = dmin>0? dmin : (dmax<0? -dmax : 0); if(dmin>0||dmax<0)nexcl++;
      if(m>worst){worst=m;worstc=c;worstslack=dmax-dmin;} }
    printf("span %4d angles/node (h=%4d): %2d/%2d centres exclude 0 ; worst |D| = %lld at c=%d "
           "(that centre's slack %lld) -> best cubic max error >= %.2f units\n",
      span,h,nexcl,ntot,(long long)worst,worstc,(long long)worstslack,(double)worst/16.0); }
  // -- empirical noise floor: the PUBLISHED model's own 4th difference (truncations included)
  printf("\nnoise floor: the published model's own 4th difference (it IS a truncated cubic per node)\n");
  { auto Vmodel=[&](int a)->i64{ int nr=(a+512)>>10, node=nr&63, t=a-1024*nr;
      int so=1,m=node; if(m>=32){so=-1;m-=32;} int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n];
      static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
      static const int U2T[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
      i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20; i64 T2=T.S? sm(arr(J,T.S)+K,U2T[n]):0;
      i64 s=flip?-1:1; return so*(C0T[n]+s*T1-T2-s*T3); };
    for(int span : {512,1024}){ int h=span/4; i64 mx=0; int mc=0;
      for(int c=0;c<65536;c+=span){ i64 D=0; for(int k=0;k<5;k++) D+=wt[k]*Vmodel((c+(k-2)*h)&0xffff);
        if(std::abs((long long)D)>mx){mx=std::abs((long long)D);mc=c;} }
      printf("  span %4d (h=%4d, samples stay INSIDE one published node): max |D_model| = %lld at c=%d\n",span,h,(long long)mx,mc); } }
  return 0; }
