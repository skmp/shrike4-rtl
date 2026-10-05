// v40_c.cpp -- the USER's two hypotheses, re-implemented from scratch (branch 40).
//  H1 "t computed in 2s complement, consumed as 1s complement"  -> negative operands off by +-1 (t level)
//     and the finer dx-level and half-unit readings, at every placement.
//  H2 "t has one more bit of precision" -> value / field-width / alignment readings.
//  plus the SIGN-INDEPENDENT version of the same idea: 1s-vs-2s complement of the ARRAY'S ROWS.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JLv[1024],JHv[1024];
static bool loadwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f) return false; char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,a,b,w; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JLv[t+512]=a;JHv[t+512]=b;n++; }
  fclose(f); return n==1024; }
// parameterised array: hot=0 model (hot one only from col 18), 1 = full 2s-complement rows, 2 = no hot ones
// xs=1: row sign from XOR of digit sign and multiplicand sign (the other admissible rule)
static i64 arrv(i64 rec,i64 mul,int hot,int xs){ i64 R[14]; i64 T=0;
    for(int k=0;k<14;k++){ auto bit=[&](int b)->int{return b<0?0:(int)((rec>>(b>62?62:b))&1);} ;
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
        int neg = xs ? ((d<0)^(mul<0)) && d!=0 : (d<0);
        i64 mag=(i64)(d<0?-d:d)*mul; i64 row=neg?~mag:mag; R[k]=row*(1ll<<(2*k));
        T+=(R[k]>>16)<<16;
        if(neg){ if(hot==0){ if(2*k>=18) T+=1ll<<(2*k);} else if(hot==1) T+=1ll<<(2*k); } }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2);
    int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16; return T; }
struct Out{int score; i64 j17,j342,jm342;};
// generic evaluation: dx1 offset for mul1, dx2 offset for mul2, I1 offset, shifts, array variant
static Out eval(int w,int s1,int s2,i64 kc,i64 K1,i64 K2,
                i64 o1n,i64 o2n,i64 oIn,int hot,int xs,int W1,int W2){
    Out o{0,0,0,0};
    auto sx=[&](i64 v,int W)->i64{ if(W<=0||W>=63) return v; i64 m=v&((1ll<<W)-1); return m>=(1ll<<(W-1))? m-(1ll<<W): m; };
    for(int t=-512;t<512;t++){
        i64 dx=(i64)t<<w;
        i64 dx1=dx+(t<0?o1n:0), dx2=dx+(t<0?o2n:0);
        dx1=sx(dx1,W1); dx2=sx(dx2,W2);
        i64 I1=(arrv(kc,dx1,hot,xs)+K1)>>s1;
        if(I1<0) I1+=oIn;
        i64 J=(arrv(I1,dx2,hot,xs)+K2)>>s2;
        if(J>=JLv[t+512]&&J<=JHv[t+512]) o.score++;
        if(t==17)o.j17=J; if(t==342)o.j342=J; if(t==-342)o.jm342=J; }
    return o; }
int main(){ if(!loadwin()){printf("FATAL\n");return 1;}
    const i64 K1=1ll<<19, K2=1ll<<19;
    Out base=eval(13,23,18,KC,K1,K2,0,0,0,0,0,0,0);
    printf("[c0] baseline: %d/1024  J(17)=%lld J(342)=%lld J(-342)=%lld\n",base.score,(long long)base.j17,(long long)base.j342,(long long)base.jm342);
    // structural fact: are any operands negative at the two pinned POSITIVE offsets?
    for(int t:{17,342}){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 P=arr(I1,dx);
        printf("[c1] t=%d: dx=%lld (>0) KC=%lld (>0) I1=%lld (%s) raw product=%lld -> no operand is negative, so NO rule keyed on negative representation can move J here\n",
            t,(long long)dx,(long long)KC,(long long)I1,I1<0?"NEG":"POS",(long long)P); }
    // ---- H1: negative-side offsets at three placements
    printf("[c2] H1 (negative-side offset, t-level d=-1..+1 at 3 placements, 27 rows; offset applied only when t<0 / I1<0)\n");
    int best=0; i64 bo[3]={0,0,0};
    for(int a=-1;a<=1;a++)for(int b=-1;b<=1;b++)for(int c=-1;c<=1;c++){
        Out o=eval(13,23,18,KC,K1,K2,(i64)a<<13,(i64)b<<13,c,0,0,0,0);
        if(o.score>best){best=o.score;bo[0]=a;bo[1]=b;bo[2]=c;}
        if(a==1&&b==1&&c==0) printf("      literal 1s-consumed (da=db=+1,dc=0): %d/1024  J(17)=%lld J(342)=%lld\n",o.score,(long long)o.j17,(long long)o.j342);
        if(o.j17!=base.j17||o.j342!=base.j342) printf("      !! POSITIVE LANDMARK MOVED at (%d,%d,%d): J(17)=%lld J(342)=%lld\n",a,b,c,(long long)o.j17,(long long)o.j342); }
    printf("      best of 27: %d/1024 at (da,db,dI1)=(%lld,%lld,%lld)\n",best,(long long)bo[0],(long long)bo[1],(long long)bo[2]);
    // finer readings: dx-level +-1,+-2, half-a-t +-4096, whole t +-8192, at the 3 placements
    printf("[c3] H1 finer/coarser negative-side offsets (dx units), 9^2 x 9 placements\n");
    static const i64 QS[]={-8192,-4096,-2,-1,0,1,2,4096,8192};
    int best2=0; i64 b2[3]={0,0,0}; int moved=0;
    for(i64 qa:QS)for(i64 qb:QS)for(i64 qc:{-2ll,-1ll,0ll,1ll,2ll}){
        Out o=eval(13,23,18,KC,K1,K2,qa,qb,qc,0,0,0,0);
        if(o.score>best2){best2=o.score;b2[0]=qa;b2[1]=qb;b2[2]=qc;}
        if(o.j17!=base.j17||o.j342!=base.j342) moved++; }
    printf("      405 configs: best %d/1024 at (qa,qb,qI1)=(%lld,%lld,%lld) ; configs that moved J(17) or J(342): %d\n",best2,(long long)b2[0],(long long)b2[1],(long long)b2[2],moved);
    // ---- H2 (a): value reading, dx=(2t+e)<<12
    printf("[c4] H2 as a VALUE: dx=(2t+e)<<12\n");
    for(int e=-4;e<=4;e++){ Out o{0,0,0,0};
        for(int t=-512;t<512;t++){ i64 dx=((i64)2*t+e)<<12; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
            if(J>=JLv[t+512]&&J<=JHv[t+512])o.score++; if(t==17)o.j17=J; if(t==342)o.j342=J; }
        printf("      e=%+d : %4d/1024  J(17)=%lld J(342)=%lld\n",e,o.score,(long long)o.j17,(long long)o.j342); }
    // ---- H2 (b): field width
    printf("[c5] H2 as a FIELD WIDTH: dx sign-extended from W bits, per multiply\n");
    int bw=0,bw1=0,bw2=0;
    for(int W1=18;W1<=28;W1++)for(int W2=18;W2<=28;W2++){ Out o=eval(13,23,18,KC,K1,K2,0,0,0,0,0,W1,W2);
        if(o.score>bw){bw=o.score;bw1=W1;bw2=W2;} }
    printf("      best %d/1024 at W1=%d W2=%d (W>=24 is a no-op here: |dx|<2^22)\n",bw,bw1,bw2);
    // ---- H2 (c): alignment, with the compensating scale law, KC held
    printf("[c6] H2 as an ALIGNMENT: dx=t<<w, s1+s2 = 41+2(w-13), KC held\n");
    for(int w=6;w<=18;w++){ int S=41+2*(w-13); int bs=0,bs1=0;
        for(int s1=6;s1<=S-6;s1++){ int s2=S-s1; if(s2<6||s2>40) continue;
            Out o=eval(w,s1,s2,KC,1ll<<(s1-4),1ll<<(s2-4),0,0,0,0,0,0,0); if(o.score>bs){bs=o.score;bs1=s1;} }
        printf("      w=%2d  best %4d/1024 at s1=%d s2=%d\n",w,bs,bs1,S-bs1); }
    // ---- sign-INDEPENDENT complement variants: the array's own rows
    printf("[c7] 1s-vs-2s complement of the ARRAY ROWS (affects positive operands too)\n");
    const char* hn[3]={"1s rows, hot one only from col 18 (the FIPR-validated rule)","full 2s-complement rows (hot one on every negative row)","1s rows, NO hot ones at all"};
    for(int hot=0;hot<3;hot++)for(int xs=0;xs<2;xs++){ Out o=eval(13,23,18,KC,K1,K2,0,0,0,hot,xs,0,0);
        printf("      hot=%d xs=%d %-58s: %4d/1024  J(17)=%lld (need 45617)  J(342)=%lld (need 18466836)  J(-342)=%lld\n",
            hot,xs,hn[hot],o.score,(long long)o.j17,(long long)o.j342,(long long)o.jm342); }
    return 0; }
