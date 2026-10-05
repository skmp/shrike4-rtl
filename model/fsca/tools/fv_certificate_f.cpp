// fv_certificate_f.cpp -- the final-multiply variant cross, with the latch dataset REGENERATED per variant.
// My own implementation of the 9 sine-field rules x 2 operand roles x 3 rounding constants x 2 sign
// encodings = 108 variants (one stated rule per variant across ALL nodes; no per-node freedom).
// For each: rebuild the fine-scale dataset, count feasible offsets (= free-J floor 0 iff 1024),
// report the three pinned windows, and ask whether any K2 certifies all 1024 offsets at g=13.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0PUB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static const i64 EB=1ll<<21,SDEV=1ll<<21;
// ---- the nine sine-field rules, each ONE rule across all 17 nodes
static void field(int sv,int n,i64&S,int&u){ int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
    auto rn=[&](int sh){ return rn_scale(s,sh); }; auto tr=[&](int sh){ return (i64)((s<<sh)/QONE); };
    switch(sv){
      case 0: S=std::min(rn(23+k),(i64)((1ll<<23)-1)); u=30+k; break;   // RN23 normalised (published)
      case 1: S=std::min(tr(23+k),(i64)((1ll<<23)-1)); u=30+k; break;   // trunc23 normalised
      case 2: S=std::min(rn(24+k),(i64)((1ll<<24)-1)); u=31+k; break;
      case 3: S=std::min(tr(24+k),(i64)((1ll<<24)-1)); u=31+k; break;
      case 4: S=std::min(rn(25+k),(i64)((1ll<<25)-1)); u=32+k; break;
      case 5: S=rn(24); u=31; break;                                     // fixed point, no octave
      case 6: S=rn(23); u=30; break;
      case 7: S=std::min(rn(26),(i64)((1ll<<26)-1)); u=33; break;        // the 26-bit C0 field
      default:S=std::min(rn(22+k),(i64)((1ll<<22)-1)); u=29+k; break; }
    if(n==0) S=0; }
static inline i64 T2_of(int r3,int k3,int sg,i64 J,i64 S,int uu){ if(!S) return 0;
    i64 K3 = k3==0?0 : (k3==1?(1ll<<19):(1ll<<(uu-1)));
    i64 P = r3? arr(S,J) : arr(J,S);
    if(sg==0) return (P+K3)>>uu;
    i64 Q=-(P+K3); return -(-((~Q)>>uu)); }
struct DS { i64 JL[1024],JH[1024]; int feas; };
static void build(int sv,int r3,int k3,int sg,int du,DS&D){
    i64 S[17]; int u[17]; for(int n=0;n<=16;n++) field(sv,n,S[n],u[n]);
    i64 LIM=12000000ll<<du; D.feas=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!S[n])continue; i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0PUB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int uu=u[n]+du;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(T2_of(r3,k3,sg,m,S[n],uu)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(T2_of(r3,k3,sg,m,S[n],uu)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        D.JL[t+512]=gl; D.JH[t+512]=gh; if(gl<=gh) D.feas++; } }
static int certify(int t,int g,int s2,i64 K2,int role,i64 jl,i64 jh){
    i64 dx=(i64)t<<g; if(jl>jh) return 0;
    if(dx==0){ for(i64 v=-4;v<=4;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=(P+K2)>>s2; if(J>=jl&&J<=jh) return 1; } return 0; }
    i64 Plo=jl*(1ll<<s2)-K2, Phi=(jh+1)*(1ll<<s2)-1-K2, ad=dx<0?-dx:dx;
    if(Phi-Plo+1>=ad+SDEV+1) return 1;
    i64 lo=Plo-EB,hi=Phi+EB,a,b;
    if(dx>0){ a=(lo>=0?lo/dx:-((-lo+dx-1)/dx)); b=(hi>=0?hi/dx:-((-hi+dx-1)/dx)); }
    else    { a=(hi>=0?-(hi/ad):(-hi+ad-1)/ad); b=(lo>=0?-(lo/ad):(-lo+ad-1)/ad); }
    a-=2;b+=2; if(a<-(1ll<<27))a=-(1ll<<27); if(b>(1ll<<27))b=(1ll<<27);
    for(i64 v=a;v<=b;v++){ i64 P=role?arr(dx,v):arr(v,dx); if(((P+K2)>>s2)>=jl&&((P+K2)>>s2)<=jh) return 1; }
    return 0; }
int main(){ if(!load_fsca(F)){puts("cannot load dump");return 1;} build_tables();
    // monotonicity of the final product in J, per variant field (validates the binary search)
    { int bad=0; for(int sv=0;sv<9;sv++) for(int n=1;n<=16;n++){ i64 S;int u; field(sv,n,S,u); if(!S)continue;
        i64 p0=arr(0,S),p1=arr(S,0);
        for(i64 J=1;J<=200000;J++){ i64 a=arr(J,S); if(a<p0)bad++; p0=a; i64 b=arr(S,J); if(b<p1)bad++; p1=b; } }
      printf("[F0] monotonicity of arr(J,S) and arr(S,J) over J in [0,2e5], 9 fields x 16 nodes: %d backward steps\n",bad); }
    struct R{int sv,r3,k3,sg,feas; i64 l17,h17,l342,h342,lm,hm;};
    std::vector<R> res(108);
    #pragma omp parallel for schedule(dynamic)
    for(int idx=0;idx<108;idx++){ int sv=idx/12, r3=(idx/6)%2, k3=(idx/2)%3, sg=idx%2; DS D; build(sv,r3,k3,sg,2,D);
        res[idx]={sv,r3,k3,sg,D.feas,D.JL[17+512],D.JH[17+512],D.JL[342+512],D.JH[342+512],D.JL[-342+512],D.JH[-342+512]}; }
    int full=0,k3fail=0,minf=1<<30,maxf=0; int j17pin=0; int sgdiff=0;
    for(auto&r:res){ if(r.feas==1024){ full++; if(r.l17==45617&&r.h17==45617)j17pin++; }
                     else { if(r.k3==2)k3fail++; minf=std::min(minf,r.feas); maxf=std::max(maxf,r.feas); } }
    for(int idx=0;idx<108;idx+=2){ if(res[idx].feas!=res[idx+1].feas||res[idx].l342!=res[idx+1].l342) sgdiff++; }
    printf("[F1] 108 variants, dataset REGENERATED for each: %d have all 1024 offsets feasible (free-J floor 0)\n",full);
    printf("     of the %d that do not, %d are k3=2 (K3=2^(u-1)) ; their feasible counts span %d..%d\n",108-full,k3fail,minf,maxf);
    printf("     sign encoding sg changes the result in %d of 54 pairs\n",sgdiff);
    printf("[F2] J(17) window = [45617,45617] in %d of the %d fully feasible variants\n",j17pin,full);
    { std::vector<std::pair<i64,i64>> w342,wm; for(auto&r:res) if(r.feas==1024){ w342.push_back({r.l342,r.h342}); wm.push_back({r.lm,r.hm}); }
      std::sort(w342.begin(),w342.end()); w342.erase(std::unique(w342.begin(),w342.end()),w342.end());
      std::sort(wm.begin(),wm.end()); wm.erase(std::unique(wm.begin(),wm.end()),wm.end());
      printf("     distinct J(342) windows:"); for(auto&p:w342) printf(" [%lld,%lld]",(long long)p.first,(long long)p.second);
      printf("\n     distinct J(-342) windows:"); for(auto&p:wm) printf(" [%lld,%lld]",(long long)p.first,(long long)p.second); puts(""); }
    // per-variant: does some K2 certify all 1024 at g=13,s2=19?  and the minimum s2-g at which that happens
    { int all19=0,pin3=0; int cnt_min[16]={0};
      #pragma omp parallel for schedule(dynamic)
      for(int idx=0;idx<108;idx++){ if(res[idx].feas!=1024) continue; int sv=idx/12,r3=(idx/6)%2,k3=(idx/2)%3,sg=idx%2;
        DS D; build(sv,r3,k3,sg,2,D); int ok19=0,okpin=0,mins=99;
        for(int s2=16;s2<=25&&mins==99;s2++){ int NJ=(s2>16)?(1<<(s2-16)):1;
          for(int q=-4;q<=8&&mins==99;q++) for(int j=0;j<NJ;j++){ i64 K2=(i64)q*(1ll<<s2)+(i64)j*65536; int f=0;
            for(int t=-512;t<=511;t++) f+=certify(t,13,s2,K2,0,D.JL[t+512],D.JH[t+512]);
            if(f==1024){ mins=s2-13; if(s2==19)ok19=1; break; } } }
        if(mins!=99){ // also the 3-pinned question at g=13,s2=19
          for(int q=-4;q<=8&&!okpin;q++) for(int j=0;j<8&&!okpin;j++){ i64 K2=(i64)q*(1ll<<19)+(i64)j*65536;
            if(certify(17,13,19,K2,0,D.JL[17+512],D.JH[17+512])&&certify(342,13,19,K2,0,D.JL[342+512],D.JH[342+512])
               &&certify(-342,13,19,K2,0,D.JL[-342+512],D.JH[-342+512])) okpin=1; } }
        // re-test s2=19 explicitly (mins may be smaller)
        if(!ok19){ for(int q=-4;q<=8&&!ok19;q++) for(int j=0;j<8;j++){ i64 K2=(i64)q*(1ll<<19)+(i64)j*65536; int f=0;
            for(int t=-512;t<=511;t++) f+=certify(t,13,19,K2,0,D.JL[t+512],D.JH[t+512]); if(f==1024){ok19=1;break;} } }
        #pragma omp critical
        { all19+=ok19; pin3+=okpin; if(mins!=99&&mins>=0&&mins<16)cnt_min[mins]++; } }
      printf("[F3] of the %d fully feasible variants: %d admit a K2 certifying ALL 1024 offsets at g=13,s2=19\n",full,all19);
      printf("     %d admit a K2 making t=17, 342 and -342 simultaneously admissible at g=13,s2=19\n",pin3);
      printf("     minimum s2-g at which some K2 certifies all 1024:"); for(int i=0;i<16;i++) if(cnt_min[i]) printf(" [s2-g=%d: %d variants]",i,cnt_min[i]); puts(""); }
    return 0; }
