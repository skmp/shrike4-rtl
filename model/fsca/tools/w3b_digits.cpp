// w3b_digits.cpp -- BRANCH 3 (a): Booth digit count of the array, swept per call site and globally.
// ND=13 is what FIPR itself validates (fsca_common.h mult28 has 13 rows); base2.h arr() uses 14.
// Windows are REGENERATED whenever a width that feeds T1/T3/T2 changes, so the score is self-consistent.
#include "w3_common.h"
#include <vector>
struct Row { const char* site; int nd; int score; int vfine; int vcoarse; i64 j17,j342,jm342; int feas; };
static void set_all(int nd){ ND_T1=ND_T3a=ND_T3b=ND_SQ1=ND_SQ2=ND_T2=nd; }
int main(){ if(!load_fsca(FD)) return 1; build_tables();
    const char* names[6] = {"ALL","T1 arr(C1f,dx)","T3a arr(t^2<<3,dx)","T3b arr(c3<<5,A3)","SQ1 arr(KC,dx)","SQ2 arr(I1,dx)+T2"};
    // note: SQ2 knob also moves ND_T2 in the "SQ2+T2" row because both consume the square latch path
    printf("site                 ND  jwin18  fullFINE  fullCOARSE  feasT   J(17)      J(342)     J(-342)\n");
    printf("targets                   1024      0          0        1024   45617      18466836   [18466858,18466867]\n");
    for (int site = 0; site < 7; site++) {
      for (int nd = 10; nd <= 18; nd++) {
        set_all(14);
        if (site==0) set_all(nd);
        else if (site==1) ND_T1=nd; else if (site==2) ND_T3a=nd; else if (site==3) ND_T3b=nd;
        else if (site==4) ND_SQ1=nd; else if (site==5) { ND_SQ2=nd; ND_T2=nd; }
        else if (site==6) ND_SQ2=nd;                       // second multiply only, T2's array left at 14
        DU=2; gen_windows();
        auto Jf=[&](int t){ return baseline_J(t); };
        int sc = score_jwin(Jf, JLw, JHw);
        int feas=0; for(int i=0;i<1024;i++) feas+=JFeas[i];
        int g; int vf = full_violations(Jf,&g);
        DU=0; auto J20=[&](int t){ i64 dx=(i64)t<<13; return chain_J(dx,dx,23,20); };
        int g2; int vc = full_violations(J20,&g2); DU=2;
        const char* nm = site==6?"SQ2 arr(I1,dx) only":names[site];
        printf("%-20s %2d  %4d    %5d(%d)   %5d(%d)   %4d   %-10lld %-10lld %-10lld%s\n",
            nd==10?nm:"", nd, sc, vf, g, vc, g2, feas,
            (long long)Jf(17),(long long)Jf(342),(long long)Jf(-342),
            (Jf(17)==45617||Jf(342)==18466836)?"   <== LANDMARK":"");
      }
      printf("\n");
    }
    return 0; }
