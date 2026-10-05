// fv_wiring_7.cpp -- (H) the exact-arithmetic identity vs the array's complement deficit;
//                    (I) chain sweep with the claim's final multiply: can anything beat 147 guard-clean?
//                    (J) the trap the claim self-reports (trunc24 field, w=31): does it reproduce?
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                            47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static int kof(int n){ if(n==0)return 0; i128 s=sin_(PI_()*n/32); int k=0; while(s*((i128)1<<(k+1))<QONE)k++; return k; }
static i64 S21[17],S23[17],STR24[17]; static int U21[17],U23[17],UTR24[17];
static const i64 MM=1ll<<24;
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int n=0;n<=16;n++){ int k=kof(n); i128 s=sin_(PI_()*n/32);
        S23[n]=n?std::min(rn_scale(s,23+k),(i64)((1ll<<23)-1)):0; U23[n]=n?30+k:30;
        S21[n]=n?std::min(rn_scale(s,21+k),(i64)((1ll<<21)-1)):0; U21[n]=n?28+k:30;
        STR24[n]=n?std::min((i64)((s<<(24+k))/QONE),(i64)((1ll<<24)-1)):0; UTR24[n]=n?31+k:30; }

    puts("=== (H) exact arithmetic vs the array ===");
    { const i64 W=26, MW=1ll<<26, C=5321677; long bad=0;
      for(i64 J=0;J<200000;J++){ i64 Kr=(1ll<<19);
        i64 a=((MW-J)*C+Kr)>>W;                       // floor(((2^W-J)C+Kr)/2^W)
        i64 b=C-((J*C-Kr+MW-1)>>W);                   // C - ceil((JC-Kr)/2^W)
        if(a!=b)bad++; }
      printf("    exact products, W=26, C=5321677, J=0..2e5: identity fails in %ld cases\n",bad); }
    { long nz=0,tot=0; std::vector<i64> vals;
      for(int w=23;w<=32;w++) for(i64 J=1;J<=40000;J++){ i64 M=1ll<<w;
        i64 d=arr(M-J,S23[6])-(arr(M,S23[6])-arr(J,S23[6])); tot++; if(d)nz++;
        if(vals.size()<20000)vals.push_back(d); }
      std::sort(vals.begin(),vals.end()); vals.erase(std::unique(vals.begin(),vals.end()),vals.end());
      printf("    Booth array, arr(2^w-J,S) - [arr(2^w,S)-arr(J,S)] nonzero in %ld of %ld ; %zu distinct values",nz,tot,vals.size());
      printf(" min %lld max %lld\n\n",(long long)vals.front(),(long long)vals.back()); }

    puts("=== (I) full model, claim's final multiply (RN21/u=28+k, feedback w=24 2sC sine-recoded) ===");
    puts("    chain sweep: s1 in 18..27, K1,K2 in {0,2^19}, both operand orders, latch shift 20");
    { int best=1<<30,bg=0,bs1=0,bk1=0,bk2=0,bro=0; int bestgc=1<<30,gs1=0,gk1=0,gk2=0,gro=0;
      for(int s1=18;s1<=27;s1++) for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int ro=0;ro<2;ro++){
        i64 K1=k1?K:0,K2=k2?K:0; int viol=0,guard=0;
        for(int node=0;node<=31;node++){int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
          int isg=(n==0||n==1||n==2||n==16);
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 I1=((ro?arr(KC,dx):arr(dx,KC))+K1)>>s1; i64 J=(arr(I1,dx)+K2)>>20;
            i64 T2=S21[n]?((arr(S21[n],MM)-arr(S21[n],MM-J)+K)>>U21[n]):0;
            i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
            if(V<wl||V>wh){viol++; if(isg)guard++;} } }
        if(viol<best){best=viol;bg=guard;bs1=s1;bk1=k1;bk2=k2;bro=ro;}
        if(guard==0&&viol<bestgc){bestgc=viol;gs1=s1;gk1=k1;gk2=k2;gro=ro;} }
      printf("    best overall     : %d violations (guard %d) at s1=%d K1=%s K2=%s %s\n",best,bg,bs1,
             bk1?"2^19":"0",bk2?"2^19":"0",bro?"arr(KC,dx)":"arr(dx,KC)");
      printf("    best GUARD-CLEAN : %s\n", bestgc==(1<<30)?"NONE (no guard-clean configuration at all)":"");
      if(bestgc!=(1<<30)) printf("                       %d violations at s1=%d K1=%s K2=%s %s\n",bestgc,gs1,
             gk1?"2^19":"0",gk2?"2^19":"0",gro?"arr(KC,dx)":"arr(dx,KC)");
      puts("    (published baseline for comparison: 147 violations, guard 0)"); }
    puts("");
    puts("=== (J) the trap the claim self-reports: trunc24 field, w=31, two's complement ===");
    { for(int w=30;w<=32;w++){ int feas=0; i64 j17l=1,j17h=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000ll;
          i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
          for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!STR24[n])continue;
            i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
            i64 t2lo=base-wh,t2hi=base-wl; i64 M=1ll<<w;
            auto g=[&](i64 J){ return (arr(STR24[n],M)-arr(STR24[n],M-J)+K)>>UTR24[n]; };
            i64 a1=0,b1=12000001ll; while(a1<b1){i64 m=(a1+b1)/2; if(g(m)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000ll; while(a2<b2){i64 m=(a2+b2+1)/2; if(g(m)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
          if(gl<=gh)feas++; if(t==17){j17l=gl;j17h=gh;} }
        printf("    w=%d : feasible %4d/1024 , t=17 window [%lld,%lld]%s\n",w,feas,
               (long long)j17l,(long long)j17h,j17l>j17h?" INFEAS":"  <== +-17 'works' but the range collapses"); } }
    return 0; }
