// s3_back.cpp -- BACKWARD TRACE: from the observed (RN,RZ) float pair, propagate backwards through
// STEP 8 (V) and STEP 7 (T2 -> arr(J,S) -> J -> arr(I1,dx) -> I1 -> arr(dx,KC) -> KC), printing what
// each intermediate SHOULD have been, in dec/hex/bin/float, beside what the model produced.
// arr() takes J and I1 as the RECODED operand and is non-monotone in it, so those stages are swept
// exhaustively. Only KC (the multiplicand) is monotone.
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
typedef int64_t i64;
static Fsca F;
#define ARR fsca2_arr
#define SM  fsca2_sm
static const char* bin(i64 v,int w){ static char b[8][110]; static int n=0; char*o=b[n=(n+1)%8];
    for(int i=w-1;i>=0;i--){ *o++=((v>>i)&1)?'1':'0'; if(i&&i%8==0)*o++='_'; } *o=0; return b[n]; }
static void line(const char*tag,i64 v,int w,double sc,const char*unit){
    printf("      %-9s %18lld  0x%012llX  0b%s",tag,(long long)v,(unsigned long long)v&0xFFFFFFFFFFFFull,bin(v,w));
    if(sc!=0) printf("  = %.12g %s",(double)v*sc,unit);
    printf("\n"); }
static void stage(const char*name,i64 mlo,i64 mhi,i64 model,int w,double sc,const char*unit){
    i64 d = model<mlo? model-mlo : (model>mhi? model-mhi : 0);
    printf("  %s\n",name);
    line("required>=",mlo,w,sc,unit);
    if(mhi!=mlo) line("required<=",mhi,w,sc,unit);
    line("model",model,w,sc,unit);
    if(d) printf("      %-9s model is %s by %+lld\n","VERDICT", d<0?"TOO LOW":"TOO HIGH",(long long)d);
    else  printf("      %-9s model is inside the required range\n","VERDICT");
    printf("\n"); }
int main(int argc,char**argv){ if(!load_fsca(F)) return 1;
  for(int ai=1;ai<argc;ai++){
    uint32_t a=(uint32_t)strtoul(argv[ai],0,16)&0xffff;
    int q=(int)((a+512u)>>10), m=q&63, t=(int)a-1024*q, neg=0, flip=0; if(t<-512)t+=65536;
    if(m>=33){m-=32;neg=1;} if(m>=17){m=32-m;flip=1;}
    i64 s=flip?-1:1, dx=(i64)t<<13;
    i64 T1=FSCA2_C1F[m]?SM(ARR(FSCA2_C1F[m],dx)+FSCA2_K,23-FSCA2_E1[m]):0;
    i64 A3=((ARR(((i64)t*t)<<3,dx)+FSCA2_K)>>20)>>3;
    i64 T3=FSCA2_C3[m]?SM(ARR(FSCA2_C3[m]<<5,A3)+FSCA2_K,26):0;
    i64 P1=ARR(dx,FSCA2_KC), I1=(P1+FSCA2_K)>>23;
    i64 P2=ARR(I1,dx), J=(P2+FSCA2_K)>>20, Jf=(P2+FSCA2_K)>>18;
    i64 P3=ARR(J,FSCA2_S[m]), T2=FSCA2_S[m]?SM(P3+FSCA2_K,FSCA2_U2[m]):0;
    i64 V=FSCA2_C0[m]+s*T1-T2-s*T3, R=neg?-V:V;
    printf("\n================================================================================\n");
    printf("BACKWARD TRACE  angle 0x%04X   raw node %d -> table node %d   t = %+d   neg=%d s=%+d\n",
        a,q&63,m,t,neg,(int)s);
    printf("================================================================================\n");
    uint32_t hrn=F.srn[a],hrz=F.srz[a];
    printf("  STEP 9'  OBSERVED FLOAT PAIR -> exact interval for the result\n");
    printf("      HW RN     0x%08X  0b%s\n",hrn,bin(hrn,32));
    printf("      HW RZ     0x%08X  0b%s\n",hrz,bin(hrz,32));
    Win w=decode(hrz,hrn); i64 lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
    printf("      the pair pins the result to [%lld, %lld]  width %lld%s\n\n",
        (long long)lo,(long long)hi,(long long)(hi-lo+1),w.exact?"  (exact)":"");
    i64 Vlo=neg?-hi:lo, Vhi=neg?-lo:hi;
    printf("  STEP 8'  COMBINE, inverted.  V = C0 + s*T1 - T2 - s*T3, and C0/T1/T3 are exact here:\n");
    printf("      C0 = %lld   s*T1 = %lld   s*T3 = %lld   so  T2 = %lld - V\n\n",
        (long long)FSCA2_C0[m],(long long)(s*T1),(long long)(s*T3),(long long)(FSCA2_C0[m]+s*T1-s*T3));
    stage("V  (pre-negate, 2^-26 grid)",Vlo,Vhi,V,28,1.0/67108864.0,"");
    i64 rest=FSCA2_C0[m]+s*T1-s*T3, T2lo=rest-Vhi, T2hi=rest-Vlo;
    stage("T2 (square term, 2^-26 grid)",T2lo,T2hi,T2,28,1.0/67108864.0,"");
    printf("  STEP 7'  SQUARE PATH, inverted\n\n");
    int u=FSCA2_U2[m];
    i64 Plo=T2lo<<u, Phi=((T2hi+1)<<u)-1;      // T2 = P>>u with P>0 here
    stage("arr(J,S) + K  (third product)",Plo,Phi,P3+FSCA2_K,52,0,"");
    // J: exhaustive sweep (arr is non-monotone in the recoded operand J)
    i64 jl=0,jh=0; bool gj=false;
    for(i64 c=(J>200000?J-200000:0);c<=J+200000;c++){ i64 p=ARR(c,FSCA2_S[m])+FSCA2_K;
        if(p>=Plo&&p<=Phi){ if(!gj){jl=c;gj=true;} jh=c; } }
    if(gj){ stage("J (coarse latch, >>20)",jl,jh,J,28,0,"");
            stage("J_fine (>>18 scale)",jl*4,jh*4+3,Jf,30,0,""); }
    else printf("  J : no value within +-200000 of the model reaches the required product\n\n");
    if(gj){
      i64 Q2lo=jl<<20, Q2hi=((jh+1)<<20)-1;
      stage("arr(I1,dx) + K  (the square)",Q2lo,Q2hi,P2+FSCA2_K,56,0,"");
      i64 il=0,ih=0; bool gi=false;
      for(i64 c=I1-30000;c<=I1+30000;c++){ i64 p=ARR(c,dx)+FSCA2_K;
          if(p>=Q2lo&&p<=Q2hi){ if(!gi){il=c;gi=true;} ih=c; } }
      if(gi){ stage("I1 (first latch, >>23)",il,ih,I1,28,0,"");
              i64 Q1lo=il<<23, Q1hi=((ih+1)<<23)-1;
              stage("arr(dx,KC) + K  (first product)",Q1lo,Q1hi,P1+FSCA2_K,56,0,"");
              // KC is the MULTIPLICAND -> monotone, so this interval is exact
              i64 kl=0,kh=0; bool gk=false;
              for(i64 c=FSCA2_KC-60000;c<=FSCA2_KC+60000;c++){ i64 p=ARR(dx,c)+FSCA2_K;
                  if(p>=Q1lo&&p<=Q1hi){ if(!gk){kl=c;gk=true;} kh=c; } }
              if(gk) stage("KC (the stored constant)",kl,kh,FSCA2_KC,24,0,"");
              else printf("  KC: no value within +-60000 works for this angle\n\n");
      } else printf("  I1: no value within +-30000 of the model reaches the required square\n\n");
    }
    printf("  SUMMARY  model result %lld, required [%lld,%lld]\n",(long long)R,(long long)lo,(long long)hi);
  }
  return 0; }
