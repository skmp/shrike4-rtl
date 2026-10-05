// w2_bf.cpp -- brute force, no cleverness: for small-shift geometries enumerate EVERY (KCw,K1,K2)
// in the bracket x full periods and count joint landmark hits.  Cross-check on w2_land's free-constant
// verdict, which uses the two-value/3-of-4 argument instead of enumeration.
#include "base2.h"
#include <cstdio>
#include <cmath>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<27;
static void brk(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    long long tot=0, h342=0, hboth=0, hall=0, cfg=0;
    // target the geometries that actually reach t=342 (from w2_gap): w in [6,8], s2 = 16
    for(int w=6;w<=8;w++) for(int s1=12;s1<=14;s1++) for(int s2=15;s2<=16;s2++){
        long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
        i64 a,b; brk(342,w,s1,s2,a,b); if(b<a||b-a>60000) continue; cfg++;
        i64 d17=(i64)17<<w, dp=(i64)342<<w, dm=(i64)-342<<w;
        #pragma omp parallel for schedule(dynamic) reduction(+:tot,h342,hboth,hall)
        for(i64 kc=a;kc<=b;kc++){
            i64 P17=arr(kc,d17), Pp=arr(kc,dp), Pm=arr(kc,dm);
            for(i64 k1=0;k1<((i64)1<<s1);k1++){ tot++;
                i64 i17=(P17+k1)>>s1, ip=(Pp+k1)>>s1, im=(Pm+k1)>>s1;
                i64 Q17=arr(i17,d17), Qp=arr(ip,dp), Qm=arr(im,dm);
                // exact K2 intervals in [0,2^s2) for each requirement, no cleverness about latch steps
                i64 per2=1ll<<s2;
                auto iv=[&](i64 Q,i64 lo,i64 hi,i64&L,i64&H){ L=lo*per2-Q; H=(hi+1)*per2-1-Q;
                    if(L<0)L=0; if(H>per2-1)H=per2-1; return L<=H; };
                i64 L1,H1,L2,H2,L3,H3;
                if(!iv(Qp,JL[854],JH[854],L1,H1)) continue; h342++;
                if(!iv(Q17,JL[529],JH[529],L2,H2)) continue;
                i64 L=L1>L2?L1:L2, H=H1<H2?H1:H2; if(L>H) continue; hboth++;
                if(!iv(Qm,JL[170],JH[170],L3,H3)) continue;
                i64 LL=L>L3?L:L3, HH=H<H3?H:H3; if(LL<=HH) hall++;
            } } }
    printf("w in [6,8], s1 in [12,14], s2 in [15,16]\ngeometries brute-forced: %lld ; (KCw,K1) pairs enumerated (K2 solved exactly): %lld\n",cfg,tot);
    printf("  hit J(342) exactly              : %lld\n",h342);
    printf("  hit J(342) AND J(17) exactly    : %lld\n",hboth);
    printf("  ... and J(-342) in window       : %lld\n",hall);
    return 0; }
