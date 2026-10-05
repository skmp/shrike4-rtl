// fv_final-fields_8_joint.cpp -- the adversarial question the report does not ask:
// "the guards pin the FIELD" was measured with the CHAIN held at the published value, and
// "the guards pin the CHAIN" was measured with the FIELD held at the published value.
// Neither establishes the pair.  Here both are swept TOGETHER: all 92400 final-square-product variants
// x all 2280 two-multiply chains, and every (variant, chain) pair whose guard nodes 0,1,2,16,30,31 are
// exact for all 1024 offsets is counted.  Coefficient tables frozen; one alignment rule per variant.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 T2LO[32][1024],T2HI[32][1024]; static i64 WLO[65536],WHI[65536]; static i64 BASE[32][1024];
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
FILE* PAIRF;
static const char* RNn[4]={"RNe","trunc","half-up","ceil"};
static const char* SRn[5]={"norm-sine-ROM","fixed-sine-ROM","fixed-ROM+shifter","C0-table-fixed","C0-table-norm"};
static const char* KCn[5]={"0","2^19","2^(u-1)","2^16","2^19+2^16"};
static inline i64 f_T2(const V&v,i64 J,i64 S,int u,i64 K3){ i64 P=v.role?arr(S,J):arr(J,S);
    if(v.sg==0) return (P+K3)>>u; i64 Q=-(P+K3); return -(-((~Q)>>u)); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){WLO[a]=w.lo;WHI[a]=w.hi;} else {WLO[a]=-w.hi;WHI[a]=-w.lo;} }
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; BASE[node][t+512]=base; int a=(node*1024+t)&0xffff;
            T2LO[node][t+512]=base-WHI[a]; T2HI[node][t+512]=base-WLO[a]; } }
    struct CH{int s1,s2,role;i64 K1,K2;}; std::vector<CH> CHS; std::vector<std::vector<i64>> JCH;
    for(int ro=0;ro<2;ro++) for(int s1=16;s1<=30;s1++) for(int s2=10;s2<=28;s2++)
      for(int i1=0;i1<2;i1++) for(int i2=0;i2<2;i2++) CHS.push_back({s1,s2,ro,i1?(1ll<<19):0,i2?(1ll<<19):0});
    JCH.resize(CHS.size());
    for(size_t c=0;c<CHS.size();c++){ JCH[c].resize(1024);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 I1=((CHS[c].role?arr(dx,KC):arr(KC,dx))+CHS[c].K1)>>CHS[c].s1;
            JCH[c][t+512]=(arr(I1,dx)+CHS[c].K2)>>CHS[c].s2; } }
    std::vector<V> VS;
    for(int W=20;W<=26;W++) for(int rnd=0;rnd<4;rnd++) for(int src=0;src<5;src++)
      for(int b=0;b<3;b++) for(int d=2;d<=12;d++) for(int role=0;role<2;role++)
        for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++) VS.push_back({W,rnd,src,b,d,role,kc,sg});
    int NV=VS.size(); printf("variants %d x chains %zu\n",NV,CHS.size());
    PAIRF=fopen("fv_final-fields_jointpairs.txt","w");
    std::vector<long long> NPAIR(NV,0); std::vector<int> BESTF(NV,1<<30), BESTC(NV,-1);
    const int gn[5]={1,2,16,30,31};
    #pragma omp parallel for schedule(dynamic,16)
    for(int i=0;i<NV;i++){ const V&v=VS[i]; i64 S[17]; int u[17]; i64 K3[17];
        bool bad=false;
        for(int n=0;n<=16;n++){ field(v.src,v.W,v.rnd,n,S[n]); u[n]=v.W+v.d+v.b*(TB[n].u2-30);
            if(u[n]<1||u[n]>62){bad=true;break;}
            switch(v.kc){case 0:K3[n]=0;break; case 1:K3[n]=1ll<<19;break; case 2:K3[n]=1ll<<(u[n]-1);break;
                         case 3:K3[n]=1ll<<16;break; default:K3[n]=(1ll<<19)+(1ll<<16);} }
        if(bad) continue;
        i64 JCAP = v.role? ((1ll<<29)-1) : ((1ll<<27)-1);
        i64 GL[1024],GH[1024];
        for(int t=0;t<1024;t++){ i64 gl=0,gh=JCAP;
            for(int gi=0;gi<5;gi++){ int node=gn[gi],flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
                i64 lo=T2LO[node][t],hi=T2HI[node][t];
                i64 x1=0,y1=JCAP+1; while(x1<y1){ i64 m=x1+(y1-x1)/2;
                    if(f_T2(v,m,S[n],u[n],K3[n])>=lo) y1=m; else x1=m+1; }
                i64 x2=-1,y2=JCAP; while(x2<y2){ i64 m=x2+(y2-x2+1)/2;
                    if(f_T2(v,m,S[n],u[n],K3[n])<=hi) x2=m; else y2=m-1; }
                if(x1>gl)gl=x1; if(x2<gh)gh=x2; if(gl>gh)break; }
            GL[t]=gl; GH[t]=gh; }
        long long np=0; int bf=1<<30,bc=-1;
        for(size_t c=0;c<CHS.size();c++){ const i64*J=JCH[c].data(); bool ok=true;
            for(int t=0;t<1024;t++) if(J[t]<GL[t]||J[t]>GH[t]){ok=false;break;}
            if(!ok) continue; np++;
            int f=0; for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
                for(int t=0;t<1024;t++){ i64 V2=BASE[node][t]-(S[n]?f_T2(v,J[t],S[n],u[n],K3[n]):0);
                    int a=(node*1024+(t-512))&0xffff; if(V2<WLO[a]||V2>WHI[a])f++; } }
            if(f<bf){bf=f;bc=(int)c;}
            #pragma omp critical
            { extern FILE* PAIRF; fprintf(PAIRF,"%d W=%d %s %s b=%d a=%d %s K3=%s %s | s1=%d s2=%d %s K1=%lld K2=%lld\n",
                f,v.W,RNn[v.rnd],SRn[v.src],v.b,v.W+v.d,v.role?"arr(S,J)":"arr(J,S)",KCn[v.kc],v.sg?"neg-1c":"pos",
                CHS[c].s1,CHS[c].s2,CHS[c].role?"arr(dx,KC)":"arr(KC,dx)",(long long)CHS[c].K1,(long long)CHS[c].K2); } }
        NPAIR[i]=np; BESTF[i]=bf; BESTC[i]=bc; }
    fclose(PAIRF);
    long long tot=0; int nv=0,best=1<<30,bi=-1;
    for(int i=0;i<NV;i++){ tot+=NPAIR[i]; if(NPAIR[i])nv++; if(BESTF[i]<best){best=BESTF[i];bi=i;} }
    printf("\nGUARD-CLEAN (variant, chain) PAIRS: %lld   over %d distinct field variants\n",tot,nv);
    const char** RN=RNn; const char** SR=SRn; const char** KCN=KCn;
    int cW[32]={0},cb[4]={0},ca[64]={0},csrc[5]={0},ckc[5]={0},cs1[40]={0},cs2[40]={0},cK2[2]={0};
    for(int i=0;i<NV;i++) if(NPAIR[i]){ const V&v=VS[i]; cW[v.W]++; cb[v.b]++; ca[v.W+v.d]++; csrc[v.src]++; ckc[v.kc]++; }
    printf("  field variants that admit >=1 guard-clean chain, by parameter:\n");
    printf("    W :"); for(int w=20;w<=26;w++) if(cW[w])printf(" %d:%d",w,cW[w]); printf("\n");
    printf("    b :"); for(int b=0;b<3;b++) if(cb[b])printf(" %d:%d",b,cb[b]); printf("\n");
    printf("    a :"); for(int a=0;a<64;a++) if(ca[a])printf(" %d:%d",a,ca[a]); printf("\n");
    printf("    src:"); for(int s=0;s<5;s++) if(csrc[s])printf(" %s:%d",SR[s],csrc[s]); printf("\n");
    printf("    K3 :"); for(int c=0;c<5;c++) if(ckc[c])printf(" %s:%d",KCN[c],ckc[c]); printf("\n");
    // which chains appear in guard-clean pairs
    { std::vector<char> used(CHS.size(),0);
      #pragma omp parallel for schedule(dynamic,16)
      for(int i=0;i<NV;i++){ if(!NPAIR[i]) continue; const V&v=VS[i]; i64 S[17]; int u[17]; i64 K3[17];
        for(int n=0;n<=16;n++){ field(v.src,v.W,v.rnd,n,S[n]); u[n]=v.W+v.d+v.b*(TB[n].u2-30);
            switch(v.kc){case 0:K3[n]=0;break; case 1:K3[n]=1ll<<19;break; case 2:K3[n]=1ll<<(u[n]-1);break;
                         case 3:K3[n]=1ll<<16;break; default:K3[n]=(1ll<<19)+(1ll<<16);} }
        i64 JCAP=v.role?((1ll<<29)-1):((1ll<<27)-1); i64 GL[1024],GH[1024];
        for(int t=0;t<1024;t++){ i64 gl=0,gh=JCAP;
            for(int gi=0;gi<5;gi++){ int node=gn[gi],flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
                i64 lo=T2LO[node][t],hi=T2HI[node][t];
                i64 x1=0,y1=JCAP+1; while(x1<y1){ i64 m=x1+(y1-x1)/2; if(f_T2(v,m,S[n],u[n],K3[n])>=lo)y1=m; else x1=m+1; }
                i64 x2=-1,y2=JCAP; while(x2<y2){ i64 m=x2+(y2-x2+1)/2; if(f_T2(v,m,S[n],u[n],K3[n])<=hi)x2=m; else y2=m-1; }
                if(x1>gl)gl=x1; if(x2<gh)gh=x2; if(gl>gh)break; } GL[t]=gl; GH[t]=gh; }
        for(size_t c=0;c<CHS.size();c++){ const i64*J=JCH[c].data(); bool ok=true;
            for(int t=0;t<1024;t++) if(J[t]<GL[t]||J[t]>GH[t]){ok=false;break;}
            if(ok) used[c]=1; } }
      int nu=0; printf("  chains appearing in at least one guard-clean pair:\n");
      for(size_t c=0;c<CHS.size();c++) if(used[c]){ nu++;
          if(nu<=40) printf("    s1=%2d s2=%2d %s K1=%lld K2=%lld\n",CHS[c].s1,CHS[c].s2,
              CHS[c].role?"arr(dx,KC)":"arr(KC,dx)",(long long)CHS[c].K1,(long long)CHS[c].K2); }
      printf("    ... %d distinct chains in total\n",nu); }
    if(bi>=0){ const V&v=VS[bi]; const CH&c=CHS[BESTC[bi]];
      printf("\nBEST full model over all guard-clean (variant, chain) PAIRS = %d\n"
             "   variant W=%d %s %s b=%d a=%d %s K3=%s %s\n   chain s1=%d s2=%d %s K1=%lld K2=%lld\n",
             best,v.W,RN[v.rnd],SR[v.src],v.b,v.W+v.d,v.role?"arr(S,J)":"arr(J,S)",KCN[v.kc],v.sg?"neg-1c":"pos",
             c.s1,c.s2,c.role?"arr(dx,KC)":"arr(KC,dx)",(long long)c.K1,(long long)c.K2); }
    return 0; }
