// wb_c0fit.cpp -- the t=0 window leaves 2-3 candidates for C0 at seven table entries.  Each choice moves
// that node's J constraint by ~2^33/C0 = 128..230 J units, so the choice matters.  Enumerate every
// combination and score the resulting J-window structure (infeasible t, +/- disjoint pairs, best
// symmetric-rule score, chain score).
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 CAND[17][4]; static int NC[17];
static i64 IV[17][4][1024][2];      // [tab][cand][t] -> J interval from node n
static i64 IVM[17][4][1024][2];     // mirror node 32-n
int main(){ if(!load_fsca(F))return 1; build_tables();
    // candidates from the t=0 window intersection
    for(int n=0;n<=16;n++){ i64 l1,h1,l2,h2; bool e1,e2; win_of((n*1024)&0xffff,l1,h1,e1);
        if(n>0&&n<16){ win_of(((32-n)*1024)&0xffff,l2,h2,e2);} else {l2=-1ll<<60;h2=1ll<<60;}
        i64 lo=std::max(l1,l2),hi=std::min(h1,h2); NC[n]=0;
        for(i64 c=lo;c<=hi&&NC[n]<4;c++) CAND[n][NC[n]++]=c; }
    printf("candidates: "); for(int n=0;n<=16;n++) printf("%d:%d ",n,NC[n]); printf("\n");
    // per node/candidate/t J intervals
    for(int n=1;n<=16;n++){ const Tab&T=TB[n];
      for(int ci=0;ci<NC[n];ci++){ i64 C0=CAND[n][ci];
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int u=U2FIT[n];
            for(int mir=0;mir<2;mir++){ int node=mir?32-n:n; if(mir&&(n==16))continue;
                i64 s=mir?-1:1; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 base=C0+s*T1-s*T3; i64 t2lo=base-hi,t2hi=base-lo;
                i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
                if(mir){IVM[n][ci][t+512][0]=a1;IVM[n][ci][t+512][1]=a2;}
                else {IV[n][ci][t+512][0]=a1;IV[n][ci][t+512][1]=a2;} } } } }
    // enumerate combinations over the ambiguous entries
    std::vector<int> amb; for(int n=1;n<=16;n++) if(NC[n]>1) amb.push_back(n);
    printf("ambiguous entries: "); for(int n:amb)printf("%d(%d) ",n,NC[n]); printf("\n");
    long total=1; for(int n:amb) total*=NC[n];
    printf("combinations: %ld\n",total);
    int bestscore=-1; std::vector<int> bestsel;
    struct Row{int sym,chain,infeas,disj; std::vector<int> sel;};
    std::vector<Row> rows;
    std::vector<int> sel(amb.size(),0);
    for(long ci=0;ci<total;ci++){ long x=ci; for(size_t i=0;i<amb.size();i++){ sel[i]=x%NC[amb[i]]; x/=NC[amb[i]]; }
        int idx[17]; for(int n=1;n<=16;n++) idx[n]=0;
        for(size_t i=0;i<amb.size();i++) idx[amb[i]]=sel[i];
        std::vector<i64> JLv(1024),JHv(1024); std::vector<char> ok(1024,0); int infeas=0;
        for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
            for(int n=1;n<=16;n++){ int c=idx[n];
                if(IV[n][c][t+512][0]>gl)gl=IV[n][c][t+512][0]; if(IV[n][c][t+512][1]<gh)gh=IV[n][c][t+512][1];
                if(n<16){ if(IVM[n][c][t+512][0]>gl)gl=IVM[n][c][t+512][0]; if(IVM[n][c][t+512][1]<gh)gh=IVM[n][c][t+512][1]; } }
            JLv[t+512]=gl;JHv[t+512]=gh;ok[t+512]=gl<=gh; if(gl>gh)infeas++; }
        int disj=0; for(int A=1;A<=511;A++){int ip=A+512,im=-A+512; if(!ok[ip]||!ok[im])continue;
            if(JHv[ip]<JLv[im]||JHv[im]<JLv[ip])disj++; }
        int sym=0,ch=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue;
            i64 Js=arr(((i64)t*t)<<1,2586787)>>17; if(Js>=JLv[t+512]&&Js<=JHv[t+512])sym++;
            i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20;
            if(J>=JLv[t+512]&&J<=JHv[t+512])ch++; }
        rows.push_back({sym,ch,infeas,disj,sel});
        if(sym>bestscore){bestscore=sym;bestsel=sel;} }
    std::sort(rows.begin(),rows.end(),[](const Row&a,const Row&b){ if(a.sym!=b.sym)return a.sym>b.sym; return a.disj<b.disj;});
    puts("sym chain infeas disj | C0 selection (entry=value)");
    for(int i=0;i<12&&i<(int)rows.size();i++){ printf("%4d %5d %5d %4d |",rows[i].sym,rows[i].chain,rows[i].infeas,rows[i].disj);
        for(size_t k=0;k<amb.size();k++) printf(" %d=%lld",amb[k],(long long)CAND[amb[k]][rows[i].sel[k]]); printf("\n"); }
    // reference: the fitted table
    printf("\nreference C0FIT: "); for(int n=1;n<=16;n++) printf("%lld ",(long long)C0FIT[n]); printf("\n");
    return 0; }
