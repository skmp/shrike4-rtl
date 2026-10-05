// w4_cap.cpp -- BRANCH 4 (a): the CAPABILITY map.  For each operand alignment w and shift split (s1,s2),
// with the rounding constants free AND the two array losses free anywhere in the array's proved range,
// can the chain's output even REACH the three pinned J values?  This is an upper bound on any
// array-loss / truncation-alignment hypothesis at that alignment: nothing about the loss function
// matters beyond its range.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
static const i64 LMAX=1ll<<16, LMIN=-655360;
// envelope of J(t) with L1,L2 free in [LMIN,LMAX] and K1,K2 free in [0,2^s)
static void env(int w,int s1,int s2,i64 KC,int t,i64&lo,i64&hi){
    i64 dx=(i64)t<<w; __int128 P1=(__int128)KC*dx;
    __int128 I1lo=(P1+LMIN)>>s1, I1hi=(P1+LMAX+(((__int128)1<<s1)-1))>>s1;
    __int128 a=I1lo*dx, b=I1hi*dx; __int128 Plo=std::min(a,b), Phi=std::max(a,b);
    lo=(i64)((Plo+LMIN)>>s2); hi=(i64)((Phi+LMAX+(((__int128)1<<s2)-1))>>s2); }
int main(){ load();
    printf("capability of an alignment = its generous envelope contains the required J.\n");
    printf("  w   (s1,s2) pairs in range   capable at t=17   at t=342   at t=-342   ALL THREE   widths at the canonical split\n");
    for(int w=2;w<=24;w++){
        if(512.0*pow(2.0,w)>=(double)(1ll<<27)) continue;
        int npair=0,c17=0,c342=0,cm=0,call=0;
        for(int s1=2;s1<=44;s1++) for(int s2=2;s2<=44;s2++){
            int E=s1+s2-2*w+2; if(E<6||E>22) continue;
            double kcf=39.478417604357434*pow(2.0,(double)E); if(kcf>=(double)(1ll<<27)) continue;
            i64 KCi=(i64)(kcf+0.5); if(KCi<64) continue;
            if(kcf*512*pow(2.0,w)/pow(2.0,s1)>=(double)(1ll<<27)) continue;
            npair++;
            i64 lo,hi; bool a=false,b=false,c=false;
            env(w,s1,s2,KCi,17,lo,hi);   if(!(JH[529]<lo||JL[529]>hi)) a=true;
            env(w,s1,s2,KCi,342,lo,hi);  if(!(JH[854]<lo||JL[854]>hi)) b=true;
            env(w,s1,s2,KCi,-342,lo,hi); if(!(JH[170]<lo||JL[170]>hi)) c=true;
            c17+=a;c342+=b;cm+=c;call+=(a&&b&&c); }
        // canonical split for this w keeps the model's scale: s1=23+(w-13), s2=18+(w-13)
        int s1c=23+(w-13), s2c=18+(w-13); char buf[128]="";
        if(s1c>=2&&s2c>=2){ i64 lo,hi; env(w,s1c,s2c,(i64)0x4ef145,17,lo,hi);
            snprintf(buf,128,"s=(%d,%d) t=17 envelope [%lld,%lld]",s1c,s2c,(long long)lo,(long long)hi); }
        printf("  %2d        %4d              %4d          %4d       %4d       %4d      %s%s\n",
            w,npair,c17,c342,cm,call,buf,w==13?"  <- the model":"");
    }
    printf("\nAll capable configurations have s2 small enough that one array loss is worth >= 1 latch unit:\n");
    printf("  a loss of L moves J by L/2^s2, so reaching the 13-unit deficit at t=17 needs 2^s2 <= 655360/13 = %d, i.e. s2 <= %d\n",
        655360/13, (int)floor(log2(655360.0/13.0)));
    printf("  the model has s2 = 18 (2^18 = 262144), %.1f times too coarse.\n", 262144.0/(655360.0/13.0));
    return 0; }
