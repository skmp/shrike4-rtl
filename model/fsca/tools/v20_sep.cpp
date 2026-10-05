// v20_sep.cpp -- (1) is the joint scan vacuous?  count triples where EACH landmark is individually
// reachable.  (2) the t=-17 window.  (3) the I1-offset feedback variant.  (4) the lattice argument
// (uniform dx bias at w=13 can never produce J(342)=18466836).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <set>
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
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529],B=JH[529], C=JL[854],D=JH[854];
    printf("(2) windows: J(+17)=[%lld,%lld]   J(-17)=[%lld,%lld]   J(+342)=[%lld,%lld]  J(-342)=[%lld,%lld]\n\n",
        (long long)JL[529],(long long)JH[529],(long long)JL[495],(long long)JH[495],
        (long long)JL[854],(long long)JH[854],(long long)JL[170],(long long)JH[170]);

    // (1) individual reachability, exhaustive per landmark
    long long tri=0, hit17=0, hit342=0, both=0, cap17=0, sc17=0, sc342=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:tri,hit17,hit342,both,cap17,sc17,sc342)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15; if(e<-30) continue;
        i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<28)) continue;
        tri++;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w;
        i128 a,b; int h1=0,h2=0;
        if(kcrange(dx17,s1,s2,A,B,K,K,a,b)){
            if(b-a>20000000){ cap17++; }
            else for(i128 kk=a;kk<=b;kk++){ i64 kc=(i64)kk; sc17++;
                i64 I=(arr(kc,dx17)+K)>>s1; i64 j=(arr(I,dx17)+K)>>s2; if(j>=A&&j<=B){h1=1;break;} } }
        if(kcrange(dxp,s1,s2,C,D,K,K,a,b)){
            for(i128 kk=a;kk<=b;kk++){ i64 kc=(i64)kk; sc342++;
                i64 I=(arr(kc,dxp)+K)>>s1; i64 j=(arr(I,dxp)+K)>>s2; if(j>=C&&j<=D){h2=1;break;} } }
        hit17+=h1; hit342+=h2; both+=(h1&&h2);
    }
    printf("(1) NON-VACUITY, fixed K1=K2=2^19, exhaustive per landmark:\n");
    printf("    scale-admissible triples                       : %lld\n",tri);
    printf("    triples where some KCw gives J(17)=45617       : %lld   (kc scanned %lld, capped triples %lld)\n",hit17,sc17,cap17);
    printf("    triples where some KCw gives J(342)=18466836   : %lld   (kc scanned %lld)\n",hit342,sc342);
    printf("    triples where EACH is reachable separately     : %lld\n",both);
    printf("    triples where ONE KCw reaches both (v20_land3) : 0\n\n");

    // (3) feedback offset applied to I1 itself
    printf("(3) feedback variant: I1 -> I1 + r for t<0 (w=13,s1=23,s2=18,KC):\n");
    for(int r=-2;r<=2;r++){ if(!r) continue; int s=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I=(arr(KC,dx)+K)>>23; if(t<0) I+=r;
            i64 j=(arr(I,dx)+K)>>18; if(j>=JL[t+512]&&j<=JH[t+512]) s++; }
        printf("    r=%+d : %d/1024\n",r,s); }

    // (4) lattice argument at w=13: can a uniform dx bias ever produce J(342)=18466836?
    printf("\n(4) lattice at w=13,s1=23,s2=18,KC: uniform dx bias p, J(342) reachable values\n");
    { std::set<i64> vals; long long hits=0; i64 dxp=(i64)342<<13;
      for(i64 p=-2000000;p<=2000000;p++){ i64 dx=dxp+p; i64 I=(arr(KC,dx)+K)>>23; i64 j=(arr(I,dx)+K)>>18;
          if(j==18466836) hits++; if(p>=-8&&p<=8) vals.insert(j); }
      printf("    p in [-2e6,+2e6] : J(342) == 18466836 occurs %lld times\n",hits);
      printf("    J(342) for p=-8..+8 :"); for(i64 v:vals) printf(" %lld",(long long)v); printf("\n"); }
    return 0;
}
