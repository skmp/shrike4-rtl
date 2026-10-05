// v41_user.cpp -- INDEPENDENT re-test of the USER'S two hypotheses.  Written from scratch; no reuse
// of m7_rep.cpp or w4_user.cpp.  Exact integer, scored on all 1024 offsets against jwin18.txt.
//   H1: t is a 2s-complement result consumed as 1s complement  -> the value used is t+1 when t<0
//       (and the converse, t-1).  Tested at EVERY placement: t into mul1, t into mul2, dx (sub-t
//       granularity), and the latch I1.
//   H2: t carries one extra bit of precision.  Three readings: as a VALUE (half-steps), as a FIELD
//       WIDTH (sign-extension from W bits), and as an ALIGNMENT (w -> w+1 with the compensating split).
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024];
static void load_win(){ FILE* f=fopen("jwin18.txt","r"); char l[512];
  while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JL[t+512]=a; JH[t+512]=b;} fclose(f);}
static inline i64 sx(i64 v,int W){ i64 m=((i64)1<<W)-1; i64 x=v&m; if(x>>(W-1)) x-=((i64)1<<W); return x; }

// ---------------- H1 ----------------
// da: offset added to t when t<0, for the operand of multiply 1
// db: same, for the operand of multiply 2
// dd: offset added to dx when dx<0 (sub-t granularity), both multiplies
// dc: offset added to I1 when I1<0
static int score_h1(int da,int db,int dd,int dc,i64* out17,i64* out342,i64* outm342){
    int sc=0;
    for(int t=-512;t<=511;t++){
        i64 ta=t+(t<0?da:0), tb=t+(t<0?db:0);
        i64 dxa=(ta<<13), dxb=(tb<<13);
        if(dxa<0) dxa+=dd; if(dxb<0) dxb+=dd;
        i64 I1=(arr(KC,dxa)+K)>>23;
        if(I1<0) I1+=dc;
        i64 J=(arr(I1,dxb)+K)>>18;
        if(J>=JL[t+512]&&J<=JH[t+512]) sc++;
        if(t==17&&out17)*out17=J; if(t==342&&out342)*out342=J; if(t==-342&&outm342)*outm342=J;
    }
    return sc;
}
// ---------------- H2 readings ----------------
static int score_val(int e){   // dx = (2t+e)<<12  : t with one extra LSB
    int sc=0; for(int t=-512;t<=511;t++){ i64 dx=((i64)2*t+e)<<12;
        i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
        if(J>=JL[t+512]&&J<=JH[t+512]) sc++; } return sc; }
static int score_width(int W1,int W2,i64* l17,i64* l342){ // dx sign-extended from W bits per multiply
    int sc=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 dxa=sx(dx,W1), dxb=sx(dx,W2);
        i64 I1=(arr(KC,dxa)+K)>>23; i64 J=(arr(I1,dxb)+K)>>18;
        if(J>=JL[t+512]&&J<=JH[t+512]) sc++;
        if(t==17&&l17)*l17=J; if(t==342&&l342)*l342=J; } return sc; }
static int score_align(int w,int s1,int s2,i64 kc,i64* l17,i64* l342,i64* lm342){
    int sc=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w;
        i64 I1=(arr(kc,dx)+K)>>s1; i64 J=(arr(I1,dx)+K)>>s2;
        if(J>=JL[t+512]&&J<=JH[t+512]) sc++;
        if(t==17&&l17)*l17=J; if(t==342&&l342)*l342=J; if(t==-342&&lm342)*lm342=J; } return sc; }

int main(){
    build_tables(); load_win();
    printf("baseline (da=db=dd=dc=0): %d/1024\n\n", score_h1(0,0,0,0,0,0,0));

    printf("=== H1: 2s-computed / 1s-consumed  (value used = t+1 for t<0), every placement ===\n");
    int best=0,ba=0,bb=0,bd=0,bc=0; int nsame=0,ntot=0; bool all17=true,all342=true;
    for(int da=-8;da<=8;da++) for(int db=-8;db<=8;db++) for(int dc=-2;dc<=2;dc++) for(int dd=-2;dd<=2;dd++){
        i64 j17,j342,jm; int s=score_h1(da,db,dd,dc,&j17,&j342,&jm); ntot++;
        if(j17!=45630) all17=false; if(j342!=18466846) all342=false;
        if(s>best){best=s;ba=da;bb=db;bd=dd;bc=dc;}
    }
    printf("  combinations scored: %d ; BEST %d/1024 at da=%d db=%d dd=%d dc=%d\n",ntot,best,ba,bb,bd,bc);
    printf("  J(17) == 45630 in EVERY combination: %s ; J(342) == 18466846 in EVERY combination: %s\n",
        all17?"YES":"NO", all342?"YES":"NO");
    { i64 a,b,c; int s=score_h1(1,1,0,0,&a,&b,&c);
      printf("  the literal 1s-consumed reading (da=db=+1): %d/1024, J(17)=%lld J(342)=%lld J(-342)=%lld\n",s,(long long)a,(long long)b,(long long)c); }
    { i64 a,b,c; int s=score_h1(-1,-1,0,0,&a,&b,&c);
      printf("  the converse (1s-computed, 2s-consumed, da=db=-1): %d/1024, J(17)=%lld J(342)=%lld J(-342)=%lld\n",s,(long long)a,(long long)b,(long long)c); }
    { i64 a,b,c; int s=score_h1(0,0,-1,0,&a,&b,&c);
      printf("  dx-level 1s complement (dd=-1, i.e. ~dx for dx<0): %d/1024, J(17)=%lld J(342)=%lld J(-342)=%lld\n",s,(long long)a,(long long)b,(long long)c); }
    { i64 a,b,c; int s=score_h1(0,0,0,1,&a,&b,&c);
      printf("  latch-level (dc=+1, I1 consumed as 1s complement): %d/1024, J(17)=%lld J(342)=%lld J(-342)=%lld\n",s,(long long)a,(long long)b,(long long)c); }

    printf("\n=== H2a: t as a VALUE with one extra LSB, dx=(2t+e)<<12 ===\n");
    for(int e=-4;e<=4;e++) printf("  e=%+d : %d/1024\n",e,score_val(e));

    printf("\n=== H2b: t as a FIELD WIDTH, dx sign-extended from W bits (per multiply) ===\n");
    int bw=0,bw1=0,bw2=0;
    for(int W1=16;W1<=28;W1++) for(int W2=16;W2<=28;W2++){ i64 a,b; int s=score_width(W1,W2,&a,&b); if(s>bw){bw=s;bw1=W1;bw2=W2;} }
    printf("  best %d/1024 at W1=%d W2=%d\n",bw,bw1,bw2);
    for(int W=20;W<=26;W++){ i64 a,b; int s=score_width(W,W,&a,&b); printf("  W1=W2=%d : %d/1024  J(17)=%lld J(342)=%lld\n",W,s,(long long)a,(long long)b); }

    printf("\n=== H2c: t as an ALIGNMENT, w -> w+1 with the compensating split (KC held) ===\n");
    for(int w=11;w<=15;w++){
        int tgt=2*w+15; int bs=0,bs1=0;
        for(int s1=8;s1<=34;s1++){ int s2=tgt-s1; if(s2<4||s2>34) continue; i64 a,b,c; int s=score_align(w,s1,s2,KC,&a,&b,&c); if(s>bs){bs=s;bs1=s1;} }
        i64 a=0,b=0,c=0; if(bs1) score_align(w,bs1,tgt-bs1,KC,&a,&b,&c);
        printf("  w=%2d  s1+s2=%2d  best %4d/1024 at (s1,s2)=(%d,%d)  J(17)=%lld[need 45617] J(342)=%lld[need 18466836] J(-342)=%lld\n",
            w,tgt,bs,bs1,tgt-bs1,(long long)a,(long long)b,(long long)c);
    }
    printf("\n  and: one extra bit kept at the FIRST latch only, w=13, s1+s2=41 held:\n");
    for(int s1=19;s1<=27;s1++){ i64 a,b,c; int s=score_align(13,s1,41-s1,KC,&a,&b,&c);
        printf("   (s1,s2)=(%2d,%2d) : %4d/1024  J(17)=%lld  J(342)=%lld  J(-342)=%lld\n",s1,41-s1,s,(long long)a,(long long)b,(long long)c); }
    return 0;
}
