// c2_bits.cpp -- black-box sensitivity: WHICH BITS of t does the missing correction depend on?
// Take the best known chain as the reference, and ask, for every subset S of t's 10 bits, how many of the 1024
// t can be satisfied by a correction that is a function of (t & S) alone.  With S = all bits this is the free
// latch (1024/1024 by construction); with S = {} it is the chain itself.  A SMALL S reaching 1024 would name
// exactly which bits the hardware's extra term reads.  Exact: per class, maximum overlap of the required
// correction intervals.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static i64 JL[1024],JH[1024];
static bool load(const char* path){ FILE*f=fopen(path,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){ JL[t+512]=a; JH[t+512]=b; } } fclose(f); return true; }
int main(int argc,char**argv){ build_tables();
    int DU = argc>1? atoi(argv[1]) : 2;                    // 2 = fine latch (shift 18), 0 = coarse (shift 20)
    const char* ds = DU? "jwin18.txt" : "jwin.txt";
    if(!load(ds)){ printf("cannot read %s\n",ds); return 1; }
    int s2 = 20 - DU;
    // reference: the best guard-clean chain, at the matching latch shift
    std::vector<i64> dlo(1024),dhi(1024); int base_ok=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>s2;
        dlo[t+512]=JL[t+512]-J; dhi[t+512]=JH[t+512]-J;
        if(dlo[t+512]<=0&&0<=dhi[t+512]) base_ok++; }
    printf("dataset %s (latch shift %d).  reference chain satisfies %d / 1024 t.\n",ds,s2,base_ok);
    printf("correction range needed: [%lld, %lld]\n",
        (long long)*std::min_element(dlo.begin(),dlo.end()),(long long)*std::max_element(dhi.begin(),dhi.end()));
    // for each bit subset, exact max-coverage per class
    struct R{ unsigned S; int nbits, score, classes; };
    std::vector<R> out;
    std::vector<std::pair<i64,int>> ev; ev.reserve(2048);
    for(unsigned S=0;S<1024;S++){
        int nb=__builtin_popcount(S); int total=0; int ncls=0;
        // group t by (u & S)
        std::vector<std::vector<int>> cls(1024);
        for(int t=-512;t<=511;t++){ unsigned u=((unsigned)t)&0x3FFu; cls[u&S].push_back(t+512); }
        for(unsigned c=0;c<1024;c++){ if(cls[c].empty()) continue; ncls++;
            ev.clear();
            for(int i : cls[c]){ ev.push_back({dlo[i],1}); ev.push_back({dhi[i]+1,-1}); }
            std::sort(ev.begin(),ev.end());
            int cur=0,best=0; for(auto&e:ev){ cur+=e.second; if(cur>best)best=cur; }
            total+=best; }
        out.push_back({S,nb,total,ncls}); }
    std::sort(out.begin(),out.end(),[](const R&a,const R&b){ if(a.score!=b.score) return a.score>b.score; return a.nbits<b.nbits; });
    puts("\nbest subsets (score = t satisfiable by a correction reading only those bits of t):");
    puts(" bits(mask)  #bits #classes  score/1024");
    for(int i=0;i<12;i++){ R&r=out[i]; printf("  0x%03x      %2d    %4d      %4d\n",r.S,r.nbits,r.classes,r.score); }
    puts("\nsmallest subset reaching each score level:");
    for(int target=1024; target>=base_ok; target-=(target>1000?1:8)){
        int bestnb=99; unsigned bestS=0;
        for(auto&r:out) if(r.score>=target && r.nbits<bestnb){ bestnb=r.nbits; bestS=r.S; }
        if(bestnb<99){ printf("  >= %4d : %2d bits (mask 0x%03x)\n",target,bestnb,bestS); if(target<=base_ok+8) break; } }
    // per-bit marginal: score of each single bit, and of all-but-one
    puts("\nmarginal value of each bit of t (single-bit subset score, and all-bits-except score):");
    for(int b=0;b<10;b++){ unsigned S1=1u<<b, Sx=0x3FFu&~S1; int s1=0,sx=0;
        for(auto&r:out){ if(r.S==S1)s1=r.score; if(r.S==Sx)sx=r.score; }
        printf("  bit %d (weight 2^%d): alone %4d   all-except %4d\n",b,b,s1,sx); }
    return 0; }
