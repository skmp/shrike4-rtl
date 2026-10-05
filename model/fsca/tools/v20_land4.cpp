// v20_land4.cpp -- the FREE-K joint landmark scan done soundly: the kc bracket itself is widened to
// allow K1 anywhere in [0,2^s1) and K2 anywhere in [0,2^s2) (v20_land3 reused the fixed-K bracket
// for the free-K pass, which is not a superset of the free-K hit set).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L=1ll<<21, RECHI=1ll<<27;
static inline i128 cdiv(i128 a,i128 b){ return a>=0?(a+b-1)/b:-((-a)/b); }
static inline i128 fdiv(i128 a,i128 b){ return a>=0?a/b:-((-a+b-1)/b); }
// kc that can give J(t) in [tL,tH] for SOME K1 in [0,K1max], K2 in [0,K2max]
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
    const i64 A=JL[529],B=JH[529], C=JL[854],D=JH[854], E=JL[170],F=JH[170];
    long long tri=0,ovft=0,disj=0,inter=0,scanned=0,joint=0,joint3=0,big=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:tri,ovft,disj,inter,scanned,joint,joint3,big)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15; if(e<-30) continue;
        i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<28)) continue;
        tri++;
        i64 M1=((i64)1<<s1)-1, M2=((i64)1<<s2)-1;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w;
        i128 a1,b1,a2,b2; bool o1=false,o2=false;
        if(!kcrange(dx17,s1,s2,A,B,M1,M2,a1,b1,o1)) continue;
        if(!kcrange(dxp ,s1,s2,C,D,M1,M2,a2,b2,o2)) continue;
        if(o1||o2) ovft++;
        i128 lo=std::max(a1,a2), hi=std::min(b1,b2);
        if(lo>hi){ disj++; continue; }
        if(hi-lo>400000000){ big++; continue; }
        inter++;
        int gj=0,g3=0;
        for(i128 kk=lo;kk<=hi;kk++){ i64 kc=(i64)kk; scanned++;
            i64 P0=arr(kc,dx17), P1=arr(kc,dxp);
            i64 v0=P0>>s1, v1=P1>>s1;
            i64 e0=((i64)1<<s1)-(P0-(v0<<s1)), e1=((i64)1<<s1)-(P1-(v1<<s1));
            for(int c0=0;c0<2;c0++) for(int c1=0;c1<2;c1++){
                i64 Lo1=0,Hi1=(i64)1<<s1;
                if(c0) Lo1=std::max(Lo1,e0); else Hi1=std::min(Hi1,e0);
                if(c1) Lo1=std::max(Lo1,e1); else Hi1=std::min(Hi1,e1);
                if(Lo1>=Hi1||Lo1>=((i64)1<<s1)) continue;
                i64 Q0=arr(v0+c0,dx17), Q1=arr(v1+c1,dxp);
                i64 q0=Q0>>s2, q1=Q1>>s2;
                i64 f0=((i64)1<<s2)-(Q0-(q0<<s2)), f1=((i64)1<<s2)-(Q1-(q1<<s2));
                for(int d0=0;d0<2;d0++) for(int d1=0;d1<2;d1++){
                    if(q0+d0<A||q0+d0>B) continue; if(q1+d1<C||q1+d1>D) continue;
                    i64 Lo2=0,Hi2=(i64)1<<s2;
                    if(d0) Lo2=std::max(Lo2,f0); else Hi2=std::min(Hi2,f0);
                    if(d1) Lo2=std::max(Lo2,f1); else Hi2=std::min(Hi2,f1);
                    if(Lo2>=Hi2||Lo2>=((i64)1<<s2)) continue;
                    i64 K1v=Lo1,K2v=Lo2;
                    auto Jf=[&](int t)->i64{ i64 dx=(i64)t<<w; i64 I=(arr(kc,dx)+K1v)>>s1; return (arr(I,dx)+K2v)>>s2; };
                    if(Jf(17)<A||Jf(17)>B||Jf(342)<C||Jf(342)>D) continue;   // direct re-evaluation
                    gj=1; i64 jm=Jf(-342); if(jm>=E&&jm<=F) g3=1;
#pragma omp critical
                    printf("!!! FREE-K JOINT HIT w=%d s1=%d s2=%d kc=%lld K1=%lld K2=%lld  J17=%lld J342=%lld J-342=%lld\n",
                        w,s1,s2,(long long)kc,(long long)K1v,(long long)K2v,(long long)Jf(17),(long long)Jf(342),(long long)jm);
                }
            }
        }
        joint+=gj; joint3+=g3;
    }
    printf("scale-admissible triples                        : %lld\n",tri);
    printf("  bracket flagged (I1 leaves the 28-bit recoder): %lld\n",ovft);
    printf("  skipped, intersection > 4e8 kc                : %lld\n",big);
    printf("triples with PROVABLY DISJOINT free-K brackets   : %lld\n",disj);
    printf("triples exhaustively scanned                    : %lld\n",inter);
    printf("kc values scanned (each x up to 16 K1/K2 cases) : %lld\n\n",scanned);
    printf("FREE K1 in [0,2^s1), K2 in [0,2^s2):  ONE (KCw,K1,K2) giving BOTH landmarks : %lld\n",joint);
    printf("                                      ... and J(-342) too                   : %lld\n",joint3);
    return 0;
}
