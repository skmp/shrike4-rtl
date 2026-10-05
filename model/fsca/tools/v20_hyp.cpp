// v20_hyp.cpp -- ADVERSARIAL VERIFIER (v20): the USER's two hypotheses, tested independently.
//  H-A : t computed in 2s complement, consumed as 1s complement  -> value used is t+1 for t<0.
//        Tested at every placement, and with KCw free at every alignment.
//  H-B : t carries one more bit of precision -> the operand is t +- 1/2 ULP (also +-1/4, +-1).
//        Tested with KCw free at every alignment and both shift splits.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L=1ll<<21, RECHI=1ll<<27;
static inline i128 cdiv(i128 a,i128 b){ return a>=0?(a+b-1)/b:-((-a)/b); }
static inline i128 fdiv(i128 a,i128 b){ return a>=0?a/b:-((-a+b-1)/b); }
// exact kc interval that can give (arr((arr(kc,dx)+K1)>>s1,dx)+K2)>>s2 in [tL,tH]
static bool kcrange(i128 dx,int s1,int s2,i64 tL,i64 tH,i64 K1,i64 K2,i128&klo,i128&khi){
    if(dx==0) return false; bool neg=dx<0; i128 ad=neg?-dx:dx;
    i128 Xlo=(i128)tL*((i128)1<<s2)-K2, Xhi=((i128)tH+1)*((i128)1<<s2)-1-K2+L;
    i128 Ilo,Ihi; if(!neg){Ilo=cdiv(Xlo,ad);Ihi=fdiv(Xhi,ad);} else {Ilo=cdiv(-Xhi,ad);Ihi=fdiv(-Xlo,ad);}
    if(Ilo>Ihi) return false;
    i128 Ylo=Ilo*((i128)1<<s1)-K1, Yhi=(Ihi+1)*((i128)1<<s1)-1-K1+L;
    if(!neg){klo=cdiv(Ylo,ad);khi=fdiv(Yhi,ad);} else {klo=cdiv(-Yhi,ad);khi=fdiv(-Ylo,ad);}
    if(klo<1)klo=1; if(khi>RECHI-1)khi=RECHI-1; return klo<=khi;
}
// the chain with a per-sign operand offset (H-A) and a uniform operand offset (H-B)
struct Tr { int mode; int d; };   // mode 0 none; 1 t-level(t<0); 2 dx-level(t<0); 3 feedback(t<0); 4 both(t<0); 5 uniform dx offset
static inline void mkdx(int t,int w,const Tr&tr,i64&dxa,i64&dxb){
    // dxa = operand of the first multiply, dxb = operand of the second (feedback) multiply
    i64 base=(i64)t<<w; dxa=base; dxb=base;
    switch(tr.mode){
      case 1: if(t<0){ i64 v=((i64)(t+tr.d))<<w; dxa=v; dxb=v; } break;
      case 2: if(t<0){ dxa=base+tr.d; dxb=base+tr.d; } break;
      case 3: if(t<0){ dxb=base+tr.d; } break;
      case 4: if(t<0){ dxa=base+tr.d; dxb=base+tr.d; } break;
      case 5: dxa=base+tr.d; dxb=base+tr.d; break;
      case 6: if(t<0){ i64 v=base-((i64)1024<<w)*tr.d; dxa=v; dxb=v; } break;  // lost sign extension
    }
}
static inline i64 Jt(int t,int w,int s1,int s2,i64 kc,i64 K1,i64 K2,const Tr&tr){
    i64 dxa,dxb; mkdx(t,w,tr,dxa,dxb); i64 I1=(arr(kc,dxa)+K1)>>s1; return (arr(I1,dxb)+K2)>>s2; }
static int score(int w,int s1,int s2,i64 kc,const Tr&tr){ int s=0;
    for(int t=-512;t<=511;t++){ i64 J=Jt(t,w,s1,s2,kc,K,K,tr); if(J>=JL[t+512]&&J<=JH[t+512]) s++; } return s; }
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529],B=JH[529], C=JL[854],D=JH[854], E=JL[170],F=JH[170];

    puts("=== H-A : 2s-complement t consumed as 1s complement (t -> t+d for t<0 only) ===");
    puts("    placement                      d    score/1024   J(17)        J(342)       J(-342)");
    const char* nm[7]={"none","t level","dx level","I1 feedback","both dx+fb","uniform dx","lost sign-ext 1024*d"};
    for(int mode=0;mode<=6;mode++){
        for(int d : {-2,-1,0,1,2}){
            if(mode==0&&d!=0) continue; if(mode!=0&&d==0) continue;
            Tr tr{mode,d};
            printf("    %-22s %+5d  %8d   %-12lld %-12lld %-12lld\n",nm[mode],d,score(13,23,18,KC,tr),
                (long long)Jt(17,13,23,18,KC,K,K,tr),(long long)Jt(342,13,23,18,KC,K,K,tr),(long long)Jt(-342,13,23,18,KC,K,K,tr));
        }
    }
    puts("\n    (structural point: any t<0-only rule leaves J(17) and J(342) bit-identical; the residual");
    puts("     needs -13 at t=17 and -10 at t=342, both on the POSITIVE branch.)");

    // ---- H-B : uniform operand offset = a fractional bit of t.  Scan every alignment and split.
    puts("\n=== H-B : t with one more bit of precision (uniform operand offset o) ===");
    puts("    For each o, KCw is chosen (exhaustively, over its exact interval) so that J(342) is EXACT;");
    puts("    the reported number is the smallest |J(17) - 45617| achievable anywhere, and the number of");
    puts("    (w,s1,s2,KCw) configurations that hit BOTH landmarks.");
    struct Res{ long long best; long long nboth; long long nfit; };
    const char* on[7]={"exact t (o=0)","t + 1/2 ULP","t - 1/2 ULP","t + 1/4 ULP","t - 1/4 ULP","t + 1 ULP","t - 1 ULP"};
    for(int oi=0; oi<7; oi++){
        long long best=(long long)1e18, nboth=0, nfit=0, scanned=0; int bw=0,bs1=0,bs2=0; i64 bkc=0;
        #pragma omp parallel for schedule(dynamic) collapse(2) reduction(min:best) reduction(+:nboth,nfit,scanned)
        for(int w=3;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
            int e=s1+s2-2*w-15; if(e<-30) continue;
            i128 nom = e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<28)) continue;
            i64 o=0;
            switch(oi){ case 0:o=0;break; case 1:o=(i64)1<<(w-1); break; case 2:o=-((i64)1<<(w-1)); break;
                        case 3:o=(i64)1<<(w-2); break; case 4:o=-((i64)1<<(w-2)); break;
                        case 5:o=(i64)1<<w; break; case 6:o=-((i64)1<<w); break; }
            i64 dx17=((i64)17<<w)+o, dxp=((i64)342<<w)+o;
            i128 klo,khi; if(!kcrange(dxp,s1,s2,C,D,K,K,klo,khi)) continue;
            if(khi-klo>4000000) continue;                       // guard (never triggers; reported below)
            for(i128 kk=klo;kk<=khi;kk++){ i64 kc=(i64)kk; scanned++;
                i64 I1=(arr(kc,dxp)+K)>>s1; i64 jp=(arr(I1,dxp)+K)>>s2;
                if(jp<C||jp>D) continue; nfit++;
                i64 I2=(arr(kc,dx17)+K)>>s1; i64 j17=(arr(I2,dx17)+K)>>s2;
                long long dd=(long long)(j17>45617?j17-45617:45617-j17);
                if(dd<best) best=dd;
                if(dd==0) nboth++;
            }
        }
        printf("    %-16s  configs with J(342) exact: %-8lld   min |J(17)-45617| = %-8lld   BOTH landmarks: %lld\n",
               on[oi],nfit,best==(long long)1e18?-1:best,nboth);
    }
    return 0;
}
