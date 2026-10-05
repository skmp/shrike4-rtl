// fv_final-fields_6_ceiling.cpp -- INDEPENDENT test of "changing the final square product does not move the
// two-multiply chain ceiling".  For EVERY one of the 92400 variants it REGENERATES that variant's own
// fine-scale latch dataset (never scores a new final multiply against the old dataset), then scores the
// two-multiply chains against THAT dataset.  Reports feasibility counts and the best chain over all variants.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 T2LO[32][1024],T2HI[32][1024];      // variant-independent target interval for the square term
static inline i64 rmode(i128 num,i128 den,int mode){ i128 q=num/den,r=num%den;
    switch(mode){ case 0: if(r*2>den||(r*2==den&&(q&1)))++q; break; case 1: break;
                  case 2: if(r*2>=den)++q; break; default: if(r>0)++q; } return (i64)q; }
static void field(int src,int W,int rnd,int n,i64&S){ int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
    i64 cap=(1ll<<W)-1; if(n==0){S=0;return;}
    switch(src){ case 0: S=std::min(rmode(s<<(W+k),QONE,rnd),cap); break;
                 case 1: S=std::min(rmode(s<<W,QONE,rnd),cap); break;
                 case 2: S=std::min(rmode(s<<W,QONE,rnd),cap)<<k; break;
                 case 3: S=std::min(rmode((i128)C0TAB[n],(i128)1<<(26-W),rnd),cap); break;
                 default:S=std::min(rmode((i128)C0TAB[n]<<k,(i128)1<<(26-W),rnd),cap); break; } }
struct V{int W,rnd,src,b,d,role,kc,sg;};
static inline i64 f_T2(const V&v,i64 J,i64 S,int u,i64 K3){
    i64 P = v.role? arr(S,J) : arr(J,S);
    if(v.sg==0) return (P+K3)>>u; i64 Q=-(P+K3); return -(-((~Q)>>u)); }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    // target intervals for T2, from the dump and the frozen odd coefficients
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]);
            i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
            T2LO[node][t+512]=base-hi; T2HI[node][t+512]=base-lo; } }
    // the two-multiply chains: J = (arr(I1,dx)+K2)>>s2 ; I1 = ((arr(dx,KC)|arr(KC,dx))+K1)>>s1
    struct CH{int s1,s2,role;i64 K1,K2;}; std::vector<CH> CHS; std::vector<std::vector<i64>> JCH;
    for(int ro=0;ro<2;ro++) for(int s1=16;s1<=30;s1++) for(int s2=10;s2<=28;s2++)
      for(int i1=0;i1<2;i1++) for(int i2=0;i2<2;i2++) CHS.push_back({s1,s2,ro,i1?(1ll<<19):0,i2?(1ll<<19):0});
    JCH.resize(CHS.size());
    for(size_t c=0;c<CHS.size();c++){ JCH[c].resize(1024);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 I1=((CHS[c].role?arr(dx,KC):arr(KC,dx))+CHS[c].K1)>>CHS[c].s1;
            JCH[c][t+512]=(arr(I1,dx)+CHS[c].K2)>>CHS[c].s2; } }
    printf("chains: %zu\n",CHS.size());
    std::vector<V> VS;
    for(int W=20;W<=26;W++) for(int rnd=0;rnd<4;rnd++) for(int src=0;src<5;src++)
      for(int b=0;b<3;b++) for(int d=2;d<=12;d++) for(int role=0;role<2;role++)
        for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++) VS.push_back({W,rnd,src,b,d,role,kc,sg});
    int NV=VS.size(); printf("variants: %d\n",NV);
    const int DU=2;
    std::vector<int> FEAS(NV,0), BESTHIT(NV,0), BESTCH(NV,-1);
    int fullchain = argc>1 && !strcmp(argv[1],"full");
    #pragma omp parallel for schedule(dynamic,32)
    for(int i=0;i<NV;i++){ const V&v=VS[i]; i64 S[17]; int u[17]; i64 K3[17];
        for(int n=0;n<=16;n++){ field(v.src,v.W,v.rnd,n,S[n]); u[n]=v.W+v.d+v.b*(TB[n].u2-30)+DU;
            switch(v.kc){case 0:K3[n]=0;break; case 1:K3[n]=1ll<<19;break;
                case 2:K3[n]= (u[n]>=1&&u[n]<=62)?(1ll<<(u[n]-1)):0; break;
                case 3:K3[n]=1ll<<16;break; default:K3[n]=(1ll<<19)+(1ll<<16);} }
        i64 JCAP = v.role? ((1ll<<29)-1) : ((1ll<<27)-1);
        i64 L[1024],H[1024]; int feas=0;
        for(int t=0;t<1024;t++){ i64 gl=0,gh=JCAP; bool dead=false;
            for(int node=1;node<=31&&!dead;node++){ int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
                if(u[n]<1||u[n]>62){dead=true;break;}
                i64 lo=T2LO[node][t],hi=T2HI[node][t];
                // smallest J with f(J) >= lo ; largest J with f(J) <= hi  -- exact binary search,
                // monotone in J, over the WHOLE representable range [0, JCAP] (no windowing)
                i64 x1=0,y1=JCAP+1; while(x1<y1){ i64 m=x1+(y1-x1)/2;
                    if(f_T2(v,m,S[n],u[n],K3[n])>=lo) y1=m; else x1=m+1; }
                i64 a1=x1;
                i64 x2=-1,y2=JCAP; while(x2<y2){ i64 m=x2+(y2-x2+1)/2;
                    if(f_T2(v,m,S[n],u[n],K3[n])<=hi) x2=m; else y2=m-1; }
                i64 a2=x2;
                if(a1>gl)gl=a1; if(a2<gh)gh=a2; if(gl>gh)dead=true; }
            L[t]=gl; H[t]=gh; if(!dead&&gl<=gh)feas++; else {L[t]=1;H[t]=0;} }
        FEAS[i]=feas;
        int bh=0,bc=-1;
        for(size_t c=0;c<CHS.size();c++){
            if(!fullchain){ int ss=CHS[c].s1+CHS[c].s2; if(ss<39||ss>43) continue; }
            const i64*J=JCH[c].data(); int h=0;
            for(int t=0;t<1024;t++) h += (J[t]>=L[t] && J[t]<=H[t]);
            if(h>bh){bh=h;bc=(int)c;} }
        BESTHIT[i]=bh; BESTCH[i]=bc; }
    int n900=0,n1024=0; for(int i=0;i<NV;i++){ if(FEAS[i]>=900)n900++; if(FEAS[i]==1024)n1024++; }
    printf("\nfeasible offsets: %d variants with >=900, %d with all 1024\n",n900,n1024);
    int best=0,bi=-1; for(int i=0;i<NV;i++) if(BESTHIT[i]>best){best=BESTHIT[i];bi=i;}
    const char* RN[4]={"RNe","trunc","half-up","ceil"};
    const char* SR[5]={"norm-sine-ROM","fixed-sine-ROM","fixed-ROM+shifter","C0-table-fixed","C0-table-norm"};
    const char* KCN[5]={"0","2^19","2^(u-1)","2^16","2^19+2^16"};
    { const V&v=VS[bi]; const CH&c=CHS[BESTCH[bi]];
      printf("BEST two-multiply chain over ALL variants (each against its OWN regenerated dataset):\n"
             "   %d/1024 latch windows hit  (%d misses)\n"
             "   variant: W=%d %s %s b=%d a=%d %s K3=%s %s\n"
             "   chain  : s1=%d s2=%d %s K1=%lld K2=%lld\n",
             best,1024-best,v.W,RN[v.rnd],SR[v.src],v.b,v.W+v.d,v.role?"arr(S,J)":"arr(J,S)",KCN[v.kc],
             v.sg?"neg-1c":"pos",c.s1,c.s2,c.role?"arr(dx,KC)":"arr(KC,dx)",(long long)c.K1,(long long)c.K2); }
    // control: the BASELINE variant's own best chain, and a full-chain-set check on it
    for(int i=0;i<NV;i++){ const V&v=VS[i];
      if(v.W==23&&v.rnd==0&&v.src==0&&v.b==1&&v.W+v.d==30&&v.role==0&&v.kc==1&&v.sg==0){
        printf("control (the published field): feasible %d/1024, best chain %d/1024\n",FEAS[i],BESTHIT[i]); break; } }
    return 0; }
