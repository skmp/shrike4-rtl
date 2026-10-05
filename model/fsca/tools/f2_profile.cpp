// f2_profile.cpp -- the deficit profile D(t) = KC*t^2/2^15 - J(t) over ALL offsets, and its dependence on t.
// This is the quantity a candidate square path has to produce.  Exact in units of 2^-20 (KC*t^2*32 is integer).
#include "f2_core.h"
#include <algorithm>
static Fsca F; typedef __int128 i128;
static double d20(i128 v){ return (double)(long long)v / 1048576.0; }
int main(){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F; Fin f{0,0,1,0}; FinCtx C; fin_build(f,C,2);
  FILE*o=fopen("f2_profile.txt","w");
  fprintf(o,"# D(t) = KC*t^2/2^15 - J(t), fine latch grid.  cols: t  DLO  DHI  width  |t|\n");
  double glo=1e18,ghi=-1e18; int alo=0,ahi=0;
  std::vector<std::pair<double,int>> mid;
  for(int t=-512;t<=511;t++){ int i=t+512; if(!t) continue; if(C.JL[i]>C.JH[i]) continue;
    i128 Q=(i128)KC*t*t*32; double lo=d20(Q-((i128)C.JH[i]<<20)), hi=d20(Q-((i128)C.JL[i]<<20));
    fprintf(o,"%5d %12.6f %12.6f %6lld %4d\n",t,lo,hi,(long long)(C.JH[i]-C.JL[i]+1),std::abs(t));
    if(lo<glo){glo=lo;alo=t;} if(hi>ghi){ghi=hi;ahi=t;} mid.push_back({(lo+hi)/2,t}); }
  fclose(o);
  printf("D(t) over all 1024 offsets: min %.3f (t=%d)   max %.3f (t=%d)   spread %.3f latch units\n",glo,alo,ghi,ahi,ghi-glo);
  // dependence on |t|: bucket
  printf("\n |t| band   n   mean D   min D    max D      (a chain's D grows like |t|/2^(s2-g); the data's does not)\n");
  int bnd[]={0,32,64,128,192,256,320,384,448,513};
  for(int b=0;b<9;b++){ double s=0,mn=1e18,mx=-1e18; int n=0;
    for(auto&p:mid){ int a=std::abs(p.second); if(a>=bnd[b]&&a<bnd[b+1]){ s+=p.first; n++; mn=std::min(mn,p.first); mx=std::max(mx,p.first);} }
    if(n) printf(" %3d-%-3d %5d  %7.3f  %7.3f  %7.3f\n",bnd[b],bnd[b+1]-1,n,s/n,mn,mx); }
  // the pinned offsets, and the +t/-t antisymmetric part
  printf("\nD(+t) - D(-t) for the offsets pinned to width <= 4 (a global latch bias cancels here):\n");
  for(int t=1;t<=511;t++){ int i=t+512,j=-t+512; if(C.JL[i]>C.JH[i]||C.JL[j]>C.JH[j])continue;
    if(C.JH[i]-C.JL[i]>3 && C.JH[j]-C.JL[j]>3) continue;
    i128 Q=(i128)KC*t*t*32;
    double plo=d20(Q-((i128)C.JH[i]<<20)),phi=d20(Q-((i128)C.JL[i]<<20));
    double mlo=d20(Q-((i128)C.JH[j]<<20)),mhi=d20(Q-((i128)C.JL[j]<<20));
    printf("  t=%4d : D(+t) in [%8.3f,%8.3f]  D(-t) in [%8.3f,%8.3f]   D(+t)-D(-t) in [%8.3f,%8.3f]\n",
      t,plo,phi,mlo,mhi,plo-mhi,phi-mlo); }
  return 0; }
