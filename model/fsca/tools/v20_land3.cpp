// v20_land3.cpp -- ADVERSARIAL VERIFIER (v20), third and final joint-landmark scan.
// Closed-form exact-integer intervals; no binary search, no monotonicity assumption, no long double.
//
// For a landmark J(t) in [tL,tH] with dx = t<<w > 0 and arr(a,b) = a*b + loss, loss in [-L,0]:
//     arr(I1,dx) must lie in [tL*2^s2 - K2, (tH+1)*2^s2 - 1 - K2]
//  => I1*dx        must lie in [tL*2^s2 - K2, (tH+1)*2^s2 - 1 - K2 + L]
//  => I1           in [Ilo, Ihi]   (exact ceil/floor division)
//     arr(kc,dx)   must lie in [Ilo*2^s1 - K1, (Ihi+1)*2^s1 - 1 - K1]
//  => kc*dx        must lie in [Ilo*2^s1 - K1, (Ihi+1)*2^s1 - 1 - K1 + L]
//  => kc           in [klo, khi]   (exact ceil/floor division)
// Every kc that hits the landmark is in [klo,khi].  Valid while |I1| < 2^27 (the array recodes a
// signed 28-bit operand); Ilo/Ihi are checked against that and the triple is reported if not.
// The recoded constant also satisfies kc in [1,2^27): kc in [2^27,2^28) recodes NEGATIVE and then
// arr(kc,dx)<0 for dx>0, so J<0 and no positive landmark can be hit (asserted below).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L=1ll<<21, RECHI=1ll<<27;
static inline i128 cdiv(i128 a,i128 b){ return a>=0 ? (a+b-1)/b : -((-a)/b); }   // ceil, b>0
static inline i128 fdiv(i128 a,i128 b){ return a>=0 ? a/b : -((-a+b-1)/b); }     // floor, b>0
// exact interval of kc that can give J(t) in [tL,tH]; returns false if empty / out of recoder range
struct Iv{ i128 lo,hi; };
static bool kcrange(int t,int w,int s1,int s2,i64 tL,i64 tH,i64 K1,i64 K2,Iv&out,bool&i1ovf){
    i128 dx=(i128)t<<w; bool neg = dx<0; i128 ad = neg? -dx : dx;
    i128 P2lo=(i128)tL*((i128)1<<s2)-K2, P2hi=((i128)tH+1)*((i128)1<<s2)-1-K2;
    i128 Xlo=P2lo, Xhi=P2hi+L;                 // I1*dx in [Xlo,Xhi]
    i128 Ilo,Ihi;
    if(!neg){ Ilo=cdiv(Xlo,ad); Ihi=fdiv(Xhi,ad); }
    else    { Ilo=cdiv(-Xhi,ad); Ihi=fdiv(-Xlo,ad); }
    if(Ilo>Ihi) return false;
    i1ovf = (Ilo<=-RECHI)||(Ihi>=RECHI);
    i128 P1lo=Ilo*((i128)1<<s1)-K1, P1hi=(Ihi+1)*((i128)1<<s1)-1-K1;
    i128 Ylo=P1lo, Yhi=P1hi+L;                 // kc*dx in [Ylo,Yhi]
    i128 klo,khi;
    if(!neg){ klo=cdiv(Ylo,ad); khi=fdiv(Yhi,ad); }
    else    { klo=cdiv(-Yhi,ad); khi=fdiv(-Ylo,ad); }
    if(klo<1) klo=1; if(khi>RECHI-1) khi=RECHI-1;
    if(klo>khi) return false; out=Iv{klo,khi}; return true;
}
static inline i64 Jf(int t,int w,int s1,int s2,i64 kc,i64 K1,i64 K2,i64&I1){
    i64 dx=(i64)t<<w; I1=(arr(kc,dx)+K1)>>s1; return (arr(I1,dx)+K2)>>s2; }
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h;
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529],B=JH[529], C=JL[854],D=JH[854], E=JL[170],F=JH[170];
    // assert the kc >= 2^27 claim
    { long long bad=0; for(int w=2;w<=24;w+=2) for(int q=0;q<3000;q++){ i64 kc=(1ll<<27)+ (i64)rand()*rand()%(1ll<<27);
        i64 dx=(i64)342<<w; if(arr(kc,dx)>0) bad++; }
      printf("assert: arr(kc,dx)<=0 for kc in [2^27,2^28), dx>0 : %lld violations\n\n",bad); }
    long long tri=0, ovf=0, dxbig=0, disj=0, inter=0; long long scanned=0;
    long long jf=0, jr=0, j3=0, r17=0,r342=0;
    long long minsepnum=0; double minppm=1e18;
    #pragma omp parallel for schedule(dynamic) collapse(2) \
      reduction(+:tri,ovf,dxbig,disj,inter,scanned,jf,jr,j3,r17,r342) reduction(min:minppm)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15;
        i128 nom = e>=0 ? ((i128)KC<<e) : ((i128)KC>>(-e));
        if(e<-30) continue;
        if(nom<1 || nom>=((i128)1<<28)) continue;
        tri++;
        if(((i64)512<<w) >= (1ll<<24)) dxbig++;
        Iv v17,v342; bool o1=false,o2=false;
        bool h1=kcrange(17,w,s1,s2,A,B,K,K,v17,o1);
        bool h2=kcrange(342,w,s1,s2,C,D,K,K,v342,o2);
        if(o1||o2){ ovf++; }
        if(h1) r17++; if(h2) r342++;
        if(!h1||!h2) continue;
        i128 lo=std::max(v17.lo,v342.lo), hi=std::min(v17.hi,v342.hi);
        if(lo>hi){ disj++;
            double g=(double)(lo-hi-1), mid=0.5*((double)(v342.lo)+(double)(v342.hi));
            double ppm=g/mid*1e6; if(ppm<minppm) minppm=ppm; continue; }
        inter++;
        int gf=0,gr=0,g3=0;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w;
        for(i128 kk=lo;kk<=hi;kk++){ i64 kc=(i64)kk; scanned++;
            i64 i1a,i1b; i64 j17=Jf(17,w,s1,s2,kc,K,K,i1a), jp=Jf(342,w,s1,s2,kc,K,K,i1b);
            if(j17>=A&&j17<=B&&jp>=C&&jp<=D){ gf=1; i64 ic; i64 jm=Jf(-342,w,s1,s2,kc,K,K,ic); if(jm>=E&&jm<=F) g3=1;
#pragma omp critical
                printf("!!! FIXED-K JOINT HIT w=%d s1=%d s2=%d kc=%lld\n",w,s1,s2,(long long)kc); }
            i64 P17=arr(kc,dx17), Pp=arr(kc,dxp);
            i64 v1=P17>>s1, v2=Pp>>s1;
            i64 e1=((i64)1<<s1)-(P17-(v1<<s1)), e2=((i64)1<<s1)-(Pp-(v2<<s1));
            for(int c1=0;c1<2;c1++) for(int c2=0;c2<2;c2++){
                bool ok1; if(!c1&&!c2) ok1=true;
                else if(c1&&c2) ok1=(e1<((i64)1<<s1))&&(e2<((i64)1<<s1));
                else if(c1)     ok1=(e1<e2)&&(e1<((i64)1<<s1));
                else            ok1=(e2<e1)&&(e2<((i64)1<<s1));
                if(!ok1) continue;
                i64 Q1=arr(v1+c1,dx17), Q2=arr(v2+c2,dxp);
                i64 q1=Q1>>s2, q2=Q2>>s2;
                i64 f1=((i64)1<<s2)-(Q1-(q1<<s2)), f2=((i64)1<<s2)-(Q2-(q2<<s2));
                for(int d1=0;d1<2;d1++) for(int d2=0;d2<2;d2++){
                    if(q1+d1<A||q1+d1>B) continue; if(q2+d2<C||q2+d2>D) continue;
                    bool ok2; if(!d1&&!d2) ok2=true;
                    else if(d1&&d2) ok2=(f1<((i64)1<<s2))&&(f2<((i64)1<<s2));
                    else if(d1)     ok2=(f1<f2)&&(f1<((i64)1<<s2));
                    else            ok2=(f2<f1)&&(f2<((i64)1<<s2));
                    if(ok2){ gr=1;
#pragma omp critical
                        printf("!!! FREE-K JOINT HIT w=%d s1=%d s2=%d kc=%lld c=(%d,%d) d=(%d,%d)\n",w,s1,s2,(long long)kc,c1,c2,d1,d2); }
                }
            }
        }
        jf+=gf; jr+=gr; j3+=g3;
    }
    printf("scale-admissible triples                          : %lld\n",tri);
    printf("  with |dx| >= 2^24 for some t (unphysical mul)   : %lld\n",dxbig);
    printf("  where the demanded I1 leaves the 28-bit recoder : %lld  (bracket flagged)\n",ovf);
    printf("kc-interval non-empty  t=17 / t=342               : %lld / %lld\n",r17,r342);
    printf("triples where the two kc-intervals are DISJOINT   : %lld  (min separation %.2f ppm)\n",disj,minppm);
    printf("triples with overlap -> exhaustively scanned      : %lld\n",inter);
    printf("kc values exhaustively scanned                    : %lld\n\n",scanned);
    printf("FIXED K1=K2=2^19 : ONE kc gives BOTH landmarks    : %lld\n",jf);
    printf("                   ... and J(-342) too            : %lld\n",j3);
    printf("FREE K1,K2       : ONE (kc,K1,K2) gives BOTH      : %lld\n",jr);
    return 0;
}
