// wb_sprec.cpp -- how precise is the sine operand of the square multiply?  For every quantisation width b
// (T2 = floor((J*RN(sin*2^b) + Kr) / 2^(b+7))) recompute the per-t allowed J window and report the
// feasibility structure.  b = 23+k is the current 23-bit normalised field; b = 60 is exact.
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 SB[17]; static int BB; static i64 KRR;
static inline i64 T2of(i64 J,int n){ if(!SB[n])return 0; return (J*SB[n]+KRR)>>(BB+7); }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    puts("  b  Kr     infeas  disj  symceil  sc_floor sc_chain  guard016");
    for(int b=22;b<=45;b++){
      for(int kri=0;kri<3;kri++){
        BB=b; KRR = kri==0?0 : kri==1?(1ll<<19) : (1ll<<(b+6));
        if(KRR>=(1ll<<(b+7))) continue;
        for(int n=0;n<=16;n++){ i128 a=PI_()*n/32; i128 s=sin_(a); SB[n]= n? rn_scale(s,b) : 0; }
        std::vector<i64> JL(1024),JH(1024); std::vector<char> ok(1024,0); i64 JMAX=12000000; int gv=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=JMAX,ggl=0,ggh=JMAX; bool gbad=false;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 t2lo=C0FIT[n]-(hi-s*T1+s*T3), t2hi=C0FIT[n]-(lo-s*T1+s*T3);
                // T2 non-decreasing in J: smallest J with T2>=t2lo, largest with T2<=t2hi
                i64 a1=0,b1=JMAX+1; while(a1<b1){i64 m=(a1+b1)/2; if(T2of(m,n)>=t2lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=JMAX;  while(a2<b2){i64 m=(a2+b2+1)/2; if(T2of(m,n)<=t2hi)a2=m;else b2=m-1;}
                if(a1>gl)gl=a1; if(a2<gh)gh=a2;
                if(node==1||node==2||node==16){ if(a1>ggl)ggl=a1; if(a2<ggh)ggh=a2; } }
            { int a=t&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
              i64 T1=sm(arr(TB[0].C1f,dx)+K,23-TB[0].e1); i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
              i64 T3=sm(arr(TB[0].c3<<5,A3)+K,26); i64 V=T1-T3; if(V<lo||V>hi) gbad=true; }
            JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; if(gbad||ggl>ggh)gv++; }
        int infeas=0,disj=0; for(int i=0;i<1024;i++) if(!ok[i])infeas++;
        for(int aa=1;aa<=511;aa++){ int ip=aa+512,im=-aa+512; if(!ok[ip]||!ok[im])continue;
            if(JH[ip]<JL[im]||JH[im]<JL[ip])disj++; }
        int s1=0,s2=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue;
            i64 Ja=(i64)KC*t*t>>17; if(Ja>=JL[t+512]&&Ja<=JH[t+512])s1++;
            i64 dxx=(i64)t<<13; i64 I1=(arr(KC,dxx)+K)>>23; i64 Jb=(arr(I1,dxx)+K)>>20;
            if(Jb>=JL[t+512]&&Jb<=JH[t+512])s2++; }
        printf("%3d %2d  %5d  %4d  %6d   %5d %5d  %5d\n",b,kri,infeas,disj,1022-2*disj,s1,s2,gv);
      } }
    return 0; }
