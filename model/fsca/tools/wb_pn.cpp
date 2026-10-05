// wb_pn.cpp -- FSRRA-style square term: ONE multiply of a per-node coefficient C2 ~ 2pi^2 sin(node) with
// the dx^2 latch Q = (arr(dx,dx)+K1)>>g.   T2 = sm(arr(C2,Q)+K,u)  (or roles swapped).
// This merges Kc and the table sine into one stored coefficient, so it has ONE truncation instead of two,
// and it is NOT in the order enumeration that solved the chain.  C2 and u are fitted per node (diagnostic).
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 WL[32][1024],WH[32][1024];
static i64 OD[32][1024];    // odd part s*(T1-T3)
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            OD[node][t+512]=s*(T1-T3);
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex); WL[node][t+512]=lo; WH[node][t+512]=hi; } }
    puts("g  K1  role |  total violations (nodes 0..31)   guard(0,1,2,16)  per-node best (C2,u,viol)");
    for(int g=16;g<=26;g++) for(int k1=0;k1<2;k1++) for(int role=0;role<2;role++){
        i64 K1=k1?K:0;
        std::vector<i64> Q(1024);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; Q[t+512]=(arr(dx,dx)+K1)>>g; }
        int grand=0, guard=0; std::vector<i64> bc(17,0); std::vector<int> bu(17,0),bv(17,0);
        for(int n=1;n<=16;n++){
            int nodes[2]={n,32-n}; int nn=(n==16)?1:2;
            int bestv=1<<30; i64 bestC=0; int bestU=0;
            for(int u=20;u<=40;u++){
                long double idl=19.7392088L*(long double)TB[n].sin26/67108864.0L*powl(2.0L,(long double)(u-32+g));
                if(idl<(1<<21)||idl>(1<<24)) continue; i64 C0i=(i64)llroundl(idl);
                int lv=1<<30; i64 lC=0;
                #pragma omp parallel for schedule(static)
                for(i64 C=C0i-2500;C<=C0i+2500;C++){ int v=0;
                    for(int i=0;i<nn;i++){ int node=nodes[i];
                        for(int t=-512;t<=511;t++){ i64 T2=sm((role?arr(Q[t+512],C):arr(C,Q[t+512]))+K,u);
                            i64 V=C0FIT[n]+OD[node][t+512]-T2;
                            if(V<WL[node][t+512]||V>WH[node][t+512])v++; } }
                    #pragma omp critical
                    { if(v<lv){lv=v;lC=C;} } }
                if(lv<bestv){bestv=lv;bestC=lC;bestU=u;} }
            bc[n]=bestC;bu[n]=bestU;bv[n]=bestv; grand+=bestv;
            if(n==1||n==2||n==16) guard+=bestv; }
        printf("%2d %3lld  %d   | %5d   guard %3d  |",g,(long long)K1,role,grand,guard);
        for(int n=1;n<=16;n++) printf(" %d:%lld/%d/%d",n,(long long)bc[n],bu[n],bv[n]); printf("\n");
        fflush(stdout); }
    return 0; }
