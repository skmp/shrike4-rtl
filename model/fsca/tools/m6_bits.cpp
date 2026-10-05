// m6_bits.cpp -- t's width and ALIGNMENT decide which of its bits reach the array's truncated columns.
// The validated loss law is  arr(rec,mul) = rec*mul + loss(rec mod 2^18, mul mod 2^16).
// With dx = t<<13, dx mod 2^16 = (t&7)<<13 -- only THREE bits of t reach the first multiply's loss.
// If the residual tracks those bits, the operand alignment is implicated.
#include "base2.h"
#include <cstdio>
#include <cstring>
static i64 JL[1024],JH[1024]; static bool have[1024];
static i64 J0(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; }
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    printf("how many DISTINCT loss values does each multiply see, as t sweeps?\n");
    { long long seen[8]; int nd=0; i64 vals[64];
      for(int t=0;t<1024;t++){ i64 dx=(i64)t<<13; i64 lo = arr(KC,dx) - (i64)KC*dx;
        bool nw=true; for(int i=0;i<nd;i++) if(vals[i]==lo) nw=false;
        if(nw&&nd<64) vals[nd++]=lo; }
      printf("  first multiply arr(KC, t<<13): %d distinct loss values over t=0..1023", nd);
      printf("   (dx mod 2^16 = (t&7)<<13, so at most 8)\n"); (void)seen; }
    printf("\nresidual (model J - nearest window edge) grouped by LOW BITS of t:\n");
    for(int mask=1;mask<=63;mask=mask*2+1){
        printf("  t & %2d :",mask);
        for(int v=0;v<=mask;v++){ double s=0;int c=0;
            for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; if((t&mask)!=v)continue;
                i64 j=J0(t); i64 d=j<JL[i]?j-JL[i]:(j>JH[i]?j-JH[i]:0); s+=d;c++; }
            if(c) printf(" %+5.2f",s/c); }
        printf("\n"); if(mask>=15) break;
    }
    printf("\nresidual grouped by t&7, SPLIT BY SIGN (mean, and #out-of-window):\n");
    for(int sgn=1;sgn>=-1;sgn-=2){ printf("  t%s:",sgn>0?">0":"<0");
        for(int v=0;v<8;v++){ double s=0;int c=0,bad=0;
            for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; if(t==0||(t>0)!=(sgn>0))continue;
                if((t&7)!=v)continue; i64 j=J0(t); i64 d=j<JL[i]?j-JL[i]:(j>JH[i]?j-JH[i]:0); s+=d;c++; if(d)bad++; }
            printf("  %d:%+.2f/%d",v,c?s/c:0.0,bad); }
        printf("\n"); }
    return 0; }
