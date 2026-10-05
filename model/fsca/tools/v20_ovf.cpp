// v20_ovf.cpp -- close the one hole in v20_land3/4: triples whose t=342 bracket is unreliable
// because the demanded intermediate latch I1 leaves the array's signed 28-bit recoder.
// For those, attack from the t=17 side instead (dx17 is 20x smaller, so I1(17) is 20x smaller),
// scanning the t=17 bracket exhaustively and testing t=342 exactly at every hit.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L=1ll<<21, RECHI=1ll<<27;
static inline i128 cdiv(i128 a,i128 b){ return a>=0?(a+b-1)/b:-((-a)/b); }
static inline i128 fdiv(i128 a,i128 b){ return a>=0?a/b:-((-a+b-1)/b); }
static bool kcrange(i128 dx,int s1,int s2,i64 tL,i64 tH,i64 K1max,i64 K2max,i128&klo,i128&khi,bool&ovf){
    if(dx==0) return false; bool neg=dx<0; i128 ad=neg?-dx:dx;
    i128 Xlo=(i128)tL*((i128)1<<s2)-K2max, Xhi=((i128)tH+1)*((i128)1<<s2)-1+L;
    i128 Ilo,Ihi; if(!neg){Ilo=cdiv(Xlo,ad);Ihi=fdiv(Xhi,ad);} else {Ilo=cdiv(-Xhi,ad);Ihi=fdiv(-Xlo,ad);}
    if(Ilo>Ihi) return false;
    ovf=(Ilo<=-RECHI)||(Ihi>=RECHI);
    i128 Ylo=Ilo*((i128)1<<s1)-K1max, Yhi=(Ihi+1)*((i128)1<<s1)-1+L;
    if(!neg){klo=cdiv(Ylo,ad);khi=fdiv(Yhi,ad);} else {klo=cdiv(-Yhi,ad);khi=fdiv(-Ylo,ad);}
    if(klo<1)klo=1; if(khi>RECHI-1)khi=RECHI-1; return klo<=khi;
}
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529],B=JH[529], C=JL[854],D=JH[854];
    long long flagged=0, rescued=0, unresolved=0, scanned=0, joint=0, capped=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:flagged,rescued,unresolved,scanned,joint,capped)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15; if(e<-30) continue;
        i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<28)) continue;
        i64 M1=((i64)1<<s1)-1, M2=((i64)1<<s2)-1;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w;
        i128 a1,b1,a2,b2; bool o1=false,o2=false;
        bool h2=kcrange(dxp,s1,s2,C,D,M1,M2,a2,b2,o2);
        if(!h2||!o2) continue;                       // only the flagged ones
        flagged++;
        bool h1=kcrange(dx17,s1,s2,A,B,M1,M2,a1,b1,o1);
        if(!h1){ rescued++; continue; }              // t=17 unreachable at all -> no joint hit
        if(o1){ unresolved++; continue; }            // both sides overflow: genuinely out of scope
        if(b1-a1>300000000){ capped++; continue; }
        rescued++;
        for(i128 kk=a1;kk<=b1;kk++){ i64 kc=(i64)kk; scanned++;
            i64 P0=arr(kc,dx17); i64 v0=P0>>s1; i64 e0=((i64)1<<s1)-(P0-(v0<<s1));
            for(int c0=0;c0<2;c0++){ if(c0&&e0>=((i64)1<<s1)) continue;
                i64 K1v = c0? e0 : 0;
                i64 Q0=arr(v0+c0,dx17); i64 q0=Q0>>s2; i64 f0=((i64)1<<s2)-(Q0-(q0<<s2));
                for(int d0=0;d0<2;d0++){ if(d0&&f0>=((i64)1<<s2)) continue;
                    if(q0+d0<A||q0+d0>B) continue;
                    // t=17 landmark hit with SOME (K1,K2); now test t=342 over the whole
                    // admissible K1,K2 sub-rectangle consistent with this selection.
                    i64 K1lo = c0? e0 : 0, K1hi = c0? ((i64)1<<s1)-1 : e0-1;
                    i64 K2lo = d0? f0 : 0, K2hi = d0? ((i64)1<<s2)-1 : f0-1;
                    i64 P1=arr(kc,dxp);
                    for(i64 kk1 : {K1lo,K1hi}){
                        i64 I=(P1+kk1)>>s1; i64 Q1=arr(I,dxp);
                        for(i64 kk2 : {K2lo,K2hi}){ i64 j=(Q1+kk2)>>s2;
                            if(j>=C&&j<=D){ joint++;
#pragma omp critical
                                printf("!!! OVERFLOW-BRANCH JOINT HIT w=%d s1=%d s2=%d kc=%lld K1=%lld K2=%lld\n",w,s1,s2,(long long)kc,(long long)kk1,(long long)kk2);
                            } } }
                } }
        }
    }
    printf("triples whose t=342 bracket is flagged (I1 out of recoder) : %lld\n",flagged);
    printf("  ... resolved from the t=17 side                          : %lld\n",rescued);
    printf("  ... skipped, t=17 bracket > 3e8 kc                       : %lld\n",capped);
    printf("  ... genuinely unresolved (both sides overflow)           : %lld\n",unresolved);
    printf("kc scanned on the t=17 side                                : %lld\n",scanned);
    printf("JOINT HITS found in the flagged region                     : %lld\n",joint);
    return 0;
}
