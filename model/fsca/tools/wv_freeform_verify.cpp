// wv_freeform_verify.cpp -- INDEPENDENT adversarial verification of the "wf_*" agent's claims.
// Written from the prose description only; does not include or reuse any wf_*.cpp.
// Everything integer.  long double appears only in printf of a ratio, never in a decision.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>

static Fsca F;
// the baseline per-node table fit, reproduced by running b2.cpp (this run: grand total 148)
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}

// ---------------------------------------------------------------- odd part + windows, per (node,t)
struct Cons { i64 lo, hi; i64 S; int u; int node; };   // T2 must lie in [lo,hi]
static std::vector<Cons> CON[1024];

static void build_constraints(){
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            const Tab&T=TB[n]; if(!T.S) continue;                 // node 0: no square term at all
            i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi; bool ex; win_of(a,lo,hi,ex);
            CON[t+512].push_back({base-hi, base-lo, T.S, U2TAB[n], node}); } }
}

// ---------------------------------------------------------------- independent window regeneration
// No monotonicity assumption: brute scan of a bracket around the ideal, recording min/max and gaps.
static i64 MYL[1024], MYH[1024]; static char MYOK[1024]; static int MYGAP[1024];
static void regen_windows(){
    #pragma omp parallel for schedule(dynamic)
    for(int ti=0; ti<1024; ti++){
        int t = ti-512;
        i64 ideal = (KC*(i64)t*(i64)t) >> 17;
        i64 j0 = ideal-4000; if(j0<0) j0=0; i64 j1 = ideal+4000;
        i64 lo=-1, hi=-1; int gaps=0; bool prev=false;
        for(i64 J=j0;J<=j1;J++){
            bool ok=true;
            for(const Cons&c : CON[ti]){ i64 T2=sm(arr(J,c.S)+K,c.u); if(T2<c.lo||T2>c.hi){ok=false;break;} }
            if(ok){ if(lo<0) lo=J; hi=J; if(!prev && lo!=J) gaps++; }
            prev=ok;
        }
        MYOK[ti]= lo>=0; MYL[ti]=lo<0?0:lo; MYH[ti]=hi<0?0:hi; MYGAP[ti]=gaps;
    }
}

// ---------------------------------------------------------------- committed dataset
static i64 JL[1024], JH[1024]; static int OKT[1024];
static bool load_jwin(const char*p){ FILE*f=fopen(p,"r"); if(!f) return false;
    for(int i=0;i<1024;i++){ int t,ok; long long a,b; if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&ok)!=4){fclose(f);return false;}
        JL[t+512]=a; JH[t+512]=b; OKT[t+512]=ok; } fclose(f); return true; }

// ---------------------------------------------------------------- candidate latch rules
static i64 rule(int id, int t){
    i64 dx=(i64)t<<13;
    switch(id){
    case 0: { i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }            // baseline chain
    case 1: { i64 I1=(arr(KC,dx)+K)>>23; return arr(I1,dx)>>20; }                // baseline, 2nd const dropped
    case 2: { i64 I1=(arr(dx,KC)+K)>>23; return (arr(I1,dx)+K)>>20; }            // roles swapped (claim: 927)
    case 3: { i64 I1=(arr(dx,20694294)+K)>>25; return arr(I1,dx)>>20; }          // claim: 936, best unconstrained
    case 4: { return (662217398ll*(i64)t*(i64)t - 45200ll*(i64)t - 12280342ll) >> 24; } // claim: 935
    }
    return 0;
}
static const char* RN_[5]={"baseline chain (arr(KC,dx))","baseline, 2nd rounding const dropped",
    "roles swapped in 1st mul: arr(dx,KC)","CLAIMED BEST: arr(dx,20694294)>>25 then arr(I1,dx)>>20",
    "CLAIMED fitted single floor"};

// ---------------------------------------------------------------- full model end-to-end
// fixed tables (the published model); returns total violations and fills per-node counts
static int e2e(int id, int per[32]){
    int tot=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n];
        i64 s=flip?-1:1; int v=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 J=rule(id,t);
            i64 T2=T.S? sm(arr(J,T.S)+K,U2TAB[n]):0;
            i64 V=C0TAB[n]+s*T1-T2-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            if(V<lo||V>hi) v++; }
        per[node]=v; tot+=v; }
    return tot;
}
// same but re-fitting C0 and u2 per node exactly as b2.cpp does (the baseline's own procedure)
static int e2e_refit(int id, int per[32]){
    int grand=0; for(int i=0;i<32;i++) per[i]=0;
    for(int n=0;n<=16;n++){
        int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int bv=1<<30; int bper[2]={0,0};
        for(i64 c=lo0-3;c<=hi0+3;c++) for(int u=TB[n].u2-1;u<=TB[n].u2+1;u++){
            int v=0,pr[2]={0,0};
            for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17); int tn=flip?32-m:m; const Tab&T=TB[tn]; i64 s=flip?-1:1;
                for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
                    i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                    i64 T2=T.S? sm(arr(rule(id,t),T.S)+K,u):0;
                    i64 V=c+s*T1-T2-s*T3;
                    int a=(m*1024+t)&0xffff; i64 L,H;bool ex; win_of(a,L,H,ex);
                    if(V<L||V>H){v++;pr[i]++;} } }
            if(v<bv){bv=v;bper[0]=pr[0];bper[1]=pr[1];} }
        grand+=bv; if(n<=31)per[n]=bper[0]; if(nn==2 && 32-n<=31) per[32-n]=bper[1];
    }
    return grand;
}

int main(int argc,char**argv){
    if(!load_fsca(F)){puts("no dump");return 1;} build_tables();
    build_constraints();

    // ---- 1. dataset provenance: regenerate jwin.txt independently
    if(!load_jwin("jwin.txt")){puts("no jwin.txt");return 1;}
    regen_windows();
    int diff=0, gapt=0, myfeas=0, comfeas=0;
    for(int i=0;i<1024;i++){
        if(MYGAP[i]) gapt++;
        if(MYOK[i]) myfeas++;
        if(OKT[i]) comfeas++;
        bool same = (OKT[i]!=0)==(MYOK[i]!=0) && (!MYOK[i] || (JL[i]==MYL[i] && JH[i]==MYH[i]));
        if(!same){ if(diff<8) printf("  DATASET DIFF t=%d committed[%lld,%lld,%d] mine[%lld,%lld,%d]\n",
            i-512,(long long)JL[i],(long long)JH[i],OKT[i],(long long)MYL[i],(long long)MYH[i],(int)MYOK[i]); diff++; }
    }
    printf("[1] dataset regeneration (brute scan, no monotonicity assumed):\n");
    printf("    lines differing from committed jwin.txt : %d / 1024\n", diff);
    printf("    t with a NON-INTERVAL feasible set      : %d\n", gapt);
    printf("    feasible t: mine %d, committed %d\n", myfeas, comfeas);

    // ---- 2. score the claimed rules
    printf("\n[2] score against committed jwin.txt (feasible t only, %d of 1024):\n", comfeas);
    for(int id=0; id<5; id++){
        int sc=0, scmine=0;
        for(int i=0;i<1024;i++){ i64 J=rule(id,i-512);
            if(OKT[i] && J>=JL[i] && J<=JH[i]) sc++;
            if(MYOK[i] && J>=MYL[i] && J<=MYH[i]) scmine++; }
        printf("    rule %d %-56s : %4d/%d  (vs my regen: %4d)\n", id, RN_[id], sc, comfeas, scmine);
    }

    // ---- 3. end to end, fixed published tables
    printf("\n[3] full model over nodes 0..31, FIXED C0/u2 tables:\n");
    for(int id=0; id<5; id++){
        int per[32]; int tot=e2e(id,per);
        bool guard = per[0]==0&&per[1]==0&&per[2]==0&&per[16]==0;
        printf("    rule %d : %4d violations   guard(0,1,2,16)=%s  [n0=%d n1=%d n2=%d n16=%d n30=%d n31=%d]\n",
            id, tot, guard?"EXACT":"BROKEN", per[0],per[1],per[2],per[16],per[30],per[31]);
    }
    printf("\n[3b] full model, C0/u2 RE-FITTED per node exactly as b2.cpp does:\n");
    for(int id=0; id<5; id++){
        int per[32]; int tot=e2e_refit(id,per);
        bool guard = per[0]==0&&per[1]==0&&per[2]==0&&per[16]==0;
        printf("    rule %d : %4d violations   guard(0,1,2,16)=%s  [n0=%d n1=%d n2=%d n16=%d]\n",
            id, tot, guard?"EXACT":"BROKEN", per[0],per[1],per[2],per[16]);
    }
    return 0;
}
