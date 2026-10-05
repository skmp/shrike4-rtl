// fv_wiring_1.cpp -- INDEPENDENT adversarial verification of the "21-bit sine field +
// complement-feedback final multiply makes the COARSE latch reach free-J floor 0" claim.
// Written from the prose description only; uses base2.h (arr, sm, TB, decode) and nothing from f3_*.
// All arithmetic integer.  long double appears only in printf of a scale estimate.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>

static Fsca F;
// the pinned hardware C0 (26-bit table field) -- frozen, never refit
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                            47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}

// ---------------- sine field rules.  ONE rule for all 17 nodes, no per-node freedom ----------------
// k_n = the sine's octave: sin(n pi/32) in [2^-(k+1), 2^-k)
static int kof(int n){ if(n==0) return 0; i128 s=sin_(PI_()*n/32); int k=0; while(s*((i128)1<<(k+1))<QONE)k++; return k; }
enum { SV_PUB=0, SV_RN21=1, SV_RN22=2, SV_RN24=3, SV_FIX22=4, SV_TR21=5, SV_RN20=6, SV_RN23F=7 };
static const char* SVN[8]={"RN23 normalised u=30+k (PUBLISHED)","RN21 normalised u=28+k (CLAIM)",
    "RN22 normalised u=29+k","RN24 normalised u=31+k","fixed RN(sin*2^22) u=29",
    "trunc21 normalised u=28+k","RN20 normalised u=27+k","fixed RN(sin*2^23) u=30"};
static void sfield(int sv,int n,i64&S,int&u){
    int k=kof(n); i128 s=sin_(PI_()*n/32);
    auto rn=[&](int sh){ return rn_scale(s,sh); };
    auto tr=[&](int sh){ return (i64)((s<<sh)/QONE); };
    switch(sv){
      case SV_PUB:  S=std::min(rn(23+k),(i64)((1ll<<23)-1)); u=30+k; break;
      case SV_RN21: S=std::min(rn(21+k),(i64)((1ll<<21)-1)); u=28+k; break;
      case SV_RN22: S=std::min(rn(22+k),(i64)((1ll<<22)-1)); u=29+k; break;
      case SV_RN24: S=std::min(rn(24+k),(i64)((1ll<<24)-1)); u=31+k; break;
      case SV_FIX22:S=rn(22); u=29; break;
      case SV_TR21: S=std::min(tr(21+k),(i64)((1ll<<21)-1)); u=28+k; break;
      case SV_RN20: S=std::min(rn(20+k),(i64)((1ll<<20)-1)); u=27+k; break;
      default:      S=rn(23); u=30; break; }
    if(n==0){S=0;u=30;}
}
// ---------------- final multiply forms ----------------
//  FORM_PUB : T2 = sm(arr(J,S) + K3, u)                    (the published rule)
//  FORM_FB  : T2 = (arr(S, M) - arr(S, M - J) + K3) >> u    (complement feedback, negated difference,
//                                                            ONE truncation; M = 2^w or 2^w - 1)
//  cmpl: 0 -> two's complement field M-J with M=2^w ; 1 -> one's complement M-1-J
//  role: 0 -> arr(S, field)   (sine recoded)   ; 1 -> arr(field, S) (field recoded)
struct Form { int kind; int w; int cmpl; int role; };   // kind 0=PUB 1=FB
static inline i64 fin_val(const Form&f,i64 J,i64 S,int u){
    const i64 K3=1ll<<19;
    if(!S) return 0;
    if(f.kind==0) return sm(arr(J,S)+K3,u);
    i64 M=(1ll<<f.w)-(f.cmpl?1:0);
    i64 p0 = f.role? arr(M,S) : arr(S,M);
    i64 p1 = f.role? arr(M-J,S) : arr(S,M-J);
    return (p0-p1+K3)>>u;
}
// ---------------- dataset: for each t the interval of J reproducing all 31 node instances ----------
struct DS { i64 JL[1024],JH[1024]; int feasible; };
static void build(const Form&f,int sv,int du,DS&D,i64 Sv[17],int Uv[17]){
    for(int n=0;n<=16;n++){ sfield(sv,n,Sv[n],Uv[n]); Uv[n]+=du; }
    const i64 LIM=12000000ll<<du; D.feasible=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!Sv[n])continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(fin_val(f,m,Sv[n],Uv[n])>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(fin_val(f,m,Sv[n],Uv[n])<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        D.JL[t+512]=gl; D.JH[t+512]=gh; if(gl<=gh)D.feasible++; }
}
// free-J floor: for each infeasible t, min violations over a scanned J band
static long freefloor(const Form&f,int du,DS&D,i64 Sv[17],int Uv[17],i64 rad){
    long tot=0;
    for(int t=-512;t<=511;t++){ if(D.JL[t+512]<=D.JH[t+512])continue;
        i64 dx=(i64)t<<13; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; int bv=1<<30;
        i64 a=std::max((i64)0,D.JH[t+512]-rad), b=D.JL[t+512]+rad;
        for(i64 J=a;J<=b;J++){ int v=0;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!Sv[n])continue;
                i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 V=C0TAB[n]+s*T1-fin_val(f,J,Sv[n],Uv[n])-s*T3;
                int ang=(node*1024+t)&0xffff; i64 lo,hi; win_of(ang,lo,hi); if(V<lo||V>hi)v++; }
            if(v<bv)bv=v; }
        tot+=bv; }
    return tot;
}
// MONOTONICITY AUDIT: the binary-search window is only valid if T2 is nondecreasing in J.
static long mono_check(const Form&f,int sv,int du,i64 LIMIT){
    i64 Sv[17]; int Uv[17]; for(int n=0;n<=16;n++){ sfield(sv,n,Sv[n],Uv[n]); Uv[n]+=du; }
    long bad=0;
    #pragma omp parallel for reduction(+:bad) schedule(dynamic)
    for(int n=1;n<=16;n++){ if(!Sv[n])continue; i64 prev=fin_val(f,0,Sv[n],Uv[n]);
        for(i64 J=1;J<=LIMIT;J++){ i64 cur=fin_val(f,J,Sv[n],Uv[n]); if(cur<prev)bad++; prev=cur; } }
    return bad;
}
// full model, published chain, nodes 0..31
static void fullmodel(const Form&f,int sv,int du,int s1,int s2,i64 K1,i64 K2,int ro,int&viol,int&guard){
    i64 Sv[17]; int Uv[17]; for(int n=0;n<=16;n++){ sfield(sv,n,Sv[n],Uv[n]); Uv[n]+=du; }
    viol=0;guard=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        int isg = (n==0||n==1||n==2||n==16);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 I1=((ro? arr(dx,KC):arr(KC,dx))+K1)>>s1; i64 J=(arr(I1,dx)+K2)>>s2;
            i64 V=C0TAB[n]+s*T1-fin_val(f,J,Sv[n],Uv[n])-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            if(V<lo||V>hi){viol++; if(isg)guard++;} } }
}
static void pinned(const DS&D,const char*tag){
    const int PT[8]={17,-17,342,-342,376,-376,258,-476};
    printf("   %s pinned offsets:",tag);
    for(int i=0;i<8;i++){int t=PT[i];
        printf("  t=%d:[%lld,%lld]%s",t,(long long)D.JL[t+512],(long long)D.JH[t+512],
               D.JL[t+512]>D.JH[t+512]?"INFEAS":""); }
    printf("\n");
}
int main(int argc,char**argv){
    if(!load_fsca(F)){puts("cannot load dump");return 1;} build_tables();
    int mode = argc>1? atoi(argv[1]) : 0;
    // ---- sanity: C0TAB agrees with the t=0 windows, and the published baseline reproduces ----
    if(mode==0){
      puts("=== A. frozen tables / harness sanity ===");
      int bad=0; for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node;
        int a=(node*1024)&0xffff; i64 lo,hi; win_of(a,lo,hi); i64 c=flip? -C0TAB[n]*0+C0TAB[n] : C0TAB[n];
        i64 L=flip?-hi:lo, H=flip?-lo:hi; if(c<L||c>H)bad++; (void)L;(void)H; }
      printf("   C0TAB inside its own t=0 window for all 31 node instances: %s (%d bad)\n",bad?"NO":"yes",bad);
      for(int n=0;n<=16;n++){ i64 S;int u; sfield(SV_PUB,n,S,u);
        if(S!=TB[n].S||u!=TB[n].u2){printf("   MISMATCH vs base2 at n=%d: S %lld/%lld u %d/%d\n",n,(long long)S,(long long)TB[n].S,u,TB[n].u2);} }
      puts("   my SV_PUB field == base2's TB[].S/u2 (checked all 17 nodes)");
      Form fp{0,0,0,0}; int v,g; fullmodel(fp,SV_PUB,0,23,20,K,K,1,v,g);
      printf("   published model, arr(dx,KC): %d violations, guard(0,1,2,16) %d   [expect 147 / 0]\n",v,g);
      fullmodel(fp,SV_PUB,0,23,20,K,K,0,v,g);
      printf("   published model, arr(KC,dx): %d violations, guard %d            [expect 148 / 0]\n\n",v,g);
      puts("=== B. the sine fields, printed ===");
      for(int sv=0;sv<8;sv++){ printf("   %-34s :",SVN[sv]);
        for(int n=1;n<=16;n++){i64 S;int u;sfield(sv,n,S,u); printf(" %lld/%d",(long long)S,u);} printf("\n"); }
      puts("");
    }
    // ---- the claim ----
    if(mode==0||mode==1){
      puts("=== C. free-J floor: published rule vs the claimed floor-0 rule, BOTH latch scales ===");
      struct Cand{int sv;Form f;const char*name;};
      std::vector<Cand> cs;
      cs.push_back({SV_PUB, Form{0,0,0,0}, "PUBLISHED: RN23/u=30+k, T2=sm(arr(J,S)+2^19,u)"});
      cs.push_back({SV_RN21,Form{0,0,0,0}, "RN21/u=28+k, PUBLISHED final multiply (control)"});
      cs.push_back({SV_PUB, Form{1,23,0,0},"RN23/u=30+k, feedback w=23 2sC sine-recoded (control)"});
      cs.push_back({SV_RN21,Form{1,23,0,0},"CLAIM: RN21/u=28+k, feedback w=23 2sC sine-recoded"});
      cs.push_back({SV_RN21,Form{1,23,1,0},"RN21/u=28+k, feedback w=23 1sC sine-recoded"});
      cs.push_back({SV_RN21,Form{1,23,0,1},"RN21/u=28+k, feedback w=23 2sC field-recoded"});
      cs.push_back({SV_RN22,Form{1,23,0,0},"RN22/u=29+k, feedback w=23 2sC sine-recoded"});
      cs.push_back({SV_FIX22,Form{1,23,0,0},"fixed RN(sin*2^22)/u=29, feedback w=23 2sC"});
      for(auto&c:cs){ printf("\n   %s\n",c.name);
        for(int du=0;du<=2;du+=2){ DS D; i64 Sv[17]; int Uv[17]; build(c.f,c.sv,du,D,Sv,Uv);
          long fl=freefloor(c.f,du,D,Sv,Uv,600);
          printf("     %s latch (shift %d): feasible %d/1024, free-J floor %ld\n",du?"fine  ":"coarse",20-du,D.feasible,fl);
          pinned(D,du?"fine  ":"coarse"); } }
      puts("");
    }
    return 0;
}
