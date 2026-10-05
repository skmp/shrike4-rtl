// rt11_i1dev.cpp -- every narrow jwin18 window re-expressed as a deviation of the FIRST latch magnitude from
// x = KC*|t|/1024 (second multiply taken exact), per sign of t.  Shows the positive side needing values ABOVE the
// exact product at some offsets (376, 420, 346, 400) and 1.5..22 units below at others.
// Build: g++ -O2 rt11_i1dev.cpp -o /tmp/rt11 && /tmp/rt11   (run from tools/)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
// For each t with a narrow window (<=40 fine J units), express the window as a deviation of the FIRST latch
// magnitude |I1| from x = KC*|t|/1024 assuming the second multiply is exact: J_fine = |I1| * |t| / 32.
// dI1 = (J - KC t^2/2^15) * 32 / |t|.   Model: +side floor -> dI1 in [-1,0) (= -frac); -side -> [0,1) (= 1-frac).
int main(){ FILE*f=fopen("jwin18.txt","r"); static long JL[1024],JH[1024]; char line[256]; while(fgets(line,256,f)){ if(line[0]=='#')continue; int t; long a,b,w; if(sscanf(line,"%d %ld %ld %ld",&t,&a,&b,&w)==4){JL[t+512]=a;JH[t+512]=b;} }
  const double KC=0x4ef145;
  puts("   t  t%4  frac(x)   dI1(+t) range      dI1(-t) range     (model: [-frac,-frac] / [1-frac,1-frac])");
  for(int a=1;a<512;a++){ long wp=JH[512+a]-JL[512+a]+1, wm=JH[512-a]-JL[512-a]+1; if(wp>40&&wm>40)continue;
    double x=KC*a/1024.0, fr=x-floor(x); double ex=KC*a*a/32768.0;
    double pl=(JL[512+a]-ex)*32.0/a, ph=(JH[512+a]-ex)*32.0/a, ml=(JL[512-a]-ex)*32.0/a, mh=(JH[512-a]-ex)*32.0/a;
    printf("%4d   %d   %.3f   ",a,a%4,fr); if(wp<=40) printf("[%+6.2f,%+6.2f]", pl,ph); else printf("      --      "); printf("   "); if(wm<=40) printf("[%+6.2f,%+6.2f]", ml,mh); else printf("      --      ");
    printf("   model %+.2f / %+.2f\n", -fr, 1-fr); }
}
