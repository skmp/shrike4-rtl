// w2_land.cpp -- BRANCH 2, landmark pass.  Exhaustive over the stored constant KCw at every alignment.
//   dx = t<<w ; I1 = (arr(KCw,dx)+K1)>>s1 ; J = (arr(I1,dx)+K2)>>s2
// Two modes for the rounding constants:
//   FIXED : K1 = K2 = 2^19 (the physically pinned array constant)
//   FREE  : K1 in [0,2^s1), K2 in [0,2^s2), handled EXACTLY -- over a full period each latch output
//           takes exactly two values v0, v0+1, and for a PAIR of offsets three of the four selections
//           are reachable (which one is excluded is decided by the order of the two breakpoints).
// Scanned region: the rigorous KCw bracket for t=+342 union t=-342 (|arr(a,b)-a*b| <= 2^21, latch
// slack +-2 ULP).  Any KCw satisfying t=342 lies inside it, so the "0 hits" statements are exhaustive.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
static void brk(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
struct Hit{ int w,s1,s2; i64 kc; int which; };
int main(){
    { FILE*f=fopen("jwin18.txt","r"); if(!f){printf("no jwin18.txt\n");return 1;} char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    const i64 J17=JL[529], J342L=JL[854], J342H=JH[854];
    long long tri=0, scanned=0;
    long long fx_342=0, fx_17=0, fx_both=0, fx_all3=0;      // per (w,s1,s2) counts (a triple counted once)
    long long fr_342=0, fr_17=0, fr_both=0, fr_all3=0;
    std::vector<Hit> hits;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:tri,scanned,fx_342,fx_17,fx_both,fx_all3,fr_342,fr_17,fr_both,fr_all3)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++){
      for(int s2=4;s2<=40;s2++){
        long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w));
        if(nom<0.5L||nom>=(long double)KCMAX) continue;
        i64 a1,b1,a2,b2; brk(342,w,s1,s2,a1,b1); brk(-342,w,s1,s2,a2,b2);
        i64 lo=std::min(a1,a2), hi=std::max(b1,b2); if(hi<lo) continue;
        tri++; scanned+=hi-lo+1;
        int F342=0,F17=0,FB=0,FA=0, R342=0,R17=0,RB=0,RA=0;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w, dxm=-dxp;
        for(i64 kc=lo;kc<=hi;kc++){
            // ---------- FIXED K ----------
            i64 i1p=(arr(kc,dxp)+K)>>s1, jp=(arr(i1p,dxp)+K)>>s2;
            int okp = (jp>=J342L&&jp<=J342H);
            if(okp){ F342=1;
                i64 i17=(arr(kc,dx17)+K)>>s1, j17=(arr(i17,dx17)+K)>>s2;
                if(j17==J17){ FB=1; i64 i1m=(arr(kc,dxm)+K)>>s1, jm=(arr(i1m,dxm)+K)>>s2;
                    if(jm>=JL[170]&&jm<=JH[170]) FA=1; } }
            { i64 i17=(arr(kc,dx17)+K)>>s1, j17=(arr(i17,dx17)+K)>>s2; if(j17==J17) F17=1; }
            // ---------- FREE K1,K2 (exact: two latch values each, 3 of 4 pair-selections reachable) ----------
            i64 P17=arr(kc,dx17), Pp=arr(kc,dxp), Pm=arr(kc,dxm);
            i64 v17=P17>>s1, vp=Pp>>s1, vm=Pm>>s1;
            i64 bp17=((i64)1<<s1)-(P17-(v17<<s1)), bpp=((i64)1<<s1)-(Pp-(vp<<s1)), bpm=((i64)1<<s1)-(Pm-(vm<<s1));
            int hit17=0, hitp=0, hitm=0, hitboth=0, hitall=0;
            for(int c17=0;c17<2;c17++){ i64 Q=arr(v17+c17,dx17); i64 q=Q>>s2;
                // J in {q, q+1} as K2 sweeps
                if(q==J17||q+1==J17) hit17=1; }
            for(int cp=0;cp<2;cp++){ i64 Q=arr(vp+cp,dxp); i64 q=Q>>s2;
                if((q>=J342L&&q<=J342H)||(q+1>=J342L&&q+1<=J342H)) hitp=1; }
            for(int cm=0;cm<2;cm++){ i64 Q=arr(vm+cm,dxm); i64 q=Q>>s2;
                if((q>=JL[170]&&q<=JH[170])||(q+1>=JL[170]&&q+1<=JH[170])) hitm=1; }
            if(hit17&&hitp){  // joint: need ONE K1 and ONE K2 to serve both offsets
              for(int c17=0;c17<2;c17++) for(int cp=0;cp<2;cp++){
                // K1 selection reachable?  c=1 means K1 >= bp.  Need a K1 with both conditions.
                bool ok1;
                if(c17==0&&cp==0) ok1=true;                       // K1 = 0
                else if(c17==1&&cp==1) ok1=(bp17<((i64)1<<s1))&&(bpp<((i64)1<<s1));
                else if(c17==1&&cp==0) ok1=(bp17<bpp)&&(bp17<((i64)1<<s1));
                else                   ok1=(bpp<bp17)&&(bpp<((i64)1<<s1));
                if(!ok1) continue;
                i64 Q17=arr(v17+c17,dx17), Qp=arr(vp+cp,dxp);
                i64 q17=Q17>>s2, qp=Qp>>s2;
                i64 k17=((i64)1<<s2)-(Q17-(q17<<s2)), kp=((i64)1<<s2)-(Qp-(qp<<s2));
                for(int d17=0;d17<2;d17++) for(int dp=0;dp<2;dp++){
                    if(q17+d17!=J17) continue; if(qp+dp<J342L||qp+dp>J342H) continue;
                    bool ok2;
                    if(d17==0&&dp==0) ok2=true;
                    else if(d17==1&&dp==1) ok2=(k17<((i64)1<<s2))&&(kp<((i64)1<<s2));
                    else if(d17==1&&dp==0) ok2=(k17<kp)&&(k17<((i64)1<<s2));
                    else                   ok2=(kp<k17)&&(kp<((i64)1<<s2));
                    if(ok2) hitboth=1;
                } } }
            if(hit17) R17=1; if(hitp) R342=1; if(hitboth){ RB=1; if(hitm) hitall=1; }
            if(hitall) RA=1;
        }
        fx_342+=F342; fx_17+=F17; fx_both+=FB; fx_all3+=FA;
        fr_342+=R342; fr_17+=R17; fr_both+=RB; fr_all3+=RA;
      }
    }
    printf("(w,s1,s2) triples with a scale-admissible KCw : %lld\n",tri);
    printf("KCw values densely scanned (t=+-342 bracket)  : %lld\n\n",scanned);
    printf("K1=K2=2^19 FIXED:\n");
    printf("  triples where some KCw gives J(342)=%lld : %lld\n",(long long)J342L,fx_342);
    printf("  triples where some KCw gives J(17)=%lld     : %lld   (inside this bracket only)\n",(long long)J17,fx_17);
    printf("  triples where ONE KCw gives BOTH             : %lld\n",fx_both);
    printf("  ... and J(-342) in window too                : %lld\n\n",fx_all3);
    printf("K1,K2 FREE (exact, all 2^s1 x 2^s2 constants):\n");
    printf("  triples where some KCw gives J(342) in window: %lld\n",fr_342);
    printf("  triples where some KCw gives J(17)=%lld     : %lld   (inside this bracket only)\n",(long long)J17,fr_17);
    printf("  triples where ONE (KCw,K1,K2) gives BOTH     : %lld\n",fr_both);
    printf("  ... and J(-342) in window too                : %lld\n",fr_all3);
    return 0; }
