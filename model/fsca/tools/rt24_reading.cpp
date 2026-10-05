// rt24_reading.cpp -- test of the reading "the first stage is MORE PRECISE than floor(KC dx / 2^23), and the J stage
// loses a data-dependent, one-signed, bounded amount".
// For a first latch with f extra fraction bits, J_f(t) = I1_f * t / 128 (real, no second-stage truncation), where
//   signed :  I1_f = floor(KC t 2^f / 1024 + k1) / 2^f      (two's-complement floor: asymmetric in t, asymmetry t/128/2^f)
//   sym    :  I1_f = sign(t) floor(KC |t| 2^f / 1024 + k1) / 2^f
//   exact  :  I1   = KC t / 1024
// and the hardware's J_hw(t) is an integer in the COARSE window [JL, JH] of jwin.txt (t = +-17 have none; skipped).
// If the reading holds there is an (f, a, R) with   a <= J_f(t) - J_hw(t) < a + R   feasible at EVERY t, R small.
// Reported: for each first stage, the number of offsets (of 1022) that admit a residual in [a, a+R), maximised over a.
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
int main(){ FILE*f=fopen("jwin.txt","r"); static long JL[1024],JH[1024]; int t; long a,b,fl; while(fscanf(f,"%d %ld %ld %ld",&t,&a,&b,&fl)==4){JL[t+512]=a;JH[t+512]=b;}
    const long double KC=0x4ef145;
    struct FS{ const char* nm; int fbits; int mode; long double k1; };   // mode 0 signed floor, 1 symmetric floor, 2 exact
    std::vector<FS> fs={{"f=0 signed, +1/16 (THE MODEL)",0,0,1.0L/16},{"f=0 signed, no K",0,0,0},{"f=1 signed",1,0,0},{"f=2 signed",2,0,0},{"f=3 signed",3,0,0},{"f=4 signed",4,0,0},{"f=6 signed",6,0,0},
                        {"f=0 symmetric",0,1,0},{"f=1 symmetric",1,1,0},{"f=2 symmetric",2,1,0},{"f=3 symmetric",3,1,0},{"exact KC t^2 / 2^17",0,2,0}};
    puts("first stage                         | offsets admitting a residual in [a, a+R), best a (coarse J units)            | min R for all 1022");
    puts("                                    |   R=1          R=2          R=3          R=4          R=5          R=6       |");
    for(auto&F:fs){ std::vector<long double> lo,hi;   // residual window per t: [Jf - JH, Jf - JL]
        for(int t=-512;t<=511;t++){ if(!t||JL[t+512]>JH[t+512])continue; long double x=KC*t/1024.0L, I1; long double sc=powl(2,F.fbits);
            if(F.mode==2)I1=x; else if(F.mode==0)I1=floorl(x*sc+F.k1*sc)/sc; else I1=(t<0?-1:1)*floorl(fabsl(x)*sc+F.k1*sc)/sc;
            long double Jf=I1*t/128.0L; lo.push_back(Jf-JH[t+512]); hi.push_back(Jf-JL[t+512]); }
        printf("%-35s |",F.nm); size_t N=lo.size();
        for(int R=1;R<=6;R++){ int best=0; long double ba=0; for(long double a=-8;a<=8;a+=0.0625L){ int c=0; for(size_t i=0;i<N;i++) if(hi[i]>=a && lo[i]<a+R) c++; if(c>best){best=c;ba=a;} } printf(" %4d (a%+5.2Lf)",best,ba); }
        // minimal R with all feasible
        long double bestR=1e9,bA=0; for(long double a=-12;a<=8;a+=0.0625L){ long double need=0; bool ok=true; for(size_t i=0;i<N;i++){ if(hi[i]<a){ok=false;break;} long double r=lo[i]-a; if(r>need)need=r; } if(ok&&need<bestR){bestR=need;bA=a;} }
        printf(" |  R=%.2Lf at a=%+.2Lf (%zu offsets)\n",bestR,bA,N); }
    return 0; }
