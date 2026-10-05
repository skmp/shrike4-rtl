// fv_final-fields_3_sweep.cpp -- INDEPENDENT reconstruction of the 92400-variant final-square-product sweep.
// Written from the prose description only; does not include or read any f1_* file.
//   T2 = ALIGN( ARR(opA,opB) + K3 , u_n ),  u_n = a + b*k  (ONE rule across all nodes, a = W + d)
//   W in [20,26] ; rounding {RNe, trunc, half-up, ceil} ; 5 field sources ; b in {0,1,2} ; d in [2,12] ;
//   both operand roles ; K3 in {0, 2^19, 2^(u-1), 2^16, 2^19+2^16} ; sign {positive, negative one's-comp}
//   = 7*4*5*3*11*2*5*2 = 92400.
// Coefficient tables C1f/e1/c3/C0 are FROZEN throughout (no per-node refit anywhere).
// Scoring is at the PUBLISHED chain: I1=(arr(dx,KC)+2^19)>>23 ; J=(arr(I1,dx)+2^19)>>20.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 WLO[65536],WHI[65536];
static i64 JT[1024];                 // the published chain's latch, per t  (chain is fixed => precompute)
static i64 BASE[32][1024];           // C0 + s*T1 - s*T3, per node, per t   (frozen coefficients)
// ---------- rounding modes ----------
static inline i64 rmode(i128 num, i128 den, int mode){   // round num/den, num>=0, den>0
    i128 q=num/den, r=num%den;
    switch(mode){ case 0: if(r*2>den || (r*2==den && (q&1))) ++q; break;      // RN even
                  case 1: break;                                              // truncate
                  case 2: if(r*2>=den) ++q; break;                            // round half up
                  default: if(r>0) ++q; }                                     // ceil
    return (i64)q; }
// ---------- the five field sources ----------
// src 0 norm-sine-ROM : round(sin*2^(W+k))            (normalised, octave in the exponent)
// src 1 fixed-sine-ROM: round(sin*2^W)                (no octave anywhere in the field)
// src 2 fixed-ROM + normalising shifter: round(sin*2^W) << k
// src 3 C0 table fixed: round(C0_n / 2^(26-W))
// src 4 C0 table normalised: round(C0_n*2^k / 2^(26-W))
static void field(int src,int W,int rnd,int n,i64&S){
    int k = TB[n].u2 - 30; i128 s = sin_(PI_()*n/32); i64 cap=(1ll<<W)-1;
    if(n==0){ S=0; return; }
    switch(src){
      case 0: S=std::min(rmode(s<<(W+k), QONE, rnd), cap); break;
      case 1: S=std::min(rmode(s<<W,     QONE, rnd), cap); break;
      case 2: S=std::min(rmode(s<<W,     QONE, rnd), cap) << k; break;
      case 3: S=std::min(rmode((i128)C0TAB[n], (i128)1<<(26-W), rnd), cap); break;
      default:S=std::min(rmode((i128)C0TAB[n]<<k, (i128)1<<(26-W), rnd), cap); break; }
}
struct V { int W,rnd,src,b,d,role,kc,sg; };
static inline i64 T2of(const V&v,i64 J,i64 S,int u){
    if(!S) return 0; if(u<1||u>60) return -(1ll<<50);
    i64 K3; switch(v.kc){case 0:K3=0;break; case 1:K3=1ll<<19;break; case 2:K3=1ll<<(u-1);break;
                          case 3:K3=1ll<<16;break; default:K3=(1ll<<19)+(1ll<<16);}
    i64 P = v.role? arr(S,J) : arr(J,S);
    if(v.sg==0) return (P+K3)>>u;
    i64 Q=-(P+K3); return -(-((~Q)>>u)); }          //  == (P+K3-1)>>u
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){WLO[a]=w.lo;WHI[a]=w.hi;} else {WLO[a]=-w.hi;WHI[a]=-w.lo;} }
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23; JT[t+512]=(arr(I1,dx)+K)>>20; }
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            BASE[node][t+512]=C0TAB[n]+s*T1-s*T3; } }
    // enumerate
    std::vector<V> VS;
    for(int W=20;W<=26;W++) for(int rnd=0;rnd<4;rnd++) for(int src=0;src<5;src++)
      for(int b=0;b<3;b++) for(int d=2;d<=12;d++) for(int role=0;role<2;role++)
        for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++) VS.push_back({W,rnd,src,b,d,role,kc,sg});
    printf("variants enumerated: %zu\n",VS.size());
    int NV=VS.size();
    std::vector<int> GUARD(NV,-1), FULL(NV,-1);
    const int gn[6]={0,1,2,16,30,31};
    #pragma omp parallel for schedule(dynamic,64)
    for(int i=0;i<NV;i++){ const V&v=VS[i]; i64 S[17]; int u[17];
        for(int n=0;n<=16;n++){ field(v.src,v.W,v.rnd,n,S[n]); u[n]=v.W+v.d+v.b*(TB[n].u2-30); }
        int g=0;
        for(int gi=0;gi<6 && g==0;gi++){ int node=gn[gi],flip=node>=17,n=flip?32-node:node;
            if(!S[n]) continue;
            for(int t=0;t<1024;t++){ i64 V2=BASE[node][t]-T2of(v,JT[t],S[n],u[n]);
                int a=(node*1024+(t-512))&0xffff; if(V2<WLO[a]||V2>WHI[a]){g++;break;} } }
        GUARD[i]=g;
        if(g==0){ int f=0;
            for(int node=0;node<=31;node++){ int flip=node>=17,nn=flip?32-node:node;
                for(int t=0;t<1024;t++){ i64 V2=BASE[node][t]-T2of(v,JT[t],S[nn],u[nn]);
                    int a=(node*1024+(t-512))&0xffff; if(V2<WLO[a]||V2>WHI[a])f++; } }
            FULL[i]=f; } }
    const char* RN[4]={"RNe","trunc","half-up","ceil"};
    const char* SR[5]={"norm-sine-ROM","fixed-sine-ROM","fixed-ROM+shifter","C0-table-fixed","C0-table-norm"};
    const char* KCN[5]={"0","2^19","2^(u-1)","2^16","2^19+2^16"};
    int nclean=0,best=1<<30,bi=-1; int cW[32]={0},cb[4]={0},cd[40]={0},csrc[5]={0},ckc[5]={0},crole[2]={0},csg[2]={0},crnd[4]={0};
    FILE*of=fopen("fv_final-fields_guardclean.txt","w");
    fprintf(of,"# W rnd src b a role K3 sign fullmodel   (guard nodes 0,1,2,16,30,31 exact for all 1024 t)\n");
    for(int i=0;i<NV;i++) if(GUARD[i]==0){ const V&v=VS[i]; nclean++;
        cW[v.W]++; cb[v.b]++; cd[v.W+v.d]++; csrc[v.src]++; ckc[v.kc]++; crole[v.role]++; csg[v.sg]++; crnd[v.rnd]++;
        fprintf(of,"W=%d %-8s %-18s b=%d a=%d %-10s K3=%-10s %-8s full=%d\n",v.W,RN[v.rnd],SR[v.src],v.b,
            v.W+v.d, v.role?"arr(S,J)":"arr(J,S)", KCN[v.kc], v.sg?"neg-1c":"pos", FULL[i]);
        if(FULL[i]<best){best=FULL[i];bi=i;} }
    fclose(of);
    printf("\nGUARD-CLEAN variants: %d of %d\n",nclean,NV);
    printf("  widths W     :"); for(int w=20;w<=26;w++) if(cW[w])printf(" %d:%d",w,cW[w]); printf("\n");
    printf("  b (octave)   :"); for(int b=0;b<3;b++) if(cb[b])printf(" %d:%d",b,cb[b]); printf("\n");
    printf("  a = W+d      :"); for(int a=0;a<40;a++) if(cd[a])printf(" %d:%d",a,cd[a]); printf("\n");
    printf("  field source :"); for(int s=0;s<5;s++) if(csrc[s])printf(" %s:%d",SR[s],csrc[s]); printf("\n");
    printf("  K3           :"); for(int c=0;c<5;c++) if(ckc[c])printf(" %s:%d",KCN[c],ckc[c]); printf("\n");
    printf("  operand role :"); for(int r=0;r<2;r++) if(crole[r])printf(" %s:%d",r?"arr(S,J)":"arr(J,S)",crole[r]); printf("\n");
    printf("  sign         :"); for(int s=0;s<2;s++) if(csg[s])printf(" %s:%d",s?"neg-1c":"pos",csg[s]); printf("\n");
    printf("  field round  :"); for(int r=0;r<4;r++) if(crnd[r])printf(" %s:%d",RN[r],crnd[r]); printf("\n");
    if(bi>=0){ const V&v=VS[bi];
        printf("\nBEST GUARD-CLEAN full model = %d   W=%d %s %s b=%d a=%d %s K3=%s %s\n",best,v.W,RN[v.rnd],
            SR[v.src],v.b,v.W+v.d,v.role?"arr(S,J)":"arr(J,S)",KCN[v.kc],v.sg?"neg-1c":"pos");
        int hist[400]={0}; for(int i=0;i<NV;i++) if(GUARD[i]==0 && FULL[i]<400) hist[FULL[i]]++;
        printf("score distribution of the guard-clean set:"); for(int s=0;s<400;s++) if(hist[s])printf(" %d(x%d)",s,hist[s]); printf("\n"); }
    // and the best over ALL variants ignoring guards, for the control
    std::vector<int> FA(NV,1<<30);
    #pragma omp parallel for schedule(dynamic,64)
    for(int i=0;i<NV;i++){ const V&v=VS[i]; i64 S[17]; int u[17];
        for(int n=0;n<=16;n++){ field(v.src,v.W,v.rnd,n,S[n]); u[n]=v.W+v.d+v.b*(TB[n].u2-30); }
        int f=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,nn=flip?32-node:node;
            for(int t=0;t<1024;t++){ i64 V2=BASE[node][t]-T2of(v,JT[t],S[nn],u[nn]);
                int a=(node*1024+(t-512))&0xffff; if(V2<WLO[a]||V2>WHI[a])f++; }
            if(f>=1<<20) break; }
        FA[i]=f; }
    int ba=1<<30,bai=-1; for(int i=0;i<NV;i++) if(FA[i]<ba){ba=FA[i];bai=i;}
    { const V&v=VS[bai];
      printf("\nBEST OVER ALL 92400 at the published chain, guards IGNORED = %d  (guard violations %d)\n"
             "   W=%d %s %s b=%d a=%d %s K3=%s %s\n",ba,GUARD[bai],v.W,RN[v.rnd],SR[v.src],v.b,v.W+v.d,
             v.role?"arr(S,J)":"arr(J,S)",KCN[v.kc],v.sg?"neg-1c":"pos"); }
    return 0; }
