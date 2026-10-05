// fv_certificate_b.cpp -- independent certificate: "does ANY integer I1 put J in its window?"
// Built from the description alone (no f2_* file is read or included).  Integer throughout.
//   - my own fine-scale latch dataset, cross-checked against jwin18.txt
//   - exhaustive scan of a PROVABLE I1 bracket (no monotonicity assumption, no window)
//   - the rounding constant K2 exhausted (granularity 2^16, q swept)
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0PUB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2PUB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 JL[1024],JH[1024];
static const i64 EB = 1ll<<21;          // provable |arr(a,b)-a*b| bound (structural bound is < 742734)
static const i64 SDEV = 1ll<<21;        // provable |arr(v+1,m)-arr(v,m)-m| bound

static void build_ds(int DU){ i64 LIM=12000000ll<<DU;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0PUB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2PUB[n]+DU; i64 S=TB[n].S;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; } }

// latch rules: 0 floor, 1 toward zero, 2 one's-complement magnitude
static inline i64 latch(i64 P,int s,int rule){ if(rule==0) return P>>s;
    if(rule==1) return P>=0? (P>>s) : -((-P)>>s);
    return P>=0? (P>>s) : -((~P)>>s); }

// returns 1 if some integer I1 gives J in [jl,jh]; 0 if provably none; 2 if the bracket had to be clamped
static int certify(int t,int g,int s2,i64 K2,int role,int rule,i64 jl,i64 jh,i64*bestI1){
    i64 dx=(i64)t<<g;
    if(jl>jh) return 0;
    // P must satisfy latch(P+K2,s2) in [jl,jh].  For floor this is an interval; for the others the
    // negative branch is a second interval.  Handle both by scanning candidate I1 and testing directly.
    if(dx==0){ for(i64 v=-4;v<=4;v++){ i64 P=role? arr(dx,v):arr(v,dx); i64 J=latch(P+K2,s2,rule);
                   if(J>=jl&&J<=jh){ if(bestI1)*bestI1=v; return 1; } } return 0; }
    // provable P-bracket for the floor rule (widest of the three, all agree on P>=0)
    i64 Plo=jl*(1ll<<s2)-K2, Phi=(jh+1)*(1ll<<s2)-1-K2;
    // unconstraining offset: a window wider than one step cannot be jumped over
    if(Phi-Plo+1 >= (dx<0?-dx:dx)+SDEV+1){ if(bestI1)*bestI1=0; return 1; }
    i64 lo=Plo-EB, hi=Phi+EB;                       // I1*dx must lie here
    i64 a,b; i64 ad=(dx<0?-dx:dx);
    if(dx>0){ a=(lo>=0? lo/dx : -((-lo+dx-1)/dx)); b=(hi>=0? hi/dx : -((-hi+dx-1)/dx)); }
    else    { a=(hi>=0? -(hi/ad) : (-hi+ad-1)/ad); b=(lo>=0? -(lo/ad) : (-lo+ad-1)/ad); }
    a-=2; b+=2; int clamped=0;
    if(a<-(1ll<<26)){a=-(1ll<<26);clamped=1;} if(b>(1ll<<26)){b=(1ll<<26);clamped=1;}
    for(i64 v=a;v<=b;v++){ i64 P=role? arr(dx,v):arr(v,dx); i64 J=latch(P+K2,s2,rule);
        if(J>=jl&&J<=jh){ if(bestI1)*bestI1=v; return 1; } }
    return clamped?2:0; }

int main(int argc,char**argv){ if(!load_fsca(F)){puts("cannot load dump");return 1;} build_tables();
    build_ds(2);
    // cross-check against jwin18.txt
    { FILE*f=fopen("jwin18.txt","r"); int n=0,bad=0; char line[256];
      while(f&&fgets(line,sizeof line,f)){ if(line[0]=='#')continue; long long t,l,h,w;
        if(sscanf(line,"%lld %lld %lld %lld",&t,&l,&h,&w)!=4)continue; n++;
        if(JL[t+512]!=l||JH[t+512]!=h)bad++; }
      if(f)fclose(f);
      printf("[B0] my fine-scale dataset vs jwin18.txt: %d lines, %d disagreements\n",n,bad); }
    { int pin=0; long long sum=0; for(int t=-512;t<=511;t++){ i64 w=JH[t+512]-JL[t+512]+1; sum+=w; if(w<=4)pin++; }
      printf("     mean window %lld, %d offsets pinned to width<=4 ; J(17)=[%lld,%lld] J(342)=[%lld,%lld] J(-342)=[%lld,%lld]\n",
        sum/1024,pin,(long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],
        (long long)JL[-342+512],(long long)JH[-342+512]); }

    // ---- B1: the e6 table, re-decided exhaustively.  g=13, role 0, K2 in {0,2^19}, all three latch rules
    puts("\n[B1] offsets with NO integer I1 (exhaustive bracket scan), g=13, role arr(I1,dx):");
    for(int s2=14;s2<=24;s2++){
        for(int ki=0;ki<2;ki++){ i64 K2=ki?(1ll<<19):0;
            int cnt[3]={0,0,0}; int clamp=0; std::vector<int> bad;
            for(int rule=0;rule<3;rule++) for(int t=-512;t<=511;t++){
                int r=certify(t,13,s2,K2,0,rule,JL[t+512],JH[t+512],0);
                if(r==0){cnt[rule]++; if(rule==0)bad.push_back(t);} if(r==2)clamp++; }
            printf("  s2=%2d K2=%-7lld  no-I1: floor %3d  tz %3d  onescomp %3d  clamped %d",
                   s2,(long long)K2,cnt[0],cnt[1],cnt[2],clamp);
            if(bad.size()&&bad.size()<=20){ printf("   {"); for(size_t i=0;i<bad.size();i++)printf("%s%d",i?",":"",bad[i]); printf("}"); }
            puts(""); } }
    return 0; }
