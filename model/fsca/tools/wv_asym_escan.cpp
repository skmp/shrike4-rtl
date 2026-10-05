// wv_asym_escan.cpp -- adversarial verification, part 9: claim item 5(d).
// I1 = floor((KC*t + E(t mod M))/W), J = (arr(I1, t<<13) + 2^19) >> s2, s1 = 13 + log2 W, s2 = 43 - s1.
// The residue classes are independent, so the exact optimum over E per class is a direct maximisation.
#include "wv_asym_win.h"
#include <cstdio>
#include <vector>
static inline i64 fdiv(i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)!=(b<0)))q--; return q; }
int main(){ if(!load_fsca(WVF))return 1; build_tables(); wv_build(0);
    for(int p : {8,10,11}){ i64 W=1ll<<p; int s1=13+p, s2=43-s1;
        for(int M : {8,256}){
            std::vector<int> best(M,0), bestE(M,0);
            #pragma omp parallel for schedule(dynamic)
            for(int r=0;r<M;r++){ int bs=-1,be=0;
                for(i64 E=0;E<W;E++){ int sc=0;
                    for(int t=-512;t<=511;t++){ if(((t%M)+M)%M!=r) continue; if(!WV_OK[t+512])continue;
                        i64 I1=fdiv(KC*(i64)t+E,W); i64 J=(arr(I1,(i64)t<<13)+(1ll<<19))>>s2;
                        if(J>=WV_LO[t+512]&&J<=WV_HI[t+512])sc++; }
                    if(sc>bs){bs=sc;be=(int)E;} }
                best[r]=bs; bestE[r]=be; }
            int tot=0; for(int r=0;r<M;r++) tot+=best[r];
            printf("W=2^%-2d (s1=%d,s2=%d)  E free per residue mod %3d : best jwin = %4d / 1022",p,s1,s2,M,tot);
            if(M==8){ printf("   E ="); for(int r=0;r<8;r++) printf(" %d",bestE[r]);
                      printf("   (the real E = 48-g:"); const int g[8]={0,5,2,7,4,1,6,3}; for(int r=0;r<8;r++)printf(" %d",48-g[r]); printf(")"); }
            puts(""); } }
    return 0; }
