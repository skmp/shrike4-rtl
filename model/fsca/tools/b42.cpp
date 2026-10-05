// b42.cpp -- what the fine-scale latch dataset actually pins, and how far simple closed forms get.
//  (a) the tightly pinned t, against the exact rational KC*t^2/2^15
//  (b) best score for J = floor((KC*t^2 + c) / 2^15)                    (symmetric, 1 parameter)
//  (c) best score for J = floor((KC*t^2 + a*t + b) / 2^15)              (2 parameters, antisymmetric term)
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
int main(){ FILE*f=fopen("jwin18.txt","r"); if(!f){puts("run b39 first");return 1;}
    char line[256]; std::vector<i64> JL(1024),JH(1024);
    while(fgets(line,sizeof line,f)){ if(line[0]=='#')continue; int t; long long lo,hi,w;
        if(sscanf(line,"%d %lld %lld %lld",&t,&lo,&hi,&w)==4){ JL[t+512]=lo; JH[t+512]=hi; } }
    fclose(f);
    puts("(a) tightly pinned t (window width <= 6):  t  [JL,JH]  exact KC*t^2/2^15  deviation");
    for(int t=-512;t<=511;t++){ if(JH[t+512]-JL[t+512]>5) continue; i64 num=(i64)KC*t*t;
        printf("  %5d [%9lld,%9lld]  %14.4Lf  [%+.3Lf,%+.3Lf]\n",t,(long long)JL[t+512],(long long)JH[t+512],
            (long double)num/32768.0L,(long double)(JL[t+512]*32768-num)/32768.0L,(long double)(JH[t+512]*32768-num)/32768.0L); }
    // (b)
    int best=-1; i64 bc=0;
    for(i64 c=-6*32768;c<=6*32768;c++){ int sc=0;
        for(int t=-512;t<=511;t++){ i64 J=((i64)KC*t*t+c)>>15; if(J>=JL[t+512]&&J<=JH[t+512])sc++; }
        if(sc>best){best=sc;bc=c;} }
    printf("\n(b) J = floor((KC*t^2 + c)/2^15): best %d/1024 at c = %lld (%.3f LSB)\n",best,(long long)bc,(double)bc/32768.0);
    // (c)
    int best2=-1; i64 ba=0,bb=0;
    #pragma omp parallel for schedule(dynamic)
    for(i64 a=-40*32768;a<=40*32768;a+=64){ int lb=-1; i64 lbb=0;
        for(i64 b=-6*32768;b<=6*32768;b+=64){ int sc=0;
            for(int t=-512;t<=511;t++){ i64 J=((i64)KC*t*t+a*t+b)>>15; if(J>=JL[t+512]&&J<=JH[t+512])sc++; }
            if(sc>lb){lb=sc;lbb=b;} }
        #pragma omp critical
        if(lb>best2){best2=lb;ba=a;bb=lbb;} }
    printf("(c) J = floor((KC*t^2 + a*t + b)/2^15): best %d/1024 at a = %lld (%.4f LSB/step), b = %lld\n",
        best2,(long long)ba,(double)ba/32768.0,(long long)bb);
    return 0; }
