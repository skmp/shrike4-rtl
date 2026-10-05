// m5_mag.cpp -- the two remaining forms of "t is formed wrong":
//  (1) t-LEVEL complement: mag_t = t>=0 ? t : ~t  (so t^2 -> (t+1)^2 for t<0), dx = mag<<13
//  (2) MAGNITUDE-FIRST: operands made positive before both multiplies, so no shift ever floors a
//      negative value; asymmetry then comes only from how the magnitude is taken (1s vs 2s complement).
#include "base2.h"
#include <cstdio>
static i64 JL[1024],JH[1024]; static bool have[1024];
static int score(i64(*g)(int,int),int par){ int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=g(i-512,par); if(j>=JL[i]&&j<=JH[i]) s++; } return s; }
static i64 tlevel(int t,int e){ int m = t>=0? t : (~t + (e+1));       // e=0 -> ~t (1s), e=1 -> -t (2s)
    i64 dx=(i64)m<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; }
static i64 magfirst(int t,int e){ i64 dx = t>=0? ((i64)t<<13) : (((i64)(-t)<<13)+e);
    i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; }
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    printf("targets: J(17)=45617  J(342)=18466836  J(-342) in [18466858,18466867]\n\n");
    printf("(1) t-LEVEL complement  (dx = mag<<13, mag from t):\n");
    for(int e=0;e<=1;e++) printf("   %s : %4d/1024   J(17)=%lld J(342)=%lld J(-342)=%lld\n",
        e? "2s (-t)  ":"1s (~t)  ",score(tlevel,e),(long long)tlevel(17,e),(long long)tlevel(342,e),(long long)tlevel(-342,e));
    printf("\n(2) MAGNITUDE-FIRST (both operands positive, no negative shifts):\n");
    for(int e=-2;e<=2;e++) printf("   |dx| %+d : %4d/1024   J(17)=%lld J(342)=%lld J(-342)=%lld%s\n",
        e,score(magfirst,e),(long long)magfirst(17,e),(long long)magfirst(342,e),(long long)magfirst(-342,e),
        e==-1?"   <- 1s complement":(e==0?"   <- exact magnitude (no asymmetry at all)":""));
    printf("\nfor reference, current model: ");
    { int s=0; for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23;
        i64 j=(arr(I1,dx)+K)>>18; if(j>=JL[i]&&j<=JH[i]) s++; } printf("%d/1024\n",s); }
    return 0; }
