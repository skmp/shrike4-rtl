// v22_hb2.cpp -- corrected decisive H-B test.  For each operand variant and each alignment, the
// stored constant is re-centred ON THAT VARIANT'S OWN SCALE (KCw ~ J(342)*2^(s1+s2)/dx(342)^2),
// then swept over a bracket certified by its endpoints to contain every KCw that can hit J(342).
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
static i64 JL[1024],JH[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f)exit(1); char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;n++;} } fclose(f); if(n!=1024)exit(1); }
struct Cfg{ int w,s1,s2; i64 kc,K1,K2; int num,den,mag; };
static inline i64 mkdx(int t,const Cfg&c){ if(c.num==0) return (i64)t<<c.w;
    int sgn=c.mag?(t<0?-1:1):1; i64 num=(i64)t*c.den+(i64)sgn*c.num; i64 v=num<<c.w;
    if(v%c.den){fprintf(stderr,"inexact\n");exit(1);} return v/c.den; }
static inline i64 Jof(int t,const Cfg&c){ i64 dx=mkdx(t,c); i64 I1=(arr(c.kc,dx)+c.K1)>>c.s1; return (arr(I1,dx)+c.K2)>>c.s2; }
static int jscore(const Cfg&c){ int s=0; for(int i=0;i<1024;i++){ i64 j=Jof(i-512,c); if(j>=JL[i]&&j<=JH[i])s++; } return s; }
int main(){ build_tables(); load_jwin();
    const i64 TA=45617, TB_=18466836;
    struct Var{const char*n;int num,den,mag;};
    Var vars[]={{"unbiased t (the model)",0,1,0},{"t + 1/2 ULP",1,2,0},{"t - 1/2 ULP",-1,2,0},
                {"|t| + 1/2 ULP",1,2,1},{"|t| - 1/2 ULP",-1,2,1},{"t + 1/4 ULP",1,4,0},{"t - 1/4 ULP",-1,4,0},
                {"t + 1 ULP",1,1,0},{"t - 1 ULP",-1,1,0}};
    printf("space: w in [6,18], s2 in [w+8,w+24], s1 in [8,2w+26]; KCw swept over a bracket re-centred\n"
           "on THIS variant's scale, span = max(4096, 2e-4 * centre); K1=K2=2^19.  Reject a triple unless\n"
           "the bracket endpoints straddle the target (so the sweep is certified complete for that triple).\n\n");
    printf("%-24s | triples | hit J(342) | min |J(17)-45617| | best config\n","variant");
    for(auto&v:vars){
        i64 bestd=-1; Cfg bc{}; long long nhit=0,ntrip=0,nunc=0;
        for(int w=6;w<=18;w++){
            // dx must stay inside the 28-bit signed recoder/operand range at |t|=512
            for(int s2=w+8;s2<=w+24;s2++) for(int s1=8;s1<=2*w+26;s1++){
                Cfg probe{w,s1,s2,1,K,K,v.num,v.den,v.mag};
                i64 dxB=mkdx(342,probe); if(dxB<=0) continue;
                if(llabs(mkdx(512,probe))>=(1ll<<27)) continue;
                long double ctr=(long double)TB_*powl(2.0L,(long double)(s1+s2))/((long double)dxB*(long double)dxB);
                if(ctr<16384.0L||ctr>=134217728.0L) continue;
                ntrip++;
                i64 c0=(i64)llroundl(ctr); i64 span=(i64)(ctr*2e-4L)+4096;
                i64 lo=c0-span, hi=c0+span; if(lo<1)lo=1;
                Cfg cl{w,s1,s2,lo,K,K,v.num,v.den,v.mag}, ch{w,s1,s2,hi,K,K,v.num,v.den,v.mag};
                if(!(Jof(342,cl)<TB_ && Jof(342,ch)>TB_)){ nunc++; continue; }   // bracket not certified
                bool any=false;
                for(i64 kc=lo;kc<=hi;kc++){ Cfg c{w,s1,s2,kc,K,K,v.num,v.den,v.mag};
                    if(Jof(342,c)!=TB_) continue; any=true;
                    i64 d=llabs(Jof(17,c)-TA); if(bestd<0||d<bestd){bestd=d;bc=c;} }
                if(any)nhit++; } }
        if(bestd<0) printf("%-24s | %7lld | %10lld | %17s | (uncertified brackets: %lld)\n",v.n,ntrip,nhit,"none",nunc);
        else printf("%-24s | %7lld | %10lld | %17lld | w=%d s1=%d s2=%d KCw=%lld J(17)=%lld J(-342)=%lld jwin18=%d (uncert %lld)\n",
            v.n,ntrip,nhit,(long long)bestd,bc.w,bc.s1,bc.s2,(long long)bc.kc,
            (long long)Jof(17,bc),(long long)Jof(-342,bc),jscore(bc),nunc);
        fflush(stdout); }
    return 0; }
