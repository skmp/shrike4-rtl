// wb_even.cpp -- for each candidate form of the even path's last multiply, recompute the per-t allowed
// window for the square latch J (scale 2^-33 of the fraction 1 - 2pi^2 dx^2) by intersecting all 31 node
// instances, and report: infeasible t, +/- disjoint pairs (= symmetric-rule ceiling), score of the two
// reference rules, and whether the guard nodes 0,1,2,16 can be satisfied for all 1024 t.
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// ---- variants of the even part E(J, n) --------------------------------------------------------
// V0 separate, 23-bit S, u=30+k                 V1 same, roles swapped
// V2 separate, 26-bit C0 operand, u=33          V3 same, roles swapped
// V4 merged, rec=C0(26b), mul=2^33-J, u=33      V5 merged, rec=2^33-J (needs 17 digits), mul=C0
// V6 merged with the 23-bit field: rec=S, mul=2^(30+k)-J*2^(k-3)?  -- see wb_merge.cpp
static int VAR=0; static i64 KR=1ll<<19;
static inline i64 Eof(i64 J,int n){ const Tab&T=TB[n]; i64 C0=C0FIT[n]; int u=U2FIT[n];
    if(!T.S) return C0;
    switch(VAR){
      case 0: return C0 - sm(arr(J,T.S)+K,u);
      case 1: return C0 - sm(arr(T.S,J)+K,u);
      case 2: return C0 - sm(arr(J,C0)+K,33);
      case 3: return C0 - sm(arr(C0,J)+K,33);
      case 4: return (arrN(C0,(1ll<<33)-J,13)+KR)>>33;
      case 5: return (arrN((1ll<<33)-J,C0,17)+KR)>>33;
    } return 0; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int v0=argc>1?atoi(argv[1]):0, v1=argc>2?atoi(argv[2]):5;
    if(argc>3) KR=atoll(argv[3]);
    printf("Kr=%lld\n",(long long)KR);
    puts("var  infeas  disjoint+-  symceil  score:floor(KCt^2/2^17)  score:chain  guard(n=0,1,2,16 viol)");
    for(VAR=v0;VAR<=v1;VAR++){
        std::vector<i64> JL(1024),JH(1024); std::vector<char> ok(1024,0);
        i64 JMAX=12000000;
        // monotonicity check on the fly via bisection assumptions: E non-increasing in J
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=JMAX;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 elo=lo-s*T1+s*T3, ehi=hi-s*T1+s*T3;       // required even part
                // largest J with E >= elo  ; smallest J with E <= ehi
                i64 a1=-1,b1=JMAX; while(a1<b1){i64 m=(a1+b1+1)/2; if(Eof(m,n)>=elo)a1=m;else b1=m-1;}  // J<=a1
                i64 a2=0,b2=JMAX+1; while(a2<b2){i64 m=(a2+b2)/2; if(Eof(m,n)<=ehi)b2=m;else a2=m+1;}   // J>=a2
                if(a2>gl)gl=a2; if(a1<gh)gh=a1; }
            JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; }
        int infeas=0,disj=0; for(int i=0;i<1024;i++) if(!ok[i])infeas++;
        for(int aa=1;aa<=511;aa++){ int ip=aa+512,im=-aa+512; if(!ok[ip]||!ok[im])continue;
            if(JH[ip]<JL[im]||JH[im]<JL[ip])disj++; }
        int s1=0,s2=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue;
            i64 Ja=(i64)KC*t*t>>17; if(Ja>=JL[t+512]&&Ja<=JH[t+512])s1++;
            i64 dxx=(i64)t<<13; i64 I1=(arr(KC,dxx)+K)>>23; i64 Jb=(arr(I1,dxx)+K)>>20;
            if(Jb>=JL[t+512]&&Jb<=JH[t+512])s2++; }
        // guard nodes: with the per-t window (which already includes them) they are satisfiable iff ok[t];
        // report how many t would violate guard nodes when J is free (i.e. guard-only feasibility)
        int gviol=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=JMAX; bool bad=false;
            int gnodes[4]={0,1,2,16};
            for(int gi=0;gi<4;gi++){ int node=gnodes[gi]; int n=node; i64 s=1;
                const Tab&T=TB[n];
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 elo=lo-s*T1+s*T3, ehi=hi-s*T1+s*T3;
                if(!T.S){ if(C0FIT[n]<elo||C0FIT[n]>ehi) bad=true; continue; }
                i64 a1=-1,b1=JMAX; while(a1<b1){i64 m=(a1+b1+1)/2; if(Eof(m,n)>=elo)a1=m;else b1=m-1;}
                i64 a2=0,b2=JMAX+1; while(a2<b2){i64 m=(a2+b2)/2; if(Eof(m,n)<=ehi)b2=m;else a2=m+1;}
                if(a2>gl)gl=a2; if(a1<gh)gh=a1; }
            if(bad||gl>gh) gviol++; }
        printf("V%d  %5d   %6d      %5d      %6d                 %5d        %5d\n",VAR,infeas,disj,1022-2*disj,s1,s2,gviol);
    }
    return 0; }
