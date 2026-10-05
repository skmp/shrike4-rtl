// n7_shape.cpp -- what SHAPE must the J correction have?  Each candidate parameter perturbs J in a
// characteristic way; if the required correction cannot have that shape, that parameter is ruled out.
//   delta = c            <- an additive term after the multiply
//   delta = a*t          <- a linear/operand-offset effect (cross term 2*dx*p)
//   delta = a*t^2        <- EXACTLY what changing KC does
//   delta = a*|t|        <- a sign-magnitude additive effect
//   delta = a*(t&7)      <- the array loss (only 3 bits of t reach it)
#include "base2.h"
#include <cstdio>
#include <climits>
#include <algorithm>
#include <vector>
static i64 JL[1024],JH[1024]; static bool have[1024];
static i64 Jm(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; }
static i64 dl[1024],dh[1024];
// feasible alpha interval for delta = alpha * f(t), alpha real
static void fit(const char* name, double (*f)(int)){
    double lo=-1e300, hi=1e300; int zviol=0;
    for(int i=0;i<1024;i++){ if(!have[i])continue; int t=i-512; double q=f(t);
        if(q==0){ if(!(dl[i]<=0&&0<=dh[i])) zviol++; continue; }
        double a=(double)dl[i]/q, b=(double)dh[i]/q; if(q<0) std::swap(a,b);
        lo=std::max(lo,a); hi=std::min(hi,b); }
    printf("  %-22s alpha in [%.6g, %.6g]  %s   (f(t)=0 offsets violating: %d)\n",
        name,lo,hi, lo>hi? "EMPTY -> this parameter CANNOT supply the correction":"feasible", zviol);
}
static double f_c(int){ return 1.0; }
static double f_t(int t){ return (double)t; }
static double f_t2(int t){ return (double)t*t; }
static double f_at(int t){ return (double)(t<0?-t:t); }
static double f_lo(int t){ return (double)(t&7); }
static double f_tl(int t){ int a=t<0?-t:t; return (double)(a&7); }
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    for(int i=0;i<1024;i++) if(have[i]){ i64 j=Jm(i-512); dl[i]=JL[i]-j; dh[i]=JH[i]-j; }
    printf("allowed correction interval width and required magnitude, by |t| band:\n");
    printf("  |t| band     n   need!=0   median |req|   max |req|   median window width\n");
    for(int b=0;b<8;b++){ int n=0,nz=0; std::vector<i64> req,wid;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512,a=t<0?-t:t; if(a/64!=b)continue; n++;
            i64 r = dl[i]>0? dl[i] : (dh[i]<0? dh[i]:0); if(r)nz++;
            req.push_back(r<0?-r:r); wid.push_back(dh[i]-dl[i]); }
        if(!n)continue; std::sort(req.begin(),req.end()); std::sort(wid.begin(),wid.end());
        printf("  %3d-%-3d  %5d %8d %14lld %11lld %20lld\n",b*64,b*64+63,n,nz,
            (long long)req[req.size()/2],(long long)req.back(),(long long)wid[wid.size()/2]); }
    printf("\nCAN A SINGLE PARAMETER SUPPLY IT?  (feasible alpha over ALL 1024 offsets)\n");
    fit("delta = c (additive)",f_c);
    fit("delta = a*t",f_t);
    fit("delta = a*t^2   [= KC]",f_t2);
    fit("delta = a*|t|",f_at);
    fit("delta = a*(t&7) [loss]",f_lo);
    fit("delta = a*(|t|&7)",f_tl);
    printf("\nKC sensitivity: one unit of KC moves J by about KC_unit*t^2/2^15\n");
    for(int t : {17,64,171,342,511}) printf("   t=%4d : dJ per unit of KC = %.4f\n",t,(double)t*t/32768.0);
    return 0; }
