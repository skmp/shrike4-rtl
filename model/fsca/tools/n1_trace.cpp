// n1_trace.cpp -- full FSCA pipeline trace, every intermediate in dec/hex/bin(+float where the value
// lives on a known grid), plus back-propagation of the hardware window to each stage.
// usage: n1_trace <angle_hex> [angle_hex ...]
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
typedef int64_t i64;
static Fsca F;
static const char* bin(i64 v,int w){ static char b[6][90]; static int n=0; char*o=b[n=(n+1)%6];
    for(int i=w-1;i>=0;i--){ *o++=((v>>i)&1)?'1':'0'; if(i&&i%8==0)*o++='_'; } *o=0; return b[n]; }
static void row(const char*name,i64 v,int w,const char*note){
    printf("  %-22s %16lld   0x%012llX   0b%s\n",(long long)0?name:name,(long long)v,(unsigned long long)v&0xFFFFFFFFFFFFull,bin(v,w));
    if(note&&*note) printf("  %-22s %s\n","",note); }
static void rowf(const char*name,i64 v,int w,double scale,const char*unit){
    char n[160]; snprintf(n,160,"= %.12g %s",(double)v*scale,unit); row(name,v,w,n); }
#define ARR fsca2_arr
#define SM  fsca2_sm
struct Stage { i64 t,dx,p1,T1,A3,T3,pa,I1,pb,J,Jf,pc,T2,V,R; };
static Stage ST[8]; static int NST=0;
static void trace(uint32_t a){
    int q=(int)((a+512u)>>10), mraw=q&63, t=(int)a-1024*q; if(t<-512)t+=65536;
    int m=mraw,neg=0,flip=0; if(m>=33){m-=32;neg=1;} if(m>=17){m=32-m;flip=1;}
    i64 s=flip?-1:1;
    printf("\n================================================================================\n");
    printf("ANGLE 0x%04X\n",a);
    printf("================================================================================\n");
    printf("STEP 0  INPUT  (FPUL & 0xffff; one turn = 65536)\n");
    rowf("angle",(i64)a,16,1.0/65536.0,"turns");
    printf("  %-22s %.12g rad,  sin = %.12g\n","",(double)a*6.283185307179586/65536.0,
        __builtin_sin((double)a*6.283185307179586/65536.0));
    printf("\nSTEP 1  NODE SPLIT   q = (angle + 512) >> 10 ;  t = angle - 1024*q\n");
    row("q (raw node 0..63)",q&63,6,"");
    rowf("t (signed offset)",t,11,1.0,"angle units  [-512,511], 11-bit 2s complement");
    printf("\nSTEP 2  QUADRANT FOLD\n");
    printf("  raw node %2d -> table node %2d   neg=%d (negate result)   flip=%d (s=%+d)\n",mraw,m,neg,flip,(int)s);
    printf("\nSTEP 3  TABLE LOOKUP  (node %d of 0..16)\n",m);
    rowf("C0  [2^-26 grid]",FSCA2_C0[m],27,1.0/67108864.0,"");
    row ("C1f (linear coef)",FSCA2_C1F[m],24,"");
    row ("e1  (C1f exponent)",FSCA2_E1[m],4,"shift is 23-e1");
    row ("c3  (cubic coef)",FSCA2_C3[m],12,"");
    row ("S   (sine coef)",FSCA2_S[m],24,"");
    row ("u2  (T2 align)",FSCA2_U2[m],6,"");
    i64 dx=(i64)t<<13;
    printf("\nSTEP 4  OPERAND  dx = t << 13\n");
    rowf("dx",dx,24,1.0/8192.0,"angle units (13 fractional bits)");
    printf("  %-22s low 3 bits of t sit at dx bits 13..15, BELOW the array's col-16 cut: t&7 = %d\n","",t&7);
    printf("\nSTEP 5  LINEAR TERM   T1 = sm(arr(C1f, dx) + K, 23-e1)\n");
    i64 p1=ARR(FSCA2_C1F[m],dx), p1k=p1+FSCA2_K, T1=FSCA2_C1F[m]?SM(p1k,23-FSCA2_E1[m]):0;
    row("arr(C1f,dx)",p1,48,""); row("+ K (2^19)",p1k,48,"");
    { char nm[40]; snprintf(nm,40,"T1 = sm(.,%d)",23-FSCA2_E1[m]); rowf(nm,T1,32,1.0/67108864.0,"[2^-26 grid]"); }
    printf("\nSTEP 6  CUBIC TERM    A3 = ((arr(t*t<<3, dx) + K) >> 20) >> 3 ;  T3 = sm(arr(c3<<5, A3) + K, 26)\n");
    i64 tt=((i64)t*t)<<3, p3a=ARR(tt,dx), A3=((p3a+FSCA2_K)>>20)>>3;
    i64 p3=ARR(FSCA2_C3[m]<<5,A3), T3=FSCA2_C3[m]?SM(p3+FSCA2_K,26):0;
    row("t*t << 3",tt,26,""); row("arr(t*t<<3,dx)",p3a,52,""); row("A3",A3,28,"");
    row("arr(c3<<5,A3)",p3,48,"");
    rowf("T3",T3,28,1.0/67108864.0,"[2^-26 grid]");
    printf("\nSTEP 7  SQUARE TERM  <<< the suspect path >>>\n");
    i64 pa=ARR(dx,FSCA2_KC), I1=(pa+FSCA2_K)>>23;
    i64 pb=ARR(I1,dx), J=(pb+FSCA2_K)>>20, Jf=(pb+FSCA2_K)>>18;
    i64 pc=ARR(J,FSCA2_S[m]), T2=FSCA2_S[m]?SM(pc+FSCA2_K,FSCA2_U2[m]):0;
    row("KC (const)",FSCA2_KC,24,"economised 2*pi^2");
    row("arr(dx, KC)",pa,52,"first multiply");
    row("+ K",pa+FSCA2_K,52,"");
    row("I1 = (.) >> 23",I1,28,"~ t*KC/2^10");
    row("arr(I1, dx)",pb,56,"second multiply (the square)");
    row("+ K",pb+FSCA2_K,56,"");
    row("J  = (.) >> 20",J,28,"COARSE latch, ~ KC*t^2/2^17");
    row("J_fine = (.) >> 18",Jf,30,"FINE latch (jwin18.txt scale)");
    row("arr(J, S)",pc,52,"third multiply");
    rowf("T2 = sm(.+K, u2)",T2,30,1.0/67108864.0,"[2^-26 grid]");
    printf("\nSTEP 8  COMBINE   V = C0 + s*T1 - T2 - s*T3\n");
    i64 V=FSCA2_C0[m]+s*T1-T2-s*T3, R=neg?-V:V;
    printf("  %-22s %16" PRId64 " %+" PRId64 " %+" PRId64 " %+" PRId64 " = %" PRId64 "\n","C0 + s*T1 - T2 - s*T3",
        FSCA2_C0[m],s*T1,-T2,-s*T3,V);
    rowf("V (pre-negate)",V,28,1.0/67108864.0,"");
    rowf("result = %s V",R,28,1.0/67108864.0,"[2^-26 grid]");
    printf("\nSTEP 9  ROUND TO float32\n");
    uint32_t mrn=fsca2_to_float(R,0), mrz=fsca2_to_float(R,1);
    float f1,f2; memcpy(&f1,&mrn,4); memcpy(&f2,&mrz,4);
    printf("  model RN  0x%08X  0b%s  %.9g\n",mrn,bin(mrn,32),(double)f1);
    printf("  model RZ  0x%08X  0b%s  %.9g\n",mrz,bin(mrz,32),(double)f2);
    uint32_t hrn=F.srn[a],hrz=F.srz[a]; memcpy(&f1,&hrn,4); memcpy(&f2,&hrz,4);
    printf("  HW    RN  0x%08X  0b%s  %.9g%s\n",hrn,bin(hrn,32),(double)f1,hrn!=mrn?"   <-- DIFFERS":"");
    printf("  HW    RZ  0x%08X  0b%s  %.9g%s\n",hrz,bin(hrz,32),(double)f2,hrz!=mrz?"   <-- DIFFERS":"");
    printf("\nSTEP 10 BACK-PROPAGATION of the hardware window\n");
    Win w=decode(hrz,hrn); i64 lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
    printf("  HW result must be in [%" PRId64 ", %" PRId64 "]%s ; model gives %" PRId64 " -> %s\n",
        lo,hi,w.exact?" (exact)":"",R,(R<lo||R>hi)?"OUTSIDE":"inside");
    i64 Vlo=neg?-hi:lo, Vhi=neg?-lo:hi;
    printf("  so V must be in    [%" PRId64 ", %" PRId64 "] ; model V = %" PRId64 " -> off by %+" PRId64 "\n",
        Vlo,Vhi,V,V<Vlo?V-Vlo:(V>Vhi?V-Vhi:0));
    i64 T2lo=FSCA2_C0[m]+s*T1-s*T3-Vhi, T2hi=FSCA2_C0[m]+s*T1-s*T3-Vlo;
    printf("  -> T2 must be in   [%" PRId64 ", %" PRId64 "] ; model T2 = %" PRId64 " -> off by %+" PRId64 "\n",
        T2lo,T2hi,T2,T2<T2lo?T2-T2lo:(T2>T2hi?T2-T2hi:0));
    { i64 jl=0,jh=0; bool got=false; for(i64 cand=J-20000;cand<=J+20000;cand++){
        i64 tc=FSCA2_S[m]?SM(ARR(cand,FSCA2_S[m])+FSCA2_K,FSCA2_U2[m]):0;
        if(tc>=T2lo&&tc<=T2hi){ if(!got){jl=cand;got=true;} jh=cand; } }
      if(got){ printf("  -> J (coarse) must be in [%" PRId64 ", %" PRId64 "] ; model J = %" PRId64 " -> off by %+" PRId64 "\n",
            jl,jh,J,J<jl?J-jl:(J>jh?J-jh:0));
        printf("  -> J_fine equivalent     [%" PRId64 ", %" PRId64 "] ; model J_fine = %" PRId64 "\n",jl*4,jh*4+3,Jf); }
      else printf("  -> NO J in +-20000 of the model value produces an acceptable T2 (!)\n"); }
    { i64 il=0,ih=0; bool got=false; for(i64 cand=I1-4000;cand<=I1+4000;cand++){
        i64 jj=(ARR(cand,dx)+FSCA2_K)>>20;
        i64 tc=FSCA2_S[m]?SM(ARR(jj,FSCA2_S[m])+FSCA2_K,FSCA2_U2[m]):0;
        if(tc>=T2lo&&tc<=T2hi){ if(!got){il=cand;got=true;} ih=cand; } }
      if(got) printf("  -> I1 must be in   [%" PRId64 ", %" PRId64 "] ; model I1 = %" PRId64 " -> off by %+" PRId64 "\n",
            il,ih,I1,I1<il?I1-il:(I1>ih?I1-ih:0));
      else printf("  -> NO I1 within +-4000 of the model value works: the second multiply's FORM is wrong here\n"); }
    if(NST<8) ST[NST++] = Stage{t,dx,p1,T1,A3,T3,pa,I1,pb,J,Jf,pc,T2,V,R};
}
static void diff(void){
    if(NST<2) return;
    printf("\n================================================================================\n");
    printf("DIFFERENTIAL  (each column is the value; delta is vs the PREVIOUS column)\n");
    printf("================================================================================\n");
    const char* nm[]={"t","dx","arr(C1f,dx)","T1","A3","T3","arr(dx,KC)","I1","arr(I1,dx)","J (coarse)","J_fine","arr(J,S)","T2","V","result"};
    for(int f=0;f<15;f++){
        printf("  %-13s",nm[f]);
        for(int i=0;i<NST;i++){ const i64* p=(const i64*)&ST[i]; printf(" %18lld",(long long)p[f]); }
        printf("\n  %-13s","   delta");
        for(int i=0;i<NST;i++){ const i64* p=(const i64*)&ST[i]; const i64* q=(const i64*)&ST[i-1];
            if(i==0) printf(" %18s","-"); else printf(" %+18lld",(long long)(p[f]-q[f])); }
        printf("\n");
    }
}
int main(int argc,char**argv){ if(!load_fsca(F)){fprintf(stderr,"dump load failed\n");return 1;}
    for(int i=1;i<argc;i++) trace((uint32_t)strtoul(argv[i],0,16)&0xffff);
    diff();
    return 0; }
