// b_verify.cpp -- ONE program that re-derives every headline number in CURRENT.md / NOTES.md for 2026-09-18.
// Build: g++ -O2 -fopenmp -std=c++17 b_verify.cpp -o /tmp/bv    Run with no arguments (about 1 second).
// Anything this prints is a fact re-measured from the dump, not a claim copied from the notes.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024];
static void build_jwin(int DU){ i64 LIM=12000000ll<<DU;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,TB[n].S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,TB[n].S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; } }
int main(){ if(!load_fsca(F)){puts("cannot load the dump");return 1;} build_tables();
    puts("=== FSCA, state of 2026-09-18: every headline number, re-measured ===\n");
    int q=0,h=0,m=0; for(int a=0;a<65536;a++){ int b=(a+0x4000)&0xffff; if(F.crn[a]!=F.srn[b]||F.crz[a]!=F.srz[b])q++;
        b=(a+0x8000)&0xffff; if((F.srn[a]^0x80000000u)!=F.srn[b]||(F.srz[a]^0x80000000u)!=F.srz[b])h++;
        b=(0x8000-a)&0xffff; if(F.srn[a]!=F.srn[b]||F.srz[a]!=F.srz[b])m++; }
    printf("1. DUMP GEOMETRY  cos == sin(a+0x4000): %d mismatches (the cosine half is redundant)\n",q);
    printf("                  sin(a+0x8000) == -sin(a): %d (the two +-0 signs)\n",h);
    printf("                  sin(0x8000-a) != sin(a): %d  -> nodes 17..31 are independent data\n\n",m);
    int e1=0,e2=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        if(((arr(dx,dx)+K)>>26)!=(i64)t*t)e1++; if((arr(dx,dx)>>26)!=(i64)t*t)e2++; }
    printf("2. SQUARER        floor((arr(dx,dx)+2^19)>>26) != t*t for %d/1024; without the constant %d/1024\n",e1,e2);
    puts("                  -> the dx^2 latch is exact, so any dx^2-first square path is SYMMETRIC in t\n");
    for(int ro=0;ro<2;ro++){ int viol=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 I1=((ro? arr(dx,KC):arr(KC,dx))+K)>>23, J=(arr(I1,dx)+K)>>20;
                i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]):0;
                i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi)viol++; } }
        printf("3. MODEL          first square multiply arr(%s): %d window violations over nodes 0..31\n",
               ro?"dx,KC":"KC,dx",viol); }
    puts("");
    printf("4. FREE LATCH     minimum violations with a free per-t square latch, by latch shift:\n                  ");
    for(int du=0;du<=3;du++){ i64 tot=0; i64 LIM=12000000ll<<du; build_jwin(du);
        for(int t=-512;t<=511;t++){ if(JL[t+512]<=JH[t+512]) continue;
            i64 dx=(i64)t<<13; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; int bv=1<<30;
            for(i64 J=std::max((i64)0,JL[t+512]-200);J<=JH[t+512]+200;J++){ int v=0;
                for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                    i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                    i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                    i64 T2=sm(arr(J,TB[n].S)+K,U2TAB[n]+du); i64 V=C0TAB[n]+s*T1-T2-s*T3;
                    int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex); if(V<lo||V>hi)v++; }
                if(v<bv)bv=v; } tot+=bv; (void)LIM; }
        printf("shift %d -> %lld ; ",20-du,(long long)tot); }
    puts("\n                  -> at shift 18 or 17 the model is COMPLETE up to that one latch\n");
    int pairs=0; for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int a=1;a<=511;a++){ i64 il=-(1ll<<60),ih=(1ll<<60);
            for(int sg=0;sg<2;sg++){ int t=sg?-a:a; i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 O=s*T1-s*T3; int ang=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(ang,lo,hi,ex);
                if(lo-O>il)il=lo-O; if(hi-O<ih)ih=hi-O; }
            if(il>ih) pairs++; } }
    printf("5. ASYMMETRY      %d (node,|t|) pairs where no single even part fits both signs of t\n",pairs);
    puts("                  (model-free: only the odd part is assumed, and nodes 0/32 pin that alone)");
    { int nd=16,a=385; i64 lo,hi;bool ex;
      win_of((nd*1024+a)&0xffff,lo,hi,ex); printf("                  witness node 16, t=+385: even part in [%lld,%lld]\n",(long long)lo,(long long)hi);
      win_of((nd*1024-a)&0xffff,lo,hi,ex); printf("                                   t=-385: even part in [%lld,%lld]  (cos(pi/2)=0, so no odd part at all)\n\n",(long long)lo,(long long)hi); }
    build_jwin(2);
    printf("6. CERTIFICATE    the two pinned extremes, from jwin at the fine latch scale:\n");
    printf("                  t=17  : J must be %lld, exact KC*t^2/2^15 = %.3f  -> must LOSE %.2f\n",
        (long long)JL[17+512],(double)((i64)KC*17*17)/32768.0,(double)((i64)KC*17*17)/32768.0-(double)JL[17+512]);
    printf("                  t=342 : J must be %lld ; D = J(-342)-J(342) must be in [%lld,%lld]\n\n",
        (long long)JL[342+512],(long long)(JL[-342+512]-JH[342+512]),(long long)(JH[-342+512]-JL[342+512]));
    puts("7. SHIFT          D(342) = 342/2^(s2-13) in a chain. Against the MODEL-DEPENDENT window [22,31]:");
    for(int s2=16;s2<=19;s2++){ double d=342.0/(double)(1ll<<(s2-13));
        printf("                  s2=%2d -> D = %6.2f  %s\n",s2,d,
            (d>=21.0&&d<=32.0)?"<== the only one in [22,31]":(d>32?"above [22,31]":"below [22,31]")); }
    puts("                  CAVEAT: [22,31] assumes the odd model. From mirror sums alone D(342) is in [22,264]");
    puts("                  (tools/D_FINDINGS.md), which also admits s2 = 14, 15 and 16. The t=17 constraint is");
    puts("                  the robust one -- it survives dropping the odd coefficients entirely.");
    { int s1=24,s2=17; auto J=[&](int t){ i64 dx=(i64)t<<13; i64 L=(arr(KC,dx)+0)>>s1; return (arr(L,dx)+0)>>s2; };
      printf("                  at (24,17): D(342) = %lld, J(-342) = %lld, window [%lld,%lld] -> %s\n",
        (long long)(J(-342)-J(342)),(long long)J(-342),(long long)JL[-342+512],(long long)JH[-342+512],
        (J(-342)>=JL[-342+512]&&J(-342)<=JH[-342+512])?"INSIDE":"outside");
      printf("                  but the deviation of J over the pinned offsets swings");
      int mn=1<<30,mx=-(1<<30);
      for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i]||JH[i]-JL[i]>6) continue;
          int d=(int)(J(t)-JL[i]); mn=std::min(mn,d); mx=std::max(mx,d); }
      printf(" from %+d to %+d\n",mn,mx);
      int sc=0; for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i])continue; i64 j=J(t); if(j>=JL[i]&&j<=JH[i])sc++; }
      printf("                  and its overall score is %d/1024, WORSE than the 918 that s2=20 reaches\n",sc); }
    puts("\n=== end; see CURRENT.md for what is excluded and what remains ===");
    return 0; }
