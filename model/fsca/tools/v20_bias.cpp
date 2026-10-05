// v20_bias.cpp -- part (B) redone with a window on J(342) that is widened by the actual chain slack
// (the previous +-200 was too tight at small s2).  Additive free bias c on J: only differences matter.
#include "base2.h"
#include <cstdio>
#include <cstring>
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
static inline i64 Jf(i64 dx,int s1,int s2,i64 kc){ i64 I=(arr(kc,dx)+K)>>s1; return (arr(I,dx)+K)>>s2; }
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529], C=JL[854], E=JL[170], F=JH[170], DIFF=C-A;
    long long ndiff=0,n3=0,scanned=0,skipped=0;
    struct Best{ int w,s1,s2; i64 kc,c; int sc; } B{0,0,0,0,0,0};
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:ndiff,n3,scanned,skipped)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15; if(e<-30) continue;
        i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<28)) continue;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w, dxm=-dxp;
        // slack on J(342) given only that the DIFFERENCE is exact
        i128 slack = ((i128)L>>s2) + (((i128)L*dxp)>>(s1+s2)) + 64;
        slack *= 4;
        i128 a,b; if(!kcrange(dxp,s1,s2,(i64)(C-slack),(i64)(C+slack),K,K,a,b)) continue;
        if(b-a>60000000){ skipped++; continue; }
        for(i128 kk=a;kk<=b;kk++){ i64 kc=(i64)kk; scanned++;
            i64 j17=Jf(dx17,s1,s2,kc), jp=Jf(dxp,s1,s2,kc);
            if(jp-j17!=DIFF) continue; ndiff++;
            i64 c=A-j17; i64 jm=Jf(dxm,s1,s2,kc)+c;
            if(jm<E||jm>F) continue; n3++;
            int sc=0; for(int t=-512;t<=511;t++){ i64 J=Jf((i64)t<<w,s1,s2,kc)+c;
                if(J>=JL[t+512]&&J<=JH[t+512]) sc++; }
#pragma omp critical
            { if(sc>B.sc) B=Best{w,s1,s2,kc,c,sc}; }
        }
    }
    printf("additive free bias c on J:  required J(342)-J(17) = %lld (baseline %lld, three short)\n",
        (long long)DIFF,(long long)(Jf((i64)342<<13,23,18,KC)-Jf((i64)17<<13,23,18,KC)));
    printf("  kc scanned %lld ; triples skipped for size %lld\n",scanned,skipped);
    printf("  configurations with the difference EXACT       : %lld\n",ndiff);
    printf("  ... and J(-342) in window with that same bias  : %lld   -> all three landmarks jointly reachable\n",n3);
    printf("  best full jwin18 score among those             : %d  (w=%d s1=%d s2=%d KCw=%lld c=%lld)\n",
        B.sc,B.w,B.s1,B.s2,(long long)B.kc,(long long)B.c);
    return 0;
}
