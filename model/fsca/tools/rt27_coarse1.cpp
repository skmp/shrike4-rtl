// rt27_coarse1.cpp -- rt24 turned around: is the FIRST latch COARSER than floor(KC dx / 2^23)?
// rt26: "hardware above the reference" is forced only for |t| >= 256 and reaches +5.6 units above the EXACT product at
// t = -469, where one first-latch LSB is worth only t/128 = 3.7.   I1 = RND(KC t / 1024 / 2^c) * 2^c, c = 0..4,
// RND in {signed floor, toward zero, nearest, away from zero}; J_ref = I1 * t / 128 (real).  Same feasibility count as
// rt24: offsets (of 1022) admitting J_ref - J_hw in [a, a+R), best a.
#include <cstdio>
#include <cmath>
#include <vector>
int main(){ FILE*f=fopen("jwin.txt","r"); static long JL[1024],JH[1024]; int t; long a,b,fl; while(fscanf(f,"%d %ld %ld %ld",&t,&a,&b,&fl)==4){JL[t+512]=a;JH[t+512]=b;}
    const long double KC=0x4ef145; const char* rn[]={"signed floor","toward zero","nearest","away from zero"};
    puts("first latch LSB   rounding         |   R=1          R=2          R=3          R=4       | min R covering all");
    for(int c=0;c<=4;c++) for(int r=0;r<4;r++){ std::vector<long double> lo,hi; long double sc=powl(2,c);
        for(int t=-512;t<=511;t++){ if(!t||JL[t+512]>JH[t+512])continue; long double x=KC*t/1024.0L/sc, I1;
            if(r==0)I1=floorl(x); else if(r==1)I1=(x<0?-1:1)*floorl(fabsl(x)); else if(r==2)I1=(x<0?-1:1)*floorl(fabsl(x)+0.5L); else I1=(x<0?-1:1)*ceill(fabsl(x));
            long double Jf=I1*sc*t/128.0L; lo.push_back(Jf-JH[t+512]); hi.push_back(Jf-JL[t+512]); }
        printf("   2^%d            %-15s |",c,rn[r]); size_t N=lo.size();
        for(int R=1;R<=4;R++){ int best=0; long double ba=0; for(long double a=-10;a<=10;a+=0.0625L){ int cnt=0; for(size_t i=0;i<N;i++) if(hi[i]>=a&&lo[i]<a+R)cnt++; if(cnt>best){best=cnt;ba=a;} } printf(" %4d (a%+5.2Lf)",best,ba); }
        long double bestR=1e9,bA=0; for(long double a=-40;a<=10;a+=0.0625L){ long double need=0; bool ok=true; for(size_t i=0;i<N;i++){ if(hi[i]<a){ok=false;break;} long double q=lo[i]-a; if(q>need)need=q; } if(ok&&need<bestR){bestR=need;bA=a;} }
        printf(" |  R=%.2Lf at a=%+.2Lf\n",bestR,bA); }
    return 0; }
