// we_miss.cpp -- full diagnostic of the 96 t the current chain misses.
// Exact internal quantities:  I1 = floor((KC*t + 48 - g)/1024) ; r1 = (KC*t + 48 - g) mod 1024 ;
// J = floor((I1*t + 64 - h)/128) ; h = -e2/8192.  Prints miss direction and every candidate predictor.
#include "we_win.h"
#include <cstring>
#include <algorithm>
#include <map>
static const int GT[8]={0,5,2,7,4,1,6,3};
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    printf("# t   sgn |t|%%8 g  r1    h   J      JL      JH   miss  KCt%%1024 KCt%%2048 KCt%%256\n");
    std::map<int,int> hm_miss,hm_all; std::map<int,int> r1b_miss,r1b_all;
    int nm=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 dx=(i64)t<<13;
        i64 e1=arr(KC,dx)-(i64)KC*dx; i64 g=(-e1/8192)-16;
        i64 num=(i64)KC*t+48-g; i64 I1=num>>10; i64 r1=num-(I1<<10);
        i64 e2=arr(I1,dx)-I1*dx; i64 h=-e2/8192;
        i64 J=(arr(I1,dx)+K)>>20;
        i64 kt=(i64)KC*t; i64 m1024=((kt%1024)+1024)%1024, m2048=((kt%2048)+2048)%2048, m256=((kt%256)+256)%256;
        int miss = J<JLo[t+512] ? (int)(J-JLo[t+512]) : (J>JHi[t+512] ? (int)(J-JHi[t+512]) : 0);
        hm_all[(int)h]++; r1b_all[(int)(r1/64)]++;
        if(miss){ nm++; hm_miss[(int)h]++; r1b_miss[(int)(r1/64)]++;
            printf("%5d %3d %4d %2lld %5lld %3lld %8lld %8lld %8lld %+4d  %5lld %5lld %4lld\n",
                t,t<0?-1:1,((t%8)+8)%8,(long long)g,(long long)r1,(long long)h,(long long)J,
                (long long)JLo[t+512],(long long)JHi[t+512],miss,(long long)m1024,(long long)m2048,(long long)m256); } }
    printf("misses=%d\n",nm);
    printf("\nh distribution (miss/all): "); for(auto&p:hm_all) printf("%d:%d/%d ",p.first,hm_miss[p.first],p.second); puts("");
    printf("r1/64 bucket (miss/all): "); for(auto&p:r1b_all) printf("%d:%d/%d ",p.first,r1b_miss[p.first],p.second); puts("");
    return 0; }
