// we_w.cpp -- exact maximum of the chain family
//     I1(t) = floor((Q*t + A[t mod M]) / W),   J(t) = (arr(I1, t<<13) + K2) >> s2,  W*2^s2 = 2^30
// The residue classes are independent, so A[] is optimised EXACTLY per class by exhaustive scan.
// Sweeps W (power of two), the class modulus M, the constant Q, and K2.  Reports the achievable score.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int MM = argc>1?atoi(argv[1]):8;            // class modulus for A
    int signsplit = argc>2?atoi(argv[2]):0;     // 1 = allow A to differ by sign of t
    printf("A-class modulus M=%d  signsplit=%d\n",MM,signsplit);
    printf(" logW  s2  K2        Q          score  A[] (per class)\n");
    for(int lw=8;lw<=16;lw++){ i64 W=1ll<<lw; int s2=30-lw; if(s2<6||s2>30) continue;
      for(int k2i=0;k2i<3;k2i++){ i64 K2 = k2i==0?(1ll<<19): k2i==1?0:(1ll<<(s2-1)); if(K2>=(1ll<<s2)) continue;
        for(i64 dq=0;dq<=0;dq++){ i64 Q=KC+dq;
          int ncls = MM*(signsplit?2:1);
          std::vector<i64> bestA(ncls,0); int total=0;
          for(int cls=0;cls<ncls;cls++){ int m=cls%MM, sg=cls/MM;
            int bs=-1; i64 ba=0;
            for(i64 A=0;A<W;A++){ int s=0;
              for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; if(((t%MM)+MM)%MM!=m) continue;
                if(signsplit && ((t<0)?1:0)!=sg) continue;
                i64 I1 = (Q*t + A) >> lw;                      // arithmetic shift = floor
                i64 J = (arr(I1,(i64)t<<13) + K2) >> s2;
                if(J>=JLo[t+512]&&J<=JHi[t+512]) s++; }
              if(s>bs){bs=s;ba=A;} }
            bestA[cls]=ba; total+=bs; }
          printf("%5d %3d %8lld %9lld  %5d  ",lw,s2,(long long)K2,(long long)Q,total);
          for(int c=0;c<ncls;c++) printf("%lld ",(long long)bestA[c]); puts("");
        } } }
    return 0; }
