// f2_pinned.cpp -- DIRECTION 2, part E.  The FULL set of hard local constraints, not just t=342.
// For every offset whose latch window is pinned to width <= 4 (and, as context, <= 12), across every
// final-multiply variant of e_final.h that keeps all 1024 offsets feasible, report:
//   * the window [JL,JH] and the deficit D = KC t^2/2^15 - J it demands
//   * the certificate: for each second-stage form (role, g, s2, K2) does ANY integer I1 reach it
//   * how persistent that offset's certificate is across the whole form space
#include "f2_core.h"
#include <algorithm>
#include <cstring>
#include <map>
static Fsca F; typedef __int128 i128;
static double d20(i128 v){ return (double)(long long)v/1048576.0; }
int main(){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F;
  FILE*o=fopen("f2_pinned_table.txt","w");
  // 1. which offsets are pinned, in EVERY fully-feasible final-multiply variant
  std::map<int,std::pair<i64,i64>> tight;           // t -> widest window over variants that keep it <=4
  std::map<int,int> cnt; int nvar=0;
  for(int sv=0;sv<NSV;sv++)for(int r3=0;r3<2;r3++)for(int k3=0;k3<3;k3++)for(int sg=0;sg<2;sg++){
    Fin f{sv,r3,k3,sg}; FinCtx C; fin_build(f,C,2); if(C.feasible<1024) continue; nvar++;
    for(int t=-512;t<=511;t++){ int i=t+512; if(!t) continue; if(C.JH[i]-C.JL[i]<=3){ cnt[t]++;
        auto it=tight.find(t); if(it==tight.end()) tight[t]={C.JL[i],C.JH[i]};
        else { it->second.first=std::min(it->second.first,C.JL[i]); it->second.second=std::max(it->second.second,C.JH[i]); } } } }
  fprintf(o,"# f2_pinned: every offset pinned to window width <= 4, over the %d final-multiply variants of\n",nvar);
  fprintf(o,"# e_final.h that keep all 1024 offsets feasible.  'in N variants' = how robust the pin is.\n");
  fprintf(o,"#   t   in N/%d   union window over those variants   width   D = KC t^2/2^15 - J  (latch units)\n",nvar);
  for(auto&kv:tight){ int t=kv.first; i128 Q=(i128)KC*t*t*32;
    fprintf(o,"%5d   %3d/%-3d   [%10lld,%10lld]  %4lld   D in [%9.3f, %9.3f]\n",t,cnt[t],nvar,
      (long long)kv.second.first,(long long)kv.second.second,(long long)(kv.second.second-kv.second.first+1),
      d20(Q-((i128)kv.second.second<<20)),d20(Q-((i128)kv.second.first<<20))); }
  // 2. the certificate per offset, over the whole second-stage form space, for the published final multiply
  Fin f0{0,0,1,0}; FinCtx C; fin_build(f0,C,2);
  fprintf(o,"\n# certificate persistence (published final multiply, fine latch scale).  For every form\n");
  fprintf(o,"# (role in {arr(I1,dx),arr(dx,I1)}) x (g in 8..14) x (s2 in g..g+12) x (K2 in {0, 2^19, 2^(s2-1),\n");
  fprintf(o,"# 2^18, 2^20, 2^s2-1}) x (latch in {floor,toward0,ones-compl}) = 6552 forms: in how many does the\n");
  fprintf(o,"# offset admit NO integer I1 at all?  (Latch rule never matters: all three agree in every cell.)\n");
  int nform=0; std::map<int,int> nol1; static int bysg[1024][13]; memset(bysg,0,sizeof bysg);
  for(int role=0;role<2;role++)for(int g=8;g<=14;g++)for(int s2=g;s2<=g+12;s2++){
    i64 K2s[6]={0,1ll<<19,1ll<<(s2-1),1ll<<18,1ll<<20,(1ll<<s2)-1};
    for(int ki=0;ki<6;ki++)for(int rule=0;rule<3;rule++){ nform++;
      for(int t=-512;t<=511;t++){ int i=t+512; if(!t) continue;
        Cert c=certify(role,(i64)t<<g,K2s[ki],s2,rule,C.JL[i],C.JH[i]);
        if(!c.hit&&!c.capped&&!c.clamped){ nol1[t]++; bysg[i][s2-g]++; } } } }
  fprintf(o,"# total forms scanned: %d\n",nform);
  std::vector<std::pair<int,int>> v(nol1.begin(),nol1.end());
  std::sort(v.begin(),v.end(),[](auto&a,auto&b){return a.second>b.second;});
  fprintf(o,"#   t   forms with NO integer I1   %% of forms   window width   is it pinned(<=4)?\n");
  for(size_t i=0;i<v.size()&&i<60;i++){ int t=v[i].first; i64 w=C.JH[t+512]-C.JL[t+512]+1;
    fprintf(o,"%5d  %8d  %8.2f%%   %8lld   %s\n",t,v[i].second,100.0*v[i].second/nform,(long long)w,w<=4?"PINNED":""); }
  fprintf(o,"\n# per-offset breakdown by s2-g, the ONLY parameter that matters (the J step per unit I1 is\n");
  fprintf(o,"# |t|/2^(s2-g)).  Entry = cells out of 252 (7 g x 2 roles x 6 K2 x 3 latch rules at that s2-g)\n");
  fprintf(o,"# in which the offset admits NO integer I1.  Offsets pinned to width <= 4 first, then width <= 12.\n");
  fprintf(o,"#    t width");
  for(int d=0;d<=12;d++) fprintf(o,"  s2-g=%-2d",d);
  fprintf(o,"\n");
  for(int pass=0;pass<2;pass++)
   for(int t=-512;t<=511;t++){ int i=t+512; if(!t)continue; i64 w=C.JH[i]-C.JL[i]+1;
     if(pass==0 && w>4) continue; if(pass==1 && (w<=4||w>12)) continue;
     fprintf(o,"%6d %5lld",t,(long long)w);
     for(int d=0;d<=12;d++) fprintf(o,"  %6d",bysg[i][d]);
     fprintf(o,"%s\n",pass==0?"   PINNED":""); }
  fclose(o);
  // stdout summary
  printf("pinned (width<=4) offsets over %d fully-feasible final-multiply variants:\n",nvar);
  for(auto&kv:tight) printf("  t=%4d pinned in %3d/%d variants, union window width %lld\n",
      kv.first,cnt[kv.first],nvar,(long long)(kv.second.second-kv.second.first+1));
  printf("\nmost persistent 'no integer I1' offsets over %d forms:\n",nform);
  for(size_t i=0;i<v.size()&&i<15;i++) printf("  t=%4d : %d forms (%.2f%%)  window width %lld\n",
      v[i].first,v[i].second,100.0*v[i].second/nform,(long long)(C.JH[v[i].first+512]-C.JL[v[i].first+512]+1));
  return 0; }
