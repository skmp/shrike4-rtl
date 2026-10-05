// v20_land.cpp -- ADVERSARIAL VERIFIER (branch v20).  Independent joint-landmark scan over
// (w, s1, s2, KCw).  Method differs from w2_land.cpp on purpose:
//   * brackets are computed by BINARY SEARCH on rigorous INTEGER monotone bounds (__int128),
//     not from a long-double slack estimate;
//   * brackets are computed for t=17 AND t=342 and INTERSECTED before scanning, so the scan
//     covers exactly the kc that could possibly serve both;
//   * every bracket is certified afterwards by exact evaluation just outside its ends.
// Chain:  dx = t<<w ;  I1 = (arr(kc,dx)+K1)>>s1 ;  J = (arr(I1,dx)+K2)>>s2
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L = 1ll<<21;              // |arr(a,b) - a*b| bound, verified <= 491120 in v20_base
static const i64 KCHI = (1ll<<27)-1;       // array recodes a SIGNED 28-bit operand
static inline i64 fsh(i128 v,int s){ return (i64)(v >> s); }   // arithmetic (floor) shift on i128

struct Bnd { i64 lo, hi; };
// exact-arithmetic outer bounds on J(kc) for a given t, with the rounding constants either
// pinned (K1=K2=2^19) or free over a full period.
static inline Bnd Jbound(int t,int w,int s1,int s2,i64 kc,bool freeK){
    i128 dx = (i128)t << w;
    i128 k1lo = freeK ? 0 : (i128)K, k1hi = freeK ? (((i128)1<<s1)-1) : (i128)K;
    i128 k2lo = freeK ? 0 : (i128)K, k2hi = freeK ? (((i128)1<<s2)-1) : (i128)K;
    i128 base = (i128)kc*dx;
    i128 P1lo = base - L + k1lo, P1hi = base + k1hi;
    i64 I1lo = fsh(P1lo,s1), I1hi = fsh(P1hi,s1);
    i128 a = (i128)I1lo*dx, b = (i128)I1hi*dx;
    i128 Qlo = std::min(a,b) - L + k2lo, Qhi = std::max(a,b) + k2hi;
    return Bnd{ fsh(Qlo,s2), fsh(Qhi,s2) };
}
// set of kc in [1,KCHI] with Jlo(kc) <= target <= Jhi(kc).  Both bounds are non-decreasing in kc
// (for dx>0 directly; for dx<0 because I1 decreases and I1*dx then increases).
static inline bool bracket(int t,int w,int s1,int s2,i64 tL,i64 tH,bool freeK,i64&lo,i64&hi){
    i64 a=1,b=KCHI;
    if(Jbound(t,w,s1,s2,a,freeK).lo > tH) return false;     // even kc=1 overshoots
    if(Jbound(t,w,s1,s2,b,freeK).hi < tL) return false;     // even kc=max undershoots
    { i64 x=a,y=b; while(x<y){ i64 m=x+(y-x)/2; if(Jbound(t,w,s1,s2,m,freeK).hi>=tL) y=m; else x=m+1; } lo=x; }
    { i64 x=a,y=b; while(x<y){ i64 m=x+(y-x+1)/2; if(Jbound(t,w,s1,s2,m,freeK).lo<=tH) x=m; else y=m-1; } hi=x; }
    return lo<=hi;
}
static inline i64 Jfix(int t,int w,int s1,int s2,i64 kc){
    i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+K)>>s1; return (arr(I1,dx)+K)>>s2; }

int main(){
    { FILE*f=fopen("jwin18.txt","r");
      if(!f){printf("no jwin18\n");return 1;} char ln[512]; long long t,l,h;
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 T17L=JL[529],T17H=JH[529], T342L=JL[854],T342H=JH[854];
    const i64 TMML=JL[170], TMMH=JH[170];
    printf("targets: J(17) in [%lld,%lld]  J(342) in [%lld,%lld]  J(-342) in [%lld,%lld]\n\n",
      (long long)T17L,(long long)T17H,(long long)T342L,(long long)T342H,(long long)TMML,(long long)TMMH);
    long long tri=0, tri_phys=0;
    long long b17=0,b342=0,bint=0, scanned=0;
    long long fx17=0,fx342=0,fxboth=0,fxall=0;
    long long fr17=0,fr342=0,frboth=0,frall=0;
    long long certfail=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) \
      reduction(+:tri,tri_phys,b17,b342,bint,scanned,fx17,fx342,fxboth,fxall,fr17,fr342,frboth,frall,certfail)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        // scale admissibility: the nominal constant KC*2^(s1+s2-2w-15) must fit the signed operand
        int e = s1+s2-2*w-15;
        i128 nom = e>=0 ? ((i128)KC<<e) : ((i128)KC>>(-e));
        if(e<0 && ((i128)KC>>(-e))==0) continue;
        if(nom<1 || nom>KCHI) continue;
        tri++;
        i64 dxmax = (i64)512<<w; if(dxmax < (1ll<<24)) tri_phys++;
        for(int mode=0;mode<2;mode++){                 // 0 = fixed K, 1 = free K
            bool fk = mode==1;
            i64 a17,b17h,a342,b342h;
            bool h1=bracket(17,w,s1,s2,T17L,T17H,fk,a17,b17h);
            bool h2=bracket(342,w,s1,s2,T342L,T342H,fk,a342,b342h);
            if(mode==0){ if(h1)b17++; if(h2)b342++; }
            if(!h1||!h2) continue;
            i64 lo=std::max(a17,a342), hi=std::min(b17h,b342h);
            if(lo>hi) continue;
            if(mode==0) bint++;
            // certify: exact J just outside the bracket must miss (fixed-K only, cheap)
            if(mode==0){
                for(int d=1;d<=2;d++){
                    if(lo-d>=1 && Jfix(17,w,s1,s2,lo-d)>=T17L && Jfix(17,w,s1,s2,lo-d)<=T17H
                       && Jfix(342,w,s1,s2,lo-d)>=T342L && Jfix(342,w,s1,s2,lo-d)<=T342H) certfail++;
                    if(hi+d<=KCHI && Jfix(17,w,s1,s2,hi+d)>=T17L && Jfix(17,w,s1,s2,hi+d)<=T17H
                       && Jfix(342,w,s1,s2,hi+d)>=T342L && Jfix(342,w,s1,s2,hi+d)<=T342H) certfail++;
                }
            }
            i64 dx17=(i64)17<<w, dxp=(i64)342<<w, dxm=-dxp;
            int g17=0,g342=0,gb=0,ga=0;
            for(i64 kc=lo;kc<=hi;kc++){
                if(mode==0) scanned++;
                if(!fk){
                    i64 j17=Jfix(17,w,s1,s2,kc); bool o17=(j17>=T17L&&j17<=T17H);
                    i64 jp =Jfix(342,w,s1,s2,kc); bool op=(jp>=T342L&&jp<=T342H);
                    if(o17) g17=1; if(op) g342=1;
                    if(o17&&op){ gb=1; i64 jm=Jfix(-342,w,s1,s2,kc); if(jm>=TMML&&jm<=TMMH) ga=1; }
                } else {
                    // free K1 in [0,2^s1), K2 in [0,2^s2): each latch takes {v, v+1}; v+1 needs K >= brk
                    i64 P17=arr(kc,dx17), Pp=arr(kc,dxp);
                    i64 v17=P17>>s1, vp=Pp>>s1;
                    i64 e17=((i64)1<<s1)-(P17-(v17<<s1)), ep=((i64)1<<s1)-(Pp-(vp<<s1));  // = 2^s1 if P%2^s1==0
                    for(int c17=0;c17<2;c17++) for(int cp=0;cp<2;cp++){
                        bool ok1;
                        if(!c17&&!cp) ok1=true;
                        else if(c17&&cp) ok1=(e17<((i64)1<<s1))&&(ep<((i64)1<<s1));
                        else if(c17)     ok1=(e17<ep)&&(e17<((i64)1<<s1));
                        else             ok1=(ep<e17)&&(ep<((i64)1<<s1));
                        if(!ok1) continue;
                        i64 Q17=arr(v17+c17,dx17), Qp=arr(vp+cp,dxp);
                        i64 q17=Q17>>s2, qp=Qp>>s2;
                        i64 f17=((i64)1<<s2)-(Q17-(q17<<s2)), fp=((i64)1<<s2)-(Qp-(qp<<s2));
                        for(int d17=0;d17<2;d17++) for(int dp=0;dp<2;dp++){
                            i64 J17=q17+d17, Jp=qp+dp;
                            if(J17<T17L||J17>T17H) continue;
                            if(Jp<T342L||Jp>T342H) continue;
                            bool ok2;
                            if(!d17&&!dp) ok2=true;
                            else if(d17&&dp) ok2=(f17<((i64)1<<s2))&&(fp<((i64)1<<s2));
                            else if(d17)     ok2=(f17<fp)&&(f17<((i64)1<<s2));
                            else             ok2=(fp<f17)&&(fp<((i64)1<<s2));
                            if(ok2){ gb=1;
#pragma omp critical
                                printf("!!! FREE-K JOINT HIT w=%d s1=%d s2=%d kc=%lld c=(%d,%d) d=(%d,%d)\n",
                                          w,s1,s2,(long long)kc,c17,cp,d17,dp);
                            }
                        }
                    }
                }
            }
            if(mode==0){ fx17+=g17; fx342+=g342; fxboth+=gb; fxall+=ga;
                         if(gb){
#pragma omp critical
                                 printf("!!! FIXED-K JOINT HIT w=%d s1=%d s2=%d\n",w,s1,s2); } }
            else       { frboth+=gb; }
        }
    }
    printf("scale-admissible (w,s1,s2) triples            : %lld   (of which dx_max < 2^24, i.e. physical multiplicand: %lld)\n",tri,tri_phys);
    printf("fixed-K brackets non-empty  t=17 / t=342      : %lld / %lld\n",b17,b342);
    printf("fixed-K triples with a NON-EMPTY intersection : %lld\n",bint);
    printf("fixed-K kc values densely scanned (intersect) : %lld\n",scanned);
    printf("bracket certification failures                : %lld\n\n",certfail);
    printf("FIXED K1=K2=2^19:\n");
    printf("  triples where some kc in the intersection gives J(17)  : %lld\n",fx17);
    printf("  triples where some kc in the intersection gives J(342) : %lld\n",fx342);
    printf("  triples where ONE kc gives BOTH                        : %lld\n",fxboth);
    printf("  ... and J(-342) in window too                          : %lld\n\n",fxall);
    printf("FREE K1 in [0,2^s1), K2 in [0,2^s2):\n");
    printf("  triples where ONE (kc,K1,K2) gives BOTH                : %lld\n",frboth);
    return 0;
}
