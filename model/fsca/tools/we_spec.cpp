// we_spec.cpp -- (a) write the COMPLETE required-asymmetry table to we_dtable.txt;
// (b) the requirements the generator must meet, in c-units (c = J*2^17 - KC*t^2);
// (c) the granularity window: with D = delta * W*|t|/2^17 + W*(hp-hm)/2^17, which W can give D(342) in [6,7]?
// (d) for W = 2048, scan the first-latch offset E: can t=+342 and t=-342 both be satisfied at once?
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    FILE*f=fopen("we_dtable.txt","w");
    fprintf(f,"# complete required sign asymmetry of the FSCA square latch\n");
    fprintf(f,"# a = |t| ; Dlo = JL(-a)-JH(a) ; Dhi = JH(-a)-JL(a) ; J(t) must satisfy Dlo <= J(-a)-J(a) <= Dhi\n");
    fprintf(f,"# cplo/cphi, cmlo/cmhi = the same windows in c-units/2^17 (c = J*2^17 - KC*t^2), i.e. the\n");
    fprintf(f,"# accumulated truncation of the square path in J LSBs.  dc = the minimum required c(-a)-c(a).\n");
    fprintf(f,"# a  Dlo  Dhi   cp_lo    cp_hi    cm_lo    cm_hi    dc_min   e1sym(a%%4==0)\n");
    int nf=0,nbig=0;
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!JOK[ip]||!JOK[im]){ fprintf(f,"%4d   infeasible\n",a); continue; }
        i64 dlo=JLo[im]-JHi[ip], dhi=JHi[im]-JLo[ip];
        i64 q=(i64)KC*a*a; const double M=131072.0;
        double cpl=(JLo[ip]*131072.0-q)/M, cph=((JHi[ip]+1)*131072.0-1-q)/M;
        double cml=(JLo[im]*131072.0-q)/M, cmh=((JHi[im]+1)*131072.0-1-q)/M;
        if(dlo>0){nf++; if(dlo>2)nbig++;}
        fprintf(f,"%4d %5lld %5lld  %8.4f %8.4f %8.4f %8.4f  %8.4f   %d\n",a,(long long)dlo,(long long)dhi,
            cpl,cph,cml,cmh,(dlo>0? cml-cph:0.0),(a%4)==0); }
    fprintf(f,"# forced D>=1: %d ; of those needing D>2: %d\n",nf,nbig); fclose(f);
    printf("wrote we_dtable.txt : %d |t| force D>=1, %d of them need D>2\n",nf,nbig);
    // (c)
    puts("\n(c) granularity window: D(342) = delta*W*342/2^17 + W*(hp-hm)/2^17, |hp-hm| <= 64");
    for(int lw=8;lw<=13;lw++){ i64 W=1ll<<lw; double base=W*342.0/131072.0, slack=W*64.0/131072.0;
        printf("   W=2^%-2d : delta=1 gives D in [%.2f,%.2f]  -> can D be in [6,7]? %s\n",lw,base-slack,base+slack,
            (base+slack>=6.0&&base-slack<=7.0)?"YES":"no"); }
    // (d)
    puts("\n(d) W=2048 (s1=24,s2=19): scan the first-latch offset E (I1 = floor((KC*t+E)/2048))");
    int best=-1; i64 bE=-1; int both=0;
    for(i64 E=0;E<2048;E++){ int ok342=0;
        for(int t : {342,-342}){ i64 I1=((i64)KC*t+E)>>11; i64 J=(arr(I1,(i64)t<<13)+K)>>19;
            if(J>=JLo[t+512]&&J<=JHi[t+512]) ok342++; }
        if(ok342>best){best=ok342;bE=E;} if(ok342==2)both++;
        // full score for this E
    }
    printf("   best over E: %d of the two t=+-342 satisfied (E=%lld) ; E values satisfying both: %d\n",best,(long long)bE,both);
    int gb=-1; i64 gE=0;
    for(i64 E=0;E<2048;E++){ int s=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue;
            i64 I1=((i64)KC*t+E)>>11; i64 J=(arr(I1,(i64)t<<13)+K)>>19; if(J>=JLo[t+512]&&J<=JHi[t+512])s++; }
        if(s>gb){gb=s;gE=E;} }
    printf("   best overall jwin score at W=2048 with a single constant E: %d (E=%lld)\n",gb,(long long)gE);
    return 0; }
