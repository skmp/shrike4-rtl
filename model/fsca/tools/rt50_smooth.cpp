// rt50_smooth.cpp -- DIAGNOSTIC (long double, scale estimate only).  rt49: the hardware's J deviates from the exact product
// by a pseudo-random -3..+4.5 coarse units.  Is that "a smooth function of t minus an ordinary truncation loss"?  For the
// fine windows [JL,JH] of jwin18.txt (fine unit = 1/4 coarse) find the smooth g(t) = a t^2 + b t + d |t| + c (deviation
// from KC t^2 / 2^15) minimising the band width U such that [g(t) - U, g(t)] meets every window:
//     U(a,b,d) = max_t (lo_t - g_t) - min_t (hi_t - g_t)     (convex; nested golden section)
// Reported for nested shape families and separately for t >= 0 / t < 0.  U <= 4 fine units would mean "one ordinary latch".
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>
typedef long double ld; static std::vector<int> TT; static std::vector<ld> LO,HI;
static ld width(ld a,ld b,ld d,int sel,ld*c=nullptr){ ld mx=-1e30L,mn=1e30L; for(size_t i=0;i<TT.size();i++){ int t=TT[i]; if(sel==1&&t<0)continue; if(sel==2&&t>=0)continue; ld g=a*t*t+b*t+d*fabsl((ld)t); if(LO[i]-g>mx)mx=LO[i]-g; if(HI[i]-g<mn)mn=HI[i]-g; } if(c)*c=mx; return mx-mn; }
static ld gs(ld lo,ld hi,auto f){ const ld ph=0.6180339887498949L; ld x1=hi-ph*(hi-lo),x2=lo+ph*(hi-lo),f1=f(x1),f2=f(x2); for(int i=0;i<70;i++){ if(f1<f2){hi=x2;x2=x1;f2=f1;x1=hi-ph*(hi-lo);f1=f(x1);} else {lo=x1;x1=x2;f1=f2;x2=lo+ph*(hi-lo);f2=f(x2);} } return (lo+hi)/2; }
int main(){ FILE*f=fopen("jwin18.txt","r"); if(!f)return 1; char line[256]; const ld KCf=(ld)0x4ef145/32768.0L;
    while(fgets(line,sizeof line,f)){ if(line[0]=='#')continue; int t; long long l,h,w; if(sscanf(line,"%d %lld %lld %lld",&t,&l,&h,&w)<3)continue; TT.push_back(t); LO.push_back((ld)l-KCf*t*t); HI.push_back((ld)h-KCf*t*t); } fclose(f);
    printf("%zu windows (fine units; 4 fine = 1 coarse J unit)\n",TT.size());
    const char*SN[3]={"all t","t >= 0","t < 0"};
    for(int sel=0;sel<3;sel++){ printf("\n%s\n",SN[sel]);
        { ld c; ld U=width(0,0,0,sel,&c); printf("   g = c                        : U = %7.2f fine   (c = %+.2f)\n",(double)U,(double)c); }
        { ld a=gs(-1e-3L,1e-3L,[&](ld a_){return width(a_,0,0,sel);}); ld c; ld U=width(a,0,0,sel,&c); printf("   g = a t^2 + c                : U = %7.2f fine   (a = %+.3e = KC %+.3f LSB, c = %+.2f)\n",(double)U,(double)a,(double)(a*32768),(double)c); }
        { ld bb=0; ld a=gs(-1e-3L,1e-3L,[&](ld a_){ ld b=gs(-0.2L,0.2L,[&](ld b_){return width(a_,b_,0,sel);}); return width(a_,b,0,sel); }); bb=gs(-0.2L,0.2L,[&](ld b_){return width(a,b_,0,sel);}); ld c; ld U=width(a,bb,0,sel,&c); printf("   g = a t^2 + b t + c          : U = %7.2f fine   (a = %+.3e, b = %+.5f, c = %+.2f)\n",(double)U,(double)a,(double)bb,(double)c); }
        if(sel==0){ ld a=gs(-1e-3L,1e-3L,[&](ld a_){ ld b=gs(-0.2L,0.2L,[&](ld b_){ ld d=gs(-0.2L,0.2L,[&](ld d_){return width(a_,b_,d_,sel);}); return width(a_,b_,d,sel); }); ld d=gs(-0.2L,0.2L,[&](ld d_){return width(a_,b,d_,sel);}); return width(a_,b,d,sel); });
            ld b=gs(-0.2L,0.2L,[&](ld b_){ ld d=gs(-0.2L,0.2L,[&](ld d_){return width(a,b_,d_,sel);}); return width(a,b_,d,sel); }); ld d=gs(-0.2L,0.2L,[&](ld d_){return width(a,b,d_,sel);}); ld c; ld U=width(a,b,d,sel,&c);
            printf("   g = a t^2 + b t + d |t| + c  : U = %7.2f fine   (a = %+.3e, b = %+.5f, d = %+.5f, c = %+.2f)\n",(double)U,(double)a,(double)b,(double)d,(double)c); } }
    return 0; }
