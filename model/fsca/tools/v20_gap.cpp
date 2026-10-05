// v20_gap.cpp -- (A) the ppm separation between the constant t=17 demands and the one t=342 demands,
// over the triples where both landmarks are individually reachable at K1=K2=2^19.
// (B) the additive-free-bias relaxation: configs whose J(342)-J(17) difference is exactly right.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L=1ll<<21, RECHI=1ll<<27;
static inline i128 cdiv(i128 a,i128 b){ return a>=0?(a+b-1)/b:-((-a)/b); }
static inline i128 fdiv(i128 a,i128 b){ return a>=0?a/b:-((-a+b-1)/b); }
static bool kcrange(i128 dx,int s1,int s2,i64 tL,i64 tH,i64 K1,i64 K2,i128&klo,i128&khi){
    if(dx==0) return false; bool neg=dx<0; i128 ad=neg?-dx:dx;
    i128 Xlo=(i128)tL*((i128)1<<s2)-K2, Xhi=((i128)tH+1)*((i128)1<<s2)-1-K2+L;
    i128 Ilo,Ihi; if(!neg){Ilo=cdiv(Xlo,ad);Ihi=fdiv(Xhi,ad);} else {Ilo=cdiv(-Xhi,ad);Ihi=fdiv(-Xlo,ad);}
    if(Ilo>Ihi) return false;
    i128 Ylo=Ilo*((i128)1<<s1)-K1, Yhi=(Ihi+1)*((i128)1<<s1)-1-K1+L;
    if(!neg){klo=cdiv(Ylo,ad);khi=fdiv(Yhi,ad);} else {klo=cdiv(-Yhi,ad);khi=fdiv(-Ylo,ad);}
    if(klo<1)klo=1; if(khi>RECHI-1)khi=RECHI-1; return klo<=khi;
}
static inline i64 Jf(i64 dx,int s1,int s2,i64 kc,i64 K1,i64 K2){ i64 I=(arr(kc,dx)+K1)>>s1; return (arr(I,dx)+K2)>>s2; }
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529], C=JL[854], E=JL[170], F=JH[170];
    // ---- (A) ppm separation
    double best=1e18; long long nboth=0, capped=0; int bw=0,bs1=0,bs2=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(min:best) reduction(+:nboth,capped)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15; if(e<-30) continue;
        i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<28)) continue;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w;
        i128 a,b; std::vector<i64> S342;
        if(!kcrange(dxp,s1,s2,C,C,K,K,a,b)) continue;
        for(i128 kk=a;kk<=b;kk++) if(Jf(dxp,s1,s2,(i64)kk,K,K)==C) S342.push_back((i64)kk);
        if(S342.empty()) continue;
        if(!kcrange(dx17,s1,s2,A,A,K,K,a,b)) continue;
        if(b-a>200000000){ capped++; continue; }
        std::vector<i64> S17;
        for(i128 kk=a;kk<=b;kk++) if(Jf(dx17,s1,s2,(i64)kk,K,K)==A) S17.push_back((i64)kk);
        if(S17.empty()) continue;
        nboth++;
        double m=1e18;
        for(i64 x : S17){ auto it=std::lower_bound(S342.begin(),S342.end(),x);
            for(int d=-1;d<=0;d++){ auto j=it+d; if(j<S342.begin()||j>=S342.end()) continue;
                double g=(double)(x> *j ? x-*j : *j-x), mid=0.5*((double)x+(double)*j);
                double ppm=g/mid*1e6; if(ppm<m) m=ppm; } }
        if(m<best) best=m;
    }
    printf("(A) triples where BOTH landmarks are individually reachable (K1=K2=2^19) : %lld  (capped %lld)\n",nboth,capped);
    printf("    minimum separation between the constant t=17 demands and t=342 demands: %.2f ppm\n",best);
    printf("    for calibration: KC=%lld vs 4*pi^2 at 2^17 = %.2f  -> %.1f ppm below\n",(long long)KC,
        4.0*M_PI*M_PI*131072.0, (4.0*M_PI*M_PI*131072.0-(double)KC)/(4.0*M_PI*M_PI*131072.0)*1e6);

    // ---- (B) additive free bias
    const i64 DIFF = C - A;
    long long ndiff=0, n3=0; int bestsc=0; long long bw2=0;
    struct Best{ int w,s1,s2; i64 kc,c; int sc; } B{0,0,0,0,0,0};
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:ndiff,n3)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15; if(e<-30) continue;
        i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<28)) continue;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w, dxm=-dxp;
        // any kc with the right difference has J(342) within [DIFF + J17min, DIFF + J17max]; bound J17
        // loosely by the whole plausible band and use the t=342 interval widened accordingly.
        i128 a,b; if(!kcrange(dxp,s1,s2,C-200,C+200,K,K,a,b)) continue;
        if(b-a>50000000) continue;
        for(i128 kk=a;kk<=b;kk++){ i64 kc=(i64)kk;
            i64 j17=Jf(dx17,s1,s2,kc,K,K), jp=Jf(dxp,s1,s2,kc,K,K);
            if(jp-j17!=DIFF) continue; ndiff++;
            i64 c=A-j17; i64 jm=Jf(dxm,s1,s2,kc,K,K)+c;
            if(jm<E||jm>F) continue; n3++;
            int sc=0; for(int t=-512;t<=511;t++){ i64 J=Jf((i64)t<<w,s1,s2,kc,K,K)+c;
                if(J>=JL[t+512]&&J<=JH[t+512]) sc++; }
            #pragma omp critical
            { if(sc>B.sc) B=Best{w,s1,s2,kc,c,sc}; }
        }
    }
    printf("\n(B) additive free bias c on J (only differences matter):\n");
    printf("    required J(342)-J(17) = %lld ; baseline gives %lld\n",(long long)DIFF,
        (long long)(Jf((i64)342<<13,23,18,KC,K,K)-Jf((i64)17<<13,23,18,KC,K,K)));
    printf("    configurations with the difference EXACT      : %lld\n",ndiff);
    printf("    ... and J(-342) also in window with that bias : %lld\n",n3);
    printf("    best full jwin18 score among those            : %d   (w=%d s1=%d s2=%d KCw=%lld c=%lld)\n",
        B.sc,B.w,B.s1,B.s2,(long long)B.kc,(long long)B.c);
    return 0;
}
