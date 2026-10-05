// c1_sep.cpp -- the user's 32x32 idea, made exact.
// The hardware sees t as a 10-bit field.  Split those bits into two groups and ask whether the required square
// latch is  J(t) = floor(KC*t^2 / 2^P) + f(groupA bits) + g(groupB bits).
// Feasibility of  f_i + g_j in [lo_ij, hi_ij]  is a system of DIFFERENCE constraints (substitute y_j = -g_j),
// so it is feasible iff the constraint graph has no negative cycle: Bellman-Ford decides it exactly.
// Every one of the 32x32 cells is hit by exactly one t, so the matrix is complete.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static i64 JL[1024],JH[1024];
static int P = 15;                 // fine latch scale: J ~ KC*t^2 / 2^15
static bool load(const char* path){ FILE*f=fopen(path,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){ JL[t+512]=a; JH[t+512]=b; } } fclose(f); return true; }
// Bellman-Ford over 2*G nodes for f_i - y_j <= hi_ij , y_j - f_i <= -lo_ij
struct Edge{ int u,v; i64 w; };
static bool feasible(int GA,int GB,const std::vector<i64>&lo,const std::vector<i64>&hi,
                     std::vector<i64>*fo=nullptr,std::vector<i64>*go=nullptr){
    int N=GA+GB; std::vector<Edge> E; E.reserve(2*GA*GB);
    for(int i=0;i<GA;i++) for(int j=0;j<GB;j++){ size_t k=(size_t)i*GB+j;
        if(lo[k]>hi[k]) return false;
        E.push_back({GA+j,i,hi[k]});        // f_i <= y_j + hi
        E.push_back({i,GA+j,-lo[k]}); }     // y_j <= f_i - lo
    std::vector<i64> d(N,0);
    for(int it=0;it<N;it++){ bool ch=false;
        for(auto&e:E) if(d[e.u]+e.w<d[e.v]){ d[e.v]=d[e.u]+e.w; ch=true; }
        if(!ch) break; }
    for(auto&e:E) if(d[e.u]+e.w<d[e.v]) return false;     // negative cycle -> infeasible
    if(fo){ fo->assign(d.begin(),d.begin()+GA); go->resize(GB);
        for(int j=0;j<GB;j++) (*go)[j] = -d[GA+j]; }
    return true; }
int main(int argc,char**argv){ build_tables();
    const char* ds = argc>1? argv[1] : "jwin18.txt";
    if(!load(ds)){ printf("cannot read %s\n",ds); return 1; }
    if(argc>2) P=atoi(argv[2]);
    printf("dataset %s, latch scale J ~ KC*t^2/2^%d\n",ds,P);
    // required Delta intervals against the reference floor(KC t^2 / 2^P)
    std::vector<i64> dlo(1024),dhi(1024); int nfeas=0;
    for(int t=-512;t<=511;t++){ i64 R=((i64)KC*t*t)>>P; dlo[t+512]=JL[t+512]-R; dhi[t+512]=JH[t+512]-R;
        if(JL[t+512]<=JH[t+512]) nfeas++; }
    printf("feasible t in the dataset: %d ; Delta range over all t: [%lld, %lld]\n",nfeas,
        (long long)*std::min_element(dlo.begin(),dlo.end()),(long long)*std::max_element(dhi.begin(),dhi.end()));
    // bit-group splits of the 10-bit field of t (two's complement, as the hardware holds it)
    struct Split{ const char* name; unsigned maskA; };
    Split sp[] = {
        {"top5 | bottom5",        0x3E0u},
        {"top4 | bottom6",        0x3C0u},
        {"top6 | bottom4",        0x3F0u},
        {"top3 | bottom7",        0x380u},
        {"top7 | bottom3",        0x3F8u},
        {"even bits | odd bits",  0x155u},
        {"Booth digits 3,4 | 0,1,2", 0x3C0u},
        {"sign+top4 | bottom5",   0x3E0u},
    };
    puts("\nsplit                     |A| |B| separable?   (f_i + g_j in [lo,hi] for all 1024 cells)");
    for(auto&s : sp){ unsigned mA=s.maskA, mB=(~mA)&0x3FFu;
        // enumerate the distinct values of each group
        std::vector<int> ia(1024,-1), ib(1024,-1); std::vector<unsigned> va,vb;
        for(int t=-512;t<=511;t++){ unsigned u=((unsigned)t)&0x3FFu, a=u&mA, b=u&mB;
            int pa=-1; for(size_t k=0;k<va.size();k++) if(va[k]==a){pa=(int)k;break;}
            if(pa<0){ pa=(int)va.size(); va.push_back(a);} 
            int pb=-1; for(size_t k=0;k<vb.size();k++) if(vb[k]==b){pb=(int)k;break;}
            if(pb<0){ pb=(int)vb.size(); vb.push_back(b);} 
            ia[t+512]=pa; ib[t+512]=pb; }
        int GA=(int)va.size(), GB=(int)vb.size();
        std::vector<i64> lo((size_t)GA*GB, -(1ll<<60)), hi((size_t)GA*GB, (1ll<<60));
        for(int t=-512;t<=511;t++){ if(JL[t+512]>JH[t+512]) continue; size_t k=(size_t)ia[t+512]*GB+ib[t+512];
            lo[k]=std::max(lo[k],dlo[t+512]); hi[k]=std::min(hi[k],dhi[t+512]); }
        std::vector<i64> f,g; bool ok=feasible(GA,GB,lo,hi,&f,&g);
        printf("%-25s %3d %3d  %s\n",s.name,GA,GB, ok?"YES  <-- SEPARABLE":"no");
        if(ok){ printf("   f ="); for(int i=0;i<GA&&i<40;i++) printf(" %lld",(long long)f[i]);
                printf("\n   g ="); for(int j=0;j<GB&&j<40;j++) printf(" %lld",(long long)g[j]); printf("\n"); } }
    return 0; }
