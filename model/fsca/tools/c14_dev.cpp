// c14_dev.cpp -- at (s1,s2) = (24,17) the chain gets the sign asymmetry exactly right (D(342) = 22, and
// J(-342) lands inside its window) but J sits ~+7..+9 too high.  What does that deviation look like across
// every tightly pinned offset?  If it is constant the fix is a constant; if it drifts, the drift names the
// missing term.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024];
static bool load(const char*p){ FILE*f=fopen(p,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){JL[t+512]=a;JH[t+512]=b;} } fclose(f); return true; }
int main(int argc,char**argv){ build_tables(); if(!load("jwin18.txt")){puts("no jwin18.txt");return 1;}
    int s1=argc>1?atoi(argv[1]):24, s2=41-s1; i64 K1=argc>2?atoll(argv[2]):0, K2=argc>3?atoll(argv[3]):0;
    auto J=[&](int t){ i64 dx=(i64)t<<13; i64 L=(arr(KC,dx)+K1)>>s1; return (arr(L,dx)+K2)>>s2; };
    printf("chain s1=%d s2=%d K1=%lld K2=%lld ; deviation = model J - required window\n",s1,s2,(long long)K1,(long long)K2);
    puts("   t  width   requiredJ        modelJ     dev(lo)  dev(hi)   dev/|t|   dev/t^2*1e5");
    int nin=0,ntot=0; std::vector<double> devs;
    for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i])continue; ntot++;
        i64 j=J(t); if(j>=JL[i]&&j<=JH[i]) nin++;
        if(JH[i]-JL[i]>6) continue;
        i64 dlo=j-JL[i], dhi=j-JH[i];
        printf("%5d %5lld  %10lld  %12lld  %+7lld %+7lld   %+8.4f  %+8.3f\n",t,(long long)(JH[i]-JL[i]+1),
            (long long)JL[i],(long long)j,(long long)dlo,(long long)dhi,
            t? (double)dlo/(double)(t<0?-t:t):0.0, (double)dlo/((double)t*t)*1e5);
        devs.push_back((double)dlo); }
    printf("\nin-window: %d of %d feasible t\n",nin,ntot);
    if(devs.size()){ double s=0,mn=1e18,mx=-1e18; for(double d:devs){s+=d; mn=std::min(mn,d); mx=std::max(mx,d);}
        printf("deviation over the pinned offsets: mean %.2f, min %.0f, max %.0f\n",s/devs.size(),mn,mx); }
    return 0; }
