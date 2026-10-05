// c4_syn.cpp -- enumerative synthesis over THREE-multiply DAGs for the square latch.
// Instead of hand-listing families, compose: each multiply draws its two operands from everything computed so
// far, each is followed by a truncating latch, and the scale law is enforced by a cheap pre-filter before the
// full 1024-t score.  This reaches topologies no hand enumeration listed (e.g. a residue produced by one
// constant while the quadratic coefficient comes from another) -- the structural degree of freedom the
// two-multiply ceiling is missing.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static i64 JL[1024],JH[1024];
static bool load(const char*p){ FILE*f=fopen(p,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){JL[t+512]=a;JH[t+512]=b;} } fclose(f); return true; }
// operand vocabulary, indexed; value depends on t and on the latches computed so far
enum { OP_KC=0, OP_DX, OP_T2, OP_T, OP_L1, OP_L2, NOPS };
static inline i64 opval(int k,int t,i64 L1,i64 L2){ switch(k){
    case OP_KC: return KC; case OP_DX: return (i64)t<<13; case OP_T2: return (i64)t*t;
    case OP_T: return t; case OP_L1: return L1; default: return L2; } }
static const char* opname[NOPS]={"KC","dx","t2","t","L1","L2"};
int main(int argc,char**argv){ build_tables();
    if(!load("jwin18.txt")){ puts("no jwin18.txt"); return 1; }
    const int P=15;                       // J ~ KC*t^2/2^15 = 157.88*t^2
    // sample t for the scale pre-filter
    const int ns=9; int st[ns]={-509,-401,-256,-127,-37,53,181,337,499};
    struct Cfg{ int a1,b1,s1,k1, a2,b2,s2,k2, a3,b3,s3,k3; };
    long long tried=0, passed=0; int best=-1; Cfg bc{};
    std::vector<Cfg> good;
    #pragma omp parallel
    {
      int lbest=-1; Cfg lbc{}; std::vector<Cfg> lgood; long long ltried=0,lpassed=0;
      #pragma omp for schedule(dynamic) collapse(2)
      for(int a1=0;a1<3;a1++) for(int b1=0;b1<3;b1++){
        if(a1==b1 && a1!=OP_DX) continue;
        for(int s1=6;s1<=30;s1++) for(int k1=0;k1<2;k1++)
        for(int a2=0;a2<5;a2++) for(int b2=0;b2<5;b2++){
          if(a2!=OP_L1 && b2!=OP_L1) continue;                 // multiply 2 must use L1
          for(int s2=4;s2<=30;s2++) for(int k2=0;k2<2;k2++)
          for(int a3=0;a3<6;a3++) for(int b3=0;b3<6;b3++){
            if(a3!=OP_L2 && b3!=OP_L2) continue;               // multiply 3 must use L2
            for(int s3=0;s3<=26;s3++){ ltried++;
              i64 K1=k1?(1ll<<19):0, K2=k2?(1ll<<19):0;
              // scale pre-filter on 9 samples
              bool ok=true; 
              for(int i=0;i<ns&&ok;i++){ int t=st[i];
                i64 L1=(arr(opval(a1,t,0,0),opval(b1,t,0,0))+K1)>>s1;
                if(L1<-(1ll<<26)||L1>(1ll<<26)) { ok=false; break; }
                i64 L2=(arr(opval(a2,t,L1,0),opval(b2,t,L1,0))+K2)>>s2;
                if(L2<-(1ll<<28)||L2>(1ll<<28)) { ok=false; break; }
                i64 J=(arr(opval(a3,t,L1,L2),opval(b3,t,L1,L2))+(1ll<<19))>>s3;
                i64 tgt=((i64)KC*t*t)>>P;
                if(J < tgt-4000 || J > tgt+4000) ok=false; }
              if(!ok) continue; lpassed++;
              int sc=0;
              for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i])continue;
                i64 L1=(arr(opval(a1,t,0,0),opval(b1,t,0,0))+K1)>>s1;
                i64 L2=(arr(opval(a2,t,L1,0),opval(b2,t,L1,0))+K2)>>s2;
                i64 J=(arr(opval(a3,t,L1,L2),opval(b3,t,L1,L2))+(1ll<<19))>>s3;
                if(J>=JL[i]&&J<=JH[i]) sc++; }
              if(sc>lbest){ lbest=sc; lbc={a1,b1,s1,k1,a2,b2,s2,k2,a3,b3,s3,0}; }
              if(sc>=1010) lgood.push_back({a1,b1,s1,k1,a2,b2,s2,k2,a3,b3,s3,0});
            } } } }
      #pragma omp critical
      { tried+=ltried; passed+=lpassed; if(lbest>best){best=lbest;bc=lbc;} 
        good.insert(good.end(),lgood.begin(),lgood.end()); }
    }
    printf("three-multiply DAGs tried: %lld, passed the scale filter: %lld\n",tried,passed);
    printf("best score: %d / 1024\n",best);
    printf("  L1 = (arr(%s,%s) + %s) >> %d\n",opname[bc.a1],opname[bc.b1],bc.k1?"2^19":"0",bc.s1);
    printf("  L2 = (arr(%s,%s) + %s) >> %d\n",opname[bc.a2],opname[bc.b2],bc.k2?"2^19":"0",bc.s2);
    printf("  J  = (arr(%s,%s) + 2^19) >> %d\n",opname[bc.a3],opname[bc.b3],bc.s3);
    printf("configs scoring >= 1010: %zu\n",good.size());
    return 0; }
