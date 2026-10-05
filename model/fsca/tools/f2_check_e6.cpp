// f2_check_e6.cpp -- re-decide e6_nol1.cpp's table EXHAUSTIVELY (it used binary search on a non-monotone
// function).  Same forms: g=13, role0 = arr(I1,dx), K2 in {0, 2^19}, the shifts e6 printed.
#include "f2_core.h"
static Fsca F;
int main(){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F; Fin f{0,0,1,0};
  for(int du=0;du<=2;du+=2){ FinCtx C; fin_build(f,C,du);
    printf("=== latch shift %d ===\n",20-du);
    for(int sv=0;sv<3;sv++){ int s2=(du==0?20:18)+sv-1;
      for(int k2=0;k2<2;k2++){ i64 K2=k2?(1ll<<19):0;
        int none=0; std::vector<int> bad; long long tot=0;
        for(int t=-512;t<=511;t++){ int i=t+512; if(!t||C.JL[i]>C.JH[i]) continue;
          Cert c=certify(0,(i64)t<<13,K2,s2,LR_FLOOR,C.JL[i],C.JH[i]); tot+=c.scanned;
          if(!c.hit&&!c.capped){none++;bad.push_back(t);} }
        printf("  s2=%2d K2=%-7lld : %3d t have NO valid I1 (exhaustive)",s2,(long long)K2,none);
        if(none){printf(" ->");for(int t:bad)printf(" %d",t);} printf("   [%lld probes]\n",tot); } } }
  return 0; }
