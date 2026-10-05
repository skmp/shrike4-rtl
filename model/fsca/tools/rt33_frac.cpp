// rt33_frac.cpp -- small |t|: for each offset, the coarse window relative to floor(X), X = KC t^2 / 2^17, and frac(X).
// Classes:  must be < floor(X)  |  must be = floor(X) or above (cannot go below)  |  must be > floor(X)  | free
// If the J latch simply TRUNCATED (no +2^19) we would see "cannot exceed floor(X)" everywhere; if it ROUNDED, offsets
// with frac >= 1/2 would force floor(X)+1.  Tabulated by frac(X) decile.
#include <cstdio>
#include <cmath>
int main(int argc,char**argv){ int TM=argc>1?atoi(argv[1]):200; FILE*f=fopen("jwin.txt","r"); static long JL[1024],JH[1024]; int tt; long a,b,fl; while(fscanf(f,"%d %ld %ld %ld",&tt,&a,&b,&fl)==4){JL[tt+512]=a;JH[tt+512]=b;}
    const long double KC=0x4ef145; int cls[10][5]={{0}};   // per decile: below, pinned-to-floor-or-more(lower edge==0), forced above, free, n
    puts("offsets whose window pins J relative to floor(X) (|window| <= 6):   t   frac(X)   window - floor(X)");
    for(int t=1;t<=TM;t++){ long double X=KC*t*t/131072.0L; long fx=(long)floorl(X); double fr=(double)(X-fx); int d=(int)(fr*10);
        for(int s=1;s>=-1;s-=2){ int i=512+s*t; if(JL[i]>JH[i])continue; long lo=JL[i]-fx, hi=JH[i]-fx; cls[d][4]++;
            if(hi<0)cls[d][0]++; else if(lo>0)cls[d][2]++; else if(lo==0)cls[d][1]++; else cls[d][3]++;
            if(hi-lo<=6) printf("   %4d   %.3f   [%+ld,%+ld]\n",s*t,fr,lo,hi); } }
    puts("\nfrac(X) decile |   n   | J < floor(X) forced | J >= floor(X) forced (lower edge = floor) | J > floor(X) forced | unconstrained");
    for(int d=0;d<10;d++) printf("   0.%d..       | %4d  |        %3d          |                 %3d                       |        %3d          |   %3d\n",d,cls[d][4],cls[d][0],cls[d][1],cls[d][2],cls[d][3]);
    return 0; }
