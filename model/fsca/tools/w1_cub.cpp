// w1_cub.cpp -- MODEL-FREE partition finder.  For a set of angles S, the minimal L-infinity deviation of
// ANY cubic from the dump on S is bounded below by the 4th-divided-difference certificate: for equally
// spaced t0..t4 (spacing s), a cubic p has sum (-1)^i C(4,i) p(t_i) == 0 exactly, so
//     slack >= ( |sum (-1)^i C(4,i) V_i| - sum C(4,i)*w_i ) / 16 ,
// where [V_i - w_i, V_i + w_i] is the dump window.  Pure integer arithmetic (windows are integers).
// A candidate node interval that STRADDLES a real node boundary contains two different cubics and the
// certificate blows up.  Scanning the certificate over every 5-point stencil therefore reads the
// partition straight off the dump.
#include "base2.h"
#include <cstring>
#include <algorithm>
static Fsca F;
static i64 VL[65536],VH[65536];
static inline void load_win(){ for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;} } }
// certificate over the interval [c-H, c+H-1] (angles taken mod 65536), all equal spacings
static i64 cert(int c,int H){
    i64 best=0; const int C4[5]={1,4,6,4,1};
    for(int s=1;s<=(2*H-1)/4;s++){
        for(int off=c-H; off+4*s<=c+H-1; off++){
            i64 mid=0,slack=0;
            for(int i=0;i<5;i++){ int a=(off+i*s)&0xffff; i64 lo=VL[a],hi=VH[a];
                i64 m=(lo+hi); /* 2*mid */ i64 w=(hi-lo); /* 2*halfwidth */
                mid += (i&1? -1:1)*(i64)C4[i]*m; slack += (i64)C4[i]*w; }
            i64 v = (mid<0?-mid:mid) - slack;   // both are doubled; ratio unaffected
            if(v>best) best=v; } }
    return best; }   // returns 2*16*minimal_slack lower bound (doubled units); divide by 32
int main(){ if(!load_fsca(F))return 1; load_win();
    printf("Minimal L-inf slack a CUBIC needs, per candidate node interval (units of 2^-26).\n");
    printf("Certificate is a rigorous LOWER bound and uses only the dump's windows.\n\n");
    for(int H : {256,512,1024}){
        printf("== half-width H=%d  (node span %d angles, %d nodes/turn) ==\n",H,2*H,65536/(2*H));
        for(int phase=0; phase<2; phase++){
            // phase 0: centres at multiples of 2H   (t centred, table point at the centre)
            // phase 1: centres at odd multiples of H (i.e. boundaries at multiples of 2H)
            i64 worst=0; int wc=0; double sum=0; int cnt=0;
            for(int c=(phase?H:0); c<16384+H; c+=2*H){
                if(c-H<0||c+H-1>=32768) continue;
                i64 v=cert(c,H); double sl=(double)v/32.0; sum+=sl; cnt++;
                if(v>worst){worst=v;wc=c;} }
            printf("  phase %d (table points at angle %s): mean slack %.2f, worst %.2f at centre %d  over %d nodes\n",
                phase, phase?"H mod 2H":"0 mod 2H", sum/cnt, (double)worst/32.0, wc, cnt); }
        printf("\n"); }
    return 0; }
