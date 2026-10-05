// v20_land2.cpp -- ADVERSARIAL VERIFIER (v20), corrected joint-landmark scan.
// Fixes the soundness hole found in v20_chk check B: the linear bracket is only valid while the
// intermediate latch I1 stays inside the array's SIGNED 28-bit recoder.  Configs where it does not
// are counted separately (they are also unphysical: the latch cannot feed the recoder).
//
// Structure of the argument, per (w,s1,s2):
//   1. rigorous integer bracket B17 = {kc : bound_lo(J(17)) <= 45617 <= bound_hi(J(17))}
//      and B342 likewise.  Any kc hitting a landmark lies in that landmark's bracket.
//   2. if B17 and B342 are DISJOINT -> no joint hit is possible, no scan needed (and the gap is a
//      rigorous lower bound on how far apart the two landmarks' demanded constants are).
//   3. otherwise scan B17 n B342 exhaustively with the exact array.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L=1ll<<21, KCHI=(1ll<<27)-1, RECHI=1ll<<27;
static inline i64 fsh(i128 v,int s){ return (i64)(v>>s); }
struct Bnd{ i64 lo,hi; bool ovf; };
static inline Bnd Jbound(int t,int w,int s1,int s2,i64 kc,bool fk){
    i128 dx=(i128)t<<w;
    i128 k1lo=fk?0:(i128)K,k1hi=fk?(((i128)1<<s1)-1):(i128)K;
    i128 k2lo=fk?0:(i128)K,k2hi=fk?(((i128)1<<s2)-1):(i128)K;
    i128 base=(i128)kc*dx;
    i64 I1lo=fsh(base-L+k1lo,s1), I1hi=fsh(base+k1hi,s1);
    bool ovf = (I1lo<=-RECHI)||(I1lo>=RECHI)||(I1hi<=-RECHI)||(I1hi>=RECHI);
    i128 a=(i128)I1lo*dx,b=(i128)I1hi*dx;
    return Bnd{ fsh(std::min(a,b)-L+k2lo,s2), fsh(std::max(a,b)+k2hi,s2), ovf };
}
static inline bool bracket(int t,int w,int s1,int s2,i64 tL,i64 tH,bool fk,i64&lo,i64&hi){
    i64 a=1,b=KCHI;
    if(Jbound(t,w,s1,s2,a,fk).lo>tH) return false;
    if(Jbound(t,w,s1,s2,b,fk).hi<tL) return false;
    { i64 x=a,y=b; while(x<y){ i64 m=x+(y-x)/2; if(Jbound(t,w,s1,s2,m,fk).hi>=tL) y=m; else x=m+1; } lo=x; }
    { i64 x=a,y=b; while(x<y){ i64 m=x+(y-x+1)/2; if(Jbound(t,w,s1,s2,m,fk).lo<=tH) x=m; else y=m-1; } hi=x; }
    return lo<=hi;
}
static inline i64 Jfix(int t,int w,int s1,int s2,i64 kc,i64&I1){
    i64 dx=(i64)t<<w; I1=(arr(kc,dx)+K)>>s1; return (arr(I1,dx)+K)>>s2; }

int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h;
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529],B=JH[529], C=JL[854],D=JH[854], E=JL[170],F=JH[170];
    long long tri=0, phys=0, ovfskip=0, dxbig=0;
    long long r17=0,r342=0, disj=0, inter=0, scanned=0, joint=0, jointfree=0, jall=0;
    double minppm=1e18;
    #pragma omp parallel for schedule(dynamic) collapse(2) \
      reduction(+:tri,phys,ovfskip,dxbig,r17,r342,disj,inter,scanned,joint,jointfree,jall) reduction(min:minppm)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15;
        i128 nom = e>=0 ? ((i128)KC<<e) : ((i128)KC>>(-e));
        if(nom<1||nom>KCHI) continue;
        tri++;
        // physicality of the multiplicand: |dx| < 2^24 for every t in [-512,511]
        bool dxok = ((i64)512<<w) < (1ll<<24); if(!dxok) dxbig++;
        // physicality of the intermediate latch at the nominal constant
        i64 dum, maxI=0; bool ok=true;
        for(int t : {511,-512,342,-342,17}){ i64 dx=(i64)t<<w; i64 i1=(arr((i64)nom,dx)+K)>>s1;
            if(i1>maxI) maxI=i1; if(i1<=-RECHI||i1>=RECHI) ok=false; }
        if(!ok){ ovfskip++; continue; }
        if(dxok) phys++;
        i64 a17,b17,a342,b342;
        bool h1=bracket(17,w,s1,s2,A,B,false,a17,b17);
        bool h2=bracket(342,w,s1,s2,C,D,false,a342,b342);
        if(h1) r17++; if(h2) r342++;
        if(!h1||!h2) continue;
        i64 lo=std::max(a17,a342), hi=std::min(b17,b342);
        if(lo>hi){ disj++;
            double gap = lo-hi-1; double mid = 0.5*((double)a342+(double)b342);
            double ppm = gap/mid*1e6; if(ppm<minppm) minppm=ppm; continue; }
        inter++;
        int gj=0,gf=0,ga=0;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w;
        for(i64 kc=lo;kc<=hi;kc++){ scanned++;
            i64 i1a,i1b; i64 j17=Jfix(17,w,s1,s2,kc,i1a), jp=Jfix(342,w,s1,s2,kc,i1b);
            if(j17>=A&&j17<=B&&jp>=C&&jp<=D){ gj=1; i64 i1c; i64 jm=Jfix(-342,w,s1,s2,kc,i1c); if(jm>=E&&jm<=F) ga=1;
#pragma omp critical
                printf("!!! FIXED-K JOINT HIT w=%d s1=%d s2=%d kc=%lld\n",w,s1,s2,(long long)kc); }
            // free K1,K2 over one full period each
            i64 P17=arr(kc,dx17), Pp=arr(kc,dxp);
            i64 v17=P17>>s1, vp=Pp>>s1;
            i64 e17=((i64)1<<s1)-(P17-(v17<<s1)), ep=((i64)1<<s1)-(Pp-(vp<<s1));
            for(int c17=0;c17<2;c17++) for(int cp=0;cp<2;cp++){
                bool ok1; if(!c17&&!cp) ok1=true;
                else if(c17&&cp) ok1=(e17<((i64)1<<s1))&&(ep<((i64)1<<s1));
                else if(c17)     ok1=(e17<ep)&&(e17<((i64)1<<s1));
                else             ok1=(ep<e17)&&(ep<((i64)1<<s1));
                if(!ok1) continue;
                i64 Q17=arr(v17+c17,dx17), Qp=arr(vp+cp,dxp);
                i64 q17=Q17>>s2, qp=Qp>>s2;
                i64 f17=((i64)1<<s2)-(Q17-(q17<<s2)), fp=((i64)1<<s2)-(Qp-(qp<<s2));
                for(int d17=0;d17<2;d17++) for(int dp=0;dp<2;dp++){
                    if(q17+d17<A||q17+d17>B) continue; if(qp+dp<C||qp+dp>D) continue;
                    bool ok2; if(!d17&&!dp) ok2=true;
                    else if(d17&&dp) ok2=(f17<((i64)1<<s2))&&(fp<((i64)1<<s2));
                    else if(d17)     ok2=(f17<fp)&&(f17<((i64)1<<s2));
                    else             ok2=(fp<f17)&&(fp<((i64)1<<s2));
                    if(ok2){ gf=1;
#pragma omp critical
                        printf("!!! FREE-K JOINT HIT w=%d s1=%d s2=%d kc=%lld c=(%d,%d) d=(%d,%d)\n",w,s1,s2,(long long)kc,c17,cp,d17,dp); }
                }
            }
        }
        joint+=gj; jointfree+=gf; jall+=ga;
    }
    printf("scale-admissible (w,s1,s2) triples                    : %lld\n",tri);
    printf("  ... rejected: intermediate latch outside +-2^27      : %lld\n",ovfskip);
    printf("  ... surviving, and with |dx| < 2^24 for all t        : %lld\n",phys);
    printf("  ... surviving triples with |dx| >= 2^24 somewhere    : %lld (kept, but unphysical multiplicand)\n",dxbig);
    printf("brackets non-empty  t=17 / t=342                       : %lld / %lld\n",r17,r342);
    printf("triples where the two brackets are PROVABLY DISJOINT   : %lld   (min separation %.2f ppm)\n",disj,minppm);
    printf("triples needing an exhaustive scan (brackets overlap)  : %lld\n",inter);
    printf("kc values exhaustively scanned in those overlaps       : %lld\n\n",scanned);
    printf("FIXED K1=K2=2^19 : triples where ONE kc gives BOTH landmarks : %lld\n",joint);
    printf("                   ... and J(-342) in window too             : %lld\n",jall);
    printf("FREE K1,K2       : triples where ONE (kc,K1,K2) gives BOTH   : %lld\n",jointfree);
    return 0;
}
