// w1_gen.cpp -- re-derive the whole model at other node counts and both split phases and score it.
// For each configuration: nodes/turn N, node span L=65536/N, split phase (CENTRED: t in [-L/2, L/2-1]
// measured from the LUT point; START: t in [0,L-1] measured from the LUT point at the interval start),
// LUT phase offset p, and operand shift w.  C0 and u2 are chosen per table entry exactly as b2.cpp does
// (best value inside the t=0 window +-3, u2 within +-1 of the derived 30+k) -- that is the only fitting,
// and it is the same allowance the baseline gets.
#include "w1_gen.h"
#include <cstring>
#include <cstdio>
static Fsca F; static i64 VL[65536], VH[65536];
static void loadw(){ for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;} } }

// score one configuration over the fundamental domain: all angles in [0, 32768)
struct Res { int viol; int entries; int exactnodes; };
static Res score(int N,int centred,int p,int w,int verbose){
    int L=65536/N, H=L/2;
    // enumerate LUT indices k whose node covers angles in [0,32768)
    // node k covers [A_k - (centred?H:0), A_k + (centred?H-1:L-1)], A_k = p + L*k
    struct Node{ int A, phi, flip; };
    std::vector<Node> nodes;
    for(int k=-1;k<=N;k++){ i64 A=(i64)p+(i64)L*k; if(A<0||A>=65536) continue;
        int lo = centred? A-H : A, hi = centred? A+H-1 : A+L-1;
        if(lo<0||hi>=32768) continue;
        int phi = A<=16384 ? A : (A<32768? 32768-A : -1); if(phi<0) continue;
        nodes.push_back({(int)A,phi,A>16384}); }
    // distinct phi -> table entries
    std::vector<int> phis; for(auto&n:nodes) phis.push_back(n.phi);
    std::sort(phis.begin(),phis.end()); phis.erase(std::unique(phis.begin(),phis.end()),phis.end());
    std::vector<GTab> TT; for(int ph:phis) TT.push_back(gderive(ph,w));
    auto idx=[&](int ph){ return (int)(std::lower_bound(phis.begin(),phis.end(),ph)-phis.begin()); };
    // choose C0,u2 per table entry jointly over all nodes that use it
    int total=0, exact=0;
    for(size_t j=0;j<phis.size();j++){
        std::vector<const Node*> use; for(auto&n:nodes) if(n.phi==phis[j]) use.push_back(&n);
        int A0=phis[j]; i64 lo0=VL[A0],hi0=VH[A0];
        int bv=1<<30; i64 bc=lo0; int bu=TT[j].u2;
        for(i64 c=lo0-3;c<=hi0+3;c++) for(int u=TT[j].u2-1;u<=TT[j].u2+1;u++){
            int v=0;
            for(auto*n:use){ int t0 = centred? -H : 0, t1 = centred? H-1 : L-1;
                for(int t=t0;t<=t1;t++){ int a=n->A+t; if(a<0||a>=32768) continue;
                    i64 V=gV(TT[j],t,n->flip,c,u,w,KC); if(V<VL[a]||V>VH[a]) v++; } }
            if(v<bv){bv=v;bc=c;bu=u;} }
        total+=bv; if(bv==0) exact++;
        if(verbose) printf("     phi=%5d  C0=%9lld (dev %+3lld)  u2=%2d  viol=%5d\n",
            phis[j],(long long)bc,(long long)(bc-TT[j].sin26),bu,bv); }
    return Res{total,(int)phis.size(),exact}; }

int main(int argc,char**argv){ if(!load_fsca(F))return 1; loadw();
    printf("Full-model window violations over the fundamental domain (angles 0..32767, 32768 samples).\n");
    printf("Baseline = the published model: N=64, CENTRED, p=0, w=13  ->  147\n\n");
    printf(" N   split    p     w   table   viol    exact-entries\n");
    for(int N : {32,64,128,256}){
        int L=65536/N;
        for(int centred=1; centred>=0; centred--){
            for(int p : {0, L/2}){
                if(centred && p!=0 && N!=64) continue;         // phase only interesting at 64 for centred
                int wnat = 22 - (int)round(log2(centred? L/2 : L/2));
                for(int w=wnat-1; w<=wnat+1; w++){
                    if(w<8||w>20) continue;
                    Res r=score(N,centred,p,w,0);
                    printf("%4d  %-7s %4d  %3d  %5d  %6d   %3d/%d%s\n",N,centred?"CENTRED":"START",p,w,
                        r.entries,r.viol,r.exactnodes,r.entries,
                        (N==64&&centred&&p==0&&w==13)?"   <== published baseline":""); } } } }
    return 0; }
