// v32a_base.cpp -- HARNESS VALIDATION for branch 32.  Nothing here is a hypothesis: it only checks
// that my own code reproduces the published contract before I use it to judge anyone's claim.
#include "v32_common.h"
#include <random>
int main(){ if(!load_fsca(FD)){puts("no dump");return 1;} build_tables();
    if(!load_jwin()){puts("no jwin18.txt");return 1;}

    // --- 1. arrN(.,.,14) must be arr(.,.) bit for bit, over the real operand domains and random ones
    {   long long n=0, bad=0;
        std::mt19937_64 rg(12345);
        for (int i=0;i<2000000;i++){ i64 rec=(i64)(rg()%(1ull<<26))-(1ll<<25); i64 mul=(i64)(rg()%(1ull<<25))-(1ll<<24);
            if (arrN(rec,mul,14)!=arr(rec,mul)) bad++; n++; }
        for (int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 I1=(arr(KC,dx)+K)>>23; i64 J18=(arr(I1,dx)+K)>>18, J20=(arr(I1,dx)+K)>>20;
            i64 chk[8]={arr(KC,dx),arr(dx,KC),arr(I1,dx),arr(((i64)t*t)<<3,dx),arr(J18,TB[16].S),arr(J20,TB[16].S),arr(TB[3].C1f,dx),arr(TB[3].c3<<5,A3_of(t,dx,14))};
            i64 chn[8]={arrN(KC,dx,14),arrN(dx,KC,14),arrN(I1,dx,14),arrN(((i64)t*t)<<3,dx,14),arrN(J18,TB[16].S,14),arrN(J20,TB[16].S,14),arrN(TB[3].C1f,dx,14),arrN(TB[3].c3<<5,A3_of(t,dx,14),14)};
            for(int i=0;i<8;i++){ n++; if(chk[i]!=chn[i]) bad++; } }
        printf("1. arrN(.,.,14) vs base2 arr(): %lld checks, %lld mismatches\n", n, bad); }

    // --- 2. regenerate jwin18.txt with b39's own algorithm and diff it against the file
    {   const int DU=2; const i64 LIM=12000000ll<<DU; int diff=0, feas=0; long long sumw=0;
        for (int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM; i64 A3=A3_of(t,dx,14);
            for (int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                i64 s=flip?-1:1; i64 T1=T1_of(n,dx,14), T3=T3_of(n,A3,14);
                i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
                i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
                i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arrN(m,TB[n].S,14)+K,u)>=t2lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arrN(m,TB[n].S,14)+K,u)<=t2hi)a2=m;else b2=m-1;}
                if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            if(gl!=FL[t+512]||gh!=FH[t+512]) diff++;
            if(gl<=gh){feas++; sumw+=gh-gl+1;} }
        printf("2. regenerated jwin18 vs the published file: %d/1024 rows differ ; %d feasible, mean width %.1f\n",
               diff, feas, (double)sumw/feas); }

    // --- 3. bisection-free validation of the published windows: endpoints in, neighbours out
    {   int okin=0, okout=0, badin=0, badout=0;
        for (int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 A3=A3_of(t,dx,14);
            auto all_nodes_ok=[&](i64 J)->bool{
                for (int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                    i64 V=V_of(node,t,J,14,2); int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
                    if(V<lo||V>hi) return false; } (void)A3; return true; };
            i64 L=FL[t+512],H=FH[t+512];
            if(all_nodes_ok(L)) okin++; else badin++;
            if(L!=H){ if(all_nodes_ok(H)) okin++; else badin++; }
            if(L>0 && !all_nodes_ok(L-1)) okout++; else if(L>0) badout++;
            if(!all_nodes_ok(H+1)) okout++; else badout++; }
        printf("3. endpoint audit (no bisection): endpoints inside %d good / %d bad ; neighbours outside %d good / %d bad\n",
               okin,badin,okout,badout); }

    // --- 4. baseline chain scored on the published windows
    {   int sc=0; for(int t=-512;t<=511;t++){ i64 J=J_of(t,14,18,0); if(J>=FL[t+512]&&J<=FH[t+512])sc++; }
        printf("4. baseline chain (ND=14, s2=18) on jwin18.txt: %d/1024 ; J(17)=%lld J(342)=%lld J(-342)=%lld\n",
               sc,(long long)J_of(17,14,18,0),(long long)J_of(342,14,18,0),(long long)J_of(-342,14,18,0)); }

    // --- 5. the published full-model counts, coarse (DU=0) and fine (DU=2), guard sequenced separately
    for (int DU=0;DU<=2;DU+=2) for (int swap=0;swap<2;swap++){
        int s2 = 20-DU; int v=0, g=0;
        for (int node=0;node<=31;node++) for (int t=-512;t<=511;t++){
            i64 J=J_of(t,14,s2,swap); i64 V=V_of(node,t,J,14,DU);
            int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            if(V<lo||V>hi){ v++; int n=node>=17?32-node:node; if(n==0||n==1||n==2||n==16) g++; } }
        printf("5. full model DU=%d (latch shift %d) arr(%s): %d violations, guard(0,1,2,16)=%d\n",
               DU,s2,swap?"dx,KC":"KC,dx",v,g); }
    return 0; }
