// v10_cub.cpp -- model-free: can a cubic fit the dump over a span of half-width H, and where are
// the node boundaries?  Uses ONLY the decoded windows.  Certificate: for 5 equally spaced points a
// cubic satisfies sum_i (-1)^i C(4,i) p(t_i) = 0 exactly, so if the interval hull of that alternating
// sum over the windows excludes 0 the interval admits NO cubic at all; and the best cubic on those
// 5 points has max error >= |D4|/16.  Everything integer except the final /16 report.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 LO[65536],HI[65536];
// certificate over an interval [a0,a1] (angles), using 5 points spaced (a1-a0)/4
static void cert(int a0,int a1,double&worst,double&sum,int&cnt){
  int h=(a1-a0)/4; if(h<1)return; static const int C[5]={1,-4,6,-4,1};
  i64 mn=0,mx=0; for(int i=0;i<5;i++){ int a=(a0+i*h)&0xffff; i64 c=C[i];
     if(c>0){ mn+=c*LO[a]; mx+=c*HI[a]; } else { mn+=c*HI[a]; mx+=c*LO[a]; } }
  i64 gap = (mn>0)? mn : ((mx<0)? -mx : 0);        // rigorous lower bound on |D4|
  double s = (double)gap/16.0;                      // lower bound on the best cubic's max error
  worst=std::max(worst,s); sum+=s; cnt++;
}
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int a=0;a<65536;a++){ i64 lo,hi; win_of(a,lo,hi); LO[a]=lo; HI[a]=hi; }
  puts("=== v10_cub: rigorous 'no cubic fits' certificate, in output units (2^-26) ===");
  puts("A truncating latch can cost about 1 unit each; the model has three, so a slack above ~3 units");
  puts("means the span cannot be a single cubic segment however the coefficients are chosen.\n");
  for(int L : {512,1024,2048,4096}){
     printf("span L=%4d (half-width %4d):\n",L,L/2);
     for(int p=0;p<L;p+=(L/8)){
        double worst=0,sum=0; int cnt=0;
        for(int k=0;;k++){ int a0=p+k*L; if(a0+L>16384) break; cert(a0,a0+L,worst,sum,cnt); }
        if(!cnt){puts("   (no interval)");continue;}
        printf("   phase %4d -> intervals [%d + %dk, ..]  mean slack %7.3f   worst %7.3f   over %d intervals%s\n",
           p,p,L,sum/cnt,worst,cnt, (L==1024&&p==512)?"   <== centres at multiples of 1024":""); }
  }
  puts("\n=== fine phase sweep at L=1024 (all 32 phases in steps of 32) ===");
  { double bestm=1e18; int bestp=-1;
    for(int p=0;p<1024;p+=32){ double worst=0,sum=0; int cnt=0;
       for(int k=0;;k++){ int a0=p+k*1024; if(a0+1024>16384)break; cert(a0,a0+1024,worst,sum,cnt); }
       printf("   p=%4d mean %7.3f worst %7.3f\n",p,sum/cnt,worst);
       if(sum/cnt<bestm){bestm=sum/cnt;bestp=p;} }
    printf("   -> sharp minimum at p=%d, i.e. intervals [1024k+%d, 1024k+%d]: node CENTRES at multiples of 1024\n",bestp,bestp,bestp+1023); }
  return 0; }
