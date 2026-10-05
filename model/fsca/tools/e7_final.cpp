// e7_final.cpp -- DIRECTION 1, the sweep: every final-multiply variant, each with its dataset regenerated.
#include "e_final.h"
static Fsca Fdump;
int main(){ if(!load_fsca(Fdump))return 1; build_tables(); FIN_F=&Fdump;
    printf("%-42s r3 k3 sg | feasible t | free-J floor | best chain (guard) | t=342 I1?\n","sine field / alignment rule");
    for(int sv=0;sv<NSV;sv++) for(int r3=0;r3<2;r3++) for(int k3=0;k3<3;k3++) for(int sg=0;sg<2;sg++){
        Fin f{sv,r3,k3,sg}; FinCtx C; fin_build(f,C,2);
        if(C.feasible<900) { if(r3==0&&k3==1&&sg==0) printf("%-42s %d  %d  %d | %4d  (too few -- skipped)\n",
            SVNAME[sv],r3,k3,sg,C.feasible); continue; }
        long ff=fin_freefloor(f,C);
        // best chain over the usual grid, and its guard
        int best=1<<30, bg=1<<30, bs1=0,bs2=0;
        for(int ro=0;ro<2;ro++) for(int s1=20;s1<=26;s1++){ int s2=41-s1;
          for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++){
            i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0; int sc=0;
            for(int t=-512;t<=511;t++){ int i=t+512; if(C.JL[i]>C.JH[i])continue; i64 dx=(i64)t<<13;
                i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K1)>>s1; i64 J=(arr(I1,dx)+K2)>>s2;
                if(J>=C.JL[i]&&J<=C.JH[i]) sc++; }
            if(1024-sc<best){ best=1024-sc; bs1=s1; bs2=s2; bg=fin_guard(f,C,s1,s2,K1,K2,ro); } } }
        // is there ANY integer I1 giving J(342) in its window?
        bool any342=false; { int i=342+512; if(C.JL[i]<=C.JH[i]){ i64 dx=(i64)342<<13;
            for(i64 v=0; v<8000000 && !any342; v+=1){ i64 J=(arr(v,dx)+0)>>(41-23); if(J>C.JH[i]) break; if(J>=C.JL[i]) any342=true; } } }
        printf("%-42s %d  %d  %d | %4d       |     %4ld     |   %4d (%d)        | %s\n",
            SVNAME[sv],r3,k3,sg,C.feasible,ff,best,bg,any342?"yes":"NO");
    }
    return 0; }
